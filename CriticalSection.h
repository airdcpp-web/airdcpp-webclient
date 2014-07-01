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

#ifndef DCPLUSPLUS_DCPP_CRITICALSECTION_H
#define DCPLUSPLUS_DCPP_CRITICALSECTION_H

#ifdef _WIN32
#include "w.h"
#else
#include <pthread.h>
//#include <sched.h>
//#include <sys/resource.h>
#endif

#include <boost/noncopyable.hpp>
#include <stdint.h>

#include <boost/thread.hpp>

namespace dcpp {

typedef boost::detail::spinlock	FastCriticalSection;
typedef boost::lock_guard<boost::detail::spinlock> FastLock;


#ifndef _WIN32
typedef boost::shared_mutex	SharedMutex;
typedef boost::shared_lock<boost::shared_mutex> RLock;
typedef boost::unique_lock<boost::shared_mutex> WLock;

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

typedef boost::recursive_mutex	CriticalSection;
typedef boost::lock_guard<boost::recursive_mutex> Lock;
typedef boost::shared_mutex FLock;

// A custom Slim Reader/Writer (SRW) solution, only works on Windows Vista or newer

class SharedMutex {
public:
	SharedMutex();
	~SharedMutex();

	void lock_shared();
	void unlock_shared();

	void lock();
	void unlock();

	SharedMutex(const SharedMutex&) = delete;
	SharedMutex& operator=(const SharedMutex&) = delete;
private:
	SRWLOCK psrw;
};

class RLock {
public:
	RLock(SharedMutex& cs);
	~RLock();

	RLock(const RLock&) = delete;
	RLock& operator=(const RLock&) = delete;
private:
	SharedMutex* cs;
};

struct WLock {
public:
	WLock(SharedMutex& cs);
	~WLock();

	WLock(const WLock&) = delete;
	WLock& operator=(const WLock&) = delete;
private:
	SharedMutex* cs;
};

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