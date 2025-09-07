/* 
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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
#include <airdcpp/favorites/FavoriteManager.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/user/HintedUser.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/share/profiles/ShareProfileManager.h>
#include <airdcpp/core/io/xml/SimpleXML.h>

namespace dcpp {

#define CONFIG_FAV_NAME "Favorites.xml"
#define CONFIG_DIR AppUtil::PATH_USER_CONFIG

using ranges::find_if;

FavoriteManager::FavoriteManager() {
	SettingsManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);
	ShareManager::getInstance()->getProfileMgr().addListener(this);
}

FavoriteManager::~FavoriteManager() {
	ClientManager::getInstance()->removeListener(this);
	SettingsManager::getInstance()->removeListener(this);
	ShareManager::getInstance()->getProfileMgr().removeListener(this);
}

void FavoriteManager::shutdown() noexcept {
	TimerManager::getInstance()->removeListener(this);
	save();
}

bool FavoriteManager::hasFavoriteDir(const string& aPath) const noexcept {
	RLock l(cs);
	return favoriteDirectories.contains(aPath);
}

bool FavoriteManager::setFavoriteDir(const string& aPath, const string& aGroupName) noexcept {
	{
		WLock l(cs);
		favoriteDirectories[aPath] = aGroupName;
	}

	setDirty();

	fire(FavoriteManagerListener::FavoriteDirectoriesUpdated());
	return true;
}

bool FavoriteManager::removeFavoriteDir(const string& aPath) noexcept {
	if (!hasFavoriteDir(aPath)) {
		return false;
	}

	{
		WLock l(cs);
		favoriteDirectories.erase(aPath);
	}

	setDirty();

	fire(FavoriteManagerListener::FavoriteDirectoriesUpdated());
	return true;
}

void FavoriteManager::setFavoriteDirs(const FavoriteDirectoryMap& dirs) noexcept {
	{
		WLock l(cs);
		favoriteDirectories = dirs;
	}

	fire(FavoriteManagerListener::FavoriteDirectoriesUpdated());
	setDirty();
}

StringPair FavoriteManager::getFavoriteDirectory(const string& aPath) const noexcept {
	RLock l(cs);
	auto i = favoriteDirectories.find(aPath);
	if (i == favoriteDirectories.end()) {
		return StringPair();
	}

	return *i;
}

GroupedDirectoryMap FavoriteManager::getGroupedFavoriteDirs() const noexcept {
	GroupedDirectoryMap ret;

	{
		RLock l(cs);
		for (const auto& [path, name] : favoriteDirectories) {
			ret[name].insert(path);
		}
	}

	return ret;
}

FavoriteManager::FavoriteDirectoryMap FavoriteManager::getFavoriteDirs() const noexcept {
	RLock l(cs);
	return favoriteDirectories;
}

// FAVORITE HUBS START
bool FavoriteManager::addFavoriteHub(const FavoriteHubEntryPtr& aEntry) noexcept {
	{
		WLock l(cs);
		if (auto i = getFavoriteHubUnsafe(aEntry->getServer()); i != favoriteHubs.end()) {
			return false;
		}

		favoriteHubs.push_back(aEntry);
	}

	setConnectState(aEntry);

	fire(FavoriteManagerListener::FavoriteHubAdded(), aEntry);
	setDirty();
	return true;
}

void FavoriteManager::onFavoriteHubUpdated(const FavoriteHubEntryPtr& aEntry) noexcept {
	// Update the connect state in case the address was changed
	setConnectState(aEntry);

	setDirty();
	fire(FavoriteManagerListener::FavoriteHubUpdated(), aEntry);
}

void FavoriteManager::autoConnect() noexcept {
	StringList hubs;

	{

		RLock l(cs);
		for (const auto& entry : favoriteHubs) {
			if (entry->getAutoConnect()) {
				hubs.emplace_back(entry->getServer());
			}
		}
	}

	for (const auto& h : hubs) {
		if (!ClientManager::getInstance()->findClient(h))
			ClientManager::getInstance()->createClient(h);
	}
}

bool FavoriteManager::removeFavoriteHub(FavoriteHubToken aToken) noexcept {
	FavoriteHubEntryPtr entry = nullptr;

	{
		WLock l(cs);
		auto i = ranges::find_if(favoriteHubs, [aToken](const FavoriteHubEntryPtr& f) { return f->getToken() == aToken; });
		if (i == favoriteHubs.end()) {
			return false;
		}

		entry = *i;
		favoriteHubs.erase(i);
	}

	fire(FavoriteManagerListener::FavoriteHubRemoved(), entry);
	setDirty();
	return true;
}

bool FavoriteManager::isUnique(const string& url, FavoriteHubToken aExcludedEntryToken) const noexcept {
	RLock l(cs);
	auto i = getFavoriteHubUnsafe(url);
	if (i == favoriteHubs.end())
		return true;

	return aExcludedEntryToken == (*i)->getToken();
}

void FavoriteManager::on(ShareProfileManagerListener::DefaultProfileChanged, ProfileToken aOldDefault, ProfileToken aNewDefault) noexcept {
	resetProfile(aOldDefault, aNewDefault, true);
}

void FavoriteManager::on(ShareProfileManagerListener::ProfileRemoved, ProfileToken aProfile) noexcept {
	resetProfile(aProfile, HUB_SETTING_DEFAULT_INT, false);
}

int FavoriteManager::resetProfile(ProfileToken aResetToken, ProfileToken aDefaultProfile, bool aNmdcOnly) noexcept {
	FavoriteHubEntryList updatedHubs;

	{
		RLock l(cs);
		for (const auto& fh : favoriteHubs) {
			if (fh->get(HubSettings::ShareProfile) == aResetToken) {
				if (!aNmdcOnly || !fh->isAdcHub()) {
					fh->get(HubSettings::ShareProfile) = aDefaultProfile;
					updatedHubs.push_back(fh);
				}
			}
		}
	}

	for (const auto& fh : updatedHubs) {
		fire(FavoriteManagerListener::FavoriteHubUpdated(), fh);
	}


	// Remove later
	fire(FavoriteManagerListener::FavoriteHubsUpdated());
	return static_cast<int>(updatedHubs.size());
}

bool FavoriteManager::hasActiveHubs() const noexcept {
	RLock l(cs);
	return ranges::any_of(favoriteHubs, [](const FavoriteHubEntryPtr& f) { 
		return f->get(HubSettings::Connection) == SettingsManager::INCOMING_ACTIVE || f->get(HubSettings::Connection6) == SettingsManager::INCOMING_ACTIVE; 
	});
}

// FAVORITE HUBS END

void FavoriteManager::save() noexcept {
	if (!xmlDirty)
		return;

	xmlDirty = false;
	lastXmlSave = GET_TICK();

	try {
		SimpleXML xml;

		xml.addTag("Favorites");
		xml.stepIn();

		{
			xml.addTag("CID", SETTING(PRIVATE_ID));

			saveFavoriteHubs(xml);
			saveFavoriteDirectories(xml);

			fire(FavoriteManagerListener::Save(), xml);
		}

		xml.stepOut();


		SettingsManager::saveSettingFile(xml, CONFIG_DIR, CONFIG_FAV_NAME);

	} catch(const Exception& e) {
		xmlDirty = true; //Oops, something went wrong, xml was not saved.
		dcdebug("FavoriteManager::save: %s\n", e.getError().c_str());
	}

}

void FavoriteManager::saveFavoriteDirectories(SimpleXML& aXml) const noexcept {
	aXml.addTag("FavoriteDirs");
	aXml.addChildAttrib("Version", 2);
	aXml.stepIn();

	const auto groupedDirs = getGroupedFavoriteDirs();
	for (const auto& [name, paths]: groupedDirs) {
		aXml.addTag("Directory", name);
		aXml.addChildAttrib("Name", name);
		aXml.stepIn();
		for (const auto& t: paths) {
			aXml.addTag("Target", t);
		}
		aXml.stepOut();
	}

	aXml.stepOut();
}

void FavoriteManager::saveFavoriteHubs(SimpleXML& aXml) const noexcept {
	aXml.addTag("Hubs");
	aXml.stepIn();

	{
		RLock l(cs);
		for (const auto& [name, group] : favHubGroups) {
			aXml.addTag("Group");
			aXml.addChildAttrib("Name", name);
			group.save(aXml);
		}

		for (const auto& i : favoriteHubs) {
			aXml.addTag("Hub");
			aXml.addChildAttrib("Name", i->getName());
			aXml.addChildAttrib("Connect", i->getAutoConnect());
			aXml.addChildAttrib("Description", i->getDescription());
			aXml.addChildAttrib("Password", i->getPassword());
			aXml.addChildAttrib("Server", i->getServer());
			aXml.addChildAttrib("Group", i->getGroup());
			aXml.addChildAttrib("ChatUserSplit", i->getChatUserSplit());
			aXml.addChildAttrib("UserListState", i->getUserListState());
#ifdef HAVE_GUI
			aXml.addChildAttrib("HubFrameOrder", i->getHeaderOrder());
			aXml.addChildAttrib("HubFrameWidths", i->getHeaderWidths());
			aXml.addChildAttrib("HubFrameVisible", i->getHeaderVisible());
			aXml.addChildAttrib("Bottom", Util::toString(i->getBottom()));
			aXml.addChildAttrib("Top", Util::toString(i->getTop()));
			aXml.addChildAttrib("Right", Util::toString(i->getRight()));
			aXml.addChildAttrib("Left", Util::toString(i->getLeft()));
#endif
			i->save(aXml);
		}
	}

	aXml.stepOut();
}

void FavoriteManager::loadCID() noexcept {
	SettingsManager::loadSettingFile(CONFIG_DIR, CONFIG_FAV_NAME, [](SimpleXML& xml) {
		if (xml.findChild("Favorites")) {
			xml.stepIn();
			if (xml.findChild("CID")) {
				xml.stepIn();
				SettingsManager::getInstance()->set(SettingsManager::PRIVATE_ID, xml.getData());
				xml.stepOut();
			}

			xml.stepOut();
		}
	});
}

void FavoriteManager::load() noexcept {
	SettingsManager::loadSettingFile(CONFIG_DIR, CONFIG_FAV_NAME, [this](SimpleXML& xml) {
		if(xml.findChild("Favorites")) {
			xml.stepIn();

			loadFavoriteHubs(xml);
			loadFavoriteDirectories(xml);

			fire(FavoriteManagerListener::Load(), xml);

			xml.stepOut();
		}
	});

	lastXmlSave = GET_TICK();
	TimerManager::getInstance()->addListener(this);
}

void FavoriteManager::loadFavoriteHubs(SimpleXML& aXml) {
	if (aXml.findChild("Hubs")) {
		aXml.stepIn();

		while (aXml.findChild("Group")) {
			string name = aXml.getChildAttrib("Name");
			if (name.empty())
				continue;

			HubSettings settings;
			settings.load(aXml);
			favHubGroups[name] = std::move(settings);
		}

		aXml.resetCurrentChild();
		while (aXml.findChild("Hub")) {
			auto e = std::make_shared<FavoriteHubEntry>();
			e->setName(aXml.getChildAttrib("Name"));
			e->setAutoConnect(aXml.getBoolChildAttrib("Connect"));
			e->setDescription(aXml.getChildAttrib("Description"));
			e->setPassword(aXml.getChildAttrib("Password"));

			auto server = aXml.getChildAttrib("Server");
			if (server.empty()) {
				dcdebug("A favorite hub with an empty address wasn't loaded: %s\n", e->getName().c_str());
				continue;
			}

			// Remove failovers
			if (auto p = server.find(';'); p != string::npos) {
				server = server.substr(0, p);
			}

			e->setServer(server);

			e->setChatUserSplit(aXml.getIntChildAttrib("ChatUserSplit"));
			e->setUserListState(aXml.getBoolChildAttrib("UserListState"));
#ifdef HAVE_GUI
			e->setHeaderOrder(aXml.getChildAttrib("HubFrameOrder", SETTING(HUBFRAME_ORDER)));
			e->setHeaderWidths(aXml.getChildAttrib("HubFrameWidths", SETTING(HUBFRAME_WIDTHS)));
			e->setHeaderVisible(aXml.getChildAttrib("HubFrameVisible", SETTING(HUBFRAME_VISIBLE)));
			e->setBottom((uint16_t)aXml.getIntChildAttrib("Bottom"));
			e->setTop((uint16_t)aXml.getIntChildAttrib("Top"));
			e->setRight((uint16_t)aXml.getIntChildAttrib("Right"));
			e->setLeft((uint16_t)aXml.getIntChildAttrib("Left"));
#endif
			e->setGroup(aXml.getChildAttrib("Group"));
			if (aXml.getBoolChildAttrib("HideShare")) {
				// For compatibility with very old favorites
				e->get(HubSettings::ShareProfile) = SP_HIDDEN;
			}

			e->load(aXml);

			// Unset share profile for old NMDC hubs and check for profiles that no longer exist.
			if (e->get(HubSettings::ShareProfile) != SP_HIDDEN &&
				(!e->isAdcHub() || !ShareManager::getInstance()->getShareProfile(e->get(HubSettings::ShareProfile)))) {
				e->get(HubSettings::ShareProfile) = HUB_SETTING_DEFAULT_INT;
			}

			favoriteHubs.push_back(e);
		}

		aXml.stepOut();
	}

	aXml.resetCurrentChild();
}

void FavoriteManager::loadFavoriteDirectories(SimpleXML& aXml) {
	if (aXml.findChild("FavoriteDirs")) {
		string version = aXml.getChildAttrib("Version");
		aXml.stepIn();
		if (version.empty() || Util::toInt(version) < 2) {
			// Convert old directories
			while (aXml.findChild("Directory")) {
				auto groupName = aXml.getChildAttrib("Name");

				favoriteDirectories[aXml.getChildData()] = groupName;
			}
		} else {
			while (aXml.findChild("Directory")) {
				auto groupName = aXml.getChildAttrib("Name");
				if (!groupName.empty()) {
					aXml.stepIn();
					while (aXml.findChild("Target")) {
						aXml.stepIn();

						auto path = aXml.getData();
						favoriteDirectories[path] = groupName;

						aXml.stepOut();
					}

					aXml.stepOut();
				}
			}
		}
		aXml.stepOut();
	}

	aXml.resetCurrentChild();
}

FavoriteHubEntryList FavoriteManager::getFavoriteHubs(const string& group) const noexcept {
	FavoriteHubEntryList ret;
	copy_if(favoriteHubs.begin(), favoriteHubs.end(), back_inserter(ret), [&group](const FavoriteHubEntryPtr& f) { return Util::stricmp(f->getGroup(), group) == 0; });
	return ret;
}

FavoriteHubEntryList FavoriteManager::getFavoriteHubs() const noexcept {
	RLock l(cs);
	return favoriteHubs;
}

void FavoriteManager::setFavHubGroups(const FavHubGroups& favHubGroups_) noexcept {
	WLock l(cs);
	favHubGroups = favHubGroups_; 
}

void FavoriteManager::setHubSetting(const string& aUrl, HubSettings::HubBoolSetting aSetting, bool aNewValue) noexcept {
	FavoriteHubEntryPtr hub;

	{
		RLock l(cs);
		auto p = getFavoriteHubUnsafe(aUrl);
		if (p == favoriteHubs.end()) {
			return;
		}

		hub = *p;
		(*p)->get(aSetting) = aNewValue;
	}

	ClientManager::getInstance()->myInfoUpdated();
	fire(FavoriteManagerListener::FavoriteHubUpdated(), hub);
}

void FavoriteManager::on(SettingsManagerListener::Load, SimpleXML&) noexcept {
	loadCID();
}

FavoriteHubEntryPtr FavoriteManager::getFavoriteHubEntry(const string& aServer) const noexcept {
	RLock l(cs);
	auto p = getFavoriteHubUnsafe(aServer);
	return p != favoriteHubs.end() ? *p : nullptr;
}

FavoriteHubEntryPtr FavoriteManager::getFavoriteHubEntry(FavoriteHubToken aToken) const noexcept {
	RLock l(cs);
	auto p = getFavoriteHubUnsafe(aToken);
	return p != favoriteHubs.end() ? *p : nullptr;
}

void FavoriteManager::mergeHubSettings(const FavoriteHubEntryPtr& entry, HubSettings& settings) const noexcept {
	// apply group settings first.
	if (const auto& name = entry->getGroup(); !name.empty()) {
		auto group = favHubGroups.find(name);
		if(group != favHubGroups.end())
			settings.merge(group->second);
	}

	// apply fav entry settings next.
	settings.merge(*entry);
}

FavoriteHubEntryList::const_iterator FavoriteManager::getFavoriteHubUnsafe(const string& aServer) const noexcept {
	//find by the primary address
	return ranges::find_if(favoriteHubs, [&aServer](const FavoriteHubEntryPtr& f) { return Util::stricmp(f->getServer(), aServer) == 0; });
}

FavoriteHubEntryList::const_iterator FavoriteManager::getFavoriteHubUnsafe(FavoriteHubToken aToken) const noexcept {
	return ranges::find_if(favoriteHubs, [aToken](const FavoriteHubEntryPtr& f) { return f->getToken() == aToken; });
}

void FavoriteManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	if (xmlDirty && aTick > (lastXmlSave + 15 * 1000)) {
		save();
	}
}

void FavoriteManager::on(ClientManagerListener::ClientCreated, const ClientPtr& aClient) noexcept {
	onConnectStateChanged(aClient, FavoriteHubEntry::STATE_CONNECTING);
}

void FavoriteManager::on(ClientManagerListener::ClientConnected, const ClientPtr& aClient) noexcept {
	onConnectStateChanged(aClient, FavoriteHubEntry::STATE_CONNECTED);
}

void FavoriteManager::on(ClientManagerListener::ClientRemoved, const ClientPtr& aClient) noexcept {
	onConnectStateChanged(aClient, FavoriteHubEntry::STATE_DISCONNECTED);
}

void FavoriteManager::on(ClientManagerListener::ClientRedirected, const ClientPtr& aOldClient, const ClientPtr& aNewClient) noexcept {
	onConnectStateChanged(aOldClient, FavoriteHubEntry::STATE_DISCONNECTED);
	onConnectStateChanged(aNewClient, FavoriteHubEntry::STATE_CONNECTING);
}

void FavoriteManager::setConnectState(const FavoriteHubEntryPtr& aEntry) noexcept {
	auto client = ClientManager::getInstance()->findClient(aEntry->getServer());
	if (client) {
		aEntry->setConnectState(client->isConnected() ? FavoriteHubEntry::STATE_CONNECTED : FavoriteHubEntry::STATE_CONNECTING);
		aEntry->setCurrentHubToken(client->getToken());
	} else {
		aEntry->setCurrentHubToken(0);
		aEntry->setConnectState(FavoriteHubEntry::STATE_DISCONNECTED);
	}
}

void FavoriteManager::onConnectStateChanged(const ClientPtr& aClient, FavoriteHubEntry::ConnectState aState) noexcept {
	auto hub = getFavoriteHubEntry(aClient->getHubUrl());
	if (hub) {
		hub->setConnectState(aState);
		if (aState == FavoriteHubEntry::STATE_DISCONNECTED) {
			hub->setCurrentHubToken(0);
		} else {
			hub->setCurrentHubToken(aClient->getToken());
		}

		fire(FavoriteManagerListener::FavoriteHubUpdated(), hub);
	}
}

} // namespace dcpp
