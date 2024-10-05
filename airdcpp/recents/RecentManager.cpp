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
#include <airdcpp/recents/RecentManager.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/filelist/DirectoryListing.h>
#include <airdcpp/filelist/DirectoryListingManager.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/private_chat/PrivateChatManager.h>
#include <airdcpp/search/RelevanceSearch.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/core/io/xml/SimpleXML.h>
#include <airdcpp/hub/user_command/UserCommand.h>

namespace dcpp {

#define CONFIG_RECENTS_NAME "Recents.xml"
#define CONFIG_DIR AppUtil::PATH_USER_CONFIG

using ranges::find_if;

string RecentManager::rootTags[RecentEntry::TYPE_LAST] = {
	"Hubs",
	"PrivateChats",
	"Filelists"
};

string RecentManager::itemTags[RecentEntry::TYPE_LAST] = {
	"Hub",
	"User",
	"User"
};

SettingsManager::IntSetting RecentManager::maxLimits[RecentEntry::TYPE_LAST] = {
	SettingsManager::MAX_RECENT_HUBS,
	SettingsManager::MAX_RECENT_PRIVATE_CHATS,
	SettingsManager::MAX_RECENT_FILELISTS,
};

RecentManager::RecentManager() {
	ClientManager::getInstance()->addListener(this);
	DirectoryListingManager::getInstance()->addListener(this);
	PrivateChatManager::getInstance()->addListener(this);

	TimerManager::getInstance()->addListener(this);
}

RecentManager::~RecentManager() {
	ClientManager::getInstance()->removeListener(this);
	DirectoryListingManager::getInstance()->removeListener(this);
	PrivateChatManager::getInstance()->removeListener(this);

	TimerManager::getInstance()->removeListener(this);
}

RecentEntryList RecentManager::getRecents(RecentEntry::Type aType) const noexcept {
	RLock l(cs); 
	return recents[aType]; 
}

void RecentManager::on(TimerManagerListener::Minute, uint64_t) noexcept {
	save();
}

void RecentManager::on(PrivateChatManagerListener::ChatCreated, const PrivateChatPtr& aChat, bool) noexcept {
	auto old = getRecent(RecentEntry::TYPE_PRIVATE_CHAT, RecentEntry::CidCompare(aChat->getUser()->getCID()));
	auto nick = ClientManager::getInstance()->getNick(aChat->getUser(), aChat->getHubUrl(), true);
	onRecentOpened(RecentEntry::TYPE_PRIVATE_CHAT, nick, Util::emptyString, aChat->getHubUrl(), aChat->getUser(), old);
}

void RecentManager::on(DirectoryListingManagerListener::ListingCreated, const DirectoryListingPtr& aListing) noexcept {
	if (aListing->getIsOwnList()) {
		return;
	}

	if (aListing->getHubUrl().empty()) {
		// Filelist loaded from disk
		return;
	}

	auto shareInfo = ClientManager::getInstance()->getShareInfo(aListing->getHintedUser());
	auto nick = ClientManager::getInstance()->getNick(aListing->getUser(), aListing->getHubUrl(), true);
	auto old = getRecent(RecentEntry::TYPE_FILELIST, RecentEntry::CidCompare(aListing->getUser()->getCID()));
	onRecentOpened(RecentEntry::TYPE_FILELIST, nick, shareInfo ? Util::formatBytes((*shareInfo).size) : Util::emptyString, aListing->getHubUrl(), aListing->getUser(), old);
}

void RecentManager::on(ClientManagerListener::ClientCreated, const ClientPtr& aClient) noexcept {
	onHubOpened(aClient);
}

void RecentManager::on(ClientManagerListener::ClientRedirected, const ClientPtr&, const ClientPtr& aNewClient) noexcept {
	onHubOpened(aNewClient);
	auto old = getRecent(RecentEntry::TYPE_HUB, RecentEntry::UrlCompare(aNewClient->getHubUrl()));
	onRecentOpened(RecentEntry::TYPE_HUB, aNewClient->getHubUrl(), Util::emptyString, aNewClient->getHubUrl(), nullptr, old);
}

void RecentManager::onHubOpened(const ClientPtr& aClient) noexcept {
	auto old = getRecent(RecentEntry::TYPE_HUB, RecentEntry::UrlCompare(aClient->getHubUrl()));
	onRecentOpened(RecentEntry::TYPE_HUB, aClient->getHubUrl(), Util::emptyString, aClient->getHubUrl(), nullptr, old);
}

void RecentManager::on(ClientManagerListener::ClientUpdated, const ClientPtr& aClient) noexcept {
	auto r = getRecent(RecentEntry::TYPE_HUB, RecentEntry::UrlCompare(aClient->getHubUrl()));
	if (!r) {
		return;
	}

	r->setName(aClient->getHubName());
	r->setDescription(aClient->getHubDescription());

	onRecentUpdated(RecentEntry::TYPE_HUB, r);
}

void RecentManager::clearRecents(RecentEntry::Type aType) noexcept {
	for (const auto& r : recents[aType]) {
		fire(RecentManagerListener::RecentRemoved(), r, aType);
	}

	{
		WLock l(cs);
		recents[aType].clear();
	}

	setDirty();
}


void RecentManager::onRecentOpened(RecentEntry::Type aType, const string& aName, const string& aDescription, const string& aUrl, const UserPtr& aUser, const RecentEntryPtr& aOldEntry) noexcept {
	dcassert(!aName.empty() && !aUrl.empty());
	if (aOldEntry) {
		// Remove and add as the last item
		removeRecent(aType, aOldEntry);
	}

	auto entry = std::make_shared<RecentEntry>(aName, aDescription, aUrl, aUser);
	{
		WLock l(cs);
		recents[aType].push_back(entry);
		checkCount(aType);
	}

	fire(RecentManagerListener::RecentAdded(), entry, aType);
	setDirty();
}

void RecentManager::removeRecent(RecentEntry::Type aType, const RecentEntryPtr& aEntry) noexcept {
	if (!aEntry) {
		return;
	}

	{
		WLock l(cs);
		recents[aType].erase(remove(recents[aType].begin(), recents[aType].end(), aEntry), recents[aType].end());
	}

	fire(RecentManagerListener::RecentRemoved(), aEntry, aType);
	setDirty();
}

void RecentManager::onRecentUpdated(RecentEntry::Type aType, const RecentEntryPtr& aEntry) noexcept {
	if (!aEntry) {
		return;
	}

	fire(RecentManagerListener::RecentUpdated(), aEntry, aType);
	setDirty();
}

void RecentManager::save() noexcept {
	if (!xmlDirty) {
		return;
	}

	xmlDirty = false;

	SimpleXML xml;

	xml.addTag("Recents");
	xml.stepIn();

	saveRecents(xml, RecentEntry::TYPE_HUB);
	saveRecents(xml, RecentEntry::TYPE_PRIVATE_CHAT);
	saveRecents(xml, RecentEntry::TYPE_FILELIST);

	xml.stepOut();

	SettingsManager::saveSettingFile(xml, CONFIG_DIR, CONFIG_RECENTS_NAME);
}

void RecentManager::saveRecents(SimpleXML& aXml, RecentEntry::Type aType) const noexcept {
	aXml.addTag(rootTags[aType]);
	aXml.stepIn();

	{
		RLock l(cs);
		for (const auto& rhe : recents[aType]) {
			aXml.addTag(itemTags[aType]);
			aXml.addChildAttrib("Name", rhe->getName());
			aXml.addChildAttrib("Description", rhe->getDescription());
			aXml.addChildAttrib("Server", rhe->getUrl());
			aXml.addChildAttrib("LastOpened", rhe->getLastOpened());
			if (rhe->getUser()) {
				aXml.addChildAttrib("CID", rhe->getUser()->getCID().toBase32());
			}
		}
	}

	aXml.stepOut();
}

void RecentManager::load() noexcept {
	SettingsManager::loadSettingFile(CONFIG_DIR, CONFIG_RECENTS_NAME, [this](SimpleXML& xml) {
		if (xml.findChild("Recents")) {
			xml.stepIn();
			loadRecents(xml, RecentEntry::TYPE_HUB);
			loadRecents(xml, RecentEntry::TYPE_PRIVATE_CHAT);
			loadRecents(xml, RecentEntry::TYPE_FILELIST);
			xml.stepOut();
		}
	});
}

void RecentManager::loadRecents(SimpleXML& aXml, RecentEntry::Type aType) {
	aXml.resetCurrentChild();
	if (aXml.findChild(rootTags[aType])) {
		aXml.stepIn();
		while (aXml.findChild(itemTags[aType])) {
			const string& name = aXml.getChildAttrib("Name");
			if (name.empty() || name == "*") {
				continue;
			}

			const string& description = aXml.getChildAttrib("Description");
			const string& hubUrl = aXml.getChildAttrib("Server");
			const time_t& lastOpened = aXml.getTimeChildAttrib("LastOpened");

			UserPtr user = nullptr;
			const string& cid = aXml.getChildAttrib("CID");
			if (!cid.empty()) {
				user = ClientManager::getInstance()->loadUser(cid, hubUrl, name);
				if (user == nullptr) {
					continue;
				}
			}

			auto e = std::make_shared<RecentEntry>(name, description, hubUrl, user, lastOpened);
			recents[aType].push_back(e);
		}
		aXml.stepOut();
	}

	// Old versions didn't have any limit for maximum recent hubs
	checkCount(aType);
}

void RecentManager::checkCount(RecentEntry::Type aType) noexcept {
	auto toRemove = static_cast<int>(recents[aType].size()) - SettingsManager::getInstance()->get(maxLimits[aType]);
	if (toRemove > 0) {
		recents[aType].erase(recents[aType].begin(), recents[aType].begin() + toRemove);
	}
}

RecentEntryList RecentManager::searchRecents(RecentEntry::Type aType, const string& aPattern, size_t aMaxResults) const noexcept {
	auto search = RelevanceSearch<RecentEntryPtr>(aPattern, [](const RecentEntryPtr& aHub) {
		return aHub->getName();
	});

	{
		RLock l(cs);
		for (const auto& e : recents[aType]) {
			search.match(e);
		}
	}

	return search.getResults(aMaxResults);
}

} // namespace dcpp
