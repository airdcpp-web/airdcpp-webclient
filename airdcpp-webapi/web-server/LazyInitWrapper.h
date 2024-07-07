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

#ifndef DCPLUSPLUS_WEBSERVER_LAZYWRAPPER_H
#define DCPLUSPLUS_WEBSERVER_LAZYWRAPPER_H

#include "forward.h"

namespace webserver {
	template <class T>

	// NOTE: initialization is not thread safe
	class LazyInitWrapper {
	public:
		typedef std::function < unique_ptr<T>() > InitF;
		LazyInitWrapper(InitF&& aInitF) : initF(std::move(aInitF)) {}

		T* operator->() {
			ensureInit();
			return module.operator->();
		}

		T* get() {
			ensureInit();
			return module.get();
		}
	private:
		void ensureInit() {
			if (!module) {
				module = initF();
			}
		}

		unique_ptr<T> module;
		InitF initF;
	};
}

#endif