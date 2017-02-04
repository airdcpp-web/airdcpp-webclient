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
#include "LogManager.h"
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

}

RecentManager::~RecentManager() {

}

void RecentManager::clearRecents() noexcept {
	for (auto r : recents) {
		fire(RecentManagerListener::RecentRemoved(), r);
	}

	{
		WLock l(cs);
		recents.clear();
	}
	delayEvents.addEvent(SAVE, [=] { saveRecents(); }, 1000);
}


void RecentManager::addRecent(const string& aUrl) noexcept {
	
	RecentEntryPtr r = getRecentEntry(aUrl);
	if (r)
		return;

	{
		WLock l(cs);
		r = make_shared<RecentEntry>(aUrl);
		recents.push_back(r);
	}

	fire(RecentManagerListener::RecentAdded(), r);
	delayEvents.addEvent(SAVE, [=] { saveRecents(); }, 5000);
}

void RecentManager::removeRecent(const string& aUrl) noexcept {
	RecentEntryPtr r = nullptr;
	{
		WLock l(cs);
		auto i = find_if(recents, [&aUrl](const RecentEntryPtr& rhe) { return Util::stricmp(rhe->getUrl(), aUrl) == 0; });
		if (i == recents.end()) {
			return;
		}
		r = *i;
		recents.erase(i);
	}
	fire(RecentManagerListener::RecentRemoved(), r);
	delayEvents.addEvent(SAVE, [=] { saveRecents(); }, 1000);
}

void RecentManager::updateRecent(const ClientPtr& aClient) noexcept {
	RecentEntryPtr r = getRecentEntry(aClient->getHubUrl());
	if (!r)
		return;


	if (r) {
		r->setName(aClient->getHubName());
		r->setDescription(aClient->getHubDescription());
	}

	fire(RecentManagerListener::RecentUpdated(), r);
	delayEvents.addEvent(SAVE, [=] { saveRecents(); }, 5000);
}

void RecentManager::saveRecents() const noexcept {
	SimpleXML xml;

	xml.addTag("Recents");
	xml.stepIn();

	xml.addTag("Hubs");
	xml.stepIn();

	{
		RLock l(cs);
		for (const auto& rhe : recents) {
			xml.addTag("Hub");
			xml.addChildAttrib("Name", rhe->getName());
			xml.addChildAttrib("Description", rhe->getDescription());
			xml.addChildAttrib("Server", rhe->getUrl());
		}
	}

	xml.stepOut();
	xml.stepOut();

	SettingsManager::saveSettingFile(xml, CONFIG_DIR, CONFIG_RECENTS_NAME);
}

void RecentManager::load() noexcept {
	try {
		SimpleXML xml;
		SettingsManager::loadSettingFile(xml, CONFIG_DIR, CONFIG_RECENTS_NAME);
		if (xml.findChild("Recents")) {
			xml.stepIn();
			loadRecents(xml);
			xml.stepOut();
		}
	} catch (const Exception& e) {
		LogManager::getInstance()->message(STRING_F(LOAD_FAILED_X, CONFIG_RECENTS_NAME % e.getError()), LogMessage::SEV_ERROR);
	}
}

void RecentManager::loadRecents(SimpleXML& aXml) {
	aXml.resetCurrentChild();
	if (aXml.findChild("Hubs")) {
		aXml.stepIn();
		while (aXml.findChild("Hub")) {
			auto e = make_shared<RecentEntry>(aXml.getChildAttrib("Server"));
			e->setName(aXml.getChildAttrib("Name"));
			e->setDescription(aXml.getChildAttrib("Description"));
			recents.push_back(e);
		}
		aXml.stepOut();
	}
}

RecentEntryPtr RecentManager::getRecentEntry(const string& aServer) const noexcept {
	RLock l(cs);
	auto i = find_if(recents, [&aServer](const RecentEntryPtr& rhe) { return Util::stricmp(rhe->getUrl(), aServer) == 0; });
	if (i != recents.end())
		return *i;

	return nullptr;
}

RecentEntryList RecentManager::searchRecents(const string& aPattern, size_t aMaxResults) const noexcept {
	auto search = RelevanceSearch<RecentEntryPtr>(aPattern, [](const RecentEntryPtr& aHub) {
		return aHub->getName();
	});

	{
		RLock l(cs);
		for (const auto& hub : recents) {
			search.match(hub);
		}
	}

	return search.getResults(aMaxResults);
}

} // namespace dcpp
