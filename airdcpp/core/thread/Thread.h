/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#ifndef DCPLUSPLUS_DCPP_THREAD_H
#define DCPLUSPLUS_DCPP_THREAD_H

#ifdef _WIN32
#include <airdcpp/core/header/w.h>
#else
#include <pthread.h>
#include <sys/resource.h>
#endif

#include <stdint.h>

namespace dcpp {

class Thread
{
public:
#ifdef _WIN32
	enum Priority {
		IDLE = THREAD_PRIORITY_IDLE,
		LOWEST = THREAD_PRIORITY_LOWEST,
		LOW = THREAD_PRIORITY_BELOW_NORMAL,
		NORMAL = THREAD_PRIORITY_NORMAL,
		HIGH = THREAD_PRIORITY_ABOVE_NORMAL,
		HIGHEST = THREAD_PRIORITY_HIGHEST
	};

	static void sleep(uint64_t millis) {
		::Sleep(static_cast<DWORD>(millis));
	}

	typedef HANDLE ThreadHandleType;

#ifdef _DEBUG
	bool isCurrentThread() const noexcept;
#endif
#else

#ifdef __linux__
	enum Priority {
		IDLE = SCHED_IDLE,
		LOWEST = SCHED_BATCH,
		LOW = SCHED_BATCH,
		NORMAL = SCHED_OTHER,
		HIGH = SCHED_OTHER,
		HIGHEST = SCHED_OTHER
	};
#elif __APPLE__
	enum Priority {
		IDLE = PRIO_DARWIN_BG,
		LOWEST = PRIO_DARWIN_BG,
		LOW = 0,
		NORMAL = 0,
		HIGH = 0,
		HIGHEST = 0
	};
#else
	enum Priority {
		IDLE = 0,
		LOWEST = 2,
		LOW = 5,
		NORMAL = 10,
		HIGH = 20,
		HIGHEST = 30
	};
#endif

	static void sleep(uint32_t millis) {
		::usleep(millis * 1000);
	}

	typedef pthread_t ThreadHandleType;
#endif
	Thread();
	virtual ~Thread();

	Thread(const Thread&) = delete;
	Thread& operator=(const Thread&) = delete;


	void start();
	void join();

	void setThreadPriority(Priority p);

	// Pause a worker thread, BE Careful by using this, thread must be in sync so it wont lock up any unwanted resources. 
	// Call only from the suspended thread because of non-win implementation
	void t_suspend();
	void t_resume();

	static void yield();
protected:
	virtual int run() = 0;

	ThreadHandleType threadHandle;
#ifdef _WIN32

	DWORD threadId;
	static unsigned int WINAPI starter(void* p);
#else
	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	bool suspended = false;

	static void* starter(void* p);
#endif
};

} // namespace dcpp

#endif // !defined(THREAD_H)
