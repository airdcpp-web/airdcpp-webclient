/*
* Copyright (C) 2001-2022 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SEMAPHORE_H
#define DCPLUSPLUS_DCPP_SEMAPHORE_H

#ifndef _WIN32
#include "CriticalSection.h"
#include <sys/time.h>
#endif

namespace dcpp {

class Semaphore {
public:
	Semaphore(const Semaphore&) = delete;
	Semaphore& operator=(const Semaphore&) = delete;
#ifdef _WIN32
	Semaphore() noexcept {
		h = CreateSemaphore(NULL, 0, MAXLONG, NULL);
	}

	void signal() noexcept {
		ReleaseSemaphore(h, 1, NULL);
	}

	bool wait() noexcept { return WaitForSingleObject(h, INFINITE) == WAIT_OBJECT_0; }
	bool wait(uint32_t millis) noexcept { return WaitForSingleObject(h, millis) == WAIT_OBJECT_0; }

	~Semaphore() noexcept {
		CloseHandle(h);
	}

private:
	HANDLE h;
#else
	Semaphore() noexcept { pthread_cond_init(&cond, NULL); }
	~Semaphore() noexcept { pthread_cond_destroy(&cond); }
	void signal() noexcept {
		Lock l(cs);
		count++;
		pthread_cond_signal(&cond);
	}

	bool wait() noexcept {
		Lock l(cs);
		if (count == 0) {
			pthread_cond_wait(&cond, &cs.getMutex());
		}
		count--;
		return true;
	}
	bool wait(uint32_t millis) noexcept {
		Lock l(cs);
		if (count == 0) {
			timeval timev;
			timespec t;
			gettimeofday(&timev, NULL);
			millis += timev.tv_usec / 1000;
			t.tv_sec = timev.tv_sec + (millis / 1000);
			t.tv_nsec = (millis % 1000) * 1000 * 1000;
			int ret = pthread_cond_timedwait(&cond, &cs.getMutex(), &t);
			if (ret != 0) {
				return false;
			}
		}
		count--;
		return true;
	}

private:
	pthread_cond_t cond;
	CriticalSection cs;
	int count = 0;
#endif
};

} // namespace dcpp

#endif // !defined(SEMAPHORE_H)
