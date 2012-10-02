/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SPEAKER_H
#define DCPLUSPLUS_DCPP_SPEAKER_H

#include <boost/range/algorithm/find.hpp>
#include <utility>
#include <vector>

#include "Thread.h"

#include "noexcept.h"

namespace dcpp {

using std::forward;
using std::vector;
using boost::range::find;

template<typename Listener>
class Speaker {
	typedef vector<Listener*> ListenerList;

public:
	Speaker() noexcept { }
	virtual ~Speaker() { }

	/// @todo simplify when we have variadic templates

	template<typename T0>
	void fire(T0&& type) noexcept {
		Lock l(listenerCS);
		tmp = listeners;
		for(auto i = tmp.begin(); i != tmp.end(); ++i) {
			(*i)->on(forward<T0>(type));
		}
	}

	template<typename T0, typename T1>
	void fire(T0&& type, T1&& p1) noexcept {
		Lock l(listenerCS);
		tmp = listeners;
		for(auto i = tmp.begin(); i != tmp.end(); ++i) {
			(*i)->on(forward<T0>(type), forward<T1>(p1));
		}
	}

	template<typename T0, typename T1, typename T2>
	void fire(T0&& type, T1&& p1, T2&& p2) noexcept {
		Lock l(listenerCS);
		tmp = listeners;
		for(auto i = tmp.begin(); i != tmp.end(); ++i) {
			(*i)->on(forward<T0>(type), forward<T1>(p1), forward<T2>(p2));
		}
	}

	template<typename T0, typename T1, typename T2, typename T3>
	void fire(T0&& type, T1&& p1, T2&& p2, T3&& p3) noexcept {
		Lock l(listenerCS);
		tmp = listeners;
		for(auto i = tmp.begin(); i != tmp.end(); ++i) {
			(*i)->on(forward<T0>(type), forward<T1>(p1), forward<T2>(p2), forward<T3>(p3));
		}
	}

	template<typename T0, typename T1, typename T2, typename T3, typename T4>
	void fire(T0&& type, T1&& p1, T2&& p2, T3&& p3, T4&& p4) noexcept {
		Lock l(listenerCS);
		tmp = listeners;
		for(auto i = tmp.begin(); i != tmp.end(); ++i) {
			(*i)->on(forward<T0>(type), forward<T1>(p1), forward<T2>(p2), forward<T3>(p3), forward<T4>(p4));
		}
	}

	template<typename T0, typename T1, typename T2, typename T3, typename T4, typename T5>
	void fire(T0&& type, T1&& p1, T2&& p2, T3&& p3, T4&& p4, T5&& p5) noexcept {
		Lock l(listenerCS);
		tmp = listeners;
		for(auto i = tmp.begin(); i != tmp.end(); ++i) {
			(*i)->on(forward<T0>(type), forward<T1>(p1), forward<T2>(p2), forward<T3>(p3), forward<T4>(p4), forward<T5>(p5));
		}
	}

	template<typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
	void fire(T0&& type, T1&& p1, T2&& p2, T3&& p3, T4&& p4, T5&& p5, T6&& p6) noexcept {
		Lock l(listenerCS);
		tmp = listeners;
		for(auto i = tmp.begin(); i != tmp.end(); ++i) {
			(*i)->on(forward<T0>(type), forward<T1>(p1), forward<T2>(p2), forward<T3>(p3), forward<T4>(p4), forward<T5>(p5), forward<T6>(p6));
		}
	}

	template<typename T0, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
	void fire(T0&& type, T1&& p1, T2&& p2, T3&& p3, T4&& p4, T5&& p5, T6&& p6, T7&& p7) noexcept {
		Lock l(listenerCS);
		tmp = listeners;
		for(auto i = tmp.begin(); i != tmp.end(); ++i) {
			(*i)->on(forward<T0>(type), forward<T1>(p1), forward<T2>(p2), forward<T3>(p3), forward<T4>(p4), forward<T5>(p5), forward<T6>(p6), forward<T7>(p7));
		}
	}

	void addListener(Listener* aListener) {
		Lock l(listenerCS);
		if(find(listeners, aListener) == listeners.end())
			listeners.push_back(aListener);
	}

	void removeListener(Listener* aListener) {
		Lock l(listenerCS);
		auto it = find(listeners, aListener);
		if(it != listeners.end())
			listeners.erase(it);
	}

	void removeListeners() {
		Lock l(listenerCS);
		listeners.clear();
	}
	
protected:
	ListenerList listeners;
	ListenerList tmp;
	CriticalSection listenerCS;
};

} // namespace dcpp

#endif // !defined(SPEAKER_H)