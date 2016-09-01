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

#include <api/common/Format.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/ResourceManager.h>


namespace webserver {
	const PropertyList FavoriteHubUtils::properties = {
		{ PROP_NAME, "name", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_HUB_URL, "hub_url", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_HUB_DESCRIPTION, "hub_description", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_AUTO_CONNECT, "auto_connect", TYPE_NUMERIC_OTHER, SERIALIZE_BOOL, SORT_NUMERIC },
		{ PROP_SHARE_PROFILE, "share_profile", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
		{ PROP_CONNECT_STATE, "connect_state", TYPE_NUMERIC_OTHER, SERIALIZE_CUSTOM, SORT_NUMERIC },
		{ PROP_NICK, "nick", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_HAS_PASSWORD, "has_password", TYPE_NUMERIC_OTHER, SERIALIZE_BOOL, SORT_NUMERIC },
		{ PROP_USER_DESCRIPTION, "user_description", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_IGNORE_PM, "ignore_private_messages", TYPE_NUMERIC_OTHER, SERIALIZE_BOOL, SORT_NUMERIC },
	};

	const PropertyItemHandler<FavoriteHubEntryPtr> FavoriteHubUtils::propertyHandler = {
		properties,
		FavoriteHubUtils::getStringInfo, FavoriteHubUtils::getNumericInfo, FavoriteHubUtils::compareEntries, FavoriteHubUtils::serializeHub
	};

	string FavoriteHubUtils::formatConnectState(const FavoriteHubEntryPtr& aEntry) noexcept {
		switch (aEntry->getConnectState()) {
			case FavoriteHubEntry::STATE_DISCONNECTED: return STRING(DISCONNECTED);
			case FavoriteHubEntry::STATE_CONNECTING: return STRING(CONNECTING);
			case FavoriteHubEntry::STATE_CONNECTED: return STRING(CONNECTED);
		}

		dcassert(0);
		return Util::emptyString;
	}

	json FavoriteHubUtils::serializeHub(const FavoriteHubEntryPtr& aEntry, int aPropertyName) noexcept {
		switch (aPropertyName) {
			case PROP_SHARE_PROFILE:
			{
				return {
					{ "id", serializeHubSetting(aEntry->get(HubSettings::ShareProfile)) },
					{ "str", aEntry->getShareProfileName() }
				};
			}
			case PROP_CONNECT_STATE:
			{
				return {
					{ "id", aEntry->getConnectState() },
					{ "str", formatConnectState(aEntry) },
					{ "current_hub_id", aEntry->getCurrentHubToken() }
				};
			}
		}

		dcassert(0);
		return nullptr;
	}

	int FavoriteHubUtils::compareEntries(const FavoriteHubEntryPtr& a, const FavoriteHubEntryPtr& b, int aPropertyName) noexcept {
		return 0;
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
			case PROP_NAME: return aEntry->getName();
			case PROP_HUB_URL: return aEntry->getServer();
			case PROP_HUB_DESCRIPTION: return aEntry->getDescription();
			case PROP_NICK: return aEntry->get(HubSettings::Nick);
			case PROP_USER_DESCRIPTION: return aEntry->get(HubSettings::Description);
			case PROP_SHARE_PROFILE: return aEntry->getShareProfileName();
			default: dcassert(0); return Util::emptyString;
		}
	}

	double FavoriteHubUtils::getNumericInfo(const FavoriteHubEntryPtr& aEntry, int aPropertyName) noexcept {
		switch (aPropertyName) {
			case PROP_AUTO_CONNECT: return (double)aEntry->getAutoConnect();
			case PROP_HAS_PASSWORD: return (double)!aEntry->getPassword().empty();
			case PROP_IGNORE_PM: return (double)aEntry->getIgnorePM();
			default: dcassert(0); return 0;
		}
	}
}