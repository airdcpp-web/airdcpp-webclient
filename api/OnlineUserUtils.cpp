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

#include <api/OnlineUserUtils.h>
#include <api/HubInfo.h>

#include <api/common/Serializer.h>
#include <api/common/Format.h>

namespace webserver {
	const PropertyList OnlineUserUtils::properties = {
		{ PROP_NICK, "nick", TYPE_TEXT, SERIALIZE_TEXT, SORT_CUSTOM },
		{ PROP_SHARED, "share_size", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_DESCRIPTION, "description", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_TAG, "tag", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_UPLOAD_SPEED, "upload_speed", TYPE_SPEED, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_DOWNLOAD_SPEED, "download_speed", TYPE_SPEED, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_IP4, "ip4", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
		{ PROP_IP6, "ip6", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
		{ PROP_EMAIL, "email", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_FILES, "file_count", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_HUB_URL, "hub_url", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_HUB_NAME , "hub_name", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_FLAGS, "flags", TYPE_LIST_TEXT, SERIALIZE_CUSTOM, SORT_NONE },
		{ PROP_CID, "cid", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_UPLOAD_SLOTS, "upload_slots", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
	};

	const PropertyItemHandler<OnlineUserPtr> OnlineUserUtils::propertyHandler = {
		OnlineUserUtils::properties,
		OnlineUserUtils::getStringInfo, OnlineUserUtils::getNumericInfo, OnlineUserUtils::compareUsers, OnlineUserUtils::serializeUser
	};

	json OnlineUserUtils::serializeUser(const OnlineUserPtr& aUser, int aPropertyName) noexcept {
		switch (aPropertyName) {
			case PROP_IP4: return Serializer::serializeIp(aUser->getIdentity().getIp4());
			case PROP_IP6: return Serializer::serializeIp(aUser->getIdentity().getIp6());
			case PROP_FLAGS: return Serializer::getOnlineUserFlags(aUser);
		}

		return nullptr;
	}

	int OnlineUserUtils::compareUsers(const OnlineUserPtr& a, const OnlineUserPtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_NICK: {
			bool a_isOp = a->getIdentity().isOp(),
				b_isOp = b->getIdentity().isOp();
			if (a_isOp && !b_isOp)
				return -1;
			if (!a_isOp && b_isOp)
				return 1;
			if (SETTING(SORT_FAVUSERS_FIRST)) {
				bool a_isFav = a->getUser()->isFavorite(),
					b_isFav = b->getUser()->isFavorite();

				if (a_isFav && !b_isFav)
					return -1;
				if (!a_isFav && b_isFav)
					return 1;
			}

			return Util::DefaultSort(a->getIdentity().getNick(), b->getIdentity().getNick());
		}
		default:
			dcassert(0);
		}

		return 0;
	}
	std::string OnlineUserUtils::getStringInfo(const OnlineUserPtr& aUser, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_NICK: return aUser->getIdentity().getNick();
		case PROP_DESCRIPTION: return aUser->getIdentity().getDescription();
		case PROP_EMAIL: return aUser->getIdentity().getEmail();
		case PROP_TAG: return aUser->getIdentity().getTag();
		case PROP_HUB_URL: return aUser->getHubUrl();
		case PROP_HUB_NAME: return aUser->getClient()->getHubName();
		case PROP_IP4: return Format::formatIp(aUser->getIdentity().getIp4());
		case PROP_IP6: return Format::formatIp(aUser->getIdentity().getIp6());
		case PROP_CID: return aUser->getUser()->getCID().toBase32();
		default: dcassert(0); return 0;
		}
	}
	double OnlineUserUtils::getNumericInfo(const OnlineUserPtr& aUser, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_SHARED: return Util::toDouble(aUser->getIdentity().getShareSize());
		case PROP_UPLOAD_SPEED: return (double)aUser->getIdentity().getAdcConnectionSpeed(false);
		case PROP_DOWNLOAD_SPEED: return (double)aUser->getIdentity().getAdcConnectionSpeed(true);
		case PROP_FILES: return Util::toDouble(aUser->getIdentity().getSharedFiles());
		case PROP_UPLOAD_SLOTS: return aUser->getIdentity().getSlots();
		default: dcassert(0); return 0;
		}
	}
}