/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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
#include "ClientManager.h"

#include "ShareManager.h"
#include "SearchManager.h"
#include "ConnectionManager.h"
#include "CryptoManager.h"
#include "FavoriteManager.h"
#include "SimpleXML.h"
#include "UserCommand.h"
#include "ResourceManager.h"
#include "LogManager.h"
#include "SearchResult.h"

#include "AdcHub.h"
#include "NmdcHub.h"

#include "QueueManager.h"
#include "FinishedManager.h"



namespace dcpp {

Client* ClientManager::getClient(const string& aHubURL) {
	Client* c;
	if(strnicmp("adc://", aHubURL.c_str(), 6) == 0) {
		c = new AdcHub(aHubURL, false);
	} else if(strnicmp("adcs://", aHubURL.c_str(), 7) == 0) {
		c = new AdcHub(aHubURL, true);
	} else if(strnicmp("nmdcs://", aHubURL.c_str(), 8) == 0) {
		c = new NmdcHub(aHubURL, true);
	} else {
		c = new NmdcHub(aHubURL, false);
	}

	{
		Lock l(cs);
		clients.insert(make_pair(const_cast<string*>(&c->getHubUrl()), c));
	}

	c->addListener(this);

	return c;
}

void ClientManager::putClient(Client* aClient) {
	fire(ClientManagerListener::ClientDisconnected(), aClient);
	aClient->removeListeners();

	{
		Lock l(cs);
		clients.erase(const_cast<string*>(&aClient->getHubUrl()));
	}
	aClient->shutdown();
	delete aClient;
}

StringList ClientManager::getHubUrls(const CID& cid, const string& hintUrl) {
	return getHubUrls(cid, hintUrl, FavoriteManager::getInstance()->isPrivate(hintUrl));
}

StringList ClientManager::getHubNames(const CID& cid, const string& hintUrl) {
	return getHubNames(cid, hintUrl, FavoriteManager::getInstance()->isPrivate(hintUrl));
}

StringList ClientManager::getNicks(const CID& cid, const string& hintUrl) {
	return getNicks(cid, hintUrl, FavoriteManager::getInstance()->isPrivate(hintUrl));
}

StringList ClientManager::getHubUrls(const CID& cid, const string& hintUrl, bool priv) const {
	Lock l(cs);
	StringList lst;
	if(!priv) {
		OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&cid));
		for(OnlineIterC i = op.first; i != op.second; ++i) {
			lst.push_back(i->second->getClientBase().getHubUrl());
		}
	} else {
		OnlineUser* u = findOnlineUserHint(cid, hintUrl);
		if(u)
			lst.push_back(u->getClientBase().getHubUrl());
	}
	return lst;
}

StringList ClientManager::getHubNames(const CID& cid, const string& hintUrl, bool priv) const {
	Lock l(cs);
	StringList lst;
	if(!priv) {
		OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&cid));
		for(OnlineIterC i = op.first; i != op.second; ++i) {
			lst.push_back(i->second->getClientBase().getHubName());		
		}
	} else {
		OnlineUser* u = findOnlineUserHint(cid, hintUrl);
		if(u)
			lst.push_back(u->getClientBase().getHubName());
	}
	return lst;
}

StringList ClientManager::getNicks(const CID& cid, const string& hintUrl, bool priv) const {
	Lock l(cs);
	StringSet ret;

	if(!priv) {
		OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&cid));
		for(OnlineIterC i = op.first; i != op.second; ++i) {
			ret.insert(i->second->getIdentity().getNick());
		}
	} else {
		OnlineUser* u = findOnlineUserHint(cid, hintUrl);
		if(u)
			ret.insert(u->getIdentity().getNick());
	}

	if(ret.empty()) {
		// offline
		NickMap::const_iterator i = nicks.find(const_cast<CID*>(&cid));
		if(i != nicks.end()) {
			ret.insert(i->second);
		} else {
			ret.insert('{' + cid.toBase32() + '}');
		}
	}

	return StringList(ret.begin(), ret.end());
}

string ClientManager::getField(const CID& cid, const string& hint, const char* field) const {
	Lock l(cs);

	OnlinePairC p;
	auto u = findOnlineUserHint(cid, hint, p);
	if(u) {
		auto value = u->getIdentity().get(field);
		if(!value.empty()) {
			return value;
		}
	}

	for(auto i = p.first; i != p.second; ++i) {
		auto value = i->second->getIdentity().get(field);
		if(!value.empty()) {
			return value;
		}
	}

	return Util::emptyString;
}

string ClientManager::getConnection(const CID& cid) const {
	Lock l(cs);
	OnlineIterC i = onlineUsers.find(const_cast<CID*>(&cid));
	if(i != onlineUsers.end()) {
		if(i->second->getIdentity().get("US").empty())
			return i->second->getIdentity().getConnection();
		else
			return Util::formatBytes(i->second->getIdentity().get("US")) + "/s";
		
	}
	return STRING(OFFLINE);
}

string ClientManager::getDLSpeed(const CID& cid) const {
	Lock l(cs);
	OnlineIterC i = onlineUsers.find(const_cast<CID*>(&cid));
	if(i != onlineUsers.end()) {
		return Util::formatBytes(i->second->getIdentity().get("DS")) + "/s";
	}
	return STRING(OFFLINE);
}

uint8_t ClientManager::getSlots(const CID& cid) const
{
	Lock l(cs);
	OnlineIterC i = onlineUsers.find(const_cast<CID*>(&cid));
	if(i != onlineUsers.end()) {
		return static_cast<uint8_t>(Util::toInt(i->second->getIdentity().get("SL")));
	}
	return 0;
}

bool ClientManager::isConnected(const string& aUrl) const {
	Lock l(cs);

	Client::Iter i = clients.find(const_cast<string*>(&aUrl));
	return i != clients.end();
}

string ClientManager::findHub(const string& ipPort) const {
	Lock l(cs);

	string ip;
	uint16_t port = 411;
	string::size_type i = ipPort.find(':');
	if(i == string::npos) {
		ip = ipPort;
	} else {
		ip = ipPort.substr(0, i);
		port = static_cast<uint16_t>(Util::toInt(ipPort.substr(i+1)));
	}

	string url;
	for(Client::Iter i = clients.begin(); i != clients.end(); ++i) {
		const Client* c = i->second;
		if(c->getIp() == ip) {
			// If exact match is found, return it
			if(c->getPort() == port)
				return c->getHubUrl();

			// Port is not always correct, so use this as a best guess...
			url = c->getHubUrl();
		}
	}

	return url;
}

const string& ClientManager::findHubEncoding(const string& aUrl) const {
	Lock l(cs);

	Client::Iter i = clients.find(const_cast<string*>(&aUrl));
	if(i != clients.end()) {
		return i->second->getEncoding();
	}
	return Text::systemCharset;
}

UserPtr ClientManager::findLegacyUser(const string& aNick) const noexcept {
	if (aNick.empty())
		return UserPtr();

	Lock l(cs);

	// this be slower now, but it's not called so often
	for(NickMap::const_iterator i = nicks.begin(); i != nicks.end(); ++i) {
		if(stricmp(i->second, aNick) == 0) {
			UserMap::const_iterator u = users.find(i->first);
			if(u != users.end() && u->second->getCID() == *i->first)
				return u->second;
		}
	}
	return UserPtr();
}

UserPtr ClientManager::getUser(const string& aNick, const string& aHubUrl) noexcept {
	CID cid = makeCid(aNick, aHubUrl);
	Lock l(cs);

	UserMap::const_iterator ui = users.find(const_cast<CID*>(&cid));
	if(ui != users.end()) {
		ui->second->setFlag(User::NMDC);
		return ui->second;
	}

	UserPtr p(new User(cid));
	p->setFlag(User::NMDC);
	users.insert(make_pair(const_cast<CID*>(&p->getCID()), p));

	return p;
}

UserPtr ClientManager::getUser(const CID& cid) noexcept {
	Lock l(cs);
	UserMap::const_iterator ui = users.find(const_cast<CID*>(&cid));
	if(ui != users.end()) {
		return ui->second;
	}

	UserPtr p(new User(cid));
	users.insert(make_pair(const_cast<CID*>(&p->getCID()), p));
	return p;
}

UserPtr ClientManager::findUser(const CID& cid) const noexcept {
	Lock l(cs);
	UserMap::const_iterator ui = users.find(const_cast<CID*>(&cid));
	if(ui != users.end()) {
		return ui->second;
	}
	return 0;
}

// deprecated
bool ClientManager::isOp(const UserPtr& user, const string& aHubUrl) const {
	Lock l(cs);
	OnlinePairC p = onlineUsers.equal_range(const_cast<CID*>(&user->getCID()));
	for(OnlineIterC i = p.first; i != p.second; ++i) {
		if(i->second->getClient().getHubUrl() == aHubUrl) {
			return i->second->getIdentity().isOp();
		}
	}
	return false;
}

bool ClientManager::isStealth(const string& aHubUrl) const {
	Lock l(cs);
	Client::Iter i = clients.find(const_cast<string*>(&aHubUrl));
	if(i != clients.end()) {
		return i->second->getStealth();
	}
	return false;
}

CID ClientManager::makeCid(const string& aNick, const string& aHubUrl) const noexcept {
	string n = Text::toLower(aNick);
	TigerHash th;
	th.update(n.c_str(), n.length());
	th.update(Text::toLower(aHubUrl).c_str(), aHubUrl.length());
	// Construct hybrid CID from the bits of the tiger hash - should be
	// fairly random, and hopefully low-collision
	return CID(th.finalize());
}

void ClientManager::putOnline(OnlineUser* ou) noexcept {
	{
		Lock l(cs);
		onlineUsers.insert(make_pair(const_cast<CID*>(&ou->getUser()->getCID()), ou));
	}
	
	if(!ou->getUser()->isOnline()) {
		ou->getUser()->setFlag(User::ONLINE);
		fire(ClientManagerListener::UserConnected(), ou->getUser());
	}
}

void ClientManager::putOffline(OnlineUser* ou, bool disconnect) noexcept {
	bool lastUser = false;
	{
		Lock l(cs);
		OnlinePair op = onlineUsers.equal_range(const_cast<CID*>(&ou->getUser()->getCID()));
		dcassert(op.first != op.second);
		for(OnlineIter i = op.first; i != op.second; ++i) {
			OnlineUser* ou2 = i->second;
			if(ou == ou2) {
				lastUser = (distance(op.first, op.second) == 1);
				onlineUsers.erase(i);
				break;
			}
		}
	}

	if(lastUser) {
		UserPtr& u = ou->getUser();
		u->unsetFlag(User::ONLINE);
		updateNick(*ou);
		if(disconnect)
			ConnectionManager::getInstance()->disconnect(u);
		fire(ClientManagerListener::UserDisconnected(), u);
	}
}

OnlineUser* ClientManager::findOnlineUserHint(const CID& cid, const string& hintUrl, OnlinePairC& p) const {
	Lock l(cs);
	p = onlineUsers.equal_range(const_cast<CID*>(&cid));
	if(p.first == p.second) // no user found with the given CID.
		return 0;

	if(!hintUrl.empty()) {
		for(auto i = p.first; i != p.second; ++i) {
			OnlineUser* u = i->second;
			if(u->getClientBase().getHubUrl() == hintUrl) {
				return u;
			}
		}
	}

	return 0;
}

OnlineUser* ClientManager::findOnlineUser(const HintedUser& user, bool priv) const {
	return findOnlineUser(user.user->getCID(), user.hint, priv);
}

OnlineUser* ClientManager::findOnlineUser(const CID& cid, const string& hintUrl, bool priv) const {
	OnlinePairC p;
	OnlineUser* u = findOnlineUserHint(cid, hintUrl, p);
	if(u) // found an exact match (CID + hint).
		return u;

	if(p.first == p.second) // no user found with the given CID.
		return 0;

	// if the hint hub is private, don't allow connecting to the same user from another hub.
	if(priv)
		return 0;

	// ok, hub not private, return a random user that matches the given CID but not the hint.
	return p.first->second;
}

void ClientManager::connect(const HintedUser& user, const string& token) {
	bool priv = FavoriteManager::getInstance()->isPrivate(user.hint);

	Lock l(cs);
	OnlineUser* u = findOnlineUser(user, priv);

	if(u) {
		u->getClientBase().connect(*u, token);
	}
}

void ClientManager::privateMessage(const HintedUser& user, const string& msg, bool thirdPerson) {
	bool priv = FavoriteManager::getInstance()->isPrivate(user.hint);

	Lock l(cs);
	OnlineUser* u = findOnlineUser(user, priv);
	
	if(u) {
		u->getClientBase().privateMessage(u, msg, thirdPerson);
	}
}

void ClientManager::userCommand(const HintedUser& user, const UserCommand& uc, ParamMap& params, bool compatibility) {
	Lock l(cs);
	/** @todo we allow wrong hints for now ("false" param of findOnlineUser) because users
	 * extracted from search results don't always have a correct hint; see
	 * SearchManager::onRES(const AdcCommand& cmd, ...). when that is done, and SearchResults are
	 * switched to storing only reliable HintedUsers (found with the token of the ADC command),
	 * change this call to findOnlineUserHint. */
	OnlineUser* ou = findOnlineUser(user.user->getCID(), user.hint.empty() ? uc.getHub() : user.hint, false);
	if(!ou)
		return;

	ou->getIdentity().getParams(params, "user", compatibility);
	ou->getClient().getHubIdentity().getParams(params, "hub", false);
	ou->getClient().getMyIdentity().getParams(params, "my", compatibility);
	ou->getClient().sendUserCmd(uc, params);
}

bool ClientManager::send(AdcCommand& cmd, const CID& cid, bool noCID /*false*/, bool noPassive /*false*/) {
	Lock l(cs);
	OnlineIterC i = onlineUsers.find(const_cast<CID*>(&cid));
	if(i != onlineUsers.end()) {
		OnlineUser& u = *i->second;
		if(cmd.getType() == AdcCommand::TYPE_UDP && !u.getIdentity().isUdpActive()) {
			if(u.getUser()->isNMDC() || noPassive)
				return false;
			cmd.setType(AdcCommand::TYPE_DIRECT);
			cmd.setTo(u.getIdentity().getSID());
			u.getClient().send(cmd);
		} else {
			try {
				Socket udp;
				if (noCID) {
					udp.writeTo(u.getIdentity().getIp(), static_cast<uint16_t>(Util::toInt(u.getIdentity().getUdpPort())), cmd.toString());
					//LogManager::getInstance()->message("NOCID SENT: " + cmd.toString());
				} else {
					udp.writeTo(u.getIdentity().getIp(), static_cast<uint16_t>(Util::toInt(u.getIdentity().getUdpPort())), cmd.toString(getMe()->getCID()));
				}
			} catch(const SocketException&) {
				dcdebug("Socket exception sending ADC UDP command\n");
			}
		}
		return true;
	}
	return false;
}

void ClientManager::infoUpdated() {
	Lock l(cs);
	for(auto i = clients.begin(); i != clients.end(); ++i) {
		Client* c = i->second;
		if(c->isConnected()) {
			c->info(false);
		}
	}
}

void ClientManager::on(NmdcSearch, Client* aClient, const string& aSeeker, int aSearchType, int64_t aSize, 
									int aFileType, const string& aString, bool isPassive) noexcept
{
	Speaker<ClientManagerListener>::fire(ClientManagerListener::IncomingSearch(), aString);

	SearchResultList l;
	ShareManager::getInstance()->search(l, aString, aSearchType, aSize, aFileType, aClient, isPassive ? 5 : 10);
	if(l.size() > 0) {
		if(isPassive) {
			string name = aSeeker.substr(4);
			// Good, we have a passive seeker, those are easier...
			string str;
			for(SearchResultList::const_iterator i = l.begin(); i != l.end(); ++i) {
				const SearchResultPtr& sr = *i;
				str += sr->toSR(*aClient);
				str[str.length()-1] = 5;
				str += Text::fromUtf8(name, aClient->getEncoding());
				str += '|';
			}
			
			if(str.size() > 0)
				aClient->send(str);
			
		} else {
			try {
				Socket udp;
				string ip, file, proto, query, fragment, port;

				Util::decodeUrl(aSeeker, proto, ip, port, file, query, fragment);
				ip = Socket::resolve(ip);
				
				if(port == "0") 
					port = "412";
				for(SearchResultList::const_iterator i = l.begin(); i != l.end(); ++i) {
					const SearchResultPtr& sr = *i;
					udp.writeTo(ip, Util::toInt(port), sr->toSR(*aClient));
				}
			} catch(...) {
				dcdebug("Search caught error\n");
			}
		}
	} else if(!isPassive && (aFileType == SearchManager::TYPE_TTH) && (aString.compare(0, 4, "TTH:") == 0)) {
		if(SETTING(EXTRA_PARTIAL_SLOTS) == 0) //disable partial uploads by setting 0
			return;

		PartsInfo partialInfo;
		string bundle;
		bool add=false, reply=false;
		TTHValue aTTH(aString.substr(4));
		if(!QueueManager::getInstance()->handlePartialSearch(NULL, aTTH, partialInfo, bundle, reply, add)) {
			return;
		}
		
		string ip, file, proto, query, fragment, port;
		Util::decodeUrl(aSeeker, proto, ip, port, file, query, fragment);
		
		try {
			AdcCommand cmd = SearchManager::getInstance()->toPSR(true, aClient->getMyNick(), aClient->getIpPort(), aTTH.toBase32(), partialInfo);
			Socket s;
			s.writeTo(Socket::resolve(ip), Util::toInt(port), cmd.toString(ClientManager::getInstance()->getMe()->getCID()));
		} catch(...) {
			dcdebug("Partial search caught error\n");		
		}
	}
}

void ClientManager::on(AdcSearch, const Client* c, const AdcCommand& adc, const CID& from) noexcept {
	bool isUdpActive = false;
	{
		Lock l(cs);
		
		OnlinePairC op = onlineUsers.equal_range(const_cast<CID*>(&from));
		for(OnlineIterC i = op.first; i != op.second; ++i) {
			const OnlineUserPtr& u = i->second;
			if(&u->getClient() == c)
			{
				isUdpActive = u->getIdentity().isUdpActive();
				break;
			}
		}			
	}
	SearchManager::getInstance()->respond(adc, from, isUdpActive, c->getIpPort());
}

void ClientManager::search(int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken, Search::searchType sType, void* aOwner) {
	Lock l(cs);

	for(auto i = clients.begin(); i != clients.end(); ++i) {
		Client* c = i->second;
		if(c->isConnected()) {
			c->search(aSizeMode, aSize, aFileType, aString, aToken, StringList() /*ExtList*/, sType, aOwner);
		}
	}
}

uint64_t ClientManager::search(StringList& who, int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken, const StringList& aExtList, Search::searchType sType, void* aOwner) {
	Lock l(cs);

	uint64_t estimateSearchSpan = 0;
	
	for(StringIter it = who.begin(); it != who.end(); ++it) {
		const string& client = *it;
		
		Client::Iter i = clients.find(const_cast<string*>(&client));
		if(i != clients.end() && i->second->isConnected()) {
			uint64_t ret = i->second->search(aSizeMode, aSize, aFileType, aString, aToken, aExtList, sType, aOwner);
			estimateSearchSpan = max(estimateSearchSpan, ret);			
		}
	}
	
	return estimateSearchSpan;
}

void ClientManager::on(TimerManagerListener::Minute, uint64_t /*aTick*/) noexcept {
	Lock l(cs);

	// Collect some garbage...
	UserIter i = users.begin();
	while(i != users.end()) {
		if(i->second->unique()) {
			NickMap::iterator n = nicks.find(const_cast<CID*>(&i->second->getCID()));
			if(n != nicks.end()) nicks.erase(n);
			users.erase(i++);
		} else {
			++i;
		}
	}

	for(auto j = clients.begin(); j != clients.end(); ++j) {
		j->second->info(false);
	}
}

UserPtr& ClientManager::getMe() {
	if(!me) {
		Lock l(cs);
		if(!me) {
			me = new User(getMyCID());
			users.insert(make_pair(const_cast<CID*>(&me->getCID()), me));
		}
	}
	return me;
}

const CID& ClientManager::getMyPID() {
	if(pid.isZero())
		pid = CID(SETTING(PRIVATE_ID));
	return pid;
}

CID ClientManager::getMyCID() {
	TigerHash tiger;
	tiger.update(getMyPID().data(), CID::SIZE);
	return CID(tiger.finalize());
}

void ClientManager::updateNick(const OnlineUser& user) noexcept {
	updateNick(user.getUser(), user.getIdentity().getNick());
}

void ClientManager::updateNick(const UserPtr& user, const string& nick) noexcept {
	if(!nick.empty()) {
		Lock l(cs);
		NickMap::iterator i = nicks.find(const_cast<CID*>(&user->getCID()));
		if(i == nicks.end()) {
			nicks[const_cast<CID*>(&user->getCID())] = nick;
		} else {
			i->second = nick;
		}
	}
}

string ClientManager::getMyNick(const string& hubUrl) const {
	Lock l(cs);
	Client::Iter i = clients.find(const_cast<string*>(&hubUrl));
	if(i != clients.end()) {
		return i->second->getMyIdentity().getNick();
	}
	return Util::emptyString;
}
	
int ClientManager::getMode(const string& aHubUrl) const {
	
	if(aHubUrl.empty()) 
		return SETTING(INCOMING_CONNECTIONS);

	int mode = 0;
	const FavoriteHubEntry* hub = FavoriteManager::getInstance()->getFavoriteHubEntry(aHubUrl);
	if(hub) {
		switch(hub->getMode()) {
			case 1 :
				mode = SettingsManager::INCOMING_DIRECT;
				break;
			case 2 :
				mode = SettingsManager::INCOMING_FIREWALL_PASSIVE;
				break;
			default:
				mode = SETTING(INCOMING_CONNECTIONS);
		}
	} else {
		mode = SETTING(INCOMING_CONNECTIONS);
	}
	return mode;
}

void ClientManager::cancelSearch(void* aOwner) {
	Lock l(cs);

	for(Client::Iter i = clients.begin(); i != clients.end(); ++i) {
		i->second->cancelSearch(aOwner);
	}
}


void ClientManager::on(Connected, const Client* c) noexcept {
	fire(ClientManagerListener::ClientConnected(), c);
}

void ClientManager::on(UserUpdated, const Client*, const OnlineUserPtr& user) noexcept {
	fire(ClientManagerListener::UserUpdated(), *user);
}

void ClientManager::on(UsersUpdated, const Client*, const OnlineUserList& l) noexcept {
	for(OnlineUserList::const_iterator i = l.begin(), iend = l.end(); i != iend; ++i) {
		updateNick(*(*i));
		fire(ClientManagerListener::UserUpdated(), *(*i)); 
	}
}

void ClientManager::on(HubUpdated, const Client* c) noexcept {
	fire(ClientManagerListener::ClientUpdated(), c);
}

void ClientManager::on(Failed, const Client* client, const string&) noexcept {
	fire(ClientManagerListener::ClientDisconnected(), client);
}

void ClientManager::on(HubUserCommand, const Client* client, int aType, int ctx, const string& name, const string& command) noexcept {
	if(BOOLSETTING(HUB_USER_COMMANDS)) {
		if(aType == UserCommand::TYPE_REMOVE) {
			int cmd = FavoriteManager::getInstance()->findUserCommand(name, client->getHubUrl());
			if(cmd != -1)
				FavoriteManager::getInstance()->removeUserCommand(cmd);
		} else if(aType == UserCommand::TYPE_CLEAR) {
 			FavoriteManager::getInstance()->removeHubUserCommands(ctx, client->getHubUrl());
 		} else {
			FavoriteManager::getInstance()->addUserCommand(aType, ctx, UserCommand::FLAG_NOSAVE, name, command, "", client->getHubUrl());
		}
	}
}

} // namespace dcpp

/**
 * @file
 * $Id: ClientManager.cpp 568 2011-07-24 18:28:43Z bigmuscle $
 */
