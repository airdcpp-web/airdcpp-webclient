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

#include "stdinc.h"
#include <airdcpp/core/thread/Thread.h>

#include <airdcpp/core/classes/Exception.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/util/SystemUtil.h>
#include <airdcpp/util/Util.h>

#ifdef _WIN32
#include <process.h>
#else
#include <sched.h>
#endif

namespace dcpp {
	
#ifdef _WIN32

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
	auto t = (Thread*)p;
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

void Thread::sleep(uint64_t millis) noexcept {
	::Sleep(static_cast<DWORD>(millis));
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

void Thread::setThreadPriority(Priority p, ThreadHandleType aHandle) noexcept {
	if (aHandle == INVALID_HANDLE_VALUE) {
		return;
	}

	if (!::SetThreadPriority(aHandle, p)) {
		dcdebug("Unable to set thread priority: %s", SystemUtil::translateError(GetLastError()).c_str());
		//dcassert(0);
		//throw ThreadException("Unable to set thread priority: " + SystemUtil::translateError(GetLastError()));
	}
}

void Thread::yield() noexcept {
	::Sleep(0);
}

Thread::ThreadHandleType Thread::getCurrentThread() noexcept {
	return GetCurrentThread();
}

#ifdef _DEBUG
bool Thread::isCurrentThread() const noexcept {
	auto currentThreadId = GetCurrentThreadId();
	return currentThreadId == threadId;
}
#endif

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
	if (!threadHandle) {
		return;
	}

	pthread_join(threadHandle, 0);
	threadHandle = 0;
}

#ifdef __linux__ 

void Thread::setThreadPriority(Priority p, ThreadHandleType aHandle) noexcept {
	if (!aHandle) {
		return;
	}

	sched_param params;
	params.sched_priority = 0; // Must always 0 on Linux
	if (pthread_setschedparam(aHandle, p, &params) != 0) {
		dcassert(0);
		//throw ThreadException("Unable to set thread priority: " + SystemUtil::translateError(errno));
	}
}

#elif __APPLE__

void Thread::setThreadPriority(Priority, ThreadHandleType) noexcept {
	// Not available
}

#else

void Thread::setThreadPriority(Priority p, ThreadHandleType aHandle) noexcept {
	if (!aHandle) {
		return;
	}

	int policy;
	sched_param params;
	if (pthread_getschedparam(aHandle, &policy, &params) != 0) {
		dcassert(0);
		//throw ThreadException("Unable to set thread priority: " + SystemUtil::translateError(errno));
		return;
	}

	params.sched_priority = p;
	if (pthread_setschedparam(aHandle, p, &params) != 0) {
		dcassert(0);
		//throw ThreadException("Unable to set thread priority: " + SystemUtil::translateError(errno));
	}
}


#endif

void Thread::yield() noexcept {
	::sched_yield();
}

void Thread::sleep(uint64_t millis) noexcept {
	::usleep(static_cast<useconds_t>(millis) * 1000);
}

Thread::ThreadHandleType Thread::getCurrentThread() noexcept {
	return pthread_self();
}

#endif


#if __APPLE__

void Thread::setCurrentThreadPriority(Priority p) noexcept {
	if (setpriority(PRIO_DARWIN_THREAD, 0, p) != 0) {
		dcassert(0);
		//throw ThreadException("Unable to set thread priority: " + SystemUtil::translateError(errno));
	}
}

#else

void Thread::setCurrentThreadPriority(Priority p) noexcept {
	setThreadPriority(p, getCurrentThread());
}

#endif


void Thread::setThreadPriority(Priority p) noexcept {
	setThreadPriority(p, threadHandle);
}


} // namespace dcpp