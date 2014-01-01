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

#ifndef DCPLUSPLUS_DCPP_TASK_H
#define DCPLUSPLUS_DCPP_TASK_H

#include <functional>

#include "forward.h"
#include "Thread.h"

namespace dcpp {

struct Task {
	virtual ~Task() { };
};

struct AsyncTask : public Task {
	AsyncTask(AsyncF aF) : f(aF) { }
	AsyncF f;
};

struct StringTask : public Task {
	StringTask(const string& str_) : str(str_) { }
	string str;
};


class TaskQueue {
public:
	typedef pair<uint8_t, std::unique_ptr<Task>> UniqueTaskPair;
	typedef pair<uint8_t, Task*> TaskPair;
	typedef deque<UniqueTaskPair> List;

	TaskQueue() {
	}

	~TaskQueue() {
		clear();
	}

	void add(UniqueTaskPair& t) { 
		Lock l(cs); 
		tasks.push_back(move(t)); 
	}

	void add(uint8_t type, std::unique_ptr<Task> && data) { 
		Lock l(cs); 
		tasks.emplace_back(type, move(data)); 
	}

	bool addUnique(uint8_t type, std::unique_ptr<Task> && data) { 
		Lock l(cs);
		auto p = boost::find_if(tasks, [type, this](const UniqueTaskPair& tp) { return tp.first == type; });
		if (p == tasks.end()) {
			tasks.emplace_back(type, move(data));
			return true;
		}
		return false;
	}


	void get(List& list) { Lock l(cs); swap(tasks, list); }
	bool getFront(TaskPair& t) { 
		Lock l(cs); 
		if (tasks.empty())
			return false;
		t = make_pair(tasks.front().first, tasks.front().second.get());
		return true;
	}

	void pop_front() {
		Lock l(cs);
		dcassert(!tasks.empty());
		tasks.pop_front();
	}

	void clear() {
		List tmp;
		get(tmp);
	}

	List& getTasks() { return tasks; }
	CriticalSection cs;
private:
	TaskQueue(const TaskQueue&);
	TaskQueue& operator=(const TaskQueue&);

	List tasks;
};

} // namespace dcpp

#endif
