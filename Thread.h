/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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
#include <sched.h>
#include <sys/resource.h>
#endif

#include <boost/noncopyable.hpp>
#include "Exception.h"
#include <stdint.h>
#include <thread>

namespace dcpp {

STANDARD_EXCEPTION(ThreadException);

class Thread : private boost::noncopyable
{
public:
#ifdef _WIN32
	enum Priority {
		IDLE = THREAD_PRIORITY_IDLE,
		LOW = THREAD_PRIORITY_BELOW_NORMAL,
		NORMAL = THREAD_PRIORITY_NORMAL,
		HIGH = THREAD_PRIORITY_ABOVE_NORMAL
	};

	Thread() : threadHandle(INVALID_HANDLE_VALUE) { }
	virtual ~Thread() { 
		if(threadHandle != INVALID_HANDLE_VALUE)
			CloseHandle(threadHandle);
	}
	
	void start();
	void join() {
		if(threadHandle == INVALID_HANDLE_VALUE) {
			return;
		}

		WaitForSingleObject(threadHandle, INFINITE);
		CloseHandle(threadHandle);
		threadHandle = INVALID_HANDLE_VALUE;
	}

	void setThreadPriority(Priority p) { ::SetThreadPriority(threadHandle, p); }

	void t_suspend() { //pause a worker thread, BE Careful by using this, Thread must be in sync so it wont lock up any unwanted resources. Call only from the suspended thread because of non-win implementation
		if(threadHandle == INVALID_HANDLE_VALUE) {
			return;
		}
		::SuspendThread(threadHandle);
	}

	void t_resume() {
		if(threadHandle == INVALID_HANDLE_VALUE) {
			return;
		}
		::ResumeThread(threadHandle);
	}


	static void sleep(uint64_t millis) { ::Sleep(static_cast<DWORD>(millis)); }
	static void yield() { ::Sleep(0); }
	

#else

	enum Priority {
		IDLE = 1,
		LOW = 1,
		NORMAL = 0,
		HIGH = -1
	};
	Thread() : threadHandle(0) { }
	virtual ~Thread() { 
		if(threadHandle != 0) {
			pthread_detach(threadHandle);
		}
	}
	void start();
	void join() { 
		if (threadHandle) {
			pthread_join(threadHandle, 0);
			threadHandle = 0;
		}
	}

	void t_suspend() { //pause a worker thread, BE Careful by using this, Thread must be in sync so it wont lock up any unwanted resources. Call only from the suspended thread
		pthread_mutex_lock(&lock);
		suspended = true;
		while (suspended)
			pthread_cond_wait(&cond, &lock);
		pthread_mutex_unlock(&lock);
	}

	void t_resume() {
		pthread_mutex_lock(&lock);
		suspended = false;
		pthread_cond_signal(&cond);
		pthread_mutex_unlock(&lock);
	}

	void setThreadPriority(Priority p) { setpriority(PRIO_PROCESS, 0, p); }
	static void sleep(uint32_t millis) { ::usleep(millis*1000); }
	static void yield() { ::sched_yield(); }
#endif

protected:
	virtual int run() = 0;
	
#ifdef _WIN32
	HANDLE threadHandle;

	static unsigned int WINAPI starter(void* p) {
		#ifdef _DEBUG
		_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
		#endif
		Thread* t = (Thread*)p;
		t->run();
		return 0;
	}
#else
	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
	bool suspended = false;

	pthread_t threadHandle;
	static void* starter(void* p) {
		Thread* t = (Thread*)p;
		t->run();
		return NULL;
	}
#endif
};

} // namespace dcpp

#endif // !defined(THREAD_H)
