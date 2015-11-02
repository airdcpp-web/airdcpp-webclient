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

#include <api/FavoriteHubUtils.h>
#include <api/FavoriteHubApi.h>

#include <api/common/Format.h>

#include <airdcpp/FavoriteManager.h>

namespace webserver {
	FavoriteHubEntryList FavoriteHubUtils::getEntryList() noexcept {
		return FavoriteManager::getInstance()->getFavoriteHubs();
	}

	json FavoriteHubUtils::serializeHub(const FavoriteHubEntryPtr& aEntry, int aPropertyName) noexcept {
		json j;

		switch (aPropertyName) {
			/*case QueueApi::PROP_SOURCES:
			{
			RLock l(QueueManager::getInstance()->getCS());
			int online = 0;
			decltype(auto) sources = aBundle->getSources();
			for (const auto& s : sources) {
			if (s.getUser().user->isOnline())
			online++;
			}

			j["online"] = online;
			j["total"] = aBundle->getSources().size();
			}

			case QueueApi::PROP_TYPE:
			{
			RLock l(QueueManager::getInstance()->getCS());
			j["files"] = aBundle->getQueueItems().size() + aBundle->getFinishedFiles().size();
			j["folders"] = aBundle->getDirectories().size();
			}*/
		}

		return j;
	}

	int FavoriteHubUtils::compareEntries(const FavoriteHubEntryPtr& a, const FavoriteHubEntryPtr& b, int aPropertyName) noexcept {
		return 0;
	}
	std::string FavoriteHubUtils::getStringInfo(const FavoriteHubEntryPtr& aEntry, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case FavoriteHubApi::PROP_NAME: return aEntry->getName();
		case FavoriteHubApi::PROP_HUB_URL: return aEntry->getServer();
		case FavoriteHubApi::PROP_HUB_DESCRIPTION: return aEntry->getDescription();
		case FavoriteHubApi::PROP_NICK: return aEntry->get(HubSettings::Nick);
		case FavoriteHubApi::PROP_USER_DESCRIPTION: return aEntry->get(HubSettings::Description);
		case FavoriteHubApi::PROP_SHARE_PROFILE: return aEntry->getShareProfile()->getDisplayName();
		default: dcassert(0); return 0;
		}
	}
	double FavoriteHubUtils::getNumericInfo(const FavoriteHubEntryPtr& aEntry, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case FavoriteHubApi::PROP_AUTO_CONNECT: return (double)aEntry->getAutoConnect();
		case FavoriteHubApi::PROP_SHARE_PROFILE: return (double)aEntry->getShareProfile()->getToken();
		case FavoriteHubApi::PROP_CONNECT_STATE: return (double)aEntry->getConnectState();
		case FavoriteHubApi::PROP_HAS_PASSWORD: return (double)!aEntry->getPassword().empty();
		default: dcassert(0); return 0;
		}
	}
}