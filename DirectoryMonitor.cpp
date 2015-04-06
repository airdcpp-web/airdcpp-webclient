/*
 * Copyright (C) 2013-2015 AirDC++ Project
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


#include "AirUtil.h"
#include "DirectoryMonitor.h"
#include "ResourceManager.h"
#include "Text.h"

/*#ifndef _WIN32
#include <sys/inotify.h>
#include <sys/epoll.h>
#endif*/

namespace dcpp {


DirectoryMonitor::DirectoryMonitor(int numThreads, bool aUseDispatcherThread) : server(new Server(this, numThreads)), dispatcher(aUseDispatcherThread) {

}

void DirectoryMonitor::callAsync(DispatcherQueue::Callback&& aF) {
	dispatcher.addTask(move(aF));
}

DirectoryMonitor::~DirectoryMonitor() {
	stopMonitoring();
	delete server;
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

#ifdef WIN32
	m_hIOCP = CreateIoCompletionPort(
							(HANDLE)INVALID_HANDLE_VALUE,
							NULL,
							0,
							m_nThreads);
	if (!m_hIOCP) {
		throw MonitorException(Util::translateError(::GetLastError()));
	}
#else
	/*fd = inotify_init();
	if (fd < 0)
		throw MonitorException(Util::translateError(::GetLastError()));

	efd = epoll_create(sizeof(int));
	if (efd < 0)
		throw MonitorException(Util::translateError(::GetLastError()));

	ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
	cfg = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);
	if (cfg < 0)
		throw MonitorException(Util::translateError(::GetLastError()));*/

#endif

	start();
}
	
void DirectoryMonitor::Server::stop() {
	m_bTerminate = true;
	{
		WLock l(cs);
		failedDirectories.clear();
		for (auto m: monitors | map_values) {
			// Each Request object will delete itself.
			m->stopMonitoring();
		}
	}

	// Wait for the thread to stop
	while (true) {
		{
			RLock l(cs);
			if (monitors.empty())
				break;
		}

		Thread::sleep(50);
	}
}

#ifdef WIN32

DirectoryMonitor::Server::Server(DirectoryMonitor* aBase, int numThreads) : base(aBase), m_bTerminate(false), m_nThreads(numThreads), m_hIOCP(NULL) {
	threadRunning.clear();
	//start();
}

DirectoryMonitor::Server::~Server() {

}

Monitor::Monitor(const string& aPath, DirectoryMonitor::Server* aServer, int monitorFlags, size_t bufferSize, bool recursive) :
	path(aPath),
	server(aServer),
	m_hDirectory(nullptr),
	m_dwFlags(monitorFlags),
	m_bChildren(recursive),
	errorCount(0),
	key(lastKey++),
	changes(0) {
		::ZeroMemory(&m_Overlapped, sizeof(OVERLAPPED));
		m_Buffer.resize(bufferSize);
}

Monitor::~Monitor() {
	dcassert(!m_hDirectory);
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
	if (!server || !server->base)
		return;
	// We could just swap back and forth between the two
	// buffers, but this code is easier to understand and debug.

	ByteVector buf;
	buf.resize(dwSize);
	memcpy(&buf[0], &m_Buffer[0], dwSize);
	server->base->callAsync([=] { server->base->processNotification(path, buf); });
}

void Monitor::stopMonitoring() {
	dcassert(m_hDirectory);
	::CancelIo(m_hDirectory);
	::CloseHandle(m_hDirectory);
	m_hDirectory = nullptr;

}

int Monitor::lastKey = 0;

void Monitor::openDirectory(HANDLE iocp) {
	// Allow this routine to be called redundantly.
	if (m_hDirectory && m_hDirectory != INVALID_HANDLE_VALUE)
		return;

	m_hDirectory = ::CreateFile(
		Text::toT(path).c_str(),					// pointer to the file name
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

	if (CreateIoCompletionPort(m_hDirectory, iocp, (ULONG_PTR) key, 0) == NULL) {
		throw MonitorException(Util::translateError(::GetLastError()));
	}
}

#else

Monitor::Monitor(const string& aPath, DirectoryMonitor::Server* aServer, int monitorFlags, size_t bufferSize) {
}

Monitor::~Monitor() { }

void Monitor::stopMonitoring() {

}

DirectoryMonitor::Server::Server(DirectoryMonitor* aBase, int numThreads) : base(aBase), m_bTerminate(false), m_nThreads(numThreads) {
	threadRunning.clear();
}

DirectoryMonitor::Server::~Server() {

}

#endif


bool DirectoryMonitor::dispatch() {
	return dispatcher.dispatch();
}

int DirectoryMonitor::Server::run() {
	while (read()) {
		//...
	}

	threadRunning.clear();
	return 0;
}

#ifdef WIN32
int DirectoryMonitor::Server::read() {
	DWORD		dwBytesXFered = 0;
	ULONG_PTR	ulKey = 0;
	OVERLAPPED*	pOl;

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

	{
		WLock l(cs);
		auto mon = find_if(monitors | map_values, [ulKey](const Monitor* m) { return (ULONG_PTR)m->key == ulKey; });
		if (mon.base() != monitors.end()) {
			if (!(*mon)->m_hDirectory) {
				//this is going to be deleted
				deleteDirectory(mon.base());
				return 1;
			}

			try {
				if (dwError != 0 || !ret) {
					// Too many changes to track, http://blogs.msdn.com/b/oldnewthing/archive/2011/08/12/10195186.aspx
					// The documentation only states the code ERROR_NOTIFY_ENUM_DIR for this, but according to the testing ERROR_NOT_ENOUGH_QUOTA and ERROR_ALREADY_EXISTS seem to be used instead....
					// (and ERROR_TOO_MANY_CMDS with network drives)
					if (dwError == ERROR_NOTIFY_ENUM_DIR || dwError == ERROR_NOT_ENOUGH_QUOTA || dwError == ERROR_ALREADY_EXISTS || dwError == ERROR_TOO_MANY_CMDS) {
						(*mon)->beginRead();
						auto base = (*mon)->server->base;
						base->callAsync([=] { base->fire(DirectoryMonitorListener::Overflow(), (*mon)->path); });
					} else {
						throw MonitorException(getErrorStr(dwError));
					}
				} else {
					if ((*mon)->errorCount > 0) {
						//LogManager::getInstance()->message("Monitoring was successfully restored for " + Text::fromT((*mon)->path), LogManager::LOG_ERROR);
						(*mon)->errorCount = 0;
					}

					if (dwBytesXFered > 0) {
						(*mon)->changes++;
						(*mon)->queueNotificationTask(dwBytesXFered);
					} /*else {
						LogManager::getInstance()->message("An empty notification was received when monitoring " + Text::fromT((*mon)->path) + " (report this)", LogManager::LOG_WARNING);
					}*/

					(*mon)->beginRead();
				}
			} catch (const MonitorException& e) {
				(*mon)->errorCount++;
				if ((*mon)->errorCount < 60) {
					//we'll most likely get the error instantly again...
					Thread::sleep(1000);

					try {
						(*mon)->openDirectory(m_hIOCP);
						(*mon)->beginRead();
						return 1;
					} catch (const MonitorException& /*e*/) {
						//go to removal
					}
				}

				failDirectory((*mon)->path, e.getError());
			}
		}
	}

	return 1;
}

void DirectoryMonitor::Server::deleteDirectory(DirectoryMonitor::Server::MonitorMap::iterator mon) {
	delete mon->second;
	monitors.erase(mon);

	if (monitors.empty()) {
		//MSDN: The completion port is freed when there are
		//no more references to it
		if (m_hIOCP) {
			CloseHandle(m_hIOCP);
			m_hIOCP = NULL;
		}
	}
}

bool DirectoryMonitor::Server::addDirectory(const string& aPath) throw(MonitorException) {
	{
		RLock l(cs);
		if (monitors.find(aPath) != monitors.end())
			return false;
	}

	init();

	Monitor* mon = new Monitor(aPath, this, 0, 32 * 1024, true);
	try {
		mon->openDirectory(m_hIOCP);

		{
			WLock l(cs);
			mon->beginRead();
			monitors.emplace(aPath, mon);
			failedDirectories.erase(aPath);
		}
	} catch (MonitorException& e) {
		mon->stopMonitoring();
		delete mon;

		{
			WLock l(cs);
			failedDirectories.insert(aPath);
		}

		throw e;
	}

	return true;
}

#else

bool DirectoryMonitor::Server::addDirectory(const string& aPath) throw(MonitorException) {
	//int fd = inotify_init();
	//if (fd < 0)
	//	throw MonitorException(Util::translateError(::GetLastError()));

	/*int wd = inotify_add_watch(fd, Text::fromUtf8(aPath), IN_MODIFY);
	if (wd < 0)
		throw MonitorException(Util::translateError(::GetLastError()));


	int cfg = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev);
	if (cfg < 0)
		throw MonitorException(Util::translateError(::GetLastError()));*/

	return true;
}

void DirectoryMonitor::Server::deleteDirectory(DirectoryMonitor::Server::MonitorMap::iterator mon) {

}

int DirectoryMonitor::Server::read() {
	/*struct epoll_event ev;
	ev.events = EPOLLIN|EPOLLOUT|EPOLLET;
	int ret = epoll_wait(efd, &ev, 100, 86400000);
	if (ret > 0) {
		
	} else if (ret < 0) {
		perror("Error in the polling");
		break;
	} else {
		perror("Timed Out");
		break;
	}*/
	return 0;
}

void DirectoryMonitor::processNotification(const string& aPath, const ByteVector& aBuf) {

}

#endif

bool DirectoryMonitor::addDirectory(const string& aPath) throw(MonitorException) {
	return server->addDirectory(aPath);
}

bool DirectoryMonitor::removeDirectory(const string& aPath) {
	return server->removeDirectory(aPath);
}

set<string> DirectoryMonitor::restoreFailedPaths() {
	return server->restoreFailedPaths();
}

size_t DirectoryMonitor::getFailedCount() {
	return server->getFailedCount();
}

size_t DirectoryMonitor::clear() {
	return server->clear();
}

size_t DirectoryMonitor::Server::clear() {
	WLock l(cs);
	failedDirectories.clear();
	for (auto& m: monitors)
		m.second->stopMonitoring();
	return monitors.size();
}

bool DirectoryMonitor::Server::removeDirectory(const string& aPath) {
	WLock l(cs);
	auto p = monitors.find(aPath);
	if (p != monitors.end()) {
		p->second->stopMonitoring();
		return true;
	}

	auto ret = failedDirectories.erase(aPath);
	return ret > 0;
}

set<string> DirectoryMonitor::Server::restoreFailedPaths() {
	set<string> failedDirectoriesCopy, restoredDirectories;
	{
		RLock l(cs);
		failedDirectoriesCopy = failedDirectories;
	}

	for (const auto& dir : failedDirectoriesCopy) {
		try {
			addDirectory(dir);
			restoredDirectories.insert(dir);
		} catch (...) {
			//...
		}
	}

	if (!restoredDirectories.empty()) {
		WLock l(cs);
		for (const auto& dir : restoredDirectories){
			failedDirectories.erase(dir);
		}
	}

	return restoredDirectories;
}

void DirectoryMonitor::Server::deviceRemoved(const string& aDrive) {
	set<string> removedPaths;

	{
		RLock l(cs);
		for (const auto& path : monitors | map_keys) {
			if (AirUtil::isParentOrExact(aDrive, path)) {
				removedPaths.insert(path);
			}
		}
	}

	if (!removedPaths.empty()) {
		WLock l(cs);
		for (const auto& path : removedPaths) {
			failDirectory(path, STRING(DEVICE_REMOVED));
		}
	}
}

void DirectoryMonitor::Server::failDirectory(const string& aPath, const string& aReason) {
	auto mon = monitors.find(aPath);
	if (mon == monitors.end())
		return;

	mon->second->stopMonitoring();
	mon->second->server->base->fire(DirectoryMonitorListener::DirectoryFailed(), mon->first, aReason);
	failedDirectories.insert(mon->first);

	deleteDirectory(mon);
}

/*void DirectoryMonitor::Server::validatePathExistance() {
	TargetUtil::VolumeSet volumes;
	TargetUtil::getVolumes(volumes);

	

	set<string> failed;

	{
		RLock l(cs);
		for (const auto path : monitors | map_keys) {
			if (TargetUtil::getMountPath(path, volumes).empty()) {
				failed.insert(path);
			}
		}
	}

	if (!failed.empty()) {
		WLock l(cs);
		for (const auto& path : failed) {
			failDirectory(path, STRING(PATH_NOT_FOUND));
		}
	}
}*/

size_t DirectoryMonitor::Server::getFailedCount() {
	RLock l(cs);
	return failedDirectories.size();
}

string DirectoryMonitor::Server::getErrorStr(int error) {
	return STRING_F(ERROR_CODE_X, Util::translateError(error) % error);
}

string DirectoryMonitor::Server::getStats() const {
	string ret;
	bool first = true;

	RLock l(cs);
	for (auto& m: monitors) {
		if (!first)
			ret += "\r\n";
		ret += m.first + " (" + Util::toString(m.second->changes) + " change notifications)";
		first = false;
	}

	return ret;
}

bool DirectoryMonitor::Server::hasDirectories() const {
	RLock l(cs);
	return !monitors.empty();
}

#ifdef _WIN32

void DirectoryMonitor::processNotification(const string& aPath, const ByteVector& aBuf) {
	char* pBase = (char*)&aBuf[0];
	string oldPath;

	for (;;)
	{
		FILE_NOTIFY_INFORMATION& fni = (FILE_NOTIFY_INFORMATION&)*pBase;

		string notifyPath(Text::fromT(tstring(fni.FileName, fni.FileNameLength / sizeof(wchar_t) )));

		// If it could be a short filename, expand it.
		auto fileName = Util::getFileName(notifyPath);

		// The maximum length of an 8.3 filename is twelve, including the dot.
		/*if (fileName.length() <= 12 && fileName.front() == _T('~')) {
			// Convert to the long filename form. Unfortunately, this
			// does not work for deletions, so it's an imperfect fix.
			wchar_t wbuf[UNC_MAX_PATH];
			if (::GetLongPathName(Text::toT(notifyPath).c_str(), wbuf, _countof (wbuf)) > 0)
				notifyPath = Text::fromT(wbuf);
		}*/

		notifyPath = aPath + notifyPath;
		switch(fni.Action) {
			case FILE_ACTION_ADDED: 
				// The file was added to the directory.
				fire(DirectoryMonitorListener::FileCreated(), notifyPath);
				break;
			case FILE_ACTION_REMOVED: 
				// The file was removed from the directory.
				fire(DirectoryMonitorListener::FileDeleted(), notifyPath);
				break;
			case FILE_ACTION_RENAMED_OLD_NAME: 
				// The file was renamed and this is the old name. 
				oldPath = notifyPath;
				break;
			case FILE_ACTION_RENAMED_NEW_NAME: 
				// The file was renamed and this is the new name.
				fire(DirectoryMonitorListener::FileRenamed(), oldPath, notifyPath);
				break;
			case FILE_ACTION_MODIFIED:
				fire(DirectoryMonitorListener::FileModified(), notifyPath);
				break;
		}

		if (!fni.NextEntryOffset)
			break;
		pBase += fni.NextEntryOffset;
	};
}

#else

#endif

} //dcpp
