/*
* Copyright (C) 2011-2021 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_ACTIVITY_MANAGER_H
#define DCPLUSPLUS_DCPP_ACTIVITY_MANAGER_H

#include "typedefs.h"

#include "SettingsManagerListener.h"
#include "TimerManagerListener.h"

#include "Speaker.h"
#include "TimerManager.h"


namespace dcpp {
	//Away modes
	enum AwayMode : uint8_t {
		AWAY_OFF,
		AWAY_IDLE,
		AWAY_MANUAL //highest value
	};

	class ActivityManagerListener {
	public:
		virtual ~ActivityManagerListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> AwayModeChanged;

		virtual void on(AwayModeChanged, AwayMode) noexcept { }
	};

	class ActivityManager : public Speaker<ActivityManagerListener>, public Singleton<ActivityManager>, public TimerManagerListener, private SettingsManagerListener
	{
	public:
		ActivityManager();
		~ActivityManager();

		void updateActivity(time_t aLastActivity = GET_TICK()) noexcept;

		bool isAway() const noexcept;
		AwayMode getAwayMode() const noexcept { return awayMode; }
		void setAway(AwayMode aAway);

		string getAwayMessage(const string& aAwayMsg, ParamMap& params) const noexcept;
	private:
		void on(SettingsManagerListener::LoadCompleted, bool aFileLoaded) noexcept override;
		void on(TimerManagerListener::Second, uint64_t aTick) noexcept override;

		AwayMode awayMode = AWAY_OFF;
		time_t lastActivity = GET_TICK();
	};

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_ACTIVITY_MANAGER_H
