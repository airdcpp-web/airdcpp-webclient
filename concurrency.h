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

#ifndef DCPLUSPLUS_DCPP_CONCURRENCY
#define DCPLUSPLUS_DCPP_CONCURRENCY

//#define HAVE_INTEL_TBB

#if defined(HAVE_INTEL_TBB)

#include <tbb/task_group.h>
#include <tbb/parallel_for_each.h>
#include <tbb/concurrent_queue.h>

namespace dcpp
{

using tbb::concurrent_queue;
using tbb::parallel_for_each;
using tbb::task_group;

class TaskScheduler {
public:
	TaskScheduler() { }

	~TaskScheduler() { }
private:
	task_scheduler_init init;
};

}

#elif _MSC_VER

#include <ppl.h>
#include <concurrent_queue.h>

namespace dcpp
{

using concurrency::concurrent_queue;
using concurrency::task_group;
using concurrency::parallel_for_each;

class TaskScheduler {
public:
	TaskScheduler() {
		concurrency::CurrentScheduler::Create(concurrency::CurrentScheduler::GetPolicy());
	}

	~TaskScheduler() {
		concurrency::CurrentScheduler::Detach();
	}
};

}

#else

#include <deque>
#include "CriticalSection.h"

namespace dcpp {

class TaskScheduler {
public:
	TaskScheduler() { }
	~TaskScheduler() { }
};

#define parallel_for_each for_each

	template <typename T>
	class concurrent_queue {
	public:
		bool push(const T& t) {
			WLock l(cs);
			queue.push_back(t);
			return true;
		}

		template <typename U>
		bool try_pop(U& t) {
			WLock l(cs);
			if (!queue.empty()) {
				t = std::move(queue.front());
				queue.pop_front();
				return true;
			}
			return false;
		}
	private:
		SharedMutex cs;
		std::deque<T> queue;
	};
}

#endif



#endif /* DCPLUSPLUS_DCPP_ATOMIC_HPP_ */
