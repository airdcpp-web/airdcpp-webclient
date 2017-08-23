/* 
 * Copyright (C) 2009-2011 Big Muscle
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

#ifndef DCPLUSPLUS_DCPP_THROTTLEMANAGER_H
#define DCPLUSPLUS_DCPP_THROTTLEMANAGER_H

#include "Singleton.h"
#include "SettingsManager.h"
#include "TimerManagerListener.h"

#include <condition_variable>
#include <mutex>


namespace dcpp
{
	
	/**
	 * Manager for throttling traffic flow speed.
	 * Inspired by Token Bucket algorithm: http://en.wikipedia.org/wiki/Token_bucket
	 */
	class ThrottleManager :
		public Singleton<ThrottleManager>, private TimerManagerListener
	{
	public:

		/*
		 * Limits a traffic and reads a packet from the network
		 */
		int read(Socket* sock, void* buffer, size_t len);
		
		/*
		 * Limits a traffic and writes a packet to the network
		 * We must handle this a little bit differently than downloads, because of that stupidity in OpenSSL
		 */		
		int write(Socket* sock, void* buffer, size_t& len);

		/*
		 * Returns current download limit.
		 */
		static int getDownLimit() noexcept;

		/*
		 * Returns current download limit.
		 */
		static int getUpLimit() noexcept;
		
		static SettingsManager::IntSetting getCurSetting(SettingsManager::IntSetting setting) noexcept;
		static void setSetting(SettingsManager::IntSetting setting, int value) noexcept;

		static const int MAX_LIMIT = 1024 * 1024; // 1 GiB/s
	private:
		
		// download limiter
		size_t						downTokens = 0;
		condition_variable	downCond;
		mutex				downMutex;
		
		// upload limiter
		size_t						upTokens = 0;
		condition_variable	upCond;
		mutex				upMutex;
			
		friend class Singleton<ThrottleManager>;
		
		// constructor
		ThrottleManager();

		// destructor
		~ThrottleManager();
		
		// TimerManagerListener
		void on(TimerManagerListener::Second, uint64_t aTick) noexcept;
				
	};

}	// namespace dcpp

#endif	// DCPLUSPLUS_DCPP_THROTTLEMANAGER_H