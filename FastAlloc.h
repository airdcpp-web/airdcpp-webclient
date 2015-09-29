/*
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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

#include "CriticalSection.h"
#include "debug.h"
#include <boost/pool/pool.hpp>

namespace dcpp {

//#define NO_FAST_ALLOC

#ifndef NO_FAST_ALLOC
struct FastAllocBase {
	static FastCriticalSection cs;
};

#ifndef SMALL_OBJECT_SIZE
		#define SMALL_OBJECT_SIZE 256  //change the small object size to a suitable value.
	#endif


class AllocManager : public FastAllocBase {
	
public:
		static AllocManager& getInstance() {
			static AllocManager Instance;
			return Instance;
		}

		void* allocate(size_t size) {
			
			if (size > SMALL_OBJECT_SIZE) {
				return ::operator new(size); //use normal new
			}

			FastLock l(cs);
			return Pools[size-1]->malloc();
		}

		void deallocate(void* m, size_t size) {
			if (size > SMALL_OBJECT_SIZE) {
				::operator delete(m); //use normal delete
			} else {
				FastLock l(cs);
				if (m)
					Pools[size-1]->free(m);
			}
		}
		~AllocManager() {
			FastLock l(cs);
			for (int i = 0; i < SMALL_OBJECT_SIZE; ++i) {
				//Pools[i]->purge_memory();	
				delete Pools[i];
				}
			}

	private:
		AllocManager() 
		{
			for(int i = 0; i < SMALL_OBJECT_SIZE; ++i)
				Pools[i] = new boost::pool<>(i + 1);
		}

		AllocManager(const AllocManager&);
		const AllocManager& operator=(const AllocManager&);

		boost::pool<>* Pools[SMALL_OBJECT_SIZE];
	};

class FastAllocator {
	public:
		
		static void* operator new(size_t size) {

			return AllocManager::getInstance().allocate(size);
		}

		static void operator delete(void* m, size_t size) {

			AllocManager::getInstance().deallocate(m, size);
		}

	virtual ~FastAllocator() { }

	};

/*
Changed to Boost pools -Night
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

		// Avoid hiding placement new that's needed by the stl containers...
		static void* operator new(size_t, void* m) {
			return m;
		}
		// ...and the warning about missing placement delete...
		static void operator delete(void*, void*) {
			// ? We didn't allocate so...
		}

	protected:
		~FastAlloc() { }

	private:
		static boost::pool< > pool;
	};

	
	template <class T> boost::pool< > FastAlloc<T> ::pool( sizeof(T) );

#else
template<class T> struct FastAlloc { };
class FastAllocator {};
#endif
} // namespace dcpp

#endif // !defined(FAST_ALLOC_H)


