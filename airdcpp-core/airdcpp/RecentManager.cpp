/*
* Copyright (C) 2011-2017 AirDC++ Project
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
#include "RecentManager.h"

#include "AirUtil.h"
#include "ClientManager.h"
#include "DirectoryListingManager.h"
#include "LogManager.h"
#include "MessageManager.h"
#include "RelevanceSearch.h"
#include "ResourceManager.h"
#include "ShareManager.h"
#include "SimpleXML.h"
#include "UserCommand.h"

namespace dcpp {

#define CONFIG_RECENTS_NAME "Recents.xml"
#define CONFIG_DIR Util::PATH_USER_CONFIG

using boost::range::find_if;

RecentManager::RecentManager() {
	ClientManager::getInstance()->addListener(this);
	DirectoryListingManager::getInstance()->addListener(this);
	MessageManager::getInstance()->addListener(this);

	TimerManager::getInstance()->addListener(this);
}

RecentManager::~RecentManager() {
	ClientManager::getInstance()->removeListener(this);
	DirectoryListingManager::getInstance()->removeListener(this);
	MessageManager::getInstance()->removeListener(this);

	TimerManager::getInstance()->removeListener(this);
}

RecentHubEntryList RecentManager::getRecentHubs() const noexcept {
	RLock l(cs); 
	return recentHubs; 
}

RecentUserEntryList RecentManager::getRecentChats() const noexcept {
	RLock l(cs);
	return recentChats;
}

RecentUserEntryList RecentManager::getRecentFilelists() const noexcept {
	RLock l(cs);
	return recentFilelists;
}

void RecentManager::on(TimerManagerListener::Minute, uint64_t) noexcept {
	if (!xmlDirty) {
		return;
	}

	xmlDirty = false;
	save();
}

void RecentManager::on(MessageManagerListener::ChatCreated, const PrivateChatPtr& aChat, bool) noexcept {
	auto r = getRecentUser(aChat->getUser()->getCID(), recentChats);
	if (r) {
		r->updateLastOpened();
		fire(RecentManagerListener::RecentChatUpdated(), r);
		return;
	}

	{
		WLock l(cs);
		r = make_shared<RecentUserEntry>(aChat->getHintedUser());
		recentChats.push_back(r);
	}

	fire(RecentManagerListener::RecentChatAdded(), r);
	setDirty();
}

void RecentManager::on(DirectoryListingManagerListener::ListingCreated, const DirectoryListingPtr& aListing) noexcept {
	if (aListing->getIsOwnList()) {
		return;
	}

	auto r = getRecentUser(aListing->getUser()->getCID(), recentFilelists);
	if (r) {
		r->updateLastOpened();
		fire(RecentManagerListener::RecentFilelistUpdated(), r);
		return;
	}

	{
		WLock l(cs);
		r = make_shared<RecentUserEntry>(aListing->getHintedUser());
		recentFilelists.push_back(r);
	}

	fire(RecentManagerListener::RecentFilelistAdded(), r);
	setDirty();
}

void RecentManager::on(ClientManagerListener::ClientCreated, const ClientPtr& c) noexcept {
	addRecentHub(c->getHubUrl());
}

void RecentManager::on(ClientManagerListener::ClientRedirected, const ClientPtr&, const ClientPtr& aNewClient) noexcept {
	addRecentHub(aNewClient->getHubUrl());
}

void RecentManager::on(ClientManagerListener::ClientUpdated, const ClientPtr& c) noexcept {
	updateRecentHub(c);
}

void RecentManager::on(ClientManagerListener::ClientRemoved, const ClientPtr& c) noexcept {
	updateRecentHub(c);
}

void RecentManager::clearRecentHubs() noexcept {
	for (const auto& r : recentHubs) {
		fire(RecentManagerListener::RecentHubRemoved(), r);
	}

	{
		WLock l(cs);
		recentHubs.clear();
	}

	setDirty();
}


void RecentManager::addRecentHub(const string& aUrl) noexcept {
	auto r = getRecentHub(aUrl);
	if (r) {
		r->updateLastOpened();
		fire(RecentManagerListener::RecentHubUpdated(), r);
		return;
	}

	{
		WLock l(cs);
		r = make_shared<RecentHubEntry>(aUrl);
		recentHubs.push_back(r);
	}

	fire(RecentManagerListener::RecentHubAdded(), r);
	setDirty();
}

void RecentManager::removeRecentHub(const string& aUrl) noexcept {
	auto r = getRecentHub(aUrl);
	if (!r) {
		return;
	}

	{
		WLock l(cs);
		recentHubs.erase(remove(recentHubs.begin(), recentHubs.end(), r), recentHubs.end());
	}

	fire(RecentManagerListener::RecentHubRemoved(), r);
	setDirty();
}

void RecentManager::updateRecentHub(const ClientPtr& aClient) noexcept {
	auto r = getRecentHub(aClient->getHubUrl());
	if (!r)
		return;


	if (r) {
		r->setName(aClient->getHubName());
		r->setDescription(aClient->getHubDescription());
	}

	fire(RecentManagerListener::RecentHubUpdated(), r);
	setDirty();
}

void RecentManager::save() const noexcept {
	SimpleXML xml;

	xml.addTag("Recents");
	xml.stepIn();

	saveRecentHubs(xml);
	saveRecentUsers(xml, "PrivateChats", recentChats);
	saveRecentUsers(xml, "Filelists", recentFilelists);

	xml.stepOut();

	SettingsManager::saveSettingFile(xml, CONFIG_DIR, CONFIG_RECENTS_NAME);
}

void RecentManager::saveRecentHubs(SimpleXML& aXml) const noexcept {
	aXml.addTag("Hubs");
	aXml.stepIn();

	{
		RLock l(cs);
		for (const auto& rhe : recentHubs) {
			aXml.addTag("Hub");
			aXml.addChildAttrib("Name", rhe->getName());
			aXml.addChildAttrib("Description", rhe->getDescription());
			aXml.addChildAttrib("Server", rhe->getUrl());
			aXml.addChildAttrib("LastOpened", rhe->getLastOpened());
		}
	}

	aXml.stepOut();
}

void RecentManager::saveRecentUsers(SimpleXML& aXml, const string& aRootTag, const RecentUserEntryList& users_) const noexcept {
	aXml.addTag(aRootTag);
	aXml.stepIn();

	{
		RLock l(cs);
		for (const auto& rhe : users_) {
			aXml.addTag("User");
			aXml.addChildAttrib("CID", rhe->getUser().user->getCID().toBase32());
			aXml.addChildAttrib("HubHint", rhe->getUser().hint);
			aXml.addChildAttrib("LastOpened", rhe->getLastOpened());
		}
	}

	aXml.stepOut();
}

void RecentManager::load() noexcept {
	try {
		SimpleXML xml;
		SettingsManager::loadSettingFile(xml, CONFIG_DIR, CONFIG_RECENTS_NAME);
		if (xml.findChild("Recents")) {
			xml.stepIn();
			loadRecentHubs(xml);
			loadRecentUsers(xml, "PrivateChats", recentChats);
			loadRecentUsers(xml, "Filelists", recentFilelists);
			xml.stepOut();
		}
	} catch (const Exception& e) {
		LogManager::getInstance()->message(STRING_F(LOAD_FAILED_X, CONFIG_RECENTS_NAME % e.getError()), LogMessage::SEV_ERROR);
	}
}

void RecentManager::loadRecentHubs(SimpleXML& aXml) {
	aXml.resetCurrentChild();
	if (aXml.findChild("Hubs")) {
		aXml.stepIn();
		while (aXml.findChild("Hub")) {
			const string& hubUrl = aXml.getChildAttrib("Server");
			const string& name = aXml.getChildAttrib("Name");
			const string& description = aXml.getChildAttrib("Description");
			const time_t& lastOpened = aXml.getLongLongChildAttrib("LastOpened");

			auto e = make_shared<RecentHubEntry>(hubUrl, name, description, lastOpened);
			recentHubs.push_back(e);
		}
		aXml.stepOut();
	}
}

void RecentManager::loadRecentUsers(SimpleXML& aXml, const string& aRootTag, RecentUserEntryList& users_) {
	aXml.resetCurrentChild();
	if (aXml.findChild(aRootTag)) {
		aXml.stepIn();
		while (aXml.findChild("User")) {
			const string& cid = aXml.getChildAttrib("CID");
			const string& hubHint = aXml.getChildAttrib("HubHint");
			const time_t& lastOpened = aXml.getLongLongChildAttrib("LastOpened");

			auto user = ClientManager::getInstance()->loadUser(cid, hubHint, Util::emptyString);
			if (user == nullptr) {
				return;
			}

			auto e = make_shared<RecentUserEntry>(HintedUser(user, hubHint), lastOpened);
			users_.push_back(e);
		}
		aXml.stepOut();
	}
}

RecentHubEntryPtr RecentManager::getRecentHub(const string& aUrl) const noexcept {
	RLock l(cs);
	auto i = find_if(recentHubs, RecentHubEntry::UrlCompare(aUrl));
	return i != recentHubs.end() ? *i : nullptr;
}

RecentUserEntryPtr RecentManager::getRecentUser(const CID& aCid, const RecentUserEntryList& aUsers) noexcept {
	RLock l(cs);
	auto i = find_if(aUsers, RecentUserEntry::CidCompare(aCid));
	return i != aUsers.end() ? *i : nullptr;
}

RecentHubEntryList RecentManager::searchRecentHubs(const string& aPattern, size_t aMaxResults) const noexcept {
	auto search = RelevanceSearch<RecentHubEntryPtr>(aPattern, [](const RecentHubEntryPtr& aHub) {
		return aHub->getName();
	});

	{
		RLock l(cs);
		for (const auto& hub : recentHubs) {
			search.match(hub);
		}
	}

	return search.getResults(aMaxResults);
}

} // namespace dcpp
