/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_TIMER_H
#define DCPLUSPLUS_DCPP_TIMER_H

#include <web-server/stdinc.h>

namespace webserver {
	class Timer : boost::noncopyable {
	public:
		typedef std::function<void(const CallBack&)> CallbackWrapper;

		// CallbackWrapper is meant to ensure the lifetime of the timer
		// (which necessary only if the timer is called from a class that can be deleted, such as sessions)
		Timer(CallBack&& aCallBack, boost::asio::io_service& aIO, time_t aIntervalMillis, const CallbackWrapper& aWrapper) : 
			cb(move(aCallBack)),
			interval(aIntervalMillis),
			timer(aIO, interval),
			cbWrapper(aWrapper)
		{

		}

		~Timer() {
			stop(true);
		}

		bool start(bool aInstantStart = true) {
			if (shutdown) {
				return false;
			}

			running = true;
			timer.expires_from_now(aInstantStart ? boost::posix_time::milliseconds(0) : interval);
			timer.async_wait([this](const boost::system::error_code& error) {
				if (error == boost::asio::error::operation_aborted) {
					// Timer stopped, no calls to this if the timer has been destructed
					return;
				}

				tick();
			});
			return true;
		}

		// Use aShutdown if the timer will be stopped permanently (e.g. the owner is being deleted)
		void stop(bool aShutdown) noexcept {
			running = false;
			shutdown = aShutdown;
			timer.cancel();
		}

		bool isRunning() const noexcept {
			return running;
		}
	private:
		void tick() {
			if (cbWrapper) {
				// We must ensure that the timer still exists when a new start call is performed
				cbWrapper(bind(&Timer::runTask, this));
			} else {
				runTask();
			}
		}

		void runTask() {
			cb();

			start(false);
		}

		CallBack cb;
		CallbackWrapper cbWrapper;

		boost::asio::deadline_timer timer;
		boost::posix_time::milliseconds interval;
		bool running = false;
		bool shutdown = false;
	};

	typedef shared_ptr<Timer> TimerPtr;
}

#endif
