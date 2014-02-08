/*
* Copyright (C) 2011-2014 AirDC++ Project
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

#ifndef DCPLUSPLUS_DISPATCHER_QUEUE
#define DCPLUSPLUS_DISPATCHER_QUEUE


#include "Thread.h"
#include "Semaphore.h"

#include "concurrency.h"

namespace dcpp {

class DispatcherQueue : public Thread {
public:
	typedef std::function<void()> Callback;
	DispatcherQueue(bool aUseDispatcherThread, Thread::Priority aThreadPrio = Thread::NORMAL) : stop(false), useDispatcherThread(aUseDispatcherThread) {
		if (useDispatcherThread) {
			start();
			setThreadPriority(aThreadPrio);
		}
	}

	~DispatcherQueue() {
		stop = true;
		if (useDispatcherThread) {
			s.signal();
			join();
		}
	}

	void addTask(Callback* aTask) {
		queue.push(aTask);
		if (useDispatcherThread)
			s.signal();
	}

	int run() {
		while (true) {
			s.wait();
			if (stop)
				break;

			dispatch();
		}
		return 0;
	}

	bool dispatch() {
		if (!queue.try_pop(t)) {
			return false;
		}

		(*t)();
		delete t;
		return true;
	}
private:
	Semaphore s;
	concurrent_queue<Callback*> queue;

	Callback* t = nullptr;
	bool stop = false;
	const bool useDispatcherThread;
};

}

#endif