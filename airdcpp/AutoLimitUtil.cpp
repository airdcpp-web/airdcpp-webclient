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

#include "stdinc.h"

#include "AutoLimitUtil.h"

#include "ResourceManager.h"
#include "SettingsManager.h"
#include "ThrottleManager.h"
#include "Util.h"


#ifdef _DEBUG

#include "LogManager.h"
#include "TimerManager.h"

namespace dcpp {

TimeCounter::TimeCounter(string aMsg) : start(GET_TICK()), msg(std::move(aMsg)) {

}

TimeCounter::~TimeCounter() {
	auto end = GET_TICK();
	LogManager::getInstance()->message(msg + ", took " + Util::toString(end - start) + " ms", LogMessage::SEV_INFO, "Debug");
}

}
#endif

namespace dcpp {

int AutoLimitUtil::getSlotsPerUser(bool download, double value, int aSlots, SettingsManager::SettingProfile aProfile) {
	if (!SETTING(MCN_AUTODETECT) && value == 0) {
		return download ? SETTING(MAX_MCN_DOWNLOADS) : SETTING(MAX_MCN_UPLOADS);
	}

	if (aProfile == SettingsManager::PROFILE_LAN) {
		return 1;
	}

	int totalSlots = aSlots;
	if (aSlots ==0)
		totalSlots = getSlots(download ? true : false);

	double speed = value;
	if (value == 0)
		speed = download ? Util::toDouble(SETTING(DOWNLOAD_SPEED)) : Util::toDouble(SETTING(UPLOAD_SPEED));

	//LogManager::getInstance()->message("Slots: " + Util::toString(slots));

	int slots;
	if (speed == 10) {
		slots=2;
	} else if (speed > 10 && speed <= 25) {
		slots=3;
	} else if (speed > 25 && speed <= 50) {
		slots=4;
	} else if (speed > 50 && speed <= 100) {
		slots= static_cast<int>((speed/10)-1);
	} else if (speed > 100) {
		slots=15;
	} else {
		slots=1;
	}

	if (slots > totalSlots)
		slots = totalSlots;
	//LogManager::getInstance()->message("Slots: " + Util::toString(slots) + " TotalSlots: " + Util::toString(totalSlots) + " Speed: " + Util::toString(speed));
	return slots;
}


int AutoLimitUtil::getSlots(bool aIsDownload, double aValue, SettingsManager::SettingProfile aProfile) {
	if (!SETTING(DL_AUTODETECT) && aValue == 0 && aIsDownload) {
		//LogManager::getInstance()->message("Slots1");
		return SETTING(DOWNLOAD_SLOTS);
	} else if (!SETTING(UL_AUTODETECT) && aValue == 0 && !aIsDownload) {
		//LogManager::getInstance()->message("Slots2");
		return SETTING(UPLOAD_SLOTS);
	}

	double speed;
	if (aValue != 0) {
		speed = aValue;
	} else if (aIsDownload) {
		int limit = SETTING(AUTO_DETECTION_USE_LIMITED) ? ThrottleManager::getInstance()->getDownLimit() : 0;
		speed = limit > 0 ? (limit * 8.00) / 1024.00 : Util::toDouble(SETTING(DOWNLOAD_SPEED));
	} else {
		int limit = SETTING(AUTO_DETECTION_USE_LIMITED) ? ThrottleManager::getInstance()->getUpLimit() : 0;
		speed = limit > 0 ? (limit * 8.00) / 1024.00 : Util::toDouble(SETTING(UPLOAD_SPEED));
	}

	int slots = 3;

	// Don't try to understand the formula used in here
	bool rar = aProfile == SettingsManager::PROFILE_RAR;
	if (speed <= 1) {
		if (rar) {
			slots=1;
		} else {
			aIsDownload ? slots=6 : slots=2;
		}
	} else if (speed > 1 && speed <= 2.5) {
		if (rar) {
			slots=2;
		} else {
			aIsDownload ? slots=15 : slots=3;
		}
	} else if (speed > 2.5 && speed <= 4) {
		if (rar) {
			aIsDownload ? slots=3 : slots=2;
		} else {
			aIsDownload ? slots=15 : slots=4;
		}
	} else if (speed > 4 && speed <= 6) {
		if (rar) {
			aIsDownload ? slots=3 : slots=3;
		} else {
			aIsDownload ? slots=20 : slots=5;
		}
	} else if (speed > 6 && speed < 10) {
		if (rar) {
			aIsDownload ? slots=5 : slots=3;
		} else {
			aIsDownload ? slots=20 : slots=6;
		}
	} else if (speed >= 10 && speed <= 50) {
		if (rar) {
			speed <= 20 ?  slots=4 : slots=5;
			if (aIsDownload) {
				slots=slots+3;
			}
		} else {
			aIsDownload ? slots=30 : slots=8;
		}
	} else if(speed > 50 && speed < 100) {
		if (rar) {
			slots= static_cast<int>(speed / 10);
			if (aIsDownload)
				slots=slots+4;
		} else {
			aIsDownload ? slots=40 : slots=12;
		}
	} else if (speed >= 100) {
		// Curves: https://www.desmos.com/calculator/vfywkguiej
		if (rar) {
			if (aIsDownload) {
				slots = static_cast<int>(ceil((log(speed + 750) - 6.61) * 100));
			} else {
				slots = static_cast<int>(ceil((log(speed + 70.0) - 4.4) * 10));
			}
		} else {
			if (aIsDownload) {
				slots = static_cast<int>((speed * 0.10) + 40);
			} else {
				slots = static_cast<int>((speed * 0.04) + 15);
			}
		}
	}

	return slots;

}

int AutoLimitUtil::getSpeedLimitKbps(bool download, double value) {

	if (!SETTING(DL_AUTODETECT) && value == 0 && download) {
		//LogManager::getInstance()->message("Slots1");
		return SETTING(MAX_DOWNLOAD_SPEED);
	} else if (!SETTING(UL_AUTODETECT) && value == 0 && !download) {
		//LogManager::getInstance()->message("Slots2");
		return SETTING(MIN_UPLOAD_SPEED);
	}

	if (value == 0)
		value = download ? Util::toDouble(SETTING(DOWNLOAD_SPEED)) : Util::toDouble(SETTING(UPLOAD_SPEED));

	return static_cast<int>(download ? value*105 : value*60);
}

int AutoLimitUtil::getMaxAutoOpened(double value) {
	if (!SETTING(UL_AUTODETECT) && value == 0) {
		return SETTING(AUTO_SLOTS);
	}

	if (value == 0)
		value = Util::toDouble(SETTING(UPLOAD_SPEED));

	int slots=1;

	if (value < 1) {
		slots=1;
	} else if (value >= 1 && value <= 5) {
		slots=2;
	}  else if (value > 5 && value <= 20) {
		slots=3;
	} else if (value > 20 && value < 100) {
		slots=4;
	} else if (value == 100) {
		slots=6;
	} else if (value >= 100) {
		slots=10;
	}

	return slots;
}

}
