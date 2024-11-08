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

#ifndef DCPLUSPLUS_DCPP_CRITICALSECTION_H
#define DCPLUSPLUS_DCPP_CRITICALSECTION_H

#include <mutex>
#include <shared_mutex>

#ifndef _WIN32
#include <pthread.h>
#endif

namespace dcpp {

using FastCriticalSection = boost::detail::spinlock;
using FastLock = std::lock_guard<boost::detail::spinlock>;

using SharedMutex = std::shared_mutex;
using RLock = std::shared_lock<std::shared_mutex>;
using WLock = std::unique_lock<std::shared_mutex>;

#ifndef _WIN32

// A custom implementation is required for Semaphore
class CriticalSection {
public:
	CriticalSection() noexcept{
		pthread_mutexattr_init(&ma);
		pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&mtx, &ma);
	}
	~CriticalSection() noexcept{
		pthread_mutex_destroy(&mtx);
		pthread_mutexattr_destroy(&ma);
	}
	void lock() noexcept{ pthread_mutex_lock(&mtx); }
	void unlock() noexcept{ pthread_mutex_unlock(&mtx); }
	pthread_mutex_t& getMutex() { return mtx; }

	CriticalSection(const CriticalSection&) = delete;
	CriticalSection& operator=(const CriticalSection&) = delete;
private:
	pthread_mutex_t mtx;
	pthread_mutexattr_t ma;
};

template<class T>
class LockBase {
public:
	LockBase(T& aCs) noexcept : cs(aCs) { cs.lock(); }
	~LockBase() noexcept{ cs.unlock(); }
private:
	LockBase& operator=(const LockBase&);
	T& cs;
};

typedef LockBase<CriticalSection> Lock;

#else

using CriticalSection = std::recursive_mutex;
using Lock = std::scoped_lock <std::recursive_mutex>;

#endif

class ConditionalRLock {
public:
	ConditionalRLock(SharedMutex& cs, bool lock);
	~ConditionalRLock();

	ConditionalRLock(const ConditionalRLock&) = delete;
	ConditionalRLock& operator=(const ConditionalRLock&) = delete;
private:
	SharedMutex* cs;
	bool lock;
};

struct ConditionalWLock {
public:
	ConditionalWLock(SharedMutex& cs, bool lock);
	~ConditionalWLock();

	ConditionalWLock(const ConditionalWLock&) = delete;
	ConditionalWLock& operator=(const ConditionalWLock&) = delete;
private:
	SharedMutex* cs;
	bool lock;
};

}

#endif