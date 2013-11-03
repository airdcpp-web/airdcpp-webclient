/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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
#include "FavHubGroup.h"
#include "FavoriteManagerListener.h"
#include "FavoriteUser.h"
#include "HttpConnection.h"
#include "HubEntry.h"
#include "SettingsManager.h"
#include "Singleton.h"
#include "User.h"
#include "UserCommand.h"
#include "ShareProfile.h"

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

class SimpleXML;

/**
 * Public hub list, favorites (hub&user). Assumed to be called only by UI thread.
 */
class FavoriteManager : public Speaker<FavoriteManagerListener>, private HttpConnectionListener, public Singleton<FavoriteManager>,
	private SettingsManagerListener, private ClientManagerListener
{
public:
// Public Hubs
	enum HubTypes {
		TYPE_NORMAL,
		TYPE_BZIP2
	};
	StringList getHubLists();
	void setHubList(int aHubList);
	int getSelectedHubList() { return lastServer; }
	void refresh(bool forceDownload = false);
	HubTypes getHubListType() { return listType; }
	HubEntryList getPublicHubs();
	bool isDownloading() { return (useHttp && running); }

// Favorite Users
	typedef unordered_map<CID, FavoriteUser> FavoriteMap;

	//remember to lock this
	const FavoriteMap& getFavoriteUsers() { return users; }
	PreviewApplication::List& getPreviewApps() { return previewApplications; }

	void addFavoriteUser(const HintedUser& aUser);
	void removeFavoriteUser(const UserPtr& aUser);
	optional<FavoriteUser> getFavoriteUser(const UserPtr& aUser) const;

	bool hasSlot(const UserPtr& aUser) const;
	void setUserDescription(const UserPtr& aUser, const string& description);
	void setAutoGrant(const UserPtr& aUser, bool grant);
	time_t getLastSeen(const UserPtr& aUser) const;
	void changeLimiterOverride(const UserPtr& aUser) noexcept;
// Favorite Hubs
	void autoConnect();
	FavoriteHubEntryList& getFavoriteHubs() { return favoriteHubs; }

	void addFavorite(const FavoriteHubEntryPtr& aEntry);
	void removeFavorite(const FavoriteHubEntryPtr& entry);
	bool isUnique(const string& aUrl, ProfileToken aToken);
	FavoriteHubEntryPtr getFavoriteHubEntry(const string& aServer) const;

	void mergeHubSettings(const FavoriteHubEntryPtr& entry, HubSettings& settings) const;
	void setHubSetting(const string& aUrl, HubSettings::HubBoolSetting aSetting, bool newValue);
// Favorite hub groups
	const FavHubGroups& getFavHubGroups() const { return favHubGroups; }
	void setFavHubGroups(const FavHubGroups& favHubGroups_) { favHubGroups = favHubGroups_; }

	FavoriteHubEntryList getFavoriteHubs(const string& group) const;

// Favorite Directories
	typedef pair<string, StringList> FavDirPair;
	typedef vector<FavDirPair> FavDirList;

	bool addFavoriteDir(const string& aName, StringList& aTargets);
	void saveFavoriteDirs(FavDirList& dirs);
	FavDirList getFavoriteDirs() { return favoriteDirs; }

// Recent Hubs
	RecentHubEntryList& getRecentHubs() { return recentHubs; };

	void addRecent(const RecentHubEntryPtr& aEntry);
	void removeRecent(const RecentHubEntryPtr& entry);
	void updateRecent(const RecentHubEntryPtr& entry);

	// remove user commands and possibly change the address for the next attempt
	optional<string> getFailOverUrl(ProfileToken aToken, const string& curHubUrl);
	void setFailOvers(const string& hubUrl, ProfileToken aToken, StringList&& fo_);
	bool blockFailOverUrl(ProfileToken aToken, string& hubAddress_);
	bool isFailOverUrl(ProfileToken aToken, const string& hubAddress_);

	RecentHubEntryPtr getRecentHubEntry(const string& aServer);

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

	void removeallRecent();

// User Commands
	UserCommand addUserCommand(int type, int ctx, Flags::MaskType flags, const string& name, const string& command, const string& to, const string& hub);
	bool getUserCommand(int cid, UserCommand& uc);
	int findUserCommand(const string& aName, const string& aUrl);
	bool moveUserCommand(int cid, int pos);
	void updateUserCommand(const UserCommand& uc);
	void removeUserCommand(int cid);
	void removeUserCommand(const string& srv);
	void removeHubUserCommands(int ctx, const string& hub);

	UserCommand::List getUserCommands() { RLock l(cs); return userCommands; }
	UserCommand::List getUserCommands(int ctx, const StringList& hub, bool& op);

	void load();
	void save();
	void recentsave();

	int resetProfile(ProfileToken oldProfile, ProfileToken newProfile, bool nmdcOnly);
	int resetProfiles(const ShareProfileInfo::List& aProfiles, ProfileToken defaultProfile);
	void onProfilesRenamed();

	bool hasActiveHubs() const;
	bool hasAdcHubs() const;

	mutable SharedMutex cs;
private:
	FavoriteHubEntryList favoriteHubs;
	FavHubGroups favHubGroups;
	FavDirList favoriteDirs;
	RecentHubEntryList recentHubs;
	PreviewApplication::List previewApplications;
	UserCommand::List userCommands;
	int lastId;

	FavoriteMap users;

	// Public Hubs
	typedef unordered_map<string, HubEntryList> PubListMap;
	PubListMap publicListMatrix;
	string publicListServer;
	bool useHttp, running;
	HttpConnection* c;
	int lastServer;
	HubTypes listType;
	string downloadBuf;
	
	/** Used during loading to prevent saving. */
	bool dontSave;

	friend class Singleton<FavoriteManager>;
	
	FavoriteManager();
	~FavoriteManager();
	
	FavoriteHubEntryList::const_iterator getFavoriteHub(const string& aServer) const;
	FavoriteHubEntryList::const_iterator getFavoriteHub(ProfileToken aToken) const;
	RecentHubEntryList::const_iterator getRecentHub(const string& aServer) const;

	// ClientManagerListener
	void on(UserConnected, const OnlineUser& user, bool wasOffline) noexcept;
	void on(UserDisconnected, const UserPtr& user, bool wentOffline) noexcept;

	// HttpConnectionListener
	void on(Data, HttpConnection*, const uint8_t*, size_t) noexcept;
	void on(Failed, HttpConnection*, const string&) noexcept;
	void on(Complete, HttpConnection*, const string&, bool) noexcept;
	void on(Redirected, HttpConnection*, const string&) noexcept;
	void on(Retried, HttpConnection*, bool) noexcept; 

	bool onHttpFinished(bool fromHttp) noexcept;

	// SettingsManagerListener
	void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept {
		loadCID();
		previewload(xml);
	}

	void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept {
		previewsave(xml);
	}

	void load(SimpleXML& aXml);
	void recentload(SimpleXML& aXml);
	void previewload(SimpleXML& aXml);
	void previewsave(SimpleXML& aXml);
	void loadCID();
};

} // namespace dcpp

#endif // !defined(FAVORITE_MANAGER_H)