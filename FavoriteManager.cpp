/* 
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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

#include "AirUtil.h"
#include "ClientManager.h"
#include "ResourceManager.h"
#include "CryptoManager.h"
#include "LogManager.h"
#include "ShareManager.h"

#include "HttpConnection.h"
#include "StringTokenizer.h"
#include "SimpleXML.h"
#include "UserCommand.h"
#include "BZUtils.h"
#include "FilteredFile.h"
#include "CID.h"

#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/for_each.hpp>

namespace dcpp {

using boost::range::for_each;
using boost::range::find_if;

FavoriteManager::FavoriteManager() : lastId(0), useHttp(false), running(false), c(NULL), lastServer(0), listType(TYPE_NORMAL), dontSave(false) {
	SettingsManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);

	File::ensureDirectory(Util::getHubListsPath());
}

FavoriteManager::~FavoriteManager() {
	ClientManager::getInstance()->removeListener(this);
	SettingsManager::getInstance()->removeListener(this);
	if(c) {
		c->removeListener(this);
		delete c;
		c = nullptr;
	}

	for_each(favoriteHubs, DeleteFunction());
	for_each(recentHubs, DeleteFunction());
	for_each(previewApplications, DeleteFunction());
}

UserCommand FavoriteManager::addUserCommand(int type, int ctx, Flags::MaskType flags, const string& name, const string& command, const string& to, const string& hub) {
	// No dupes, add it...
	auto cmd = UserCommand(lastId++, type, ctx, flags, name, command, to, hub);

	{
		Lock l(cs);
		userCommands.emplace_back(cmd);
	}

	if(!cmd.isSet(UserCommand::FLAG_NOSAVE)) 
		save();

	return cmd;
}

bool FavoriteManager::getUserCommand(int cid, UserCommand& uc) {
	Lock l(cs);
	for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
		if(i->getId() == cid) {
			uc = *i;
			return true;
		}
	}
	return false;
}

bool FavoriteManager::moveUserCommand(int cid, int pos) {
	dcassert(pos == -1 || pos == 1);
	Lock l(cs);
	for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
		if(i->getId() == cid) {
			swap(*i, *(i + pos));
			return true;
		}
	}
	return false;
}

void FavoriteManager::updateUserCommand(const UserCommand& uc) {
	bool nosave = true;
	{
		Lock l(cs);
		for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
			if(i->getId() == uc.getId()) {
				*i = uc;
				nosave = uc.isSet(UserCommand::FLAG_NOSAVE);
				break;
			}
		}
	}

	if(!nosave)
		save();
}

int FavoriteManager::findUserCommand(const string& aName, const string& aUrl) {
	Lock l(cs);
	for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
		if(i->getName() == aName && i->getHub() == aUrl) {
			return i->getId();
		}
	}
	return -1;
}

void FavoriteManager::removeUserCommand(int cid) {
	bool nosave = true;
	{
		Lock l(cs);
		for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
			if(i->getId() == cid) {
				nosave = i->isSet(UserCommand::FLAG_NOSAVE);
				userCommands.erase(i);
				break;
			}
		}
	}

	if(!nosave)
		save();
}
void FavoriteManager::removeUserCommand(const string& srv) {
	Lock l(cs);
	for(auto i = userCommands.begin(); i != userCommands.end(); ) {
		if((i->getHub() == srv) && i->isSet(UserCommand::FLAG_NOSAVE)) {
			i = userCommands.erase(i);
		} else {
			++i;
		}
	}
}

void FavoriteManager::removeHubUserCommands(int ctx, const string& hub) {
	Lock l(cs);
	for(auto i = userCommands.begin(); i != userCommands.end(); ) {
		if(i->getHub() == hub && i->isSet(UserCommand::FLAG_NOSAVE) && i->getCtx() & ctx) {
			i = userCommands.erase(i);
		} else {
			++i;
		}
	}
}

void FavoriteManager::addFavoriteUser(const UserPtr& aUser) {
	{
		Lock l(cs);
		if(users.find(aUser->getCID()) != users.end()) {
			return;
		}
	}

	StringList urls = move(ClientManager::getInstance()->getHubUrls(aUser->getCID()));
	StringList nicks = move(ClientManager::getInstance()->getNicks(aUser->getCID()));
        
	/// @todo make this an error probably...
	if(urls.empty())
		urls.push_back(Util::emptyString);
	if(nicks.empty())
		nicks.push_back(Util::emptyString);

	FavoriteUser fu = FavoriteUser(aUser, Util::toString(nicks), urls[0], aUser->getCID().toBase32());
	{
		Lock l (cs);
		users.insert(make_pair(aUser->getCID(), fu)).first;
	}

	fire(FavoriteManagerListener::UserAdded(), fu);
}

void FavoriteManager::removeFavoriteUser(const UserPtr& aUser) {
	Lock l(cs);
	auto i = users.find(aUser->getCID());
	if(i != users.end()) {
		fire(FavoriteManagerListener::UserRemoved(), i->second);
		users.erase(i);
		save();
	}
}

std::string FavoriteManager::getUserURL(const UserPtr& aUser) const {
	Lock l(cs);
	auto i = users.find(aUser->getCID());
	if(i != users.end()) {
		const FavoriteUser& fu = i->second;
		return fu.getUrl();
	}
	return Util::emptyString;
}

void FavoriteManager::addFavorite(const FavoriteHubEntry& aEntry) {
	FavoriteHubEntry* f;

	auto i = getFavoriteHub(aEntry.getServers()[0].first);
	if(i != favoriteHubs.end()) {
		return;
	}
	f = new FavoriteHubEntry(aEntry);
	favoriteHubs.push_back(f);
	fire(FavoriteManagerListener::FavoriteAdded(), f);
	save();
}

void FavoriteManager::removeFavorite(const FavoriteHubEntry* entry) {
	auto i = find(favoriteHubs.begin(), favoriteHubs.end(), entry);
	if(i == favoriteHubs.end()) {
		return;
	}

	fire(FavoriteManagerListener::FavoriteRemoved(), entry);
	favoriteHubs.erase(i);
	delete entry;
	save();
}

bool FavoriteManager::addFavoriteDir(const string& aName, StringList& aTargets){
	auto p = find_if(favoriteDirs, CompareFirst<string, StringList>(aName));
	if (p != favoriteDirs.end())
		return false;

	sort(aTargets.begin(), aTargets.end());
	favoriteDirs.push_back(make_pair(aName, aTargets));
	save();
	return true;
}

bool FavoriteManager::isFavoriteHub(const std::string& url) {
	return getFavoriteHub(url) != favoriteHubs.end();
}

void FavoriteManager::saveFavoriteDirs(FavDirList dirs) {
	favoriteDirs.clear();
	favoriteDirs = dirs;
	save();
}


void FavoriteManager::addRecent(const RecentHubEntry& aEntry) {
	auto i = getRecentHub(aEntry.getServer());
	if(i != recentHubs.end()) {
		return;
	}
	RecentHubEntry* f = new RecentHubEntry(aEntry);
	recentHubs.push_back(f);
	fire(FavoriteManagerListener::RecentAdded(), f);
	recentsave();
}

void FavoriteManager::removeRecent(const RecentHubEntry* entry) {
	auto i = find(recentHubs.begin(), recentHubs.end(), entry);
	if(i == recentHubs.end()) {
		return;
	}
		
	fire(FavoriteManagerListener::RecentRemoved(), entry);
	recentHubs.erase(i);
	delete entry;
	recentsave();
}

void FavoriteManager::updateRecent(const RecentHubEntry* entry) {
	auto i = find(recentHubs.begin(), recentHubs.end(), entry);
	if(i == recentHubs.end()) {
		return;
	}
		
	fire(FavoriteManagerListener::RecentUpdated(), entry);
	recentsave();
}

class XmlListLoader : public SimpleXMLReader::CallBack {
public:
	XmlListLoader(HubEntryList& lst) : publicHubs(lst) { }
	~XmlListLoader() { }
	void startTag(const string& name, StringPairList& attribs, bool) {
		if(name == "Hub") {
			const string& name = getAttrib(attribs, "Name", 0);
			const string& server = getAttrib(attribs, "Address", 1);
			const string& description = getAttrib(attribs, "Description", 2);
			const string& users = getAttrib(attribs, "Users", 3);
			const string& country = getAttrib(attribs, "Country", 4);
			const string& shared = getAttrib(attribs, "Shared", 5);
			const string& minShare = getAttrib(attribs, "Minshare", 5);
			const string& minSlots = getAttrib(attribs, "Minslots", 5);
			const string& maxHubs = getAttrib(attribs, "Maxhubs", 5);
			const string& maxUsers = getAttrib(attribs, "Maxusers", 5);
			const string& reliability = getAttrib(attribs, "Reliability", 5);
			const string& rating = getAttrib(attribs, "Rating", 5);
			publicHubs.push_back(HubEntry(name, server, description, users, country, shared, minShare, minSlots, maxHubs, maxUsers, reliability, rating));
		}
	}
private:
	HubEntryList& publicHubs;
};

bool FavoriteManager::onHttpFinished(bool fromHttp) noexcept {
	MemoryInputStream mis(downloadBuf);
	bool success = true;

	Lock l(cs);
	HubEntryList& list = publicListMatrix[publicListServer];
	list.clear();

	try {
		XmlListLoader loader(list);

		if((listType == TYPE_BZIP2) && (!downloadBuf.empty())) {
			FilteredInputStream<UnBZFilter, false> f(&mis);
			SimpleXMLReader(&loader).parse(f);
		} else {
			SimpleXMLReader(&loader).parse(mis);
		}
	} catch(const Exception&) {
		success = false;
		fire(FavoriteManagerListener::Corrupted(), fromHttp ? publicListServer : Util::emptyString);
	}

	if(fromHttp) {
		try {
			File f(Util::getHubListsPath() + Util::validateFileName(publicListServer), File::WRITE, File::CREATE | File::TRUNCATE);
			f.write(downloadBuf);
			f.close();
		} catch(const FileException&) { }
	}

	downloadBuf = Util::emptyString;
	
	return success;
}

int FavoriteManager::resetProfiles(const ProfileTokenList& aProfiles, ShareProfilePtr defaultProfile) {
	int counter = 0;
	{
		Lock l(cs);
		for(auto k = aProfiles.begin(), iend = aProfiles.end(); k != iend; ++k) {
			for(auto i = favoriteHubs.begin(), iend = favoriteHubs.end(); i != iend; ++i) {
				if ((*i)->getShareProfile() == *k) {
					(*i)->setShareProfile(defaultProfile);
					counter++;
				}
			}
		}
	}

	if (counter > 0)
		fire(FavoriteManagerListener::FavoritesUpdated());

	return counter;
}

void FavoriteManager::onProfilesRenamed() {
	fire(FavoriteManagerListener::FavoritesUpdated());
}

void FavoriteManager::save() {
	if(dontSave)
		return;

	Lock l(cs);
	try {
		SimpleXML xml;

		xml.addTag("Favorites");
		xml.stepIn();

		xml.addTag("CID", SETTING(PRIVATE_ID));

		xml.addTag("Hubs");
		xml.stepIn();

		for(auto i = favHubGroups.begin(), iend = favHubGroups.end(); i != iend; ++i) {
			xml.addTag("Group");
			xml.addChildAttrib("Name", i->first);
			xml.addChildAttrib("Private", i->second.priv);
		}

		for(auto i = favoriteHubs.begin(), iend = favoriteHubs.end(); i != iend; ++i) {
			xml.addTag("Hub");
			xml.addChildAttrib("Name", (*i)->getName());
			xml.addChildAttrib("Connect", (*i)->getConnect());
			xml.addChildAttrib("Description", (*i)->getDescription());
			xml.addChildAttrib("Nick", (*i)->getNick(false));
			xml.addChildAttrib("Password", (*i)->getPassword());
			xml.addChildAttrib("Server", (*i)->getServerStr());
			xml.addChildAttrib("UserDescription", (*i)->getUserDescription());
			xml.addChildAttrib("Encoding", (*i)->getEncoding());
			xml.addChildAttrib("ChatUserSplit", (*i)->getChatUserSplit());
			xml.addChildAttrib("StealthMode", (*i)->getStealth());
			xml.addChildAttrib("UserListState", (*i)->getUserListState());
			xml.addChildAttrib("HubFrameOrder",	(*i)->getHeaderOrder());
			xml.addChildAttrib("HubFrameWidths", (*i)->getHeaderWidths());
			xml.addChildAttrib("HubFrameVisible", (*i)->getHeaderVisible());
			xml.addChildAttrib("Mode", Util::toString((*i)->getMode()));
			xml.addChildAttrib("IP", (*i)->getIP());
			xml.addChildAttrib("FavNoPM", (*i)->getFavNoPM());	
			xml.addChildAttrib("HubShowJoins", (*i)->getHubShowJoins());	//show joins
			xml.addChildAttrib("HubLogMainchat", (*i)->getHubLogMainchat());
			xml.addChildAttrib("SearchInterval", Util::toString((*i)->getSearchInterval()));
			xml.addChildAttrib("Group", (*i)->getGroup());
			xml.addChildAttrib("ChatNotify", (*i)->getChatNotify());
			xml.addChildAttrib("Bottom",			Util::toString((*i)->getBottom()));
			xml.addChildAttrib("Top",				Util::toString((*i)->getTop()));
			xml.addChildAttrib("Right",				Util::toString((*i)->getRight()));
			xml.addChildAttrib("Left",				Util::toString((*i)->getLeft()));
			xml.addChildAttrib("ShareProfile",		(*i)->getShareProfile()->getToken());
		}

		xml.stepOut();

		xml.addTag("Users");
		xml.stepIn();
		for(auto i = users.begin(), iend = users.end(); i != iend; ++i) {
			xml.addTag("User");
			xml.addChildAttrib("LastSeen", i->second.getLastSeen());
			xml.addChildAttrib("GrantSlot", i->second.isSet(FavoriteUser::FLAG_GRANTSLOT));
			xml.addChildAttrib("UserDescription", i->second.getDescription());
			xml.addChildAttrib("Nick", i->second.getNick());
			xml.addChildAttrib("URL", i->second.getUrl());
			xml.addChildAttrib("CID", i->first.toBase32());
		}
		xml.stepOut();

		xml.addTag("UserCommands");
		xml.stepIn();
		for(auto i = userCommands.begin(), iend = userCommands.end(); i != iend; ++i) {
			if(!i->isSet(UserCommand::FLAG_NOSAVE)) {
				xml.addTag("UserCommand");
				xml.addChildAttrib("Type", i->getType());
				xml.addChildAttrib("Context", i->getCtx());
				xml.addChildAttrib("Name", i->getName());
				xml.addChildAttrib("Command", i->getCommand());
				xml.addChildAttrib("To", i->getTo());
				xml.addChildAttrib("Hub", i->getHub());
			}
		}
		xml.stepOut();

		//Favorite download to dirs
		xml.addTag("FavoriteDirs");
		xml.addChildAttrib("Version", 2);
		xml.stepIn();
		FavDirList spl = getFavoriteDirs();
		for(auto i = spl.begin(), iend = spl.end(); i != iend; ++i) {
			xml.addTag("Directory", i->first);
			xml.addChildAttrib("Name", i->first);
			xml.stepIn();
			for (auto s = i->second.begin(); s != i->second.end(); ++s) {
				xml.addTag("Target", (*s));
			}
			xml.stepOut();
		}
		xml.stepOut();

		xml.stepOut();

		string fname = getConfigFile();

		File f(fname + ".tmp", File::WRITE, File::CREATE | File::TRUNCATE);
		f.write(SimpleXML::utf8Header);
		f.write(xml.toXML());
		f.close();
		//dont overWrite with empty file.
		if(File::getSize(fname + ".tmp") > 0) {
			File::deleteFile(fname);
			File::renameFile(fname + ".tmp", fname);
		}
	} catch(const Exception& e) {
		dcdebug("FavoriteManager::save: %s\n", e.getError().c_str());
	}
}

void FavoriteManager::previewload(SimpleXML& aXml){
	aXml.resetCurrentChild();
	if(aXml.findChild("PreviewApps")) {
		aXml.stepIn();
		while(aXml.findChild("Application")) {					
			addPreviewApp(aXml.getChildAttrib("Name"), aXml.getChildAttrib("Application"), 
				aXml.getChildAttrib("Arguments"), aXml.getChildAttrib("Extension"));			
		}
		aXml.stepOut();
	}	
}

void FavoriteManager::previewsave(SimpleXML& aXml){
	aXml.addTag("PreviewApps");
	aXml.stepIn();
	for(auto i = previewApplications.begin(); i != previewApplications.end(); ++i) {
		aXml.addTag("Application");
		aXml.addChildAttrib("Name", (*i)->getName());
		aXml.addChildAttrib("Application", (*i)->getApplication());
		aXml.addChildAttrib("Arguments", (*i)->getArguments());
		aXml.addChildAttrib("Extension", (*i)->getExtension());
	}
	aXml.stepOut();
}

void FavoriteManager::recentsave() {
	try {
		SimpleXML xml;

		xml.addTag("Recents");
		xml.stepIn();

		xml.addTag("Hubs");
		xml.stepIn();

		for(RecentHubEntry::Iter i = recentHubs.begin(); i != recentHubs.end(); ++i) {
			xml.addTag("Hub");
			xml.addChildAttrib("Name", (*i)->getName());
			xml.addChildAttrib("Description", (*i)->getDescription());
			xml.addChildAttrib("Users", (*i)->getUsers());
			xml.addChildAttrib("Shared", (*i)->getShared());
			xml.addChildAttrib("Server", (*i)->getServer());
		}

		xml.stepOut();
		xml.stepOut();
		
		string fname = Util::getPath(Util::PATH_USER_CONFIG) + "Recents.xml";

		File f(fname + ".tmp", File::WRITE, File::CREATE | File::TRUNCATE);
		f.write(SimpleXML::utf8Header);
		f.write(xml.toXML());
		f.close();
		File::deleteFile(fname);
		File::renameFile(fname + ".tmp", fname);
	} catch(const Exception& e) {
		dcdebug("FavoriteManager::recentsave: %s\n", e.getError().c_str());
	}
}

void FavoriteManager::loadCID() {
	try {
		SimpleXML xml;
		if(Util::fileExists(getConfigFile())) {
			xml.fromXML(File(getConfigFile(), File::READ, File::OPEN).read());
		
			if(xml.findChild("Favorites")) {
				xml.stepIn();
				if(xml.findChild("CID")) {
					xml.stepIn();
					SettingsManager::getInstance()->set(SettingsManager::PRIVATE_ID, xml.getData());
					xml.stepOut();
				}

				xml.stepOut();
			}
		}
	} catch(const Exception& e) {
		LogManager::getInstance()->message("Error Loading CID : " + e.getError(), LogManager::LOG_ERROR);
	}

	if(SETTING(PRIVATE_ID).length() != 39 || CID(SETTING(PRIVATE_ID)).isZero()) {
		SettingsManager::getInstance()->set(SettingsManager::PRIVATE_ID, CID::generate().toBase32());
	}
}

void FavoriteManager::load() {
	
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

	try {
		SimpleXML xml;
		Util::migrate(getConfigFile());
		if(Util::fileExists(getConfigFile())) {
			xml.fromXML(File(getConfigFile(), File::READ, File::OPEN).read());
		
			if(xml.findChild("Favorites")) {
				xml.stepIn();
				load(xml);
				xml.stepOut();
			}
			//we have load it fine now, so make a backup of a working favorites.xml
			File::deleteFile(getConfigFile() + ".bak");
			CopyFile(Text::toT(getConfigFile()).c_str(), Text::toT(getConfigFile() + ".bak").c_str(), FALSE);
		}
	} catch(const Exception& e) {
		dcdebug("FavoriteManager::load: %s\n", e.getError().c_str());
		LogManager::getInstance()->message("Error Loading Favorites.xml : " + e.getError(), LogManager::LOG_ERROR);
	}

	try {
		Util::migrate(Util::getPath(Util::PATH_USER_CONFIG) + "Recents.xml");
		
		SimpleXML xml;
		xml.fromXML(File(Util::getPath(Util::PATH_USER_CONFIG) + "Recents.xml", File::READ, File::OPEN).read());
		
		if(xml.findChild("Recents")) {
			xml.stepIn();
			recentload(xml);
			xml.stepOut();
		}
	} catch(const Exception& e) {
		dcdebug("FavoriteManager::recentload: %s\n", e.getError().c_str());
	}
}

void FavoriteManager::load(SimpleXML& aXml) {
	dontSave = true;
	bool needSave = false;

	aXml.resetCurrentChild();
	if(aXml.findChild("Hubs")) {
		aXml.stepIn();

		while(aXml.findChild("Group")) {
			string name = aXml.getChildAttrib("Name");
			if(name.empty())
				continue;
			FavHubGroupProperties props = { aXml.getBoolChildAttrib("Private") };
			favHubGroups[name] = props;
		}

		aXml.resetCurrentChild();
		while(aXml.findChild("Hub")) {
			FavoriteHubEntry* e = new FavoriteHubEntry();
			e->setName(aXml.getChildAttrib("Name"));
			e->setConnect(aXml.getBoolChildAttrib("Connect"));
			e->setDescription(aXml.getChildAttrib("Description"));
			e->setNick(aXml.getChildAttrib("Nick"));
			e->setPassword(aXml.getChildAttrib("Password"));
			e->setServerStr(aXml.getChildAttrib("Server"));
			e->setUserDescription(aXml.getChildAttrib("UserDescription"));
			e->setEncoding(aXml.getChildAttrib("Encoding"));
			e->setChatUserSplit(aXml.getIntChildAttrib("ChatUserSplit"));
			e->setStealth(aXml.getBoolChildAttrib("StealthMode"));
			e->setUserListState(aXml.getBoolChildAttrib("UserListState"));
			e->setHeaderOrder(aXml.getChildAttrib("HubFrameOrder", SETTING(HUBFRAME_ORDER)));
			e->setHeaderWidths(aXml.getChildAttrib("HubFrameWidths", SETTING(HUBFRAME_WIDTHS)));
			e->setHeaderVisible(aXml.getChildAttrib("HubFrameVisible", SETTING(HUBFRAME_VISIBLE)));
			e->setBottom((uint16_t)aXml.getIntChildAttrib("Bottom") );
			e->setTop((uint16_t)	aXml.getIntChildAttrib("Top"));
			e->setRight((uint16_t)	aXml.getIntChildAttrib("Right"));
			e->setLeft((uint16_t)	aXml.getIntChildAttrib("Left"));
			e->setMode(Util::toInt(aXml.getChildAttrib("Mode")));
			e->setIP(aXml.getChildAttrib("IP"));
			e->setFavNoPM(aXml.getBoolChildAttrib("FavNoPM"));
			e->setHubShowJoins(aXml.getBoolChildAttrib("HubShowJoins")); //show joins
			e->setHubLogMainchat(aXml.getBoolChildAttrib("HubLogMainchat")); 
			e->setSearchInterval(Util::toUInt32(aXml.getChildAttrib("SearchInterval")));
			e->setGroup(aXml.getChildAttrib("Group"));
			e->setChatNotify(aXml.getBoolChildAttrib("ChatNotify"));
			if (aXml.getBoolChildAttrib("HideShare")) {
				e->setShareProfile(ShareManager::getInstance()->getShareProfile(SP_HIDDEN));
			} else {
				auto profile = aXml.getIntChildAttrib("ShareProfile");
				e->setShareProfile(ShareManager::getInstance()->getShareProfile(profile, true));
			}

			favoriteHubs.push_back(e);
		}

		aXml.stepOut();
	}
	aXml.resetCurrentChild();
	if(aXml.findChild("Users")) {
		aXml.stepIn();
		while(aXml.findChild("User")) {
			UserPtr u;
			const string& cid = aXml.getChildAttrib("CID");
			const string& nick = aXml.getChildAttrib("Nick");
			const string& hubUrl = aXml.getChildAttrib("URL");

			if(cid.length() != 39) {
				if(nick.empty() || hubUrl.empty())
					continue;
				u = ClientManager::getInstance()->getUser(nick, hubUrl);
			} else {
				u = ClientManager::getInstance()->getUser(CID(cid));
			}
			auto i = users.insert(make_pair(u->getCID(), FavoriteUser(u, nick, hubUrl, cid))).first;

			if(aXml.getBoolChildAttrib("GrantSlot"))
				i->second.setFlag(FavoriteUser::FLAG_GRANTSLOT);

			i->second.setLastSeen((uint32_t)aXml.getIntChildAttrib("LastSeen"));
			i->second.setDescription(aXml.getChildAttrib("UserDescription"));

		}
		aXml.stepOut();
	}

	aXml.resetCurrentChild();
	if(aXml.findChild("UserCommands")) {
		aXml.stepIn();
		while(aXml.findChild("UserCommand")) {
			addUserCommand(aXml.getIntChildAttrib("Type"), aXml.getIntChildAttrib("Context"), 0, aXml.getChildAttrib("Name"),
				aXml.getChildAttrib("Command"), aXml.getChildAttrib("To"), aXml.getChildAttrib("Hub"));
		}
		aXml.stepOut();
	}

	//Favorite download to dirs
	aXml.resetCurrentChild();
	if(aXml.findChild("FavoriteDirs")) {
		string version = aXml.getChildAttrib("Version");
		aXml.stepIn();
		if (version.empty() || Util::toInt(version) < 2) {
			//convert old directories
			while(aXml.findChild("Directory")) {
				string virt = aXml.getChildAttrib("Name");
				string d(aXml.getChildData());
				StringList targets;
				targets.push_back(d);
				FavoriteManager::getInstance()->addFavoriteDir(virt, targets);
			}
			needSave = true;
		} else {
			while(aXml.findChild("Directory")) {
				string name = aXml.getChildAttrib("Name");
				if (!name.empty()) {
					aXml.stepIn();
					StringList targets;
					while(aXml.findChild("Target")) {
						aXml.stepIn();
						string path = aXml.getData();
						if( path[ path.length() -1 ] != PATH_SEPARATOR ) {
							path += PATH_SEPARATOR;
						}
						if (find(targets.begin(), targets.end(), path) == targets.end()) {
							targets.push_back(path);
						}
						aXml.stepOut();
					}
					if (!targets.empty()) {
						FavoriteManager::getInstance()->addFavoriteDir(name, targets);
					}
					aXml.stepOut();
				}
			}
		}
		aXml.stepOut();
	}

	dontSave = false;
	if(needSave)
		save();
}

void FavoriteManager::userUpdated(const OnlineUser& info) {
	Lock l(cs);
	auto i = users.find(info.getUser()->getCID());
	if(i != users.end()) {
		FavoriteUser& fu = i->second;
		fu.update(info);
		save();
	}
}
	
FavoriteHubEntryList FavoriteManager::getFavoriteHubs(const string& group) const {
	FavoriteHubEntryList ret;
	ret.reserve(favoriteHubs.size());
	copy_if(favoriteHubs.begin(), favoriteHubs.end(), ret.begin(), [&group](const FavoriteHubEntryPtr f) { return stricmp(f->getGroup(), group) == 0; });
	return ret;
}

bool FavoriteManager::hasSlot(const UserPtr& aUser) const { 
	Lock l(cs);
	auto i = users.find(aUser->getCID());
	if(i == users.end())
		return false;
	return i->second.isSet(FavoriteUser::FLAG_GRANTSLOT);
}

time_t FavoriteManager::getLastSeen(const UserPtr& aUser) const { 
	Lock l(cs);
	auto i = users.find(aUser->getCID());
	if(i == users.end())
		return 0;
	return i->second.getLastSeen();
}

void FavoriteManager::setAutoGrant(const UserPtr& aUser, bool grant) {
	Lock l(cs);
	auto i = users.find(aUser->getCID());
	if(i == users.end())
		return;
	if(grant)
		i->second.setFlag(FavoriteUser::FLAG_GRANTSLOT);
	else
		i->second.unsetFlag(FavoriteUser::FLAG_GRANTSLOT);
	save();
}
void FavoriteManager::setUserDescription(const UserPtr& aUser, const string& description) {
	Lock l(cs);
	auto i = users.find(aUser->getCID());
	if(i == users.end())
		return;
	i->second.setDescription(description);
	save();
}

void FavoriteManager::recentload(SimpleXML& aXml) {
	aXml.resetCurrentChild();
	if(aXml.findChild("Hubs")) {
		aXml.stepIn();
		while(aXml.findChild("Hub")) {
			RecentHubEntry* e = new RecentHubEntry();
			e->setName(aXml.getChildAttrib("Name"));
			e->setDescription(aXml.getChildAttrib("Description"));
			e->setUsers(aXml.getChildAttrib("Users"));
			e->setShared(aXml.getChildAttrib("Shared"));
			e->setServer(aXml.getChildAttrib("Server"));
			recentHubs.push_back(e);
		}
		aXml.stepOut();
	}
}

StringList FavoriteManager::getHubLists() {
	StringTokenizer<string> lists(SETTING(HUBLIST_SERVERS), ';');
	return lists.getTokens();
}

FavoriteHubEntry* FavoriteManager::getFavoriteHubEntry(const string& aServer) const {
	auto p = getFavoriteHub(aServer);
	return p != favoriteHubs.end() ? *p : nullptr;
}

FavoriteHubEntryList::const_iterator FavoriteManager::getFavoriteHub(const string& aServer) const {
	//find by the primary address
	return find_if(favoriteHubs, [&aServer](const FavoriteHubEntryPtr f) { return stricmp(f->getServers()[0].first, aServer) == 0; });
}

FavoriteHubEntryList::const_iterator FavoriteManager::getFavoriteHub(ProfileToken aToken) const {
	return find_if(favoriteHubs, [aToken](const FavoriteHubEntryPtr f) { return f->getToken() == aToken; });
}

bool FavoriteManager::getFailOverUrl(ProfileToken aToken, string& hubAddress_) {
	//removeUserCommand(hubAddress_);

	if (aToken == 0)
		return false;

	Lock l (cs);
	auto p = getFavoriteHub(aToken);
	if (p != favoriteHubs.end()) {
		auto& servers = (*p)->getServers();
		if (servers.size() > 1) {
			auto s = find_if(servers.begin(), servers.end(), CompareFirst<string, bool>(hubAddress_));
			if (s != servers.end()) {
				//find the next address that hasn't been blocked
				auto beginPos = s;
				for (;;) {
					s == servers.end()-1 ? s = servers.begin() : s++;
					if (s == beginPos) {
						break;
					}

					if (!s->second) {
						hubAddress_ = s->first;
						return true;
					}
				}
			}
		}
	}

	return false;
}

bool FavoriteManager::blockFailOverUrl(ProfileToken aToken, string& hubAddress_) {
	if (aToken == 0)
		return false;

	Lock l (cs);
	auto p = getFavoriteHub(aToken);
	if (p != favoriteHubs.end() && (*p)->getServers()[0].first != hubAddress_) {
		(*p)->blockFailOver(hubAddress_);
		hubAddress_ = (*p)->getServers()[0].first;
		return true;
	}
	return false;
}

void FavoriteManager::setFailOvers(const string& hubUrl, ProfileToken aToken, StringList&& aAddresses) {
	Lock l (cs);
	auto p = getFavoriteHub(aToken);
	if (p != favoriteHubs.end() && (*p)->getServers()[0].first == hubUrl) { //only update if we are connecting with the primary address
		(*p)->addFailOvers(move(aAddresses));
		save();
	}
}

bool FavoriteManager::isFailOverUrl(ProfileToken aToken, const string& hubAddress_) {
	if (aToken == 0)
		return false;

	Lock l (cs);
	auto p = getFavoriteHub(aToken);
	return (p != favoriteHubs.end() && (*p)->getServers()[0].first != hubAddress_);
}

RecentHubEntry::Iter FavoriteManager::getRecentHub(const string& aServer) const {
	return find_if(recentHubs, [&aServer](const RecentHubEntry* rhe) { return stricmp(rhe->getServer(), aServer) == 0; });
}

void FavoriteManager::setHubList(int aHubList) {
	lastServer = aHubList;
	refresh();
}

void FavoriteManager::refresh(bool forceDownload /* = false */) {
	StringList sl = getHubLists();
	if(sl.empty())
		return;
	publicListServer = sl[(lastServer) % sl.size()];
	if(strnicmp(publicListServer.c_str(), "http://", 7) != 0) {
		lastServer++;
		return;
	}

	if(!forceDownload) {
		string path = Util::getHubListsPath() + Util::validateFileName(publicListServer);
		if(File::getSize(path) > 0) {
			useHttp = false;
			string fileDate;
			{
				Lock l(cs);
				publicListMatrix[publicListServer].clear();
			}
			listType = (stricmp(path.substr(path.size() - 4), ".bz2") == 0) ? TYPE_BZIP2 : TYPE_NORMAL;
			try {
				File cached(path, File::READ, File::OPEN);
				downloadBuf = cached.read();
				char buf[20];
				time_t fd = cached.getLastModified();
				if (strftime(buf, 20, "%x", localtime(&fd))) {
					fileDate = string(buf);
				}
			} catch(const FileException&) {
				downloadBuf = Util::emptyString;
			}
			if(!downloadBuf.empty()) {
				if (onHttpFinished(false)) {
					fire(FavoriteManagerListener::LoadedFromCache(), publicListServer, fileDate);
				}		
				return;
			}
		}
	}

	if(!running) {
		useHttp = true;
		{
			Lock l(cs);
			publicListMatrix[publicListServer].clear();
		}
		fire(FavoriteManagerListener::DownloadStarting(), publicListServer);
		if(c == NULL)
			c = new HttpConnection();
		c->addListener(this);
		c->downloadFile(publicListServer);
		running = true;
	}
}

UserCommand::List FavoriteManager::getUserCommands(int ctx, const StringList& hubs, bool& op) {
	vector<bool> isOp(hubs.size());

	for(size_t i = 0; i < hubs.size(); ++i) {
		if(ClientManager::getInstance()->isOp(ClientManager::getInstance()->getMe(), hubs[i])) {
			isOp[i] = true;
			op = true; // ugly hack
		}
	}

	Lock l(cs);
	UserCommand::List lst;
	for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
		UserCommand& uc = *i;
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
					lst.push_back(*i);
					break;
				}
			} else if((!hubAdc && !commandAdc) || uc.isChat()) {
				if((uc.getHub().length() == 0) || 
					(uc.getHub() == "op" && isOp[j]) ||
					(uc.getHub() == hub) )
				{
					lst.push_back(*i);
					break;
				}
			}
		}
	}
	return lst;
}

// HttpConnectionListener
void FavoriteManager::on(Data, HttpConnection*, const uint8_t* buf, size_t len) noexcept { 
	if(useHttp)
		downloadBuf.append((const char*)buf, len);
}

void FavoriteManager::on(Failed, HttpConnection*, const string& aLine) noexcept { 
	c->removeListener(this);
	lastServer++;
	running = false;
	if(useHttp){
		downloadBuf = Util::emptyString;
		fire(FavoriteManagerListener::DownloadFailed(), aLine);
	}
}
void FavoriteManager::on(Complete, HttpConnection*, const string& aLine, bool fromCoral) noexcept {
	bool parseSuccess = false;
	c->removeListener(this);
	if(useHttp) {
		if(c->getMimeType() == "application/x-bzip2")
			listType = TYPE_BZIP2;
		parseSuccess = onHttpFinished(true);
	}	
	running = false;
	if(parseSuccess) {
		fire(FavoriteManagerListener::DownloadFinished(), aLine, fromCoral);
	}
}
void FavoriteManager::on(Redirected, HttpConnection*, const string& aLine) noexcept { 
	if(useHttp)
		fire(FavoriteManagerListener::DownloadStarting(), aLine);
}

void FavoriteManager::on(Retried, HttpConnection*, const bool Connected) noexcept {
	if (Connected)
		downloadBuf = Util::emptyString;
}

void FavoriteManager::on(UserUpdated, const OnlineUser& user) noexcept {
	userUpdated(user);
}
void FavoriteManager::on(UserDisconnected, const UserPtr& user) noexcept {
	bool isFav = false;
	{
		Lock l(cs);
		auto i = users.find(user->getCID());
		if(i != users.end()) {
			isFav = true;
			i->second.setLastSeen(GET_TIME());
			save();
		}
	}
	if(isFav)
		fire(FavoriteManagerListener::StatusChanged(), user);
}

void FavoriteManager::on(UserConnected, const OnlineUser& aUser) noexcept {
	bool isFav = false;
	UserPtr user = aUser.getUser();
	{
		Lock l(cs);
		auto i = users.find(user->getCID());
		if(i != users.end()) {
			isFav = true;
		}
	}

	if(isFav)
		fire(FavoriteManagerListener::StatusChanged(), user);
}

} // namespace dcpp