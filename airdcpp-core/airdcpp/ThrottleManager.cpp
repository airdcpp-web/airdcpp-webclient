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

#include "stdinc.h"
#include "ThrottleManager.h"

#include "DownloadManager.h"
#include "Socket.h"
#include "TimerManager.h"
#include "UploadManager.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread.hpp>
#include <boost/detail/lightweight_mutex.hpp>

namespace dcpp {
	// The actual limiting code is from StrongDC++
	// Bandwidth limiting in DC++ is broken: https://www.airdcpp.net/forum/viewtopic.php?f=7&t=4485&p=8856#p8856

	#define CONDWAIT_TIMEOUT		250

	// constructor
	ThrottleManager::ThrottleManager(void)
	{
		TimerManager::getInstance()->addListener(this);
	}

	// destructor
	ThrottleManager::~ThrottleManager()
	{
		TimerManager::getInstance()->removeListener(this);

		// release conditional variables on exit
		downCond.notify_all();
		upCond.notify_all();
	}

	/*
	 * Limits a traffic and reads a packet from the network
	 */
	int ThrottleManager::read(Socket* sock, void* buffer, size_t len)
	{
		size_t downs = DownloadManager::getInstance()->getDownloadCount();
		if(getDownLimit() == 0 || downs == 0)
			return sock->read(buffer, len);

		boost::unique_lock<boost::mutex> lock(downMutex);


		if(downTokens > 0)
		{
			size_t slice = (getDownLimit() * 1024) / downs;
			size_t readSize = min(slice, min(len, downTokens));
				
			// read from socket
			readSize = sock->read(buffer, readSize);
				
			if(readSize > 0)
				downTokens -= readSize;

			// next code can't be in critical section, so we must unlock here
			lock.unlock();

			// give a chance to other transfers to get a token
			boost::thread::yield();
			return readSize;
		}

		// no tokens, wait for them
		downCond.timed_wait(lock, boost::posix_time::millisec(CONDWAIT_TIMEOUT));
		return -1;	// from BufferedSocket: -1 = retry, 0 = connection close
	}
	
	/*
	 * Limits a traffic and writes a packet to the network
	 * We must handle this a little bit differently than downloads, because of that stupidity in OpenSSL
	 */		
	int ThrottleManager::write(Socket* sock, void* buffer, size_t& len)
	{
		size_t ups = UploadManager::getInstance()->getUploadCount();
		if(getUpLimit() == 0 || ups == 0)
			return sock->write(buffer, len);
		
		boost::unique_lock<boost::mutex> lock(upMutex);
		
		if(upTokens > 0)
		{
			size_t slice = (getUpLimit() * 1024) / ups;
			len = min(slice, min(len, upTokens));
			upTokens -= len;

			// next code can't be in critical section, so we must unlock here
			lock.unlock();

			// write to socket			
			int sent = sock->write(buffer, len);

			// give a chance to other transfers to get a token
			boost::thread::yield();
			return sent;
		}
		
		// no tokens, wait for them
		upCond.timed_wait(lock, boost::posix_time::millisec(CONDWAIT_TIMEOUT));
		return 0;	// from BufferedSocket: -1 = failed, 0 = retry
	}

	void ThrottleManager::setSetting(SettingsManager::IntSetting setting, int value) noexcept {
		if (value < 0 || value > MAX_LIMIT)
			value = 0;
		SettingsManager::getInstance()->set(setting, value);
	}
	
	int ThrottleManager::getUpLimit() noexcept {
		return SettingsManager::getInstance()->get(getCurSetting(SettingsManager::MAX_UPLOAD_SPEED_MAIN));
	}

	int ThrottleManager::getDownLimit() noexcept {
		return SettingsManager::getInstance()->get(getCurSetting(SettingsManager::MAX_DOWNLOAD_SPEED_MAIN));
	}


	SettingsManager::IntSetting ThrottleManager::getCurSetting(SettingsManager::IntSetting setting) noexcept {
		SettingsManager::IntSetting upLimit = SettingsManager::MAX_UPLOAD_SPEED_MAIN;
		SettingsManager::IntSetting downLimit = SettingsManager::MAX_DOWNLOAD_SPEED_MAIN;
		//SettingsManager::IntSetting slots     = SettingsManager::SLOTS_PRIMARY;

		if (SETTING(TIME_DEPENDENT_THROTTLE)) {
			time_t currentTime;
			time(&currentTime);
			int currentHour = localtime(&currentTime)->tm_hour;
			if ((SETTING(BANDWIDTH_LIMIT_START) < SETTING(BANDWIDTH_LIMIT_END) &&
				currentHour >= SETTING(BANDWIDTH_LIMIT_START) && currentHour < SETTING(BANDWIDTH_LIMIT_END)) ||
				(SETTING(BANDWIDTH_LIMIT_START) > SETTING(BANDWIDTH_LIMIT_END) &&
				(currentHour >= SETTING(BANDWIDTH_LIMIT_START) || currentHour < SETTING(BANDWIDTH_LIMIT_END))))
			{
				upLimit = SettingsManager::MAX_UPLOAD_SPEED_ALTERNATE;
				downLimit = SettingsManager::MAX_DOWNLOAD_SPEED_ALTERNATE;
				//slots     = SettingsManager::SLOTS_ALTERNATE_LIMITING;
			}
		}

		switch (setting) {
		case SettingsManager::MAX_UPLOAD_SPEED_MAIN:
			return upLimit;
		case SettingsManager::MAX_DOWNLOAD_SPEED_MAIN:
			return downLimit;
			//case SettingsManager::SLOTS:
			//	return slots;
		default:
			return setting;
		}
	}

	// TimerManagerListener
	void ThrottleManager::on(TimerManagerListener::Second, uint64_t /*aTick*/) noexcept {
		auto downLimit = getDownLimit() * 1024;
		auto upLimit = getUpLimit() * 1024;
		
		// readd tokens
		if(downLimit > 0)
		{
			boost::lock_guard<boost::mutex> lock(downMutex);
			downTokens = downLimit;
			downCond.notify_all();
		}
			
		if(upLimit > 0)
		{
			boost::lock_guard<boost::mutex> lock(upMutex);
			upTokens = upLimit;
			upCond.notify_all();
		}
	}


}	// namespace dcpp