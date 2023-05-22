/*
 * Copyright (C) 2001-2023 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SINGLETON_H
#define DCPLUSPLUS_DCPP_SINGLETON_H

#include "debug.h"

#include <boost/noncopyable.hpp>

namespace dcpp {

template<typename T>
class Singleton : boost::noncopyable {
public:
	Singleton() { }
	virtual ~Singleton() { }

	static T* getInstance() {
		//dcassert(instance);
		return instance;
	}
	
	static void newInstance() {
		if(instance)
			delete instance;
		
		instance = new T();
	}
	
	static void deleteInstance() {
		if(instance)
			delete instance;
		instance = NULL;
	}
protected:
	static T* instance;

};

template<class T> T* Singleton<T>::instance = NULL;

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_SINGLETON_H