/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#include <web-server/JsonUtil.h>
#include <web-server/Session.h>

#include <api/common/Deserializer.h>

#include <airdcpp/ClientManager.h>
#include <airdcpp/ShareManager.h>

namespace webserver {
	CID Deserializer::parseCID(const string& aCID) {
		if (!Encoder::isBase32(aCID.c_str())) {
			throw std::invalid_argument("Invalid CID");
		}

		return CID(aCID);
	}

	UserPtr Deserializer::getUser(const string& aCID, bool aAllowMe) {
		return getUser(parseCID(aCID), aAllowMe);
	}

	UserPtr Deserializer::getUser(const CID& aCID, bool aAllowMe) {
		if (!aAllowMe && aCID == ClientManager::getInstance()->getMyCID()) {
			throw std::invalid_argument("Own CID isn't allowed for this command");
		}

		auto u = ClientManager::getInstance()->findUser(aCID);
		if (!u) {
			throw std::invalid_argument("User not found");
		}

		return u;
	}

	TTHValue Deserializer::parseTTH(const string& aTTH) {
		if (!Encoder::isBase32(aTTH.c_str())) {
			throw std::invalid_argument("Invalid TTH");
		}

		return TTHValue(aTTH);
	}

	UserPtr Deserializer::deserializeUser(const json& aJson, bool aAllowMe, bool aOptional) {
		const auto cid = JsonUtil::getOptionalField<string>("cid", aJson, !aOptional);
		if (!cid) {
			return nullptr;
		}

		return getUser(*cid, aAllowMe);
	}

	HintedUser Deserializer::deserializeHintedUser(const json& aJson, bool aAllowMe, const string& aFieldName) {
		auto userJson = JsonUtil::getRawField(aFieldName, aJson);
		return parseHintedUser(userJson, aFieldName, aAllowMe);
	}

	HintedUser Deserializer::parseHintedUser(const json& aJson, const string& aFieldName, bool aAllowMe) {
		auto user = deserializeUser(aJson, aAllowMe, false);
		auto hubUrl = JsonUtil::getField<string>("hub_url", aJson, aAllowMe && user == ClientManager::getInstance()->getMe());
		return HintedUser(user, hubUrl);
	}

	OnlineUserPtr Deserializer::deserializeOnlineUser(const json& aJson, bool aAllowMe, const string& aFieldName) {
		auto hintedUser = deserializeHintedUser(aJson, aAllowMe, aFieldName);

		auto onlineUser = ClientManager::getInstance()->findOnlineUser(hintedUser, false);
		if (!onlineUser) {
			throw std::invalid_argument("User is offline");
		}

		return onlineUser;
	}

	TTHValue Deserializer::deserializeTTH(const json& aJson) {
		return parseTTH(JsonUtil::getField<string>("tth", aJson, false));
	}

	Priority Deserializer::deserializePriority(const json& aJson, bool aAllowDefault) {
		auto minAllowed = aAllowDefault ? Priority::DEFAULT : Priority::PAUSED_FORCE;

		auto priority = JsonUtil::getOptionalRangeField<int>("priority", aJson, !aAllowDefault, static_cast<int>(minAllowed), static_cast<int>(Priority::HIGHEST));
		if (!priority) {
			return Priority::DEFAULT;
		}

		return static_cast<Priority>(*priority);
	}

	void Deserializer::deserializeDownloadParams(const json& aJson, const SessionPtr& aSession, string& targetDirectory_, string& targetName_, Priority& priority_) {
		// Target path
		targetDirectory_ = JsonUtil::getOptionalFieldDefault<string>("target_directory", aJson, SETTING(DOWNLOAD_DIRECTORY));

		ParamMap params;
		params["username"] = aSession->getUser()->getUserName();
		targetDirectory_ = Util::formatParams(targetDirectory_, params, nullptr, 0);

		// A default target name can be provided
		auto name = JsonUtil::getOptionalField<string>("target_name", aJson, targetName_.empty());
		if (name) {
			targetName_ = *name;
		}

		priority_ = deserializePriority(aJson, true);
	}

	StringList Deserializer::deserializeHubUrls(const json& aJson) {
		auto hubUrls = JsonUtil::getOptionalFieldDefault<StringList>("hub_urls", aJson, StringList());
		if (hubUrls.empty()) {
			ClientManager::getInstance()->getOnlineClients(hubUrls);
		}

		return hubUrls;
	}


	ClientPtr Deserializer::deserializeClient(const json& aJson, bool aOptional) {
		const auto hubUrl = JsonUtil::getOptionalField<string>("hub_url", aJson, !aOptional);
		if (!hubUrl) {
			return nullptr;
		}

		auto client = ClientManager::getInstance()->getClient(*hubUrl);
		if (!client) {
			//if (aOptional) {
			//	return nullptr;
			//}

			throw std::invalid_argument("Hub " + *hubUrl + " was not found");
		}

		return client;
	}

	pair<string, bool> Deserializer::deserializeChatMessage(const json& aJson) {
		return { 
			JsonUtil::getField<string>("text", aJson, false),
			JsonUtil::getOptionalFieldDefault<bool>("third_person", aJson, false)
		};
	}

	map<string, LogMessage::Severity> severityMappings = {
		{ "notify", LogMessage::SEV_NOTIFY },
		{ "info", LogMessage::SEV_INFO },
		{ "warning", LogMessage::SEV_WARNING },
		{ "error", LogMessage::SEV_ERROR },
	};

	LogMessage::Severity Deserializer::parseSeverity(const string& aText) {
		auto i = severityMappings.find(aText);
		if (i != severityMappings.end()) {
			return i->second;
		}

		throw std::invalid_argument("Invalid severity: " + aText);
	}

	pair<string, LogMessage::Severity> Deserializer::deserializeStatusMessage(const json& aJson) {
		return {
			JsonUtil::getField<string>("text", aJson, false),
			parseSeverity(JsonUtil::getField<string>("severity", aJson, false))
		};
	}

	ProfileToken Deserializer::deserializeShareProfile(const json& aJson) {
		auto profile = deserializeOptionalShareProfile(aJson);
		if (!profile) {
			return SETTING(DEFAULT_SP);
		}

		return *profile;
	}

	OptionalProfileToken Deserializer::deserializeOptionalShareProfile(const json& aJson) {
		auto profile = JsonUtil::getOptionalField<ProfileToken>("share_profile", aJson);
		if (profile && !ShareManager::getInstance()->getShareProfile(*profile)) {
			throw std::invalid_argument("Invalid share profile: " + Util::toString(*profile));
		}

		return profile;
	}

	TTHValue Deserializer::tthArrayValueParser(const json& aJson, const string& aFieldName) {
		auto tthStr = JsonUtil::parseValue<string>(aFieldName, aJson, false);
		return parseTTH(tthStr);
	}

	CID Deserializer::cidArrayValueParser(const json& aJson, const string& aFieldName) {
		auto cidStr = JsonUtil::parseValue<string>(aFieldName, aJson, false);
		return getUser(cidStr, true)->getCID();
	}

	HintedUser Deserializer::hintedUserArrayValueParser(const json& aJson, const string& aFieldName) {
		return parseHintedUser(aJson, aFieldName, true);
	}
}
