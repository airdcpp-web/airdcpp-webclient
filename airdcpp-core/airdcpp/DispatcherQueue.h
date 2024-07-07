/*
* Copyright (C) 2011-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DISPATCHER_QUEUE
#define DCPLUSPLUS_DISPATCHER_QUEUE


#include "Thread.h"
#include "Semaphore.h"

#include "concurrency.h"

namespace dcpp {

class DispatcherQueue : public Thread {
public:
	typedef std::function<void(Callback&)> DispatchF;

	// You may pass an optional function that will handle executing the callbacks (can be used for exception handling)
	DispatcherQueue(bool aStartThread, Thread::Priority aThreadPrio = Thread::NORMAL, DispatchF aDispatchF = nullptr) : threadPriority(aThreadPrio), dispatchF(aDispatchF) {
		if (aStartThread) {
			start();
		}
	}

	~DispatcherQueue() {
		stopping = true;
		if (started) {
			s.signal();
			join();
		}
	}

	void start() {
		started = true;

		Thread::start();
	}

	// The function will be executed after the thread has been stopped
	void stop(Callback aCompletionF = nullptr) noexcept {
		stopF = aCompletionF;
		stopping = true;
		s.signal();
	}

	void addTask(Callback&& aTask) {
		queue.push(new Callback(std::move(aTask)));
		if (started)
			s.signal();
	}

	int run() {
		setThreadPriority(threadPriority);
		while (true) {
			s.wait();
			if (stopping) {
				stopping = false;
				started = false;
				if (stopF) {
					stopF();
				}

				break;
			}

			dispatch();
		}
		return 0;
	}

	bool dispatch() {
		if (!queue.try_pop(t)) {
			return false;
		}

		if (dispatchF) {
			dispatchF(*t);
		} else {
			(*t)();
		}

		delete t;
		return true;
	}
private:
	Semaphore s;
	concurrent_queue<Callback*> queue;

	Callback* t = nullptr;
	bool stopping = false;
	bool started = false;
	const Thread::Priority threadPriority;

	// Optional function that can be passed to the stop function
	Callback stopF = nullptr;

	// Function that will execute the callbacks
	DispatchF dispatchF = nullptr;
};

}

#endif