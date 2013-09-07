/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#include "ResourceManager.h"

#ifdef _WIN32
#include <process.h>
#endif

namespace dcpp {
	
#ifdef _WIN32

SharedMutex::SharedMutex() {
	InitializeSRWLock(&psrw);
}

SharedMutex::~SharedMutex() {

}

void SharedMutex::lock_shared() {
	AcquireSRWLockShared(&psrw);
}

void SharedMutex::lock() {
	AcquireSRWLockExclusive(&psrw);
}

void SharedMutex::unlock_shared() {
	ReleaseSRWLockShared(&psrw);
}

void SharedMutex::unlock() {
	ReleaseSRWLockExclusive(&psrw);
}


RLock::RLock(SharedMutex& aCS) : cs(&aCS) {
	aCS.lock_shared();
}

RLock::~RLock() {
	cs->unlock_shared();
}

WLock::WLock(SharedMutex& aCS) : cs(&aCS) {
	aCS.lock();
}

WLock::~WLock() {
	cs->unlock();
}

DWORD threadId;
void Thread::start() {
	join();
	if ((threadHandle = reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, &starter, this, 0, reinterpret_cast<unsigned int*>(&threadId)))) == NULL) {
		throw ThreadException(STRING(UNABLE_TO_CREATE_THREAD));
	}
}


#else
	void Thread::start() {
		join();
		if (pthread_create(&threadHandle, NULL, &starter, this) != 0) {
			throw ThreadException(STRING(UNABLE_TO_CREATE_THREAD));
		}
	}
#endif

ConditionalRLock::ConditionalRLock(SharedMutex& aCS, bool aLock) : cs(&aCS), lock(aLock) {
	if (lock)
		aCS.lock_shared();
}

ConditionalRLock::~ConditionalRLock() {
	if (lock)
		cs->unlock_shared();
}

ConditionalWLock::ConditionalWLock(SharedMutex& aCS, bool aLock) : cs(&aCS), lock(aLock) {
	if (lock)
		aCS.lock();
}

ConditionalWLock::~ConditionalWLock() {
	if (lock)
		cs->unlock();
}


} // namespace dcpp