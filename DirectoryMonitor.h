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

#ifndef DCPLUSPLUS_DIRECTORY_MONITOR
#define DCPLUSPLUS_DIRECTORY_MONITOR

#include <functional>
#include <string>

#include "DirectoryMonitorListener.h"
#include "Speaker.h"

#include "typedefs.h"
#include "noexcept.h"
#include "atomic.h"
#include "Exception.h"
#include "Util.h"

#include "Thread.h"
#include "Semaphore.h"
#include <boost/lockfree/queue.hpp>

using std::string;

namespace dcpp {

//typedef std::function<void (const string&)> ActionFunction;
//typedef std::function<void (const string& /*old*/, const string& /*new*/)> RenameFunction;

STANDARD_EXCEPTION(MonitorException);

class Monitor;
class DirectoryMonitor : public Speaker<DirectoryMonitorListener>, public Thread {
public:
	enum Event {
		EVENT_FILEACTION,
		EVENT_OVERFLOW,
	};

	DirectoryMonitor(int numThreads, bool useDispatcherThread);
	~DirectoryMonitor();

	bool addDirectory(const string& aPath) throw(MonitorException);
	bool removeDirectory(const string& aPath);
	size_t clear();

	void stopMonitoring();
	void init() throw(MonitorException);

	// returns true as long as there are messages queued
	bool dispatch();
private:
	friend class Monitor;
	class Server : public Thread {
	public:
		Server(DirectoryMonitor* aBase, int numThreads);
		~Server();
		bool addDirectory(const string& aPath) throw(MonitorException);
		bool removeDirectory(const string& aPath);
		size_t clear();

		void stop();

		DirectoryMonitor* base;
		virtual int run();
		void init() throw(MonitorException);
	private:
		SharedMutex cs;
		std::unordered_map<string, Monitor*, noCaseStringHash, noCaseStringEq> monitors;

		int read();

		int	m_nThreads;
		bool m_bTerminate;
		HANDLE m_hIOCP;
		atomic_flag threadRunning;
	};

	struct NotifyTask {
		NotifyTask(Event aEvent, const tstring& aPath) : eventType(aEvent), path(aPath) { }
		Event eventType;
		ByteVector buf;
		tstring path;
	};

	virtual int run();
	boost::lockfree::queue<NotifyTask*> queue;
	bool stop;
	Semaphore s;
	const bool useDispatcherThread;

	Server* server;

	void addTask(NotifyTask* aTask);
	void processNotification(const tstring& aPath, ByteVector& aBuf);
};

class Monitor : boost::noncopyable {
public:
	friend class DirectoryMonitor;

	Monitor(const string& aPath, DirectoryMonitor::Server* aParent, int monitorFlags, size_t bufferSize, bool recursive);
	~Monitor();

	void openDirectory(HANDLE iocp);
	void beginRead();

	void stopMonitoring();

	void queueNotificationTask(int dwSize);
	DirectoryMonitor::Server* server;
private:
	void processNotification();

	// Parameters from the caller for ReadDirectoryChangesW().
	int				m_dwFlags;
	int				m_bChildren;
	const tstring	path;

	// Result of calling CreateFile().
	HANDLE		m_hDirectory;

	// Required parameter for ReadDirectoryChangesW().
	OVERLAPPED	m_Overlapped;

	// Data buffer for the request.
	// Since the memory is allocated by malloc, it will always
	// be aligned as required by ReadDirectoryChangesW().
	ByteVector m_Buffer;
	int errorCount;
};

} //dcpp

#endif // DCPLUSPLUS_DIRECTORY_MONITOR