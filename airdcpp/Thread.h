/*
 * Copyright (C) 2001-2017 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_THREAD_H
#define DCPLUSPLUS_DCPP_THREAD_H

#ifdef _WIN32
#include "w.h"
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
#else
	enum Priority {
		IDLE = 19,
		LOWEST = 14,
		LOW = 7,
		NORMAL = 0,
		// Priorities above normal ( <0 ) can't be used without root permissions
		HIGH = 0,
		HIGHEST = 0
	};

	static void sleep(uint32_t millis) {
		::usleep(millis * 1000);
	}
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
	
#ifdef _WIN32
	HANDLE threadHandle;

	static unsigned int WINAPI starter(void* p);
#else
	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	bool suspended = false;

	pthread_t threadHandle;
	static void* starter(void* p);
#endif
};

} // namespace dcpp

#endif // !defined(THREAD_H)
