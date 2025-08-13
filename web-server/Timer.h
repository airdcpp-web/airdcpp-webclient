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

#ifndef DCPLUSPLUS_WEBSERVER_TIMER_H
#define DCPLUSPLUS_WEBSERVER_TIMER_H

#include "forward.h"

#include <airdcpp/core/header/debug.h>

namespace webserver {
	class Timer : public boost::noncopyable {
	public:
		using CallbackWrapper = std::function<void (const Callback &)>;

		// CallbackWrapper is meant to ensure the lifetime of the timer
		// (which necessary only if the timer is called from a class that can be deleted, such as sessions)
		Timer(Callback&& aCallback, boost::asio::io_context& aIO, time_t aIntervalMillis, const CallbackWrapper& aWrapper) :
			cb(std::move(aCallback)),
			cbWrapper(aWrapper),
			timer(aIO),
			interval(aIntervalMillis)
		{
			dcdebug("Timer %p was created\n", this);
		}

		~Timer() {
			stop(true);
			dcdebug("Timer %p was destroyed\n", this);
		}

		bool start(bool aInstantTick) {
			if (shutdown) {
				return false;
			}

			running = true;
			scheduleNext(aInstantTick ? boost::posix_time::milliseconds(0) : interval);
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

		void flush() {
			timer.cancel();
			scheduleNext(boost::posix_time::milliseconds(0));
		}
	private:
		// Static in case the timer has been destructed
		static void tick(const boost::system::error_code& error, const CallbackWrapper& cbWrapper, Timer* aTimer) {
			if (error == boost::asio::error::operation_aborted) {
				return;
			}

			if (cbWrapper) {
				// We must ensure that the timer still exists when a new start call is performed
				cbWrapper(std::bind(&Timer::runTask, aTimer));
			} else {
				aTimer->runTask();
			}
		}

		void scheduleNext(const boost::posix_time::milliseconds& aFromNow) {
			if (!running) {
				return;
			}

			timer.expires_from_now(aFromNow);
			timer.async_wait(std::bind(&Timer::tick, std::placeholders::_1, cbWrapper, this));
		}

		void runTask() {
			cb();

			scheduleNext(interval);
		}

		Callback cb;
		CallbackWrapper cbWrapper;

		boost::asio::deadline_timer timer;
		boost::posix_time::milliseconds interval;
		bool running = false;
		bool shutdown = false;
	};

	using TimerPtr = shared_ptr<Timer>;
}

#endif
