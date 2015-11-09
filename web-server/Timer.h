/*
* Copyright (C) 2011-2015 AirDC++ Project
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
		Timer(CallBack&& aCallBack, boost::asio::io_service& aIO, time_t aIntervalMillis) : cb(move(aCallBack)),
			interval(aIntervalMillis),
			timer(aIO, interval) {

		}

		~Timer() {
			stop(true);
		}

		void start(bool aInstantStart = true) {
			running = true;
			timer.expires_from_now(aInstantStart ? boost::posix_time::milliseconds(0) : interval);
			timer.async_wait(bind(&Timer::tick, this, std::placeholders::_1));
		}

		void stop(bool aWaitShutdown) {
			auto cancelled = timer.cancel();

			if (cancelled > 0 && aWaitShutdown) {
				std::this_thread::sleep_for(std::chrono::milliseconds(30));
				if (running && !timer.get_io_service().stopped()) {
					// cancel again in case someone starts the timer meanwhile.....
					stop(true);
				}
			}
		}

		bool isRunning() const noexcept {
			return running;
		}
	private:
		void tick(const boost::system::error_code& error) {
			if (error == boost::asio::error::operation_aborted) {
				running = false;
				return;
			}

			cb();
			start(false);
		}

		CallBack cb;
		boost::asio::deadline_timer timer;
		boost::posix_time::milliseconds interval;
		bool running = false;
	};

	typedef shared_ptr<Timer> TimerPtr;
}

#endif
