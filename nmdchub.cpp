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

#include "stdinc.h"
#include "NmdcHub.h"

#include "ConnectivityManager.h"
#include "ResourceManager.h"
#include "ChatMessage.h"
#include "ClientManager.h"
#include "SearchManager.h"
#include "ShareManager.h"
#include "CryptoManager.h"
#include "ConnectionManager.h"
#include "version.h"

#include "Socket.h"
#include "UserCommand.h"
#include "StringTokenizer.h"
#include "DebugManager.h"
#include "QueueManager.h"
#include "ZUtils.h"
#include "ThrottleManager.h"
#include "UploadManager.h"

namespace dcpp {

NmdcHub::NmdcHub(const string& aHubURL) : Client(aHubURL, '|'), supportFlags(0),
	lastBytesShared(0), lastUpdate(0)
{
}

NmdcHub::~NmdcHub() {
	clearUsers();
}

string NmdcHub::toUtf8(const string& str) const { 
	return Text::validateUtf8(str) ? str : Text::toUtf8(str, getEncoding()); 
}

string NmdcHub::fromUtf8(const string& str) const { 
	return Text::fromUtf8(str, getEncoding()); 
}


#define checkstate() if(state != STATE_NORMAL) return

int NmdcHub::connect(const OnlineUser& aUser, const string&, string& /*lastError_*/) {
	if(state == STATE_NORMAL) {
		dcdebug("NmdcHub::connect %s\n", aUser.getIdentity().getNick().c_str());
		if(isActive()) {
			connectToMe(aUser);
		} else {
			revConnectToMe(aUser);
		}
	}

	return AdcCommand::SUCCESS;
}

void NmdcHub::refreshLocalIp() noexcept {
	if((!CONNSETTING(NO_IP_OVERRIDE) || getUserIp4().empty()) && !getMyIdentity().getIp4().empty()) {
		// Best case - the server detected it
		localIp = getMyIdentity().getIp();
	} else {
		localIp.clear();
	}
	if(localIp.empty()) {
		localIp = getUserIp4();
		if(!localIp.empty()) {
			localIp = Socket::resolve(localIp, AF_INET);
		}
		if(localIp.empty()) {
			localIp = sock->getLocalIp();
			if(localIp.empty()) {
				localIp = AirUtil::getLocalIp(false);
			}
		}
	}
}

void NmdcHub::refreshUserList(bool refreshOnly) {
	if(refreshOnly) {
		Lock l(cs);

		OnlineUserList v;
		for(auto n: users | map_values)
			v.push_back(n);

		fire(ClientListener::UsersUpdated(), this, v);
	} else {
		clearUsers();
		getNickList();
	}
}

OnlineUser& NmdcHub::getUser(const string& aNick) {
	OnlineUser* u = NULL;
	{
		Lock l(cs);
		
		NickIter i = users.find(aNick);
		if(i != users.end())
			return *i->second;
	}

	UserPtr p;
	if(aNick == get(Nick)) {
		p = ClientManager::getInstance()->getMe();
	} else {
		p = ClientManager::getInstance()->getUser(aNick, getHubUrl());
	}

	{
		Lock l(cs);
		u = users.insert(make_pair(aNick, new OnlineUser(p, *this, 0))).first->second;
		u->inc();
		u->getIdentity().setNick(aNick);
		if(u->getUser() == getMyIdentity().getUser()) {
			setMyIdentity(u->getIdentity());
			u->getIdentity().setConnectMode(isActive() ? Identity::MODE_ACTIVE_V4 : Identity::MODE_PASSIVE_V4);
		}
	}
	
	ClientManager::getInstance()->putOnline(u);
	return *u;
}

void NmdcHub::supports(const StringList& feat) { 
	string x;
	for(const auto& f: feat)
		x+= f + ' ';

	send("$Supports " + x + '|');
}

OnlineUserPtr NmdcHub::findUser(const string& aNick) const {
	Lock l(cs);
	NickIter i = users.find(aNick);
	return i == users.end() ? NULL : i->second;
}

void NmdcHub::putUser(const string& aNick) {
	OnlineUser* ou = NULL;
	{
		Lock l(cs);
		NickIter i = users.find(aNick);
		if(i == users.end())
		return;
		ou = i->second;
		users.erase(i);

		availableBytes -= ou->getIdentity().getBytesShared();
	}
	ClientManager::getInstance()->putOffline(ou);
	ou->dec();
}

void NmdcHub::clearUsers() {
	NickMap u2;

	{
		Lock l(cs);
		u2.swap(users);
		availableBytes = 0;
	}

	for(auto ou: u2 | map_values) {
		ClientManager::getInstance()->putOffline(ou);
		ou->dec();
	}
}

void NmdcHub::updateFromTag(Identity& id, const string& tag) {
	StringTokenizer<string> tok(tag, ',');
	string::size_type j;
	id.set("US", Util::emptyString);
	if(tag.find("AirDC++") != string::npos)
		id.getUser()->setFlag(User::AIRDCPLUSPLUS);

	for(auto& i: tok.getTokens()) {
		if(i.length() < 2)
			continue;

		if(i.compare(0, 2, "H:") == 0) {
			StringTokenizer<string> t(i.substr(2), '/');
			if(t.getTokens().size() != 3)
				continue;
			id.set("HN", t.getTokens()[0]);
			id.set("HR", t.getTokens()[1]);
			id.set("HO", t.getTokens()[2]);
		} else if(i.compare(0, 2, "S:") == 0) {
			id.set("SL", i.substr(2));		
		} else if((j = i.find("V:")) != string::npos) {
			if (j > 2)
				id.set("AP", i.substr(0, j-1));
			i.erase(i.begin(), i.begin() + j + 2);
			id.set("VE", i);
		} else if(i.compare(0, 2, "M:") == 0) {
			if(i.size() == 3) {
				if(i[2] == 'A')
					id.getUser()->unsetFlag(User::PASSIVE);
				else
					id.getUser()->setFlag(User::PASSIVE);
			}
		} else if((j = i.find("L:")) != string::npos) {
			i.erase(i.begin() + j, i.begin() + j + 2);
			id.set("US", Util::toString(Util::toInt(i) * 1024));
		}
	}
	/// @todo Think about this
	id.set("TA", '<' + tag + '>');
}

void NmdcHub::onLine(const string& aLine) noexcept {
	if(aLine.length() == 0)
		return;

	if(aLine[0] != '$') {
		if (SETTING(SUPPRESS_MAIN_CHAT)) return;

		// Check if we're being banned...
		if(state != STATE_NORMAL) {
			if(Util::findSubString(aLine, "banned") != string::npos) {
				setAutoReconnect(false);
			}
		}
		string line = toUtf8(aLine);
		if(line[0] != '<') {
			fire(ClientListener::StatusMessage(), this, unescape(line));
			return;
		}
		string::size_type i = line.find('>', 2);
		if(i == string::npos) {
			fire(ClientListener::StatusMessage(), this, unescape(line));
			return;
		}
		string nick = line.substr(1, i-1);
		string message;
		if((line.length()-1) > i) {
			message = line.substr(i+2);
		} else {
			fire(ClientListener::StatusMessage(), this, unescape(line));
			return;
		}

		if((line.find("Hub-Security") != string::npos) && (line.find("was kicked by") != string::npos)) {
			fire(ClientListener::StatusMessage(), this, unescape(line), ClientListener::FLAG_IS_SPAM);
			return;
		} else if((line.find("is kicking") != string::npos) && (line.find("because:") != string::npos)) {
			fire(ClientListener::StatusMessage(), this, unescape(line), ClientListener::FLAG_IS_SPAM);
			return;
		}

		ChatMessage chatMessage = { unescape(message), findUser(nick) };

		if(!chatMessage.from) {
			OnlineUserPtr o = &getUser(nick);
			// Assume that messages from unknown users come from the hub
			o->getIdentity().setHub(true);
			o->getIdentity().setHidden(true);
			fire(ClientListener::UserUpdated(), this, o);

			chatMessage.from = o;
		}

		if (Util::strnicmp(chatMessage.text, "/me ", 4) == 0) {
			chatMessage.thirdPerson = true;
			chatMessage.text = chatMessage.text.substr(4);
		}

		fire(ClientListener::Message(), this, chatMessage);
		return;
    }

 	string cmd;
	string param;
	string::size_type x;
	
	if( (x = aLine.find(' ')) == string::npos) {
		cmd = aLine.substr(1);
	} else {
		cmd = aLine.substr(1, x - 1);
		param = toUtf8(aLine.substr(x+1));
	}

	if(cmd == "Search") {
		if((state != STATE_NORMAL) /*|| getShareProfile() == SP_HIDDEN*/) {
			return;
		}
		string::size_type i = 0;
		string::size_type j = param.find(' ', i);
		if(j == string::npos || i == j)
			return;
		
		string seeker = param.substr(i, j-i);

		bool isPassive = (seeker.compare(0, 4, "Hub:") == 0);
		bool meActive = isActive();

		// Filter own searches
		if(meActive && isPassive == false) {
			if(seeker == (localIp + ":" + SearchManager::getInstance()->getPort())) {
				return;
			}
		} else {
			// Hub:seeker
			if(Util::stricmp(seeker.c_str() + 4, getMyNick().c_str()) == 0) {
				return;
			}
		}

		i = j + 1;
		
		uint64_t tick = GET_TICK();
		clearFlooders(tick);

		seekers.push_back(make_pair(seeker, tick));

		// First, check if it's a flooder
		for(const auto& f: flooders) {
			if(f.first == seeker) {
				return;
			}
		}

		int count = 0;
		for(auto& f: seekers) {
			if(f.first == seeker)
				count++;

			if(count > 7) {
			    if(isOp()) {
					if(isPassive)
						fire(ClientListener::SearchFlood(), this, seeker.substr(4));
					else
						fire(ClientListener::SearchFlood(), this, seeker + " " + STRING(NICK_UNKNOWN));
				}
				
				flooders.push_back(make_pair(seeker, tick));
				return;
			}
		}

		int a;
		if(param[i] == 'F') {
			a = SearchManager::SIZE_DONTCARE;
		} else if(param[i+2] == 'F') {
			a = SearchManager::SIZE_ATLEAST;
		} else {
			a = SearchManager::SIZE_ATMOST;
		}
		i += 4;
		j = param.find('?', i);
		if(j == string::npos || i == j)
			return;
		string size = param.substr(i, j-i);
		i = j + 1;
		j = param.find('?', i);
		if(j == string::npos || i == j)
			return;
		int type = Util::toInt(param.substr(i, j-i)) - 1;
		i = j + 1;
		string terms = unescape(param.substr(i));

		if(terms.size() > 0) {
			if(isPassive) {
				OnlineUserPtr u = findUser(seeker.substr(4));

				if(u == NULL) {
					return;
				}

				if(!u->getUser()->isSet(User::PASSIVE)) {
					u->getUser()->setFlag(User::PASSIVE);
					updated(u);
				}

				// ignore if we or remote client don't support NAT traversal in passive mode
				// although many NMDC hubs won't send us passive if we're in passive too, so just in case...
				if(!meActive && !u->getUser()->isSet(User::NAT_TRAVERSAL))
					return;
			}

			fire(ClientListener::NmdcSearch(), this, seeker, a, Util::toInt64(size), type, terms, isPassive);
		}
	} else if(cmd == "MyINFO") {
		string::size_type i, j;
		i = 5;
		j = param.find(' ', i);
		if( (j == string::npos) || (j == i) )
			return;
		string nick = param.substr(i, j-i);
		
		if(nick.empty())
			return;

		i = j + 1;

		OnlineUser& u = getUser(nick);

		// If he is already considered to be the hub (thus hidden), probably should appear in the UserList
		if(u.getIdentity().isHidden()) {
			u.getIdentity().setHidden(false);
			u.getIdentity().setHub(false);
		}

		j = param.find('$', i);
		if(j == string::npos)
			return;

		string tmpDesc = unescape(param.substr(i, j-i));
		// Look for a tag...
		if(tmpDesc.size() > 0 && tmpDesc[tmpDesc.size()-1] == '>') {
			x = tmpDesc.rfind('<');
			if(x != string::npos) {
				// Hm, we have something...disassemble it...
				updateFromTag(u.getIdentity(), tmpDesc.substr(x + 1, tmpDesc.length() - x - 2));
				tmpDesc.erase(x);
			}
		}
		u.getIdentity().setDescription(tmpDesc);

		i = j + 3;
		j = param.find('$', i);
		if(j == string::npos)
			return;

		string connection = (i == j) ? Util::emptyString : param.substr(i, j-i-1);
		
		 if(connection.empty()) { 	 
	        // No connection = bot... 	 VERY unreliable but... 
			 //Users cant understand why it sends away messages to bots/opchats so...
	           u.getUser()->setFlag(User::BOT); 	 
	           u.getIdentity().setBot(true); 	 
	             } else { 	 
	           u.getUser()->unsetFlag(User::BOT); 	 
	           u.getIdentity().setBot(false); 	 
	           }


		u.getIdentity().setHub(false);
		u.getIdentity().setHidden(false);

		u.getIdentity().setNmdcConnection(connection);
		u.getIdentity().setStatus(Util::toString(param[j-1]));
		
		
		if(u.getIdentity().getStatus() & Identity::TLS) {
			u.getUser()->setFlag(User::TLS);
		} else {
			u.getUser()->unsetFlag(User::TLS);
		}

		if((u.getIdentity().getStatus() & Identity::AIRDC) && !u.getUser()->isSet(User::AIRDCPLUSPLUS))
			u.getUser()->setFlag(User::AIRDCPLUSPLUS); //if we have a tag its already set.


		if(u.getIdentity().getStatus() & Identity::NAT) {
			u.getUser()->setFlag(User::NAT_TRAVERSAL);
		} else {
			u.getUser()->unsetFlag(User::NAT_TRAVERSAL);
		}

		i = j + 1;
		j = param.find('$', i);

		if(j == string::npos)
			return;

		u.getIdentity().setEmail(unescape(param.substr(i, j-i)));

		i = j + 1;
		j = param.find('$', i);
		if(j == string::npos)
			return;

		availableBytes -= u.getIdentity().getBytesShared();
		u.getIdentity().setBytesShared(param.substr(i, j-i));
		availableBytes += u.getIdentity().getBytesShared();

		if(u.getUser() == getMyIdentity().getUser()) {
			setMyIdentity(u.getIdentity());
		}
		
		fire(ClientListener::UserUpdated(), this, &u);
	} else if(cmd == "Quit") {
		if(!param.empty()) {
			const string& nick = param;
			OnlineUserPtr u = findUser(nick);
			if(!u)
				return;

			fire(ClientListener::UserRemoved(), this, u);

			putUser(nick);
		}
	} else if(cmd == "ConnectToMe") {
		if(state != STATE_NORMAL) {
			return;
		}
		string::size_type i = param.find(' ');
		string::size_type j;
		if( (i == string::npos) || ((i + 1) >= param.size()) ) {
			return;
		}
		i++;
		j = param.find(':', i);
		if(j == string::npos) {
			return;
		}
		string server = param.substr(i, j-i);
		if(j+1 >= param.size()) {
			return;
		}
		
		string senderNick;
		string port;

		i = param.find(' ', j+1);
		if(i == string::npos) {
			port = param.substr(j+1);
		} else {
			senderNick = param.substr(i+1);
			port = param.substr(j+1, i-j-1);
		}

		bool secure = false;
		if(port[port.size() - 1] == 'S') {
			port.erase(port.size() - 1);
			if(CryptoManager::getInstance()->TLSOk()) {
				secure = true;
			}
		}

		if(port[port.size() - 1] == 'N') {
			if(senderNick.empty())
				return;

			port.erase(port.size() - 1);

			// Trigger connection attempt sequence locally ...
			ConnectionManager::getInstance()->nmdcConnect(server, port, Util::toString(sock->getLocalPort()), 
				BufferedSocket::NAT_CLIENT, getMyNick(), getHubUrl(), getEncoding(), getStealth(), secure && !getStealth());

			// ... and signal other client to do likewise.
			send("$ConnectToMe " + senderNick + " " + localIp + ":" + Util::toString(sock->getLocalPort()) + (secure ? "RS" : "R") + "|");
			return;
		} else if(port[port.size() - 1] == 'R') {
			port.erase(port.size() - 1);
				
			// Trigger connection attempt sequence locally
			ConnectionManager::getInstance()->nmdcConnect(server, port, Util::toString(sock->getLocalPort()), 
				BufferedSocket::NAT_SERVER, getMyNick(), getHubUrl(), getEncoding(), getStealth(), secure);
			return;
		}
		
		if(port.empty())
			return;
			
		// For simplicity, we make the assumption that users on a hub have the same character encoding
		ConnectionManager::getInstance()->nmdcConnect(server, port, getMyNick(), getHubUrl(), getEncoding(), getStealth(), secure);
	} else if(cmd == "RevConnectToMe") {
		if(state != STATE_NORMAL) {
			return;
		}

		string::size_type j = param.find(' ');
		if(j == string::npos) {
			return;
		}

		OnlineUserPtr u = findUser(param.substr(0, j));
		if(u == NULL)
			return;

		if(isActive()) {
			connectToMe(*u);
		} else if(u->getIdentity().getStatus() & Identity::NAT) {
			bool secure = CryptoManager::getInstance()->TLSOk() && u->getUser()->isSet(User::TLS) && !getStealth();
			// NMDC v2.205 supports "$ConnectToMe sender_nick remote_nick ip:port", but many NMDC hubsofts block it
			// sender_nick at the end should work at least in most used hubsofts
			send("$ConnectToMe " + fromUtf8(u->getIdentity().getNick()) + " " + localIp + ":" + Util::toString(sock->getLocalPort()) + (secure ? "NS " : "N ") + fromUtf8(getMyNick()) + "|");
		} else {
			if(!u->getUser()->isSet(User::PASSIVE)) {
				u->getUser()->setFlag(User::PASSIVE);
				// Notify the user that we're passive too...
				revConnectToMe(*u);
				updated(u);

				return;
			}
		}
	} else if(cmd == "SR") {
		SearchManager::getInstance()->onSR(aLine);
	} else if(cmd == "HubName") {


		// Workaround replace newlines in topic with spaces, to avoid funny window titles //ApexDC
		// If " - " found, the first part goes to hub name, rest to description
		// If no " - " found, first word goes to hub name, rest to description

		string::size_type i;
		while((i = param.find("\r\n")) != string::npos)
			param.replace(i, 2, " ");
		i = param.find(" - ");
		if(i == string::npos) {
			i = param.find(' ');
			if(i == string::npos) {
				getHubIdentity().setNick(unescape(param));
				getHubIdentity().setDescription(Util::emptyString);			
			} else {
				getHubIdentity().setNick(unescape(param.substr(0, i)));
				getHubIdentity().setDescription(unescape(param.substr(i+1)));
			}
		} else {
			getHubIdentity().setNick(unescape(param.substr(0, i)));
			getHubIdentity().setDescription(unescape(param.substr(i+3)));
		}
		fire(ClientListener::HubUpdated(), this);

		

	} else if(cmd == "Supports") {
		StringTokenizer<string> st(param, ' ');
		StringList& sl = st.getTokens();
		for(auto& i: sl) {
			if(i == "UserCommand") {
				supportFlags |= SUPPORTS_USERCOMMAND;
			} else if(i == "NoGetINFO") {
				supportFlags |= SUPPORTS_NOGETINFO;
			} else if(i == "UserIP2") {
				supportFlags |= SUPPORTS_USERIP2;
			}
		}
	} else if(cmd == "UserCommand") {
		string::size_type i = 0;
		string::size_type j = param.find(' ');
		if(j == string::npos)
			return;

		int type = Util::toInt(param.substr(0, j));
		i = j+1;
 		if(type == UserCommand::TYPE_SEPARATOR || type == UserCommand::TYPE_CLEAR) {
			int ctx = Util::toInt(param.substr(i));
			fire(ClientListener::HubUserCommand(), this, type, ctx, Util::emptyString, Util::emptyString);
		} else if(type == UserCommand::TYPE_RAW || type == UserCommand::TYPE_RAW_ONCE) {
			j = param.find(' ', i);
			if(j == string::npos)
				return;
			int ctx = Util::toInt(param.substr(i));
			i = j+1;
			j = param.find('$');
			if(j == string::npos)
				return;
			string name = unescape(param.substr(i, j-i));
			// NMDC uses '\' as a separator but both ADC and our internal representation use '/'
			Util::replace("/", "//", name);
			Util::replace("\\", "/", name);
			i = j+1;
			string command = unescape(param.substr(i, param.length() - i));
			fire(ClientListener::HubUserCommand(), this, type, ctx, name, command);
		}
	} else if(cmd == "Lock") {
		if(state != STATE_PROTOCOL || aLine.size() < 6) {
			return;
		}
		state = STATE_IDENTIFY;

		// Param must not be toUtf8'd...
		param = aLine.substr(6);

		if(!param.empty()) {
			string::size_type j = param.find(" Pk=");
			string lock, pk;
			if( j != string::npos ) {
				lock = param.substr(0, j);
				pk = param.substr(j + 4);
			} else {
				// Workaround for faulty linux hubs...
				j = param.find(" ");
				if(j != string::npos)
					lock = param.substr(0, j);
				else
					lock = param;
			}

			if(CryptoManager::getInstance()->isExtended(lock)) {
				StringList feat;
				feat.push_back("UserCommand");
				feat.push_back("NoGetINFO");
				feat.push_back("NoHello");
				feat.push_back("UserIP2");
				feat.push_back("TTHSearch");
				feat.push_back("ZPipe0");
					
				if(CryptoManager::getInstance()->TLSOk() && !getStealth())
					feat.push_back("TLS");
					
				supports(feat);
			}

			key(CryptoManager::getInstance()->makeKey(lock));
			OnlineUser& ou = getUser(get(Nick));
			validateNick(ou.getIdentity().getNick());
		}
	} else if(cmd == "Hello") {
		if(!param.empty()) {
			OnlineUser& u = getUser(param);

			if(u.getUser() == getMyIdentity().getUser()) {
				u.getUser()->setFlag(User::AIRDCPLUSPLUS);
				if(isActive())
					u.getUser()->unsetFlag(User::PASSIVE);
				else
					u.getUser()->setFlag(User::PASSIVE);
			}

			if(state == STATE_IDENTIFY && u.getUser() == getMyIdentity().getUser()) {
				state = STATE_NORMAL;
				updateCounts(false, true);
				fire(ClientListener::HubUpdated(), this);

				version();
				getNickList();
				myInfo(true);
			}

			fire(ClientListener::UserUpdated(), this, &u);
		}
	} else if(cmd == "ForceMove") {
		disconnect(false);
		fire(ClientListener::Redirect(), this, param);
	} else if(cmd == "HubIsFull") {
		fire(ClientListener::HubFull(), this);
	} else if(cmd == "ValidateDenide") {		// Mind the spelling...
		disconnect(false);
		fire(ClientListener::NickTaken(), this);
	} else if(cmd == "UserIP") {
		if(!param.empty()) {
			OnlineUserList v;
			StringTokenizer<string> t(param, "$$");
			StringList& l = t.getTokens();
			for(auto& it: l) {
				string::size_type j = 0;
				if((j = it.find(' ')) == string::npos)
					continue;
				if((j+1) == it.length())
					continue;

				auto u = findUser(it.substr(0, j));

				if(!u)
					continue;

				u->getIdentity().setIp4(it.substr(j+1));
				if(u->getUser() == getMyIdentity().getUser()) {
					setMyIdentity(u->getIdentity());
					refreshLocalIp();
				}
				v.push_back(u);
			}

			updated(v);
		}
	} else if(cmd == "NickList") {
		if(!param.empty()) {
			OnlineUserList v;
			StringTokenizer<string> t(param, "$$");
			StringList& sl = t.getTokens();

			for(StringIter it = sl.begin(); it != sl.end(); ++it) {
				if(it->empty())
					continue;
				
				v.push_back(&getUser(*it));
			}

			if(!(supportFlags & SUPPORTS_NOGETINFO)) {
				string tmp;
				// Let's assume 10 characters per nick...
				tmp.reserve(v.size() * (11 + 10 + getMyNick().length())); 
				string n = ' ' + fromUtf8(getMyNick()) + '|';
				for(OnlineUserList::const_iterator i = v.begin(); i != v.end(); ++i) {
					tmp += "$GetINFO ";
					tmp += fromUtf8((*i)->getIdentity().getNick());
					tmp += n;
				}
				if(!tmp.empty()) {
					send(tmp);
				}
			} 

			fire(ClientListener::UsersUpdated(), this, v);
		}
	} else if(cmd == "OpList") {
		if(!param.empty()) {
			OnlineUserList v;
			StringTokenizer<string> t(param, "$$");
			StringList& sl = t.getTokens();
			for(StringIter it = sl.begin(); it != sl.end(); ++it) {
				if(it->empty())
					continue;
				OnlineUser& ou = getUser(*it);
				ou.getIdentity().setOp(true);
				if(ou.getUser() == getMyIdentity().getUser()) {
					setMyIdentity(ou.getIdentity());
				}
				v.push_back(&ou);
			}

			updateCounts(false, true);
			fire(ClientListener::UsersUpdated(), this, v);

			// Special...to avoid op's complaining that their count is not correctly
			// updated when they log in (they'll be counted as registered first...)
			myInfo(false);
		}
	} else if(cmd == "To:") {
		string::size_type i = param.find("From:");
		if(i == string::npos)
			return;

		i+=6;
		string::size_type j = param.find('$', i);
		if(j == string::npos)
			return;

		string rtNick = param.substr(i, j - 1 - i);
		if(rtNick.empty())
			return;
		i = j + 1;

		if(param.size() < i + 3 || param[i] != '<')
			return;

		j = param.find('>', i);
		if(j == string::npos)
			return;

		string fromNick = param.substr(i+1, j-i-1);
		if(fromNick.empty() || param.size() < j + 2)
			return;

       	ChatMessage message = { unescape(param.substr(j + 2)), findUser(fromNick), &getUser(getMyNick()), findUser(rtNick) };

		if(!message.replyTo || !message.from) {
			if(!message.replyTo) {
				// Assume it's from the hub
				OnlineUser* replyTo = &getUser(rtNick);
				replyTo->getIdentity().setHub(true);
				replyTo->getIdentity().setHidden(true);
				fire(ClientListener::UserUpdated(), this, replyTo);
			}
			if(!message.from) {
				// Assume it's from the hub
				OnlineUser* from = &getUser(fromNick);
				from->getIdentity().setHub(true);
				from->getIdentity().setHidden(true);
				fire(ClientListener::UserUpdated(), this, from);
			}

			// Update pointers just in case they've been invalidated
			message.replyTo = findUser(rtNick);
			message.from = findUser(fromNick);
		}

		if (Util::strnicmp(message.text, "/me ", 4) == 0) {
			message.thirdPerson = true;
			message.text = message.text.substr(4);
		}

		fire(ClientListener::Message(), this, message);
	} else if(cmd == "GetPass") {
		OnlineUser& ou = getUser(getMyNick());
		ou.getIdentity().set("RG", "1");
		setMyIdentity(ou.getIdentity());
		onPassword();
	} else if(cmd == "BadPass") {
		setPassword(Util::emptyString);
	} else if(cmd == "ZOn") {
		try {
			sock->setMode(BufferedSocket::MODE_ZPIPE);
		} catch (const Exception& e) {
			dcdebug("NmdcHub::onLine %s failed with error: %s\n", cmd.c_str(), e.getError().c_str());
		}

	} else if(cmd == "HubTopic") {
		fire(ClientListener::HubTopic(), this, param);
	} else {
		dcdebug("NmdcHub::onLine Unknown command %s\n", aLine.c_str());
	} 
}

string NmdcHub::checkNick(const string& aNick) {
	string tmp = aNick;
	for(size_t i = 0; i < aNick.size(); ++i) {
		if(static_cast<uint8_t>(tmp[i]) <= 32 || tmp[i] == '|' || tmp[i] == '$' || tmp[i] == '<' || tmp[i] == '>') {
			tmp[i] = '_';
		}
	}
	return tmp;
}

void NmdcHub::connectToMe(const OnlineUser& aUser) {
	checkstate();
	dcdebug("NmdcHub::connectToMe %s\n", aUser.getIdentity().getNick().c_str());
	string nick = fromUtf8(aUser.getIdentity().getNick());
	ConnectionManager::getInstance()->nmdcExpect(nick, getMyNick(), getHubUrl());
	
	bool secure = CryptoManager::getInstance()->TLSOk() && aUser.getUser()->isSet(User::TLS) && !getStealth();
	string port = secure ? ConnectionManager::getInstance()->getSecurePort() : ConnectionManager::getInstance()->getPort();
	send("$ConnectToMe " + nick + " " + localIp + ":" + port + (secure ? "S" : "") + "|");
}

void NmdcHub::revConnectToMe(const OnlineUser& aUser) {
	checkstate(); 
	dcdebug("NmdcHub::revConnectToMe %s\n", aUser.getIdentity().getNick().c_str());
	send("$RevConnectToMe " + fromUtf8(getMyNick()) + " " + fromUtf8(aUser.getIdentity().getNick()) + "|");
}

bool NmdcHub::hubMessage(const string& aMessage, string& error_, bool thirdPerson) { 
	if(state != STATE_NORMAL) {
		error_ = STRING(CONNECTING_IN_PROGRESS);
		return false;
	}

	send(fromUtf8( "<" + getMyNick() + "> " + escape(thirdPerson ? "/me " + aMessage : aMessage) + "|" ) );
	return true;
}

void NmdcHub::myInfo(bool alwaysSend) {
	if(!alwaysSend && lastUpdate + 15000 > GET_TICK()) return; // antispam

	checkstate();

	reloadSettings(false);

	
	char status = Identity::NORMAL;
	char modeChar = '?';
	if(SETTING(OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SOCKS5)
		modeChar = '5';
	else if(isActive())
		modeChar = 'A';
	else 
		modeChar = 'P';
	


	string dc;
	string version;

	if (getStealth()) {
		dc = "++";
		version = DCVERSIONSTRING;
	} else {
		dc = APPNAME;

		status |= Identity::AIRDC;


		version = shortVersionString;
		if(AirUtil::getAway()) {
			status |= Identity::AWAY;
		}
		if(!isActive()) {
			status |= Identity::NAT;
		}
	}
	
	if (CryptoManager::getInstance()->TLSOk()) {
		status |= Identity::TLS;
	}

	string uploadSpeed;
	int upLimit = ThrottleManager::getInstance()->getUpLimit();
	if (upLimit > 0) {
		uploadSpeed = Util::toString(upLimit) + " KiB/s";
	} else {
		uploadSpeed = SETTING(UPLOAD_SPEED);
	}

	char myInfo[256];
	snprintf(myInfo, sizeof(myInfo), "$MyINFO $ALL %s %s<%s V:%s,M:%c,H:%s,S:%d>$ $%s%c$%s$", fromUtf8(getMyNick()).c_str(),
		fromUtf8(escape(get(Description))).c_str(), dc.c_str(), version.c_str(), modeChar, getCounts().c_str(), 
		UploadManager::getInstance()->getSlots(), fromUtf8(uploadSpeed).c_str(), status, fromUtf8(escape(get(Email))).c_str());

	int64_t newBytesShared = getShareProfile() == SP_HIDDEN ? 0 : ShareManager::getInstance()->getTotalShareSize(SETTING(DEFAULT_SP));
	if (strcmp(myInfo, lastMyInfo.c_str()) != 0 || alwaysSend || (newBytesShared != lastBytesShared && lastUpdate + 15*60*1000 < GET_TICK())) {
		dcdebug("MyInfo %s...\n", getMyNick().c_str());		
		send(string(myInfo) + Util::toString(newBytesShared) + "$|");
		
		lastMyInfo = myInfo;
		lastBytesShared = newBytesShared;
		lastUpdate = GET_TICK();
	}
}

void NmdcHub::search(const SearchPtr& s){
	checkstate();
	if (s->aschOnly)
		return;

	char c1 = (s->sizeType == SearchManager::SIZE_DONTCARE || s->sizeType == SearchManager::SIZE_EXACT) ? 'F' : 'T';
	char c2 = (s->sizeType == SearchManager::SIZE_ATLEAST) ? 'F' : 'T';

	string tmp = ((s->fileType == SearchManager::TYPE_TTH) ? "TTH:" + s->query : fromUtf8(escape(s->query)));
	Util::replace(tmp, "\"", ""); //can't use quotes in NMDC searches

	string::size_type i;
	while((i = tmp.find(' ')) != string::npos) {
		tmp[i] = '$';
	}
	string tmp2;
	if(isActive() && !SETTING(SEARCH_PASSIVE)) {
		tmp2 = localIp + ':' + SearchManager::getInstance()->getPort();
	} else {
		tmp2 = "Hub:" + fromUtf8(getMyNick());
	}

	string type = Util::toString((s->fileType == SearchManager::TYPE_FILE ? SearchManager::TYPE_ANY : s->fileType)+1);

	send("$Search " + tmp2 + ' ' + c1 + '?' + c2 + '?' + Util::toString(s->size) + '?' + type + '?' + tmp + '|');
}

string NmdcHub::validateMessage(string tmp, bool reverse) {
	string::size_type i = 0;

	if(reverse) {
		while( (i = tmp.find("&#36;", i)) != string::npos) {
			tmp.replace(i, 5, "$");
			i++;
		}
		i = 0;
		while( (i = tmp.find("&#124;", i)) != string::npos) {
			tmp.replace(i, 6, "|");
			i++;
		}
		i = 0;
		while( (i = tmp.find("&amp;", i)) != string::npos) {
			tmp.replace(i, 5, "&");
			i++;
		}
	} else {
		i = 0;
		while( (i = tmp.find("&amp;", i)) != string::npos) {
			tmp.replace(i, 1, "&amp;");
			i += 4;
		}
		i = 0;
		while( (i = tmp.find("&#36;", i)) != string::npos) {
			tmp.replace(i, 1, "&amp;");
			i += 4;
		}
		i = 0;
		while( (i = tmp.find("&#124;", i)) != string::npos) {
			tmp.replace(i, 1, "&amp;");
			i += 4;
		}
		i = 0;
		while( (i = tmp.find('$', i)) != string::npos) {
			tmp.replace(i, 1, "&#36;");
			i += 4;
		}
		i = 0;
		while( (i = tmp.find('|', i)) != string::npos) {
			tmp.replace(i, 1, "&#124;");
			i += 5;
		}
	}
	return tmp;
}

void NmdcHub::privateMessage(const string& nick, const string& message, bool thirdPerson) {
	send("$To: " + fromUtf8(nick) + " From: " + fromUtf8(getMyNick()) + " $" + fromUtf8(escape("<" + getMyNick() + "> " + (thirdPerson ? "/me " + message : message))) + "|");
}

bool NmdcHub::privateMessage(const OnlineUserPtr& aUser, const string& aMessage, string& error_, bool thirdPerson) {
	if(state != STATE_NORMAL) {
		error_ = STRING(CONNECTING_IN_PROGRESS);
		return false;
	}

	privateMessage(aUser->getIdentity().getNick(), aMessage, thirdPerson);
	// Emulate a returning message...
	OnlineUserPtr ou = findUser(getMyNick());
	if(ou) {
		ChatMessage message = { aMessage, ou, aUser, ou };
		fire(ClientListener::Message(), this, message);
		return true;
	}
	error_ = STRING(USER_OFFLINE);
	return false;
}

void NmdcHub::sendUserCmd(const UserCommand& command, const ParamMap& params) {
	checkstate();
	string cmd = Util::formatParams(command.getCommand(), params);
	if(command.isChat()) {
		string error;
		if(command.getTo().empty()) {
			hubMessage(cmd, error);
		} else {
			privateMessage(command.getTo(), cmd, false);
		}
	} else {
		send(fromUtf8(cmd));
	}
}

void NmdcHub::clearFlooders(uint64_t aTick) {
	while(!seekers.empty() && seekers.front().second + (5 * 1000) < aTick) {
		seekers.pop_front();
	}

	while(!flooders.empty() && flooders.front().second + (120 * 1000) < aTick) {
		flooders.pop_front();
	}
}

void NmdcHub::on(Connected) noexcept {
	Client::on(Connected());

	if(state != STATE_PROTOCOL) {
		return;
	}

	supportFlags = 0;
	lastMyInfo.clear();
	lastBytesShared = 0;
	lastUpdate = 0;
	refreshLocalIp();
}

void NmdcHub::on(Line, const string& aLine) noexcept {
	Client::on(Line(), aLine);
	onLine(aLine);
}

void NmdcHub::on(Failed, const string& aLine) noexcept {
	clearUsers();
	Client::on(Failed(), aLine);
	updateCounts(true, false);	
}

void NmdcHub::on(Second, uint64_t aTick) noexcept {
	Client::on(Second(), aTick);

	if(state == STATE_NORMAL && (aTick > (getLastActivity() + 120*1000)) ) {
		send("|", 1);
	}
}

void NmdcHub::on(Minute, uint64_t /*aTick*/) noexcept {
	refreshLocalIp();
}

void NmdcHub::getUserList(OnlineUserList& list) const {
	Lock l(cs);
	for(auto& u: users | map_values) {
		list.push_back(u);
	}
}

size_t NmdcHub::getUserCount() const { 
	Lock l(cs); 
	size_t userCount = 0;
	for(auto& i: users) {
		if(!i.second->isHidden()) {
			++userCount;
		}
	}
	return userCount;
}

} // namespace dcpp