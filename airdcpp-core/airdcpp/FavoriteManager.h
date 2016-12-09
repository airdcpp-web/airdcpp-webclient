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

#include "FavHubGroup.h"
#include "FavoriteUser.h"
#include "HttpConnection.h"
#include "HubEntry.h"
#include "Singleton.h"
#include "UserCommand.h"

namespace dcpp {
	
class PreviewApplication {
public:
	typedef PreviewApplication* Ptr;
	typedef vector<Ptr> List;
	typedef List::const_iterator Iter;

	PreviewApplication() noexcept {}
	PreviewApplication(string n, string a, string r, string e) : name(n), application(a), arguments(r), extension(e) {};
	~PreviewApplication() noexcept { }	

	GETSET(string, name, Name);
	GETSET(string, application, Application);
	GETSET(string, arguments, Arguments);
	GETSET(string, extension, Extension);
};

/**
 * Public hub list, favorites (hub&user). Assumed to be called only by UI thread.
 */
class FavoriteManager : public Speaker<FavoriteManagerListener>, private HttpConnectionListener, public Singleton<FavoriteManager>,
	private SettingsManagerListener, private ClientManagerListener, private ShareManagerListener
{
public:
// Public Hubs
	enum HubTypes {
		TYPE_NORMAL,
		TYPE_BZIP2
	};
	StringList getHubLists() noexcept;
	void setHubList(int aHubList) noexcept;
	int getSelectedHubList() const noexcept { return lastServer; }
	void refresh(bool forceDownload = false) noexcept;
	HubTypes getHubListType() const noexcept { return listType; }
	HubEntryList getPublicHubs() noexcept;
	bool isDownloading() const noexcept { return (useHttp && running); }

// Favorite Users
	typedef unordered_map<CID, FavoriteUser> FavoriteMap;

	//remember to lock this
	const FavoriteMap& getFavoriteUsers() const noexcept { return users; }
	PreviewApplication::List& getPreviewApps() noexcept { return previewApplications; }

	void addFavoriteUser(const HintedUser& aUser) noexcept;
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

	GroupedDirectoryMap getGroupedFavoriteDirs() const noexcept;
	FavoriteDirectoryMap getFavoriteDirs() const noexcept;
// Recent Hubs
	RecentHubEntryList& getRecentHubs() noexcept { return recentHubs; };

	void addRecent(const RecentHubEntryPtr& aEntry) noexcept;
	void removeRecent(const RecentHubEntryPtr& aEntry) noexcept;
	void updateRecent(const RecentHubEntryPtr& aEntry) noexcept;

	RecentHubEntryPtr getRecentHubEntry(const string& aServer) const noexcept;
	RecentHubEntryList searchRecentHubs(const string& aPattern, size_t aMaxResults) const noexcept;

	PreviewApplication* addPreviewApp(string name, string application, string arguments, string extension){
		PreviewApplication* pa = new PreviewApplication(name, application, arguments, extension);
		previewApplications.push_back(pa);
		return pa;
	}

	PreviewApplication* removePreviewApp(unsigned int index){
		if(previewApplications.size() > index)
			previewApplications.erase(previewApplications.begin() + index);	
		return NULL;
	}

	PreviewApplication* getPreviewApp(unsigned int index, PreviewApplication &pa){
		if(previewApplications.size() > index)
			pa = *previewApplications[index];	
		return NULL;
	}
	
	PreviewApplication* updatePreviewApp(int index, PreviewApplication &pa){
		*previewApplications[index] = pa;
		return NULL;
	}

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
	void save() noexcept;

	void clearRecent() noexcept;
	void saveRecent() const noexcept;

	bool hasActiveHubs() const noexcept;

	mutable SharedMutex cs;
private:
	FavoriteHubEntryList favoriteHubs;
	FavHubGroups favHubGroups;
	FavoriteDirectoryMap favoriteDirectories;
	RecentHubEntryList recentHubs;
	PreviewApplication::List previewApplications;
	UserCommand::List userCommands;
	int lastId = 0;

	FavoriteMap users;

	// Public Hubs
	typedef unordered_map<string, HubEntryList> PubListMap;
	PubListMap publicListMatrix;
	string publicListServer;
	bool useHttp = false, running = false;
	HttpConnection* c = nullptr;
	int lastServer = 0;
	HubTypes listType = TYPE_NORMAL;
	string downloadBuf;
	
	/** Used during loading to prevent saving. */
	bool loaded = false;

	friend class Singleton<FavoriteManager>;
	
	FavoriteManager();
	~FavoriteManager();
	
	FavoriteHubEntryList::const_iterator getFavoriteHub(const string& aServer) const noexcept;
	FavoriteHubEntryList::const_iterator getFavoriteHub(ProfileToken aToken) const noexcept;
	RecentHubEntryList::const_iterator getRecentHub(const string& aServer) const noexcept;

	int resetProfile(ProfileToken oldProfile, ProfileToken newProfile, bool nmdcOnly) noexcept;

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

	// HttpConnectionListener
	void on(Data, HttpConnection*, const uint8_t*, size_t) noexcept;
	void on(Failed, HttpConnection*, const string&) noexcept;
	void on(Complete, HttpConnection*, const string&, bool) noexcept;
	void on(Redirected, HttpConnection*, const string&) noexcept;
	void on(Retried, HttpConnection*, bool) noexcept; 

	bool onHttpFinished(bool fromHttp) noexcept;

	void onConnectStateChanged(const ClientPtr& aClient, FavoriteHubEntry::ConnectState aState) noexcept;
	void setConnectState(const FavoriteHubEntryPtr& aEntry) noexcept;

	// SettingsManagerListener
	void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept;
	void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept;

	void loadFavoriteHubs(SimpleXML& aXml);
	void loadFavoriteDirectories(SimpleXML& aXml);
	void loadFavoriteUsers(SimpleXML& aXml);
	void loadUserCommands(SimpleXML& aXml);
	void loadRecent(SimpleXML& aXml);
	void loadPreview(SimpleXML& aXml);

	void saveFavoriteHubs(SimpleXML& aXml) const noexcept;
	void saveFavoriteUsers(SimpleXML& aXml) const noexcept;
	void saveFavoriteDirectories(SimpleXML& aXml) const noexcept;
	void saveUserCommands(SimpleXML& aXml) const noexcept;
	void savePreview(SimpleXML& aXml) const noexcept;

	void loadCID() noexcept;
};

} // namespace dcpp

#endif // !defined(FAVORITE_MANAGER_H)