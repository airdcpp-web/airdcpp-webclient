/*
 * Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_FAVORITE_MANAGER_H
#define DCPLUSPLUS_DCPP_FAVORITE_MANAGER_H

#include "ClientManagerListener.h"
#include "FavoriteManagerListener.h"
#include "SettingsManagerListener.h"
#include "ShareManagerListener.h"
#include "TimerManagerListener.h"

#include "FavHubGroup.h"
#include "FavoriteUser.h"
#include "HubEntry.h"
#include "Singleton.h"
#include "Speaker.h"
#include "UserCommand.h"

namespace dcpp {

class FavoriteManager : public Speaker<FavoriteManagerListener>, public Singleton<FavoriteManager>,
	private SettingsManagerListener, private ClientManagerListener, private ShareManagerListener, private TimerManagerListener
{
public:
// Favorite Users
	typedef unordered_map<CID, FavoriteUser> FavoriteMap;

	//remember to lock this
	const FavoriteMap& getFavoriteUsers() const noexcept { return users; }

	void addFavoriteUser(const HintedUser& aUser) noexcept;
	void addSavedUser(const UserPtr& aUser) noexcept;
	void removeFavoriteUser(const UserPtr& aUser) noexcept;
	optional<FavoriteUser> getFavoriteUser(const UserPtr& aUser) const noexcept;

	bool hasSlot(const UserPtr& aUser) const noexcept;
	void setUserDescription(const UserPtr& aUser, const string& description) noexcept;
	void setAutoGrant(const UserPtr& aUser, bool grant) noexcept;
	time_t getLastSeen(const UserPtr& aUser) const noexcept;
	void changeLimiterOverride(const UserPtr& aUser) noexcept;
// Favorite Hubs
	void autoConnect() noexcept;
	FavoriteHubEntryList& getFavoriteHubs() noexcept { return favoriteHubs; }

	bool addFavoriteHub(const FavoriteHubEntryPtr& aEntry) noexcept;
	void onFavoriteHubUpdated(const FavoriteHubEntryPtr& aEntry) noexcept;
	bool removeFavoriteHub(ProfileToken aToken) noexcept;
	bool isUnique(const string& aUrl, ProfileToken aToken) const noexcept;
	FavoriteHubEntryPtr getFavoriteHubEntry(const string& aServer) const noexcept;
	FavoriteHubEntryPtr getFavoriteHubEntry(const ProfileToken& aToken) const noexcept;

	void mergeHubSettings(const FavoriteHubEntryPtr& entry, HubSettings& settings) const noexcept;
	void setHubSetting(const string& aUrl, HubSettings::HubBoolSetting aSetting, bool newValue) noexcept;
// Favorite hub groups
	const FavHubGroups& getFavHubGroups() const noexcept { return favHubGroups; }
	void setFavHubGroups(const FavHubGroups& favHubGroups_) noexcept { favHubGroups = favHubGroups_; }

	FavoriteHubEntryList getFavoriteHubs(const string& group) const noexcept;

	// Favorite Directories (path -> grouped name)
	typedef map<string, string> FavoriteDirectoryMap;

	// For adding or renaming of favorite directories
	bool setFavoriteDir(const string& aPath, const string& aGroupName) noexcept;
	bool removeFavoriteDir(const string& aPath) noexcept;
	bool hasFavoriteDir(const string& aPath) const noexcept;
	void setFavoriteDirs(const FavoriteDirectoryMap& dirs) noexcept;
	StringPair getFavoriteDirectory(const string& aPath) const noexcept;

	GroupedDirectoryMap getGroupedFavoriteDirs() const noexcept;
	FavoriteDirectoryMap getFavoriteDirs() const noexcept;

// User Commands
	UserCommand addUserCommand(int type, int ctx, Flags::MaskType flags, const string& name, const string& command, const string& to, const string& hub) noexcept;
	bool getUserCommand(int cid, UserCommand& uc) noexcept;
	int findUserCommand(const string& aName, const string& aUrl) noexcept;
	bool moveUserCommand(int cid, int pos) noexcept;
	void updateUserCommand(const UserCommand& uc) noexcept;
	void removeUserCommand(int cid) noexcept;
	void removeUserCommand(const string& srv) noexcept;
	void removeHubUserCommands(int ctx, const string& hub) noexcept;

	UserCommand::List getUserCommands() noexcept { RLock l(cs); return userCommands; }
	UserCommand::List getUserCommands(int ctx, const StringList& hub, bool& op) noexcept;

	void load() noexcept;
	void setDirty() { xmlDirty = true; }
	void shutdown() noexcept;

	bool hasActiveHubs() const noexcept;

	mutable SharedMutex cs;
private:
	FavoriteHubEntryList favoriteHubs;
	FavHubGroups favHubGroups;
	FavoriteDirectoryMap favoriteDirectories;
	UserCommand::List userCommands;
	int lastId = 0;

	uint64_t lastXmlSave = 0;
	atomic<bool> xmlDirty{ false };
	void save() noexcept;

	//Favorite users
	FavoriteMap users;
	//Saved users
	unordered_set<UserPtr, User::Hash> savedUsers;

	FavoriteUser createUser(const UserPtr& aUser, const string& aUrl);
	
	friend class Singleton<FavoriteManager>;
	
	FavoriteManager();
	~FavoriteManager();
	
	FavoriteHubEntryList::const_iterator getFavoriteHub(const string& aServer) const noexcept;
	FavoriteHubEntryList::const_iterator getFavoriteHub(ProfileToken aToken) const noexcept;

	int resetProfile(ProfileToken oldProfile, ProfileToken newProfile, bool nmdcOnly) noexcept;

	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t tick) noexcept;

	// ShareManagerListener
	void on(ShareManagerListener::DefaultProfileChanged, ProfileToken aOldDefault, ProfileToken aNewDefault) noexcept;
	void on(ShareManagerListener::ProfileRemoved, ProfileToken aProfile) noexcept;

	// ClientManagerListener
	void on(ClientManagerListener::UserConnected, const OnlineUser& user, bool wasOffline) noexcept;
	void on(ClientManagerListener::UserDisconnected, const UserPtr& user, bool wentOffline) noexcept;

	void on(ClientManagerListener::ClientCreated, const ClientPtr& c) noexcept;
	void on(ClientManagerListener::ClientConnected, const ClientPtr& c) noexcept;
	void on(ClientManagerListener::ClientRemoved, const ClientPtr& c) noexcept;
	void on(ClientManagerListener::ClientRedirected, const ClientPtr& aOldClient, const ClientPtr& aNewClient) noexcept;

	void onConnectStateChanged(const ClientPtr& aClient, FavoriteHubEntry::ConnectState aState) noexcept;
	void setConnectState(const FavoriteHubEntryPtr& aEntry) noexcept;

	// SettingsManagerListener
	void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept;

	void loadFavoriteHubs(SimpleXML& aXml);
	void loadFavoriteDirectories(SimpleXML& aXml);
	void loadFavoriteUsers(SimpleXML& aXml);
	void loadUserCommands(SimpleXML& aXml);

	void saveFavoriteHubs(SimpleXML& aXml) const noexcept;
	void saveFavoriteUsers(SimpleXML& aXml) noexcept;
	void saveFavoriteDirectories(SimpleXML& aXml) const noexcept;
	void saveUserCommands(SimpleXML& aXml) const noexcept;

	void loadCID() noexcept;
};

} // namespace dcpp

#endif // !defined(FAVORITE_MANAGER_H)