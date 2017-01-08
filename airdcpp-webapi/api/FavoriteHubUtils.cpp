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
#include <airdcpp/Util.h>


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
		{ PROP_NMDC_ENCODING, "nmdc_encoding", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },

		{ PROP_CONN_MODE4, "connection_mode_v4", TYPE_NUMERIC_OTHER, SERIALIZE_CUSTOM, SORT_NUMERIC },
		{ PROP_CONN_MODE6, "connection_mode_v6", TYPE_NUMERIC_OTHER, SERIALIZE_CUSTOM, SORT_NUMERIC },
		{ PROP_IP4, "connection_ip_v4", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_IP6, "connection_ip_v6", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
	};

	const PropertyItemHandler<FavoriteHubEntryPtr> FavoriteHubUtils::propertyHandler = {
		properties,
		FavoriteHubUtils::getStringInfo, FavoriteHubUtils::getNumericInfo, FavoriteHubUtils::compareEntries, FavoriteHubUtils::serializeHub
	};

	string FavoriteHubUtils::getConnectStateStr(const FavoriteHubEntryPtr& aEntry) noexcept {
		switch (aEntry->getConnectState()) {
			case FavoriteHubEntry::STATE_DISCONNECTED: return STRING(DISCONNECTED);
			case FavoriteHubEntry::STATE_CONNECTING: return STRING(CONNECTING);
			case FavoriteHubEntry::STATE_CONNECTED: return STRING(CONNECTED);
		}

		dcassert(0);
		return Util::emptyString;
	}

	string FavoriteHubUtils::getConnectStateId(const FavoriteHubEntryPtr& aEntry) noexcept {
		switch (aEntry->getConnectState()) {
			case FavoriteHubEntry::STATE_DISCONNECTED: return "disconnected";
			case FavoriteHubEntry::STATE_CONNECTING: return "connecting";
			case FavoriteHubEntry::STATE_CONNECTED: return "connected";
		}

		dcassert(0);
		return Util::emptyString;
	}

	json FavoriteHubUtils::serializeHub(const FavoriteHubEntryPtr& aEntry, int aPropertyName) noexcept {
		switch (aPropertyName) {
			case PROP_SHARE_PROFILE:
			{
				auto defined = HubSettings::defined(aEntry->get(HubSettings::ShareProfile));
				return {
					{ "id", serializeHubSetting(aEntry->get(HubSettings::ShareProfile)) },
					{ "str", defined ? aEntry->getShareProfileName() : Util::emptyString }
				};
			}
			case PROP_CONNECT_STATE:
			{
				return {
					{ "id", aEntry->getConnectState() },
					//{ "id", getConnectStateId(aEntry) },
					{ "str", getConnectStateStr(aEntry) },
					{ "current_hub_id", aEntry->getCurrentHubToken() }
				};
			}
			case PROP_CONN_MODE4: return serializeHubSetting(aEntry->get(HubSettings::Connection));
			case PROP_CONN_MODE6: return serializeHubSetting(aEntry->get(HubSettings::Connection6));
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

		return aSetting.value;
	}

	json FavoriteHubUtils::serializeHubSetting(int aSetting) noexcept {
		if (!HubSettings::defined(aSetting)) {
			return nullptr;
		}

		return aSetting;
	}

	string FavoriteHubUtils::serializeHubSetting(const string& aSetting) noexcept {
		if (!HubSettings::defined(aSetting)) {
			return Util::emptyString;
		}

		return aSetting;
	}

	std::string FavoriteHubUtils::getStringInfo(const FavoriteHubEntryPtr& aEntry, int aPropertyName) noexcept {
		switch (aPropertyName) {
			case PROP_NAME: return aEntry->getName();
			case PROP_HUB_URL: return aEntry->getServer();
			case PROP_HUB_DESCRIPTION: return aEntry->getDescription();
			case PROP_NICK: return serializeHubSetting(aEntry->get(HubSettings::Nick));
			case PROP_USER_DESCRIPTION: return serializeHubSetting(aEntry->get(HubSettings::Description));
			case PROP_SHARE_PROFILE: return HubSettings::defined(aEntry->get(HubSettings::ShareProfile)) ? aEntry->getShareProfileName() : Util::emptyString;
			case PROP_NMDC_ENCODING: return serializeHubSetting(aEntry->get(HubSettings::NmdcEncoding));
			case PROP_IP4: return serializeHubSetting(aEntry->get(HubSettings::UserIp));
			case PROP_IP6: return serializeHubSetting(aEntry->get(HubSettings::UserIp6));
			default: dcassert(0); return Util::emptyString;
		}
	}

	double FavoriteHubUtils::getNumericInfo(const FavoriteHubEntryPtr& aEntry, int aPropertyName) noexcept {
		switch (aPropertyName) {
			case PROP_AUTO_CONNECT: return (double)aEntry->getAutoConnect();
			case PROP_HAS_PASSWORD: return (double)!aEntry->getPassword().empty();
			case PROP_CONN_MODE4: return (double)aEntry->get(HubSettings::Connection);
			case PROP_CONN_MODE6: return (double)aEntry->get(HubSettings::Connection6);
			default: dcassert(0); return 0;
		}
	}
}