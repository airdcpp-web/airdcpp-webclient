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

#ifndef DCPLUSPLUS_DCPP_FAVORITE_MANAGER_H
#define DCPLUSPLUS_DCPP_FAVORITE_MANAGER_H

#include "ClientManagerListener.h"
#include "FavoriteManagerListener.h"
#include "SettingsManagerListener.h"
#include "ShareProfileManagerListener.h"
#include "TimerManagerListener.h"

#include "FavHubGroup.h"
#include "HubEntry.h"
#include "Singleton.h"
#include "Speaker.h"

namespace dcpp {

class FavoriteManager : public Speaker<FavoriteManagerListener>, public Singleton<FavoriteManager>,
	private SettingsManagerListener, private ClientManagerListener, private ShareProfileManagerListener, private TimerManagerListener
{
public:
// Favorite Hubs
	void autoConnect() noexcept;
	FavoriteHubEntryList& getFavoriteHubsUnsafe() noexcept { return favoriteHubs; }

	bool addFavoriteHub(const FavoriteHubEntryPtr& aEntry) noexcept;
	void onFavoriteHubUpdated(const FavoriteHubEntryPtr& aEntry) noexcept;
	bool removeFavoriteHub(FavoriteHubToken aToken) noexcept;
	bool isUnique(const string& aUrl, FavoriteHubToken aExcludedEntryToken) const noexcept;
	FavoriteHubEntryPtr getFavoriteHubEntry(const string& aServer) const noexcept;
	FavoriteHubEntryPtr getFavoriteHubEntry(FavoriteHubToken aToken) const noexcept;

	void mergeHubSettings(const FavoriteHubEntryPtr& entry, HubSettings& settings) const noexcept;
	void setHubSetting(const string& aUrl, HubSettings::HubBoolSetting aSetting, bool newValue) noexcept;
// Favorite hub groups
	const FavHubGroups& getFavHubGroupsUnsafe() const noexcept { return favHubGroups; }
	void setFavHubGroups(const FavHubGroups& favHubGroups_) noexcept;

	FavoriteHubEntryList getFavoriteHubs(const string& group) const noexcept;
	FavoriteHubEntryList getFavoriteHubs() const noexcept;

	// Favorite Directories (path -> grouped name)
	using FavoriteDirectoryMap = map<string, string>;

	// For adding or renaming of favorite directories
	bool setFavoriteDir(const string& aPath, const string& aGroupName) noexcept;
	bool removeFavoriteDir(const string& aPath) noexcept;
	bool hasFavoriteDir(const string& aPath) const noexcept;
	void setFavoriteDirs(const FavoriteDirectoryMap& dirs) noexcept;
	StringPair getFavoriteDirectory(const string& aPath) const noexcept;

	GroupedDirectoryMap getGroupedFavoriteDirs() const noexcept;
	FavoriteDirectoryMap getFavoriteDirs() const noexcept;

	void load() noexcept;
	void setDirty() { xmlDirty = true; }
	void shutdown() noexcept;

	bool hasActiveHubs() const noexcept;

	SharedMutex& getCS() noexcept {
		return cs;
	}
private:
	mutable SharedMutex cs;

	FavoriteHubEntryList favoriteHubs;
	FavHubGroups favHubGroups;
	FavoriteDirectoryMap favoriteDirectories;

	uint64_t lastXmlSave = 0;
	atomic<bool> xmlDirty{ false };
	void save() noexcept;
	
	friend class Singleton<FavoriteManager>;
	
	FavoriteManager();
	~FavoriteManager() override;
	
	FavoriteHubEntryList::const_iterator getFavoriteHubUnsafe(const string& aServer) const noexcept;
	FavoriteHubEntryList::const_iterator getFavoriteHubUnsafe(FavoriteHubToken aToken) const noexcept;

	int resetProfile(ProfileToken oldProfile, ProfileToken newProfile, bool nmdcOnly) noexcept;

	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t tick) noexcept override;

	// ShareManagerListener
	void on(ShareProfileManagerListener::DefaultProfileChanged, ProfileToken aOldDefault, ProfileToken aNewDefault) noexcept override;
	void on(ShareProfileManagerListener::ProfileRemoved, ProfileToken aProfile) noexcept override;

	// ClientManagerListener
	void on(ClientManagerListener::ClientCreated, const ClientPtr& c) noexcept override;
	void on(ClientManagerListener::ClientConnected, const ClientPtr& c) noexcept override;
	void on(ClientManagerListener::ClientRemoved, const ClientPtr& c) noexcept override;
	void on(ClientManagerListener::ClientRedirected, const ClientPtr& aOldClient, const ClientPtr& aNewClient) noexcept override;

	void onConnectStateChanged(const ClientPtr& aClient, FavoriteHubEntry::ConnectState aState) noexcept;
	void setConnectState(const FavoriteHubEntryPtr& aEntry) noexcept;

	// SettingsManagerListener
	void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept override;

	void loadFavoriteHubs(SimpleXML& aXml);
	void loadFavoriteDirectories(SimpleXML& aXml);

	void saveFavoriteHubs(SimpleXML& aXml) const noexcept;
	void saveFavoriteDirectories(SimpleXML& aXml) const noexcept;

	static void loadCID() noexcept;
};

} // namespace dcpp

#endif // !defined(FAVORITE_MANAGER_H)