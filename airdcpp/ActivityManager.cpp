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

#include "stdinc.h"

#include "ActivityManager.h"
#include "ClientManager.h"
#include "SettingsManager.h"


namespace dcpp {

ActivityManager::ActivityManager() {
	TimerManager::getInstance()->addListener(this);
}

ActivityManager::~ActivityManager() {
	TimerManager::getInstance()->removeListener(this);
}

void ActivityManager::updateActivity(time_t aLastActivity) noexcept {
	if (aLastActivity < lastActivity) {
		return;
	}

	lastActivity = aLastActivity;
	if (awayMode != AWAY_MANUAL) {
		setAway(AWAY_OFF);
	}
}

void ActivityManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	if (!SETTING(AWAY_IDLE_TIME) || awayMode != AWAY_OFF) {
		return;
	}

	if ((lastActivity + SETTING(AWAY_IDLE_TIME) * 60 * 1000ULL) < aTick) {
		setAway(AWAY_IDLE); 
	}
}

void ActivityManager::setAway(AwayMode aNewMode) {
	if (aNewMode == awayMode) {
		return;
	}

	if (aNewMode == AWAY_MANUAL || (awayMode == AWAY_MANUAL && aNewMode == AWAY_OFF)) {
		//only save the state if away mode is set by user
		SettingsManager::getInstance()->set(SettingsManager::AWAY, aNewMode != AWAY_OFF);
	}

	awayMode = aNewMode;
	if (awayMode > AWAY_OFF)
		lastActivity = GET_TICK();

	ClientManager::getInstance()->infoUpdated();
	fire(ActivityManagerListener::AwayModeChanged(), awayMode);
}

string ActivityManager::getAwayMessage(const string& aAwayMsg, ParamMap& params) const noexcept {
	params["idleTI"] = Util::formatSeconds(GET_TICK() - lastActivity);
	return Util::formatParams(aAwayMsg, params);
}

} // namespace dcpp