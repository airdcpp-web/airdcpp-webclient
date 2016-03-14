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

#include <api/FavoriteHubUtils.h>
#include <api/FavoriteHubApi.h>

#include <api/common/Format.h>
#include <web-server/JsonUtil.h>

#include <airdcpp/FavoriteManager.h>

namespace webserver {
	FavoriteHubEntryList FavoriteHubUtils::getEntryList() noexcept {
		return FavoriteManager::getInstance()->getFavoriteHubs();
	}

	string FavoriteHubUtils::formatConnectState(const FavoriteHubEntryPtr& aEntry) noexcept {
		switch (aEntry->getConnectState()) {
			case FavoriteHubEntry::STATE_DISCONNECTED: return STRING(DISCONNECTED);
			case FavoriteHubEntry::STATE_CONNECTING: return STRING(CONNECTING);
			case FavoriteHubEntry::STATE_CONNECTED: return STRING(CONNECTED);
		}

		return Util::emptyString;
	}

	json FavoriteHubUtils::serializeHub(const FavoriteHubEntryPtr& aEntry, int aPropertyName) noexcept {
		switch (aPropertyName) {
			case FavoriteHubApi::PROP_SHARE_PROFILE:
			{
				json j;
				j["id"] = serializeHubSetting(aEntry->get(HubSettings::ShareProfile));
				j["str"] = aEntry->getShareProfileName();
				return j;
			}
			case FavoriteHubApi::PROP_CONNECT_STATE:
			{
				json j;
				j["id"] = aEntry->getConnectState();
				j["str"] = formatConnectState(aEntry);
				j["current_hub_id"] = aEntry->getCurrentHubToken();
				return j;
			}
		}

		dcassert(0);
		return nullptr;
	}

	int FavoriteHubUtils::compareEntries(const FavoriteHubEntryPtr& a, const FavoriteHubEntryPtr& b, int aPropertyName) noexcept {
		return 0;
	}

	optional<int> FavoriteHubUtils::deserializeIntHubSetting(const string& aFieldName, const json& aJson) {
		auto p = aJson.find(aFieldName);
		if (p == aJson.end()) {
			return boost::none;
		}

		if ((*p).is_null()) {
			return HUB_SETTING_DEFAULT_INT;
		}

		return JsonUtil::parseValue<int>(aFieldName, *p);
	}

	json FavoriteHubUtils::serializeHubSetting(tribool aSetting) noexcept {
		if (!HubSettings::defined(aSetting)) {
			return nullptr;
		}

		// TODO: test when we use for this
		return aSetting.value;
	}

	json FavoriteHubUtils::serializeHubSetting(int aSetting) noexcept {
		if (!HubSettings::defined(aSetting)) {
			return nullptr;
		}

		return aSetting;
	}

	std::string FavoriteHubUtils::getStringInfo(const FavoriteHubEntryPtr& aEntry, int aPropertyName) noexcept {
		switch (aPropertyName) {
			case FavoriteHubApi::PROP_NAME: return aEntry->getName();
			case FavoriteHubApi::PROP_HUB_URL: return aEntry->getServer();
			case FavoriteHubApi::PROP_HUB_DESCRIPTION: return aEntry->getDescription();
			case FavoriteHubApi::PROP_NICK: return aEntry->get(HubSettings::Nick);
			case FavoriteHubApi::PROP_USER_DESCRIPTION: return aEntry->get(HubSettings::Description);
			case FavoriteHubApi::PROP_SHARE_PROFILE: return aEntry->getShareProfileName();
			default: dcassert(0); return Util::emptyString;
		}
	}

	double FavoriteHubUtils::getNumericInfo(const FavoriteHubEntryPtr& aEntry, int aPropertyName) noexcept {
		switch (aPropertyName) {
			case FavoriteHubApi::PROP_AUTO_CONNECT: return (double)aEntry->getAutoConnect();
			case FavoriteHubApi::PROP_HAS_PASSWORD: return (double)!aEntry->getPassword().empty();
			case FavoriteHubApi::PROP_IGNORE_PM: return (double)aEntry->getFavNoPM();
			default: dcassert(0); return 0;
		}
	}
}