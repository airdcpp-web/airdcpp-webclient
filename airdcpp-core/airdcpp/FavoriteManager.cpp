/* 
 * Copyright (C) 2001-2023 Jacek Sieka, arnetheduck on gmail point com
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
#include "FavoriteManager.h"

#include "ClientManager.h"
#include "RelevanceSearch.h"
#include "ResourceManager.h"
#include "ShareManager.h"
#include "SimpleXML.h"
#include "UserCommand.h"

namespace dcpp {

#define CONFIG_FAV_NAME "Favorites.xml"
#define CONFIG_DIR Util::PATH_USER_CONFIG

using boost::range::find_if;

FavoriteManager::FavoriteManager() {
	SettingsManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);
	ShareManager::getInstance()->addListener(this);
}

FavoriteManager::~FavoriteManager() {
	ClientManager::getInstance()->removeListener(this);
	SettingsManager::getInstance()->removeListener(this);
	ShareManager::getInstance()->removeListener(this);
}

void FavoriteManager::shutdown() noexcept {
	TimerManager::getInstance()->removeListener(this);
	save();
}

UserCommand FavoriteManager::addUserCommand(int type, int ctx, Flags::MaskType flags, const string& name, const string& command, const string& to, const string& hub) noexcept {
	
	// The following management is to protect users against malicious hubs or clients.
	// Hubs (or clients) can send an arbitrary amount of user commands, which means that there is a possibility that
	// the client will need to manage thousands and thousands of user commands.
	// This can naturally cause problems with memory etc, so the client may even crash at some point.
	// The following management tries to remedy this problem by doing two things;
	// a) Replaces previous user commands (if they have the same name etc)
	// b) Restricts the amount of user commands that pertain to a particlar hub
	// Note that this management only cares about externally created user commands, 
	// which means that the user themselves can create however large user commands.
	if (flags == UserCommand::FLAG_NOSAVE)
	{
		const int maximumUCs = 2000; // Completely arbitrary
		int externalCommands = 0; // Used to count the number of external commands
		RLock l(cs);
		for (auto& uc : userCommands) {
			if ((uc.isSet(UserCommand::FLAG_NOSAVE)) &&	// Only care about external commands...
				(uc.getHub() == hub))	// ... and those pertaining to this particular hub.
			{
				++externalCommands;

				// If the UC is generally identical otherwise, change the command
				if ((uc.getName() == name) &&
					(uc.getCtx() == ctx) &&
					(uc.getType() == type) &&
					(uc.isSet(flags)) &&
					(uc.getTo() == to))
				{
					uc.setCommand(command);
					return uc;
				}
			}

		}

		// Validate if there's too many user commands
		if (maximumUCs <= externalCommands)
		{
			return userCommands.back();
		}
	}
	
	// No dupes, add it...
	auto cmd = UserCommand(lastId++, type, ctx, flags, name, command, to, hub);

	{
		WLock l(cs);
		userCommands.emplace_back(cmd);
	}

	if(!cmd.isSet(UserCommand::FLAG_NOSAVE)) 
		setDirty();

	return cmd;
}

bool FavoriteManager::getUserCommand(int cid, UserCommand& uc) noexcept {
	WLock l(cs);
	for(auto& u: userCommands) {
		if(u.getId() == cid) {
			uc = u;
			return true;
		}
	}
	return false;
}

bool FavoriteManager::moveUserCommand(int cid, int pos) noexcept {
	dcassert(pos == -1 || pos == 1);
	WLock l(cs);
	for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
		if(i->getId() == cid) {
			swap(*i, *(i + pos));
			return true;
		}
	}
	return false;
}

void FavoriteManager::updateUserCommand(const UserCommand& uc) noexcept {
	bool nosave = true;
	{
		WLock l(cs);
		for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
			if(i->getId() == uc.getId()) {
				*i = uc;
				nosave = uc.isSet(UserCommand::FLAG_NOSAVE);
				break;
			}
		}
	}

	if(!nosave)
		setDirty();
}

int FavoriteManager::findUserCommand(const string& aName, const string& aUrl) noexcept {
	RLock l(cs);
	for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
		if(i->getName() == aName && i->getHub() == aUrl) {
			return i->getId();
		}
	}
	return -1;
}

void FavoriteManager::removeUserCommand(int cid) noexcept {
	bool nosave = true;
	{
		WLock l(cs);
		for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
			if(i->getId() == cid) {
				nosave = i->isSet(UserCommand::FLAG_NOSAVE);
				userCommands.erase(i);
				break;
			}
		}
	}

	if(!nosave)
		setDirty();
}
void FavoriteManager::removeUserCommand(const string& srv) noexcept {
	WLock l(cs);
	userCommands.erase(std::remove_if(userCommands.begin(), userCommands.end(), [&](const UserCommand& uc) {
		return uc.getHub() == srv && uc.isSet(UserCommand::FLAG_NOSAVE);
	}), userCommands.end());

}

void FavoriteManager::removeHubUserCommands(int ctx, const string& hub) noexcept {
	WLock l(cs);
	for(auto i = userCommands.begin(); i != userCommands.end(); ) {
		if(i->getHub() == hub && i->isSet(UserCommand::FLAG_NOSAVE) && i->getCtx() & ctx) {
			i = userCommands.erase(i);
		} else {
			++i;
		}
	}
}

FavoriteUser FavoriteManager::createUser(const UserPtr& aUser, const string& aUrl) {
	string nick;
	int64_t seen = 0;
	string hubUrl = aUrl;

	//prefer to use the add nick
	auto ou = ClientManager::getInstance()->findOnlineUser(aUser->getCID(), aUrl);
	if (!ou) {
		//offline
		auto ofu = ClientManager::getInstance()->getOfflineUser(aUser->getCID());
		if (ofu) {
			nick = ofu->getNick();
			seen = ofu->getLastSeen();
			hubUrl = ofu->getUrl();
		}
	}
	else {
		nick = ou->getIdentity().getNick();
	}

	auto fu = FavoriteUser(aUser, nick, hubUrl, aUser->getCID().toBase32());
	fu.setLastSeen(seen);
	return fu;
}

void FavoriteManager::addFavoriteUser(const HintedUser& aUser) noexcept {
	if(aUser.user == ClientManager::getInstance()->getMe()) // we cant allow adding ourself as a favorite user :P
		return;

	{
		RLock l(cs);
		if(users.find(aUser.user->getCID()) != users.end()) {
			return;
		}
	}

	auto fu = createUser(aUser.user, aUser.hint);
	{
		WLock l (cs);
		users.emplace(aUser.user->getCID(), fu);
	}

	aUser.user->setFlag(User::FAVORITE);
	fire(FavoriteManagerListener::FavoriteUserAdded(), fu);
}

void FavoriteManager::addSavedUser(const UserPtr& aUser) noexcept {
	if (aUser == ClientManager::getInstance()->getMe()) // no reason saving ourself
		return;

	{
		RLock l(cs);
		if (savedUsers.find(aUser) != savedUsers.end()) {
			return;
		}
	}

	{
		WLock l(cs);
		savedUsers.emplace(aUser);
	}
	setDirty();
}

void FavoriteManager::removeFavoriteUser(const UserPtr& aUser) noexcept {
	{
		WLock l(cs);
		auto i = users.find(aUser->getCID());
		if(i != users.end()) {
			aUser->unsetFlag(User::FAVORITE);
			fire(FavoriteManagerListener::FavoriteUserRemoved(), i->second);
			users.erase(i);
		}
	}

	setDirty();
}

optional<FavoriteUser> FavoriteManager::getFavoriteUser(const UserPtr &aUser) const noexcept {
	RLock l(cs);
	auto i = users.find(aUser->getCID());
	return i == users.end() ? optional<FavoriteUser>() : i->second;
}


void FavoriteManager::changeLimiterOverride(const UserPtr& aUser) noexcept {
	RLock l(cs);
	auto i = users.find(aUser->getCID());
	if (i != users.end()) {
		if (!i->second.isSet(FavoriteUser::FLAG_SUPERUSER))
			i->second.setFlag(FavoriteUser::FLAG_SUPERUSER);
		else
			i->second.unsetFlag(FavoriteUser::FLAG_SUPERUSER);
	}
}

bool FavoriteManager::hasFavoriteDir(const string& aPath) const noexcept {
	RLock l(cs);
	return favoriteDirectories.find(aPath) != favoriteDirectories.end();
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
		for (const auto& fd : favoriteDirectories) {
			ret[fd.second].insert(fd.first);
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
		auto i = getFavoriteHub(aEntry->getServer());
		if (i != favoriteHubs.end()) {
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
		if (!ClientManager::getInstance()->hasClient(h))
			ClientManager::getInstance()->createClient(h);
	}
}

bool FavoriteManager::removeFavoriteHub(ProfileToken aToken) noexcept {
	FavoriteHubEntryPtr entry = nullptr;

	{
		WLock l(cs);
		auto i = find_if(favoriteHubs, [aToken](const FavoriteHubEntryPtr& f) { return f->getToken() == aToken; });
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

bool FavoriteManager::isUnique(const string& url, ProfileToken aToken) const noexcept {
	auto i = getFavoriteHub(url);
	if (i == favoriteHubs.end())
		return true;

	return aToken == (*i)->getToken();
}

void FavoriteManager::on(ShareManagerListener::DefaultProfileChanged, ProfileToken aOldDefault, ProfileToken aNewDefault) noexcept {
	resetProfile(aOldDefault, aNewDefault, true);
}

void FavoriteManager::on(ShareManagerListener::ProfileRemoved, ProfileToken aProfile) noexcept {
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
	return any_of(favoriteHubs.begin(), favoriteHubs.end(), [](const FavoriteHubEntryPtr& f) { return f->get(HubSettings::Connection) == SettingsManager::INCOMING_ACTIVE || f->get(HubSettings::Connection6) == SettingsManager::INCOMING_ACTIVE; });
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
			saveFavoriteUsers(xml);
			saveUserCommands(xml);
			saveFavoriteDirectories(xml);
		}

		xml.stepOut();


		SettingsManager::saveSettingFile(xml, CONFIG_DIR, CONFIG_FAV_NAME);

	} catch(const Exception& e) {
		xmlDirty = true; //Oops, something went wrong, xml was not saved.
		dcdebug("FavoriteManager::save: %s\n", e.getError().c_str());
	}

}

void FavoriteManager::saveUserCommands(SimpleXML& aXml) const noexcept {
	aXml.addTag("UserCommands");
	aXml.stepIn();

	{
		RLock l(cs);
		for (const auto& i : userCommands) {
			if (!i.isSet(UserCommand::FLAG_NOSAVE)) {
				aXml.addTag("UserCommand");
				aXml.addChildAttrib("Type", i.getType());
				aXml.addChildAttrib("Context", i.getCtx());
				aXml.addChildAttrib("Name", i.getName());
				aXml.addChildAttrib("Command", i.getCommand());
				aXml.addChildAttrib("To", i.getTo());
				aXml.addChildAttrib("Hub", i.getHub());
			}
		}
	}

	aXml.stepOut();
}

void FavoriteManager::saveFavoriteUsers(SimpleXML& aXml) noexcept {
	aXml.addTag("Users");
	aXml.stepIn();

	{
		RLock l(cs);
		for (const auto& i : users) {
			aXml.addTag("User");
			aXml.addChildAttrib("LastSeen", i.second.getLastSeen());
			aXml.addChildAttrib("GrantSlot", i.second.isSet(FavoriteUser::FLAG_GRANTSLOT));
			aXml.addChildAttrib("SuperUser", i.second.isSet(FavoriteUser::FLAG_SUPERUSER));
			aXml.addChildAttrib("UserDescription", i.second.getDescription());
			aXml.addChildAttrib("Nick", i.second.getNick());
			aXml.addChildAttrib("URL", i.second.getUrl());
			aXml.addChildAttrib("CID", i.first.toBase32());
			aXml.addChildAttrib("Favorite", true);
		}

		for (auto& s : savedUsers) {
			auto u = createUser(s, Util::emptyString);
			aXml.addTag("User");
			aXml.addChildAttrib("LastSeen", u.getLastSeen());
			aXml.addChildAttrib("Nick", u.getNick());
			aXml.addChildAttrib("URL", u.getUrl());
			aXml.addChildAttrib("CID", s->getCID().toBase32());
			aXml.addChildAttrib("Favorite", false);
		}
	}

	aXml.stepOut();
}

void FavoriteManager::saveFavoriteDirectories(SimpleXML& aXml) const noexcept {
	aXml.addTag("FavoriteDirs");
	aXml.addChildAttrib("Version", 2);
	aXml.stepIn();

	const auto groupedDirs = getGroupedFavoriteDirs();
	for (const auto& fde : groupedDirs) {
		aXml.addTag("Directory", fde.first);
		aXml.addChildAttrib("Name", fde.first);
		aXml.stepIn();
		for (const auto& t : fde.second) {
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
		for (const auto& i : favHubGroups) {
			aXml.addTag("Group");
			aXml.addChildAttrib("Name", i.first);
			i.second.save(aXml);
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
	SettingsManager::loadSettingFile(CONFIG_DIR, CONFIG_FAV_NAME, [this](SimpleXML& xml) {
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
	
	// Add NMDC standard op commands
	static const char kickstr[] = 
		"$To: %[userNI] From: %[myNI] $<%[myNI]> You are being kicked because: %[kickline:Reason]|<%[myNI]> is kicking %[userNI] because: %[kickline:Reason]|$Kick %[userNI]|";
	addUserCommand(UserCommand::TYPE_RAW_ONCE, UserCommand::CONTEXT_USER | UserCommand::CONTEXT_SEARCH, UserCommand::FLAG_NOSAVE, 
		STRING(KICK_USER), kickstr, "", "op");
	static const char kickfilestr[] = 
		"$To: %[userNI] From: %[myNI] $<%[myNI]> You are being kicked because: %[kickline:Reason] %[fileFN]|<%[myNI]> is kicking %[userNI] because: %[kickline:Reason] %[fileFN]|$Kick %[userNI]|";
	addUserCommand(UserCommand::TYPE_RAW_ONCE, UserCommand::CONTEXT_SEARCH, UserCommand::FLAG_NOSAVE, 
		STRING(KICK_USER_FILE), kickfilestr, "", "op");
	static const char redirstr[] =
		"$OpForceMove $Who:%[userNI]$Where:%[line:Target Server]$Msg:%[line:Message]|";
	addUserCommand(UserCommand::TYPE_RAW_ONCE, UserCommand::CONTEXT_USER | UserCommand::CONTEXT_SEARCH, UserCommand::FLAG_NOSAVE, 
		STRING(REDIRECT_USER), redirstr, "", "op");

	SettingsManager::loadSettingFile(CONFIG_DIR, CONFIG_FAV_NAME, [this](SimpleXML& xml) {
		if(xml.findChild("Favorites")) {
			xml.stepIn();

			loadFavoriteHubs(xml);
			loadFavoriteUsers(xml);
			loadUserCommands(xml);
			loadFavoriteDirectories(xml);

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
			FavoriteHubEntryPtr e = new FavoriteHubEntry();
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
			auto p = server.find(';');
			if (p != string::npos) {
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
}

void FavoriteManager::loadUserCommands(SimpleXML& aXml) {
	if (aXml.findChild("UserCommands")) {
		aXml.stepIn();
		while (aXml.findChild("UserCommand")) {
			addUserCommand(aXml.getIntChildAttrib("Type"), aXml.getIntChildAttrib("Context"), 0, aXml.getChildAttrib("Name"),
				aXml.getChildAttrib("Command"), aXml.getChildAttrib("To"), aXml.getChildAttrib("Hub"));
		}
		aXml.stepOut();
	}

	aXml.resetCurrentChild();
}

void FavoriteManager::loadFavoriteUsers(SimpleXML& aXml) {
	if (aXml.findChild("Users")) {
		aXml.stepIn();
		while (aXml.findChild("User")) {
			const string& cid = aXml.getChildAttrib("CID");
			const string& nick = aXml.getChildAttrib("Nick");
			const string& hubUrl = aXml.getChildAttrib("URL");
			bool isFavorite = Util::toBool(Util::toInt(aXml.getChildAttrib("Favorite", "1")));
			auto lastSeen = (uint32_t)aXml.getIntChildAttrib("LastSeen");
			auto u = ClientManager::getInstance()->loadUser(cid, hubUrl, nick, lastSeen);
			if(!u || !isFavorite)
				continue;

			u->setFlag(User::FAVORITE);
			auto i = users.emplace(u->getCID(), FavoriteUser(u, nick, hubUrl, u->getCID().toBase32())).first;

			if (aXml.getBoolChildAttrib("GrantSlot"))
				i->second.setFlag(FavoriteUser::FLAG_GRANTSLOT);
			if (aXml.getBoolChildAttrib("SuperUser"))
				i->second.setFlag(FavoriteUser::FLAG_SUPERUSER);

			i->second.setLastSeen(lastSeen);
			i->second.setDescription(aXml.getChildAttrib("UserDescription"));
			
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

void FavoriteManager::setHubSetting(const string& aUrl, HubSettings::HubBoolSetting aSetting, bool aNewValue) noexcept {
	FavoriteHubEntryPtr hub;

	{
		RLock l(cs);
		auto p = getFavoriteHub(aUrl);
		if (p == favoriteHubs.end()) {
			return;
		}

		hub = *p;
		(*p)->get(aSetting) = aNewValue;
	}

	fire(FavoriteManagerListener::FavoriteHubUpdated(), hub);
}

bool FavoriteManager::hasSlot(const UserPtr& aUser) const noexcept {
	RLock l(cs);
	auto i = users.find(aUser->getCID());
	if(i == users.end())
		return false;
	return i->second.isSet(FavoriteUser::FLAG_GRANTSLOT);
}

time_t FavoriteManager::getLastSeen(const UserPtr& aUser) const noexcept {
	RLock l(cs);
	auto i = users.find(aUser->getCID());
	if(i == users.end())
		return 0;
	return i->second.getLastSeen();
}

void FavoriteManager::setAutoGrant(const UserPtr& aUser, bool grant) noexcept {
	{
		RLock l(cs);
		auto i = users.find(aUser->getCID());
		if(i == users.end())
			return;
		if(grant)
			i->second.setFlag(FavoriteUser::FLAG_GRANTSLOT);
		else
			i->second.unsetFlag(FavoriteUser::FLAG_GRANTSLOT);
	}

	setDirty();
}
void FavoriteManager::setUserDescription(const UserPtr& aUser, const string& description) noexcept {
	{
		RLock l(cs);
		auto i = users.find(aUser->getCID());
		if(i == users.end())
			return;
		i->second.setDescription(description);
	}

	setDirty();
}

void FavoriteManager::on(SettingsManagerListener::Load, SimpleXML&) noexcept {
	loadCID();
}

FavoriteHubEntryPtr FavoriteManager::getFavoriteHubEntry(const string& aServer) const noexcept {
	RLock l(cs);
	auto p = getFavoriteHub(aServer);
	return p != favoriteHubs.end() ? *p : nullptr;
}

FavoriteHubEntryPtr FavoriteManager::getFavoriteHubEntry(const ProfileToken& aToken) const noexcept {
	RLock l(cs);
	auto p = getFavoriteHub(aToken);
	return p != favoriteHubs.end() ? *p : nullptr;
}

void FavoriteManager::mergeHubSettings(const FavoriteHubEntryPtr& entry, HubSettings& settings) const noexcept {
	// apply group settings first.
	const string& name = entry->getGroup();
	if(!name.empty()) {
		auto group = favHubGroups.find(name);
		if(group != favHubGroups.end())
			settings.merge(group->second);
	}

	// apply fav entry settings next.
	settings.merge(*entry);
}

FavoriteHubEntryList::const_iterator FavoriteManager::getFavoriteHub(const string& aServer) const noexcept {
	//find by the primary address
	return find_if(favoriteHubs, [&aServer](const FavoriteHubEntryPtr& f) { return Util::stricmp(f->getServer(), aServer) == 0; });
}

FavoriteHubEntryList::const_iterator FavoriteManager::getFavoriteHub(ProfileToken aToken) const noexcept {
	return find_if(favoriteHubs, [aToken](const FavoriteHubEntryPtr& f) { return f->getToken() == aToken; });
}

UserCommand::List FavoriteManager::getUserCommands(int ctx, const StringList& hubs, bool& op) noexcept {
	vector<bool> isOp(hubs.size());

	for(size_t i = 0; i < hubs.size(); ++i) {
		if(ClientManager::getInstance()->isOp(ClientManager::getInstance()->getMe(), hubs[i])) {
			isOp[i] = true;
			op = true; // ugly hack
		}
	}

	RLock l(cs);
	UserCommand::List lst;
	for(const auto& uc: userCommands) {
		if(!(uc.getCtx() & ctx)) {
			continue;
		}

		for(size_t j = 0; j < hubs.size(); ++j) {
			const string& hub = hubs[j];
			bool hubAdc = hub.compare(0, 6, "adc://") == 0 || hub.compare(0, 7, "adcs://") == 0;
			bool commandAdc = uc.getHub().compare(0, 6, "adc://") == 0 || uc.getHub().compare(0, 7, "adcs://") == 0;
			if(hubAdc && commandAdc) {
				if((uc.getHub() == "adc://" || uc.getHub() == "adcs://") ||
					((uc.getHub() == "adc://op" || uc.getHub() == "adcs://op") && isOp[j]) ||
					(uc.getHub() == hub) )
				{
					lst.push_back(uc);
					break;
				}
			} else if((!hubAdc && !commandAdc) || uc.isChat()) {
				if((uc.getHub().length() == 0) || 
					(uc.getHub() == "op" && isOp[j]) ||
					(uc.getHub() == hub) )
				{
					lst.push_back(uc);
					break;
				}
			}
		}
	}
	return lst;
}


void FavoriteManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	if (xmlDirty && aTick > (lastXmlSave + 15 * 1000)) {
		save();
	}
}

void FavoriteManager::on(UserDisconnected, const UserPtr& user, bool wentOffline) noexcept {
	bool isFav = false;
	{
		RLock l(cs);
		auto i = users.find(user->getCID());
		if(i != users.end()) {
			isFav = true;
			if (wentOffline) {
				i->second.setLastSeen(GET_TIME());
			}
		}
	}

	if(isFav)
		fire(FavoriteManagerListener::FavoriteUserUpdated(), user);
}

void FavoriteManager::on(UserConnected, const OnlineUser& aUser, bool /*wasOffline*/) noexcept {
	UserPtr user = aUser.getUser();

	if(user->isSet(User::FAVORITE))
		fire(FavoriteManagerListener::FavoriteUserUpdated(), user);
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
	auto client = ClientManager::getInstance()->getClient(aEntry->getServer());
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
