/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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

#if !defined(FAST_ALLOC_H)
#define FAST_ALLOC_H

#include "Thread.h"
#include "debug.h"
#include <boost/pool/pool.hpp>

namespace dcpp {

#ifndef _DEBUG
struct FastAllocBase {
	static FastCriticalSection cs;
};

/*
cannot use this with a class that has subclasses, it will reserve the wrong amount of memory for a subclass.
*/
template <class T>
class FastAlloc : public FastAllocBase  {
	
	public:
		static void* operator new ( size_t s ) {
			
			if(s != sizeof(T)) {
				return ::operator new(s); //use default new
			}
			FastLock l(cs);
			return pool.malloc();
		}

		static void operator delete(void* m, size_t s) {
			
			if (s != sizeof(T)) 
				::operator delete(m); //use default delete
		
			else if(m) {
				FastLock l(cs);
				pool.free(m);
			}
		}

	protected:
		~FastAlloc() { }

	private:
		static boost::pool< > pool;
	};

	
	template <class T> boost::pool< > FastAlloc<T> ::pool( sizeof(T) );

#else
template<class T> struct FastAlloc { };
#endif
} // namespace dcpp

#endif // !defined(FAST_ALLOC_H)

/**
 * @file
 * $Id: FastAlloc.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
