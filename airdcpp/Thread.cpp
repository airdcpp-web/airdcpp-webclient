/*
 * Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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
#include "Thread.h"

#include "Exception.h"
#include "ResourceManager.h"
#include "Util.h"

#ifdef _WIN32
#include <process.h>
#else
#include <sched.h>
#endif

namespace dcpp {
	
#ifdef _WIN32

DWORD threadId;
void Thread::start() {
	join();
	if ((threadHandle = reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, &starter, this, 0, reinterpret_cast<unsigned int*>(&threadId)))) == NULL) {
		throw ThreadException(STRING(UNABLE_TO_CREATE_THREAD));
	}
}

Thread::Thread() : threadHandle(INVALID_HANDLE_VALUE) { 

}

Thread::~Thread() {
	if (threadHandle != INVALID_HANDLE_VALUE)
		CloseHandle(threadHandle);
}

unsigned int WINAPI Thread::starter(void* p) {
#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	Thread* t = (Thread*)p;
	t->run();
	return 0;
}

void Thread::join() {
	if (threadHandle == INVALID_HANDLE_VALUE) {
		return;
	}

	WaitForSingleObject(threadHandle, INFINITE);
	CloseHandle(threadHandle);
	threadHandle = INVALID_HANDLE_VALUE;
}

void Thread::t_suspend() {
	if (threadHandle == INVALID_HANDLE_VALUE) {
		return;
	}
	::SuspendThread(threadHandle);
}

void Thread::t_resume() {
	if (threadHandle == INVALID_HANDLE_VALUE) {
		return;
	}
	::ResumeThread(threadHandle);
}

void Thread::setThreadPriority(Priority p) {
	if (!::SetThreadPriority(threadHandle, p)) {
		dcassert(0);
		//throw ThreadException("Unable to set thread priority: " + Util::translateError(GetLastError()));
	}
}

void Thread::yield() { 
	::Sleep(0);
}

#else
void Thread::start() {
	join();
	if (pthread_create(&threadHandle, NULL, &starter, this) != 0) {
		throw ThreadException(STRING(UNABLE_TO_CREATE_THREAD));
	}
}

void* Thread::starter(void* p) {
	Thread* t = (Thread*)p;
	t->run();
	return NULL;
}

Thread::Thread() : threadHandle(0) { }

Thread::~Thread() {
	if (threadHandle != 0) {
		pthread_detach(threadHandle);
	}
}

void Thread::t_suspend() {
	pthread_mutex_lock(&lock);
	suspended = true;
	while (suspended)
		pthread_cond_wait(&cond, &lock);
	pthread_mutex_unlock(&lock);
}

void Thread::t_resume() {
	pthread_mutex_lock(&lock);
	suspended = false;
	pthread_cond_signal(&cond);
	pthread_mutex_unlock(&lock);
}

void Thread::join() {
	if (threadHandle) {
		pthread_join(threadHandle, 0);
		threadHandle = 0;
	}
}

void Thread::setThreadPriority(Priority p) {
	if (setpriority(PRIO_PROCESS, 0, p) != 0) {
		dcassert(0);
		//throw ThreadException("Unable to set thread priority: " + Util::translateError(errno));
	}
}

void Thread::yield() {
	::sched_yield();
}

#endif

} // namespace dcpp