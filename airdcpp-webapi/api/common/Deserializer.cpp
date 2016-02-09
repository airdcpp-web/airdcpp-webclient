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

#include <web-server/stdinc.h>
#include <web-server/JsonUtil.h>

#include <api/common/Deserializer.h>

#include <airdcpp/ClientManager.h>

namespace webserver {
	CID Deserializer::parseCID(const string& aCID) {
		if (!Encoder::isBase32(aCID.c_str())) {
			throw std::invalid_argument("Invalid CID");
		}

		return CID(aCID);
	}

	TTHValue Deserializer::parseTTH(const string& aTTH) {
		if (!Encoder::isBase32(aTTH.c_str())) {
			throw std::invalid_argument("Invalid TTH");
		}

		return TTHValue(aTTH);
	}

	UserPtr Deserializer::parseUser(const json& aJson, bool aAllowMe) {
		auto cid = parseCID(JsonUtil::getField<string>("cid", aJson, false));
		if (!aAllowMe && cid == ClientManager::getInstance()->getMyCID()) {
			throw std::invalid_argument("Own CID isn't allowed for this command");
		}

		return ClientManager::getInstance()->findUser(cid);
	}

	UserPtr Deserializer::deserializeUser(const json& aJson, bool aAllowMe, const string& aFieldName) {
		auto userJson = JsonUtil::getRawValue(aFieldName, aJson);
		return parseUser(userJson);
	}

	HintedUser Deserializer::deserializeHintedUser(const json& aJson, bool aAllowMe, const string& aFieldName) {
		auto userJson = JsonUtil::getRawValue(aFieldName, aJson);
		auto user = parseUser(userJson, aAllowMe);
		return HintedUser(user, JsonUtil::getField<string>("hub_url", userJson, aAllowMe && user == ClientManager::getInstance()->getMe()));
	}

	TTHValue Deserializer::deserializeTTH(const json& aJson) {
		return parseTTH(JsonUtil::getField<string>("tth", aJson, false));
	}

	QueueItemBase::Priority Deserializer::deserializePriority(const json& aJson, bool allowDefault) {
		int minAllowed = allowDefault ? QueueItemBase::DEFAULT : QueueItemBase::PAUSED_FORCE;

		auto priority = JsonUtil::getEnumField<int>("priority", aJson, !allowDefault, minAllowed, QueueItemBase::HIGHEST);
		if (!priority) {
			return QueueItemBase::Priority::DEFAULT;
		}

		return static_cast<QueueItemBase::Priority>(*priority);
	}

	void Deserializer::deserializeDownloadParams(const json& aJson, string& targetDirectory_, string& targetName_, TargetUtil::TargetType& targetType_, QueueItemBase::Priority& priority_) {
		targetDirectory_ = JsonUtil::getOptionalFieldDefault<string>("target_directory", aJson, SETTING(DOWNLOAD_DIRECTORY), false);
		targetName_ = JsonUtil::getField<string>("target_name", aJson, false);

		auto targetType = JsonUtil::getEnumField<int>("target_type", aJson, false, 0, TargetUtil::TARGET_LAST-1);
		if (!targetType) {
			targetType = TargetUtil::TARGET_PATH;
		}

		priority_ = deserializePriority(aJson, true);
		targetType_ = static_cast<TargetUtil::TargetType>(*targetType);;
	}
}
