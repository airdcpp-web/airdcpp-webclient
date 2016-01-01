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

#include <api/OnlineUserUtils.h>
#include <api/HubInfo.h>

#include <api/common/Serializer.h>
#include <api/common/Format.h>

namespace webserver {
	json OnlineUserUtils::serializeUser(const OnlineUserPtr& aUser, int aPropertyName) noexcept {
		switch (aPropertyName) {
			case HubInfo::PROP_IP4: return Serializer::serializeIp(aUser->getIdentity().getIp4());
			case HubInfo::PROP_IP6: return Serializer::serializeIp(aUser->getIdentity().getIp6());
			case HubInfo::PROP_FLAGS: return Serializer::getOnlineUserFlags(aUser);
		}

		return nullptr;
	}

	int OnlineUserUtils::compareUsers(const OnlineUserPtr& a, const OnlineUserPtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case HubInfo::PROP_NICK: {
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

			return Util::stricmp(a->getIdentity().getNick(), b->getIdentity().getNick());
		}
		default:
			dcassert(0);
		}

		return 0;
	}
	std::string OnlineUserUtils::getStringInfo(const OnlineUserPtr& aUser, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case HubInfo::PROP_NICK: return aUser->getIdentity().getNick();
		case HubInfo::PROP_DESCRIPTION: return aUser->getIdentity().getDescription();
		case HubInfo::PROP_EMAIL: return aUser->getIdentity().getEmail();
		case HubInfo::PROP_TAG: return aUser->getIdentity().getTag();
		case HubInfo::PROP_HUB_URL: return aUser->getHubUrl();
		case HubInfo::PROP_HUB_NAME: return aUser->getClient()->getHubName();
		case HubInfo::PROP_IP4: return aUser->getIdentity().getIp4();
		case HubInfo::PROP_IP6: return aUser->getIdentity().getIp6();
		case HubInfo::PROP_CID: return aUser->getUser()->getCID().toBase32();
		default: dcassert(0); return 0;
		}
	}
	double OnlineUserUtils::getNumericInfo(const OnlineUserPtr& aUser, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case HubInfo::PROP_SHARED: return Util::toDouble(aUser->getIdentity().getShareSize());
		case HubInfo::PROP_UPLOAD_SPEED: return (double)aUser->getIdentity().getAdcConnectionSpeed(false);
		case HubInfo::PROP_DOWNLOAD_SPEED: return (double)aUser->getIdentity().getAdcConnectionSpeed(true);
		//case HubInfo::PROP_ACTIVE4: return aUser->getIdentity().;
		//case HubInfo::PROP_ACTIVE6: return (double)aResult->sr->getDate();
		case HubInfo::PROP_FILES: return Util::toDouble(aUser->getIdentity().getSharedFiles());
		default: dcassert(0); return 0;
		}
	}
}