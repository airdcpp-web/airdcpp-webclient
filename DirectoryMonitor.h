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

#include "DirectoryMonitorListener.h"
#include "Speaker.h"

#include "typedefs.h"
#include "noexcept.h"
#include "atomic.h"

#include "Exception.h"
#include "Thread.h"
#include "Util.h"

#include "DispatcherQueue.h"

using std::string;

namespace dcpp {

typedef std::function<void ()> AsyncF;

STANDARD_EXCEPTION(MonitorException);

class Monitor;
class DirectoryMonitor : public Speaker<DirectoryMonitorListener> {
public:
	DirectoryMonitor(int numThreads, bool useDispatcherThread);
	~DirectoryMonitor();

	bool addDirectory(const string& aPath) throw(MonitorException);
	bool removeDirectory(const string& aPath);
	size_t clear();

	void stopMonitoring();
	void init() throw(MonitorException);

	// returns true as long as there are messages queued
	bool dispatch();
	void callAsync(const DispatcherQueue::Callback& aF);
	string getStats() const {
		return server->getStats();
	}
	bool hasDirectories() const {
		return server->hasDirectories();
	}
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

		string getStats() const;
		bool hasDirectories() const;
		static string getErrorStr(int error);
	private:
		typedef std::unordered_map<string, Monitor*, noCaseStringHash, noCaseStringEq> MonitorMap;
		mutable SharedMutex cs;
		MonitorMap monitors;

		int read();

		bool m_bTerminate;
		atomic_flag threadRunning;

		void deleteDirectory(MonitorMap::iterator mon);

#ifdef WIN32
		HANDLE m_hIOCP;
#else
		int efd = -1;
		int fd = -1;
#endif
		int	m_nThreads;
	};

	Server* server;

	void processNotification(const string& aPath, const ByteVector& aBuf);
	DispatcherQueue dispatcher;
};

class Monitor : boost::noncopyable {
public:
	static int lastKey;
	friend class DirectoryMonitor;

#ifdef WIN32
	Monitor(const string& aPath, DirectoryMonitor::Server* aParent, int monitorFlags, size_t bufferSize, bool recursive);
	~Monitor();

	void openDirectory(HANDLE iocp);
	void beginRead();
#else
	//int addWatch(const string& aPath);

	Monitor(const string& aPath, DirectoryMonitor::Server* aParent, int monitorFlags, size_t bufferSize);
	~Monitor();
#endif

	void stopMonitoring();

	void queueNotificationTask(int dwSize);
	DirectoryMonitor::Server* server;
private:
	uint64_t changes;
#ifdef WIN32
	void processNotification();

	// Parameters from the caller for ReadDirectoryChangesW().
	int				m_dwFlags;
	int				m_bChildren;
	const string	path;

	// Result of calling CreateFile().
	HANDLE		m_hDirectory;

	// Required parameter for ReadDirectoryChangesW().
	OVERLAPPED	m_Overlapped;

	// Data buffer for the request.
	// Since the memory is allocated by malloc, it will always
	// be aligned as required by ReadDirectoryChangesW().
	ByteVector m_Buffer;
	int errorCount;
	int key;
#else

#endif
};

} //dcpp

#endif // DCPLUSPLUS_DIRECTORY_MONITOR
