/*
 * Copyright (C) 2013 AirDC++ Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "stdinc.h"

#include "DirectoryMonitor.h"
#include "Text.h"
#include "LogManager.h"

namespace dcpp {


DirectoryMonitor::DirectoryMonitor(int numThreads, bool aUseDispatcherThread) : server(new Server(this, numThreads)), useDispatcherThread(aUseDispatcherThread), queue(1024), stop(false) {
	//init();
	if (useDispatcherThread)
		start();
}

void DirectoryMonitor::callAsync(AsyncF aF) {
	addTask(TYPE_ASYNC, new AsyncTask(aF));
}

void DirectoryMonitor::addTask(TaskType aType, Task* aTask) { 
	queue.push(new TaskQueue::UniqueTaskPair(aType, unique_ptr<Task>(aTask))); 
	if (useDispatcherThread) {
		s.signal(); 
	} /*else {
		processNotification(aTask->path, aTask->buf);
		delete aTask;
	}*/
}

DirectoryMonitor::~DirectoryMonitor() {
	stop = true;
	stopMonitoring();
	delete server;

	if (useDispatcherThread) {
		s.signal();
		join();
	}
}

void DirectoryMonitor::stopMonitoring() {
	server->stop();
}

void DirectoryMonitor::init() throw(MonitorException) {
	server->init();
}

void DirectoryMonitor::Server::init() throw(MonitorException) {
	if (threadRunning.test_and_set())
		return;

	m_hIOCP = CreateIoCompletionPort(
							(HANDLE)INVALID_HANDLE_VALUE,
							NULL,
							0,
							m_nThreads);
	if (!m_hIOCP) {
		throw MonitorException(Util::translateError(::GetLastError()));
	}

	start();
}
	
void DirectoryMonitor::Server::stop() {
	m_bTerminate = true;
	{
		WLock l(cs);
		for (auto m: monitors | map_values) {
			// Each Request object will delete itself.
			m->stopMonitoring();
		}

		if (m_hIOCP)
		{
			CloseHandle(m_hIOCP);
			m_hIOCP = NULL;
		}
	}

	// Wait for the thread to stop
	while (true) {
		{
			RLock l(cs);
			if (monitors.empty())
				break;
		}

		Sleep(50);
	}
}

DirectoryMonitor::Server::Server(DirectoryMonitor* aBase, int numThreads) : base(aBase), m_bTerminate(false), m_nThreads(numThreads), m_hIOCP(NULL) {
	threadRunning.clear();
	//start();
}

DirectoryMonitor::Server::~Server() {

}

void Monitor::beginRead() {
	DWORD dwBytes=0;

	// This call needs to be reissued after every APC.
	int success = ::ReadDirectoryChangesW(
		m_hDirectory,						// handle to directory
		&m_Buffer[0],                       // read results buffer
		m_Buffer.size(),					// length of buffer
		m_bChildren,                        // monitoring option
		FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_CREATION|FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME,        // filter conditions
		&dwBytes,                           // bytes returned
		&m_Overlapped,                      // overlapped buffer
		NULL);           // completion routine

	if (success == 0)
		throw MonitorException(Util::translateError(::GetLastError()));
}

void Monitor::queueNotificationTask(int dwSize) {
	// We could just swap back and forth between the two
	// buffers, but this code is easier to understand and debug.
	auto f = new DirectoryMonitor::NotifyTask(path);
	f->buf.resize(dwSize);
	memcpy(&f->buf[0], &m_Buffer[0], dwSize);
	server->base->addTask(DirectoryMonitor::TYPE_NOTIFICATION, f);
}

void Monitor::stopMonitoring() {
	::CancelIo(m_hDirectory);
	::CloseHandle(m_hDirectory);
	m_hDirectory = nullptr;
}

int DirectoryMonitor::run() {
	while (true) {
		s.wait();
		if (stop)
			break;

		dispatch();
	}
	return 0;
}

bool DirectoryMonitor::dispatch() {
	unique_ptr<TaskQueue::UniqueTaskPair> t;
	if(!queue.pop(t)) {
		return false;
	}

	if (t->first == TYPE_OVERFLOW) {
		fire(DirectoryMonitorListener::Overflow(), static_cast<StringTask*>(t->second.get())->str);
	} else if (t->first == TYPE_NOTIFICATION) {
		processNotification(static_cast<NotifyTask*>(t->second.get())->path, static_cast<NotifyTask*>(t->second.get())->buf);
	} else if (t->first == TYPE_ASYNC) {
		static_cast<AsyncTask*>(t->second.get())->f();
	}
	return true;
}

int DirectoryMonitor::Server::run() {
	while (read()) {
		//...
	}

	//we are stopping (quiting/no dirs to monitor)

	WLock l(cs);
	m_bTerminate = false;
	for (auto m: monitors | map_values) {
		delete m;
	}

	monitors.clear();
	threadRunning.clear();
	return 0;
}

int DirectoryMonitor::Server::read() {
	DWORD		dwBytesXFered = 0;
	//Monitor*	mon = 0;
	ULONG_PTR	ulKey = 0;
	OVERLAPPED*	pOl = 0;

	auto ret = GetQueuedCompletionStatus(m_hIOCP, &dwBytesXFered, &ulKey, &pOl, INFINITE);
	auto dwError = GetLastError();
	if (!ret) {
		if (!m_hIOCP) {
			// shutting down
			return 0;
		}

		if (dwError == WAIT_TIMEOUT) {
			//harmless
			return 1;
		}
	}

	unique_ptr<string> removeDir(nullptr);
	{
		RLock l(cs);
		auto mon = find_if(monitors | map_values, [ulKey](const Monitor* m) { return (ULONG_PTR)m->m_hDirectory == ulKey; });
		if (mon.base() != monitors.end()) {
			try {
				if (dwError != 0) {
					if (dwError == ERROR_NOTIFY_ENUM_DIR) {
						(*mon)->beginRead();

						// Too many changes to track, http://blogs.msdn.com/b/oldnewthing/archive/2011/08/12/10195186.aspx
						(*mon)->server->base->addTask(DirectoryMonitor::TYPE_OVERFLOW, new StringTask(Text::fromT((*mon)->path)));
					} else {
						throw MonitorException(Util::translateError(dwError));
					}
				} else {
					if ((*mon)->errorCount > 0) {
						LogManager::getInstance()->message("Monitoring was successfully restored for " + Text::fromT((*mon)->path), LogManager::LOG_ERROR);
						(*mon)->errorCount = 0;
					}

					if (dwBytesXFered > 0) {
						(*mon)->queueNotificationTask(dwBytesXFered);
					} else {
						LogManager::getInstance()->message("An empty notification was received when monitoring " + Text::fromT((*mon)->path) + " (report this)", LogManager::LOG_WARNING);
					}

					(*mon)->beginRead();
				}
			} catch (const MonitorException& e) {
				auto path = Text::fromT((*mon)->path);
				if ((*mon)->errorCount == 0)
					LogManager::getInstance()->message("Error when monitoring " + path + ": " + e.getError() + ". Retrying for 60 seconds...", LogManager::LOG_ERROR);

				(*mon)->errorCount++;

				if ((*mon)->errorCount == 60) {
					// remove this directory from monitoring
					LogManager::getInstance()->message("A failed directory " + path + " has been removed from monitoring", LogManager::LOG_ERROR);
					removeDir.reset(new string(path));
				} else {
					if ((*mon)->m_hDirectory == INVALID_HANDLE_VALUE) {
						(*mon)->openDirectory(m_hIOCP);
					}

					//we'll most likely get the error instantly again...
					Sleep(1000);
				}
			}
		}
	}

	if (removeDir) {
		removeDirectory(*removeDir);
	}

	return 1;
}

bool DirectoryMonitor::addDirectory(const string& aPath) {
	return server->addDirectory(aPath);
}

bool DirectoryMonitor::removeDirectory(const string& aPath) {
	return server->removeDirectory(aPath);
}

size_t DirectoryMonitor::clear() {
	return server->clear();
}

size_t DirectoryMonitor::Server::clear() {
	StringList remove;

	{
		RLock l(cs);
		for (auto& m: monitors)
			remove.push_back(m.first);
	}

	for (auto& p: remove)
		removeDirectory(p);
	return remove.size();
}

bool DirectoryMonitor::Server::addDirectory(const string& aPath) throw(MonitorException) {
	{
		RLock l(cs);
		if (monitors.find(aPath) != monitors.end())
			return false;
	}

	init();

	Monitor* mon = new Monitor(aPath, this, 0, 4*1024, true);
	try {
		mon->openDirectory(m_hIOCP);

		{
			WLock l(cs);
			mon->beginRead();
			monitors.emplace(aPath, mon);
		}
	} catch(MonitorException& e) {
		mon->stopMonitoring();
		delete mon;
		throw e;
	}

	return true;
}

bool DirectoryMonitor::Server::removeDirectory(const string& aPath) {
	WLock l(cs);
	auto p = monitors.find(aPath);
	if (p != monitors.end()) {
		p->second->stopMonitoring();
		delete p->second;

		monitors.erase(p);

		if (monitors.empty()) {
			//MSDN: The completion port is freed when there are
			//no more references to it
			if (m_hIOCP) {
				CloseHandle(m_hIOCP);
				m_hIOCP = NULL;
			}
		}

		return true;
	}

	return false;
}

Monitor::Monitor(const string& aPath, DirectoryMonitor::Server* aServer, int monitorFlags, size_t bufferSize, bool recursive) : 
	path(Text::toT(aPath)), 
	server(aServer), 
	m_hDirectory(nullptr),
	m_dwFlags(monitorFlags),
	m_bChildren(recursive),
	errorCount(0)

{
	::ZeroMemory(&m_Overlapped, sizeof(OVERLAPPED));
	m_Buffer.resize(bufferSize);
}

void Monitor::openDirectory(HANDLE iocp) {
	// Allow this routine to be called redundantly.
	if (m_hDirectory)
		return;

	m_hDirectory = ::CreateFile(
		path.c_str(),					// pointer to the file name
		FILE_LIST_DIRECTORY,                // access (read/write) mode
		FILE_SHARE_READ						// share mode
		 | FILE_SHARE_WRITE
		 | FILE_SHARE_DELETE,
		NULL,                               // security descriptor
		OPEN_EXISTING,                      // how to create
		FILE_FLAG_BACKUP_SEMANTICS			// file attributes
		 | FILE_FLAG_OVERLAPPED,
		NULL);                              // file with attributes to copy

	if (m_hDirectory == INVALID_HANDLE_VALUE) {
		throw MonitorException(Util::translateError(::GetLastError()));
	}

	if (CreateIoCompletionPort(m_hDirectory, iocp, (ULONG_PTR)m_hDirectory, 0) == NULL) {
		throw MonitorException(Util::translateError(::GetLastError()));
	}
}

Monitor::~Monitor() {
	dcassert(!m_hDirectory);
}

void DirectoryMonitor::processNotification(const tstring& aPath, ByteVector& aBuf) {
	char* pBase = (char*)&aBuf[0];
	string oldPath;

	for (;;)
	{
		FILE_NOTIFY_INFORMATION& fni = (FILE_NOTIFY_INFORMATION&)*pBase;

		tstring notifyPath(fni.FileName, fni.FileNameLength/sizeof(wchar_t));

		// If it could be a short filename, expand it.
		auto fileName = Util::getFileName(notifyPath);

		// The maximum length of an 8.3 filename is twelve, including the dot.
		if (fileName.length() <= 12 && fileName.front() == _T('~')) {
			// Convert to the long filename form. Unfortunately, this
			// does not work for deletions, so it's an imperfect fix.
			wchar_t wbuf[UNC_MAX_PATH];
			if (::GetLongPathName(notifyPath.c_str(), wbuf, _countof (wbuf)) > 0)
				notifyPath = wbuf;
		}

		notifyPath = aPath + notifyPath;
		switch(fni.Action) {
			case FILE_ACTION_ADDED: 
				// The file was added to the directory.
				fire(DirectoryMonitorListener::FileCreated(), Text::fromT(notifyPath));
				break;
			case FILE_ACTION_REMOVED: 
				// The file was removed from the directory.
				fire(DirectoryMonitorListener::FileDeleted(), Text::fromT(notifyPath));
				break;
			case FILE_ACTION_RENAMED_OLD_NAME: 
				// The file was renamed and this is the old name. 
				oldPath = Text::fromT(notifyPath);
				break;
			case FILE_ACTION_RENAMED_NEW_NAME: 
				// The file was renamed and this is the new name.
				fire(DirectoryMonitorListener::FileRenamed(), oldPath, Text::fromT(notifyPath));
				break;
			case FILE_ACTION_MODIFIED:
				fire(DirectoryMonitorListener::FileModified(), Text::fromT(notifyPath));
				break;
		}

		if (!fni.NextEntryOffset)
			break;
		pBase += fni.NextEntryOffset;
	};
}

} //dcpp