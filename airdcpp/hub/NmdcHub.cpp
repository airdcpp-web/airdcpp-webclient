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
#include <airdcpp/hub/NmdcHub.h>

#include <airdcpp/connectivity/ConnectivityManager.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/message/Message.h>
#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/search/SearchManager.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/core/crypto/CryptoManager.h>
#include <airdcpp/connection/ConnectionManager.h>
#include <airdcpp/core/version.h>

#include <airdcpp/connection/socket/Socket.h>
#include <airdcpp/hub/user_command/UserCommand.h>
#include <airdcpp/util/text/StringTokenizer.h>
#include <airdcpp/protocol/ProtocolCommandManager.h>
#include <airdcpp/queue/QueueManager.h>
#include <airdcpp/core/io/compress/ZUtils.h>
#include <airdcpp/connection/ThrottleManager.h>
#include <airdcpp/transfer/upload/UploadManager.h>
#include <airdcpp/hub/activity/ActivityManager.h>
#include <airdcpp/util/NetworkUtil.h>
#include <airdcpp/util/ValueGenerator.h>

namespace dcpp {

NmdcHub::NmdcHub(const string& aHubURL, const ClientPtr& aOldClient) : Client(aHubURL, '|', aOldClient)
{
}

NmdcHub::~NmdcHub() {

}

string NmdcHub::toUtf8(const string& str) noexcept {
	if (str.empty() || Text::validateUtf8(str)) {
		return str;
	}

	if (Util::stricmp(get(HubSettings::NmdcEncoding), Text::utf8) == 0) {
		// Validation failed earlier
		statusMessage(STRING(UTF_VALIDATION_ERROR), LogMessage::SEV_ERROR);
		return Util::emptyString;
	}

	auto ret = Text::toUtf8(str, get(HubSettings::NmdcEncoding));
	if (ret.empty()) {
		statusMessage(STRING_F(DECODING_ERROR, get(HubSettings::NmdcEncoding)), LogMessage::SEV_ERROR);
	}

	return ret;
}

string NmdcHub::fromUtf8(const string& str) noexcept {
	if (str.empty()) {
		return str;
	}

	auto ret = Text::fromUtf8(str, get(HubSettings::NmdcEncoding));
	if (ret.empty()) {
		statusMessage(STRING_F(INVALID_ENCODING, get(HubSettings::NmdcEncoding)), LogMessage::SEV_ERROR);
	}

	return ret;
}


#define checkstate() if(!stateNormal()) return

int NmdcHub::connect(const OnlineUser& aUser, const string&, string& /*lastError_*/) noexcept {
	if(stateNormal()) {
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
		localIp = getMyIdentity().getIp4();
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
				localIp = NetworkUtil::getLocalIp(false);
			}
		}
	}
}

void NmdcHub::refreshUserList(bool refreshOnly) noexcept {
	if(refreshOnly) {
		RLock l(cs);

		OnlineUserList v;
		for(auto n: users | views::values)
			v.push_back(n);

		fire(ClientListener::UsersUpdated(), this, v);
	} else {
		clearUsers();
		getNickList();
	}
}

OnlineUserPtr NmdcHub::getUser(const string& aNick) noexcept {
	OnlineUserPtr u = nullptr;
	{
		RLock l(cs);
		
		auto i = users.find(aNick);
		if(i != users.end())
			return i->second;
	}

	UserPtr p;
	if(aNick == get(Nick)) {
		p = ClientManager::getInstance()->getMe();
	} else {
		p = ClientManager::getInstance()->getNmdcUser(aNick, getHubUrl());
	}

	auto client = ClientManager::getInstance()->findClient(getHubUrl());

	{
		WLock l(cs);
		u = users.emplace(aNick, std::make_shared<OnlineUser>(p, client, ValueGenerator::rand())).first->second;
		u->getIdentity().setNick(aNick);
		if (u->getUser() == getMyIdentity().getUser()) {
			setMyIdentity(u->getIdentity());
		}
	}
	
	onUserConnected(u);
	return u;
}

void NmdcHub::supports(const StringList& feat) { 
	string x;
	for(const auto& f: feat)
		x+= f + ' ';

	send("$Supports " + x + '|');
}

OnlineUserPtr NmdcHub::findUser(const string& aNick) const noexcept {
	RLock l(cs);
	NickIter i = users.find(aNick);
	return i == users.end() ? nullptr : i->second;
}


OnlineUserPtr NmdcHub::findUser(dcpp::SID aSID) const noexcept {
	auto i = ranges::find_if(users | views::values, [=](const OnlineUserPtr& u) {
		return u->getIdentity().getSID() == aSID;
	});

	return i.base() != users.end() ? *i : nullptr;
}

void NmdcHub::putUser(const string& aNick) noexcept {
	OnlineUserPtr ou = nullptr;
	{
		WLock l(cs);
		NickIter i = users.find(aNick);
		if(i == users.end())
		return;
		ou = i->second;
		users.erase(i);

		availableBytes -= ou->getIdentity().getBytesShared();
	}

	onUserDisconnected(ou, false);
}

void NmdcHub::clearUsers() noexcept {
	NickMap u2;

	{
		WLock l(cs);
		u2.swap(users);
		availableBytes = 0;
	}

	for(auto ou: u2 | views::values) {
		ClientManager::getInstance()->putOffline(ou);
	}
}

void NmdcHub::updateFromTag(Identity& id, const string& tag) {
	StringTokenizer<string> tok(tag, ',');
	string::size_type j;
	id.set("US", Util::emptyString);
	//if(tag.find("AirDC++") != string::npos)
	//	id.getUser()->setFlag(User::AIRDCPLUSPLUS);

	for(auto& i: tok.getTokens()) {
		if(i.length() < 2)
			continue;

		if(i.compare(0, 2, "H:") == 0) {
			StringTokenizer<string> t(i.substr(2), '/', true);
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
	if(aLine.empty())
		return;

	if(aLine[0] != '$') {
		// Check if we're being banned...
		if(!stateNormal()) {
			if(Util::findSubString(aLine, "banned") != string::npos) {
				setAutoReconnect(false);
			}
		}
		string line = toUtf8(aLine);
		if (line.empty()) {
			return;
		}

		if(line[0] != '<') {
			statusMessage(unescape(line), LogMessage::SEV_INFO);
			return;
		}
		string::size_type i = line.find('>', 2);
		if(i == string::npos) {
			statusMessage(unescape(line), LogMessage::SEV_INFO);
			return;
		}
		string nick = line.substr(1, i-1);
		string message;
		if((line.length()-1) > i && line[i+1] == ' ') {
			message = line.substr(i+2);
		} else {
			statusMessage(unescape(line), LogMessage::SEV_INFO);
			return;
		}

		if((line.find("Hub-Security") != string::npos) && (line.find("was kicked by") != string::npos)) {
			statusMessage(unescape(line), LogMessage::SEV_VERBOSE);
			return;
		} else if((line.find("is kicking") != string::npos) && (line.find("because:") != string::npos)) {
			statusMessage(unescape(line), LogMessage::SEV_VERBOSE);
			return;
		}

		bool thirdPerson = false;
		auto text = unescape(message);
		if (Util::strnicmp(text, "/me ", 4) == 0) {
			thirdPerson = true;
			text = text.substr(4);
		}

		auto chatMessage = std::make_shared<ChatMessage>(text, findUser(nick));
		chatMessage->setThirdPerson(thirdPerson);

		if(!chatMessage->getFrom()) {
			auto o = getUser(nick);
			// Assume that messages from unknown users come from the hub
			o->getIdentity().setHub(true);
			o->getIdentity().setHidden(true);
			fire(ClientListener::UserUpdated(), this, o);

			chatMessage->setFrom(o);
		}

		onChatMessage(chatMessage);
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
		if (!stateNormal()) {
			return;
		}
		string::size_type i = 0;
		string::size_type j = param.find(' ', i);
		if(j == string::npos || i == j)
			return;
		
		string seeker = param.substr(i, j-i);

		bool isPassive = seeker.size() > 4 && seeker.compare(0, 4, "Hub:") == 0;
		bool meActive = isActive();

		// Filter own searches
		if (meActive && !isPassive) {
			if(seeker == (localIp + ":" + SearchManager::getInstance()->getPort())) {
				return;
			}
		} else if (isPassive && Util::stricmp(seeker.c_str() + 4, getMyNick().c_str()) == 0) {
			return;
		}

		i = j + 1;

		{
			// Nick (passive) or IP (active)
			auto target = isPassive ? seeker.substr(4) : seeker;
			if (!checkIncomingSearch(target)) {
				return;
			}
		}

		int a;
		if(param[i] == 'F') {
			a = Search::SIZE_DONTCARE;
		} else if(param[i+2] == 'F') {
			a = Search::SIZE_ATLEAST;
		} else {
			a = Search::SIZE_ATMOST;
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

		// without terms, this is an invalid search.
		if (!terms.empty()) {
			if (isPassive) {
				// mark the user as passive
				auto u = findUser(seeker.substr(4));
				if (!u) {
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

			SearchManager::getInstance()->respond(this, seeker, a, Util::toInt64(size), type, terms, isPassive);
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

		auto u = getUser(nick);

		// If he is already considered to be the hub (thus hidden), probably should appear in the UserList
		if(u->getIdentity().isHidden()) {
			u->getIdentity().setHidden(false);
			u->getIdentity().setHub(false);
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
				updateFromTag(u->getIdentity(), tmpDesc.substr(x + 1, tmpDesc.length() - x - 2));
				tmpDesc.erase(x);
			}
		}
		u->getIdentity().setDescription(tmpDesc);

		i = j + 3;
		j = param.find('$', i);
		if(j == string::npos)
			return;

		string connection = (i == j) ? Util::emptyString : param.substr(i, j-i-1);

		if (connection.empty()) { 	 
			// No connection = bot... 	 VERY unreliable but... 
			//Users cant understand why it sends away messages to bots/opchats so...
			u->getUser()->setFlag(User::BOT);
			u->getIdentity().setBot(true);
		} else { 	 
			u->getUser()->unsetFlag(User::BOT);
			u->getIdentity().setBot(false);
		}

		u->getIdentity().setHub(false);
		u->getIdentity().setHidden(false);

		u->getIdentity().setNmdcConnection(connection);
		u->getIdentity().setStatus(Util::toString(param[j-1]));
		
		
		if(u->getIdentity().getStatus() & Identity::TLS) {
			u->getUser()->setFlag(User::TLS);
		} else {
			u->getUser()->unsetFlag(User::TLS);
		}

		//if((u.getIdentity().getStatus() & Identity::AIRDC) && !u.getUser()->isSet(User::AIRDCPLUSPLUS))
		//	u.getUser()->setFlag(User::AIRDCPLUSPLUS); //if we have a tag its already set.


		if(u->getIdentity().getStatus() & Identity::NAT) {
			u->getUser()->setFlag(User::NAT_TRAVERSAL);
		} else {
			u->getUser()->unsetFlag(User::NAT_TRAVERSAL);
		}

		i = j + 1;
		j = param.find('$', i);

		if(j == string::npos)
			return;

		u->getIdentity().setEmail(unescape(param.substr(i, j-i)));

		i = j + 1;
		j = param.find('$', i);
		if(j == string::npos)
			return;

		availableBytes -= u->getIdentity().getBytesShared();
		u->getIdentity().setBytesShared(param.substr(i, j-i));
		availableBytes += u->getIdentity().getBytesShared();

		if(u->getUser() == getMyIdentity().getUser()) {
			setMyIdentity(u->getIdentity());
		}
		
		fire(ClientListener::UserUpdated(), this, u);
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
		if(!stateNormal()) {
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
		string senderPort;

		i = param.find(' ', j+1);
		if(i == string::npos) {
			senderPort = param.substr(j+1);
		} else {
			senderNick = param.substr(i+1);
			senderPort = param.substr(j+1, i-j-1);
		}

		bool connectSecure = false;
		if(senderPort[senderPort.size() - 1] == 'S') {
			senderPort.erase(senderPort.size() - 1);
			if(CryptoManager::getInstance()->TLSOk()) {
				connectSecure = true;
			}
		}

		if (!checkIncomingCTM(server)) {
			return;
		}

		if(senderPort[senderPort.size() - 1] == 'N') {
			if(senderNick.empty())
				return;

			senderPort.erase(senderPort.size() - 1);

			// Trigger connection attempt sequence locally ...
			SocketConnectOptions connectOptions(senderPort, connectSecure, NatRole::CLIENT);
			ConnectionManager::getInstance()->nmdcConnect(server, connectOptions, Util::toString(sock->getLocalPort()),
				getMyNick(), getHubUrl(), get(HubSettings::NmdcEncoding));

			// ... and signal other client to do likewise.
			send("$ConnectToMe " + fromUtf8(senderNick) + " " + localIp + ":" + Util::toString(sock->getLocalPort()) + (connectSecure ? "RS" : "R") + "|");
			return;
		} else if(senderPort[senderPort.size() - 1] == 'R') {
			senderPort.erase(senderPort.size() - 1);
				
			// Trigger connection attempt sequence locally
			SocketConnectOptions connectOptions(senderPort, connectSecure, NatRole::SERVER);
			ConnectionManager::getInstance()->nmdcConnect(server, connectOptions, Util::toString(sock->getLocalPort()),
				getMyNick(), getHubUrl(), get(HubSettings::NmdcEncoding));
			return;
		}
		
		if(senderPort.empty())
			return;
			
		// For simplicity, we make the assumption that users on a hub have the same character encoding
		SocketConnectOptions connectOptions(senderPort, connectSecure);
		ConnectionManager::getInstance()->nmdcConnect(server, connectOptions, getMyNick(), getHubUrl(), get(HubSettings::NmdcEncoding));
	} else if(cmd == "RevConnectToMe") {
		if(!stateNormal()) {
			return;
		}

		string::size_type j = param.find(' ');
		if(j == string::npos) {
			return;
		}

		OnlineUserPtr u = findUser(param.substr(0, j));
		if (!u)
			return;

		if(isActive()) {
			connectToMe(*u);
		} else if(u->getIdentity().getStatus() & Identity::NAT) {
			bool connectSecure = CryptoManager::getInstance()->TLSOk() && u->getUser()->isSet(User::TLS);
			// NMDC v2.205 supports "$ConnectToMe sender_nick remote_nick ip:port", but many NMDC hubsofts block it
			// sender_nick at the end should work at least in most used hubsofts
			send("$ConnectToMe " + fromUtf8(u->getIdentity().getNick()) + " " + localIp + ":" + Util::toString(sock->getLocalPort()) + (connectSecure ? "NS " : "N ") + fromUtf8(getMyNick()) + "|");
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
				hubIdentity.setNick(unescape(param));
				hubIdentity.setDescription(Util::emptyString);
			} else {
				hubIdentity.setNick(unescape(param.substr(0, i)));
				hubIdentity.setDescription(unescape(param.substr(i+1)));
			}
		} else {
			hubIdentity.setNick(unescape(param.substr(0, i)));
			hubIdentity.setDescription(unescape(param.substr(i+3)));
		}
		fire(ClientListener::HubUpdated(), this);

		

	} else if(cmd == "Supports") {
		StringTokenizer<string> st(param, ' ', true);
		for(const auto& i: st.getTokens()) {
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
		if(getConnectState() != STATE_PROTOCOL || aLine.size() < 6) {
			return;
		}

		setConnectState(STATE_IDENTIFY);

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
				j = param.find(' ');
				if(j != string::npos)
					lock = param.substr(0, j);
				else
					lock = param;
			}

			if(CryptoManager::getInstance()->isExtended(lock)) {
				StringList feat;
				feat.emplace_back("UserCommand");
				feat.emplace_back("NoGetINFO");
				feat.emplace_back("NoHello");
				feat.emplace_back("UserIP2");
				feat.emplace_back("TTHSearch");
				feat.emplace_back("ZPipe0");
					
				if(CryptoManager::getInstance()->TLSOk())
					feat.emplace_back("TLS");
					
				supports(feat);
			}

			key(CryptoManager::getInstance()->makeKey(lock));
			auto& ou = *getUser(get(Nick));
			validateNick(ou.getIdentity().getNick());
		}
	} else if(cmd == "Hello") {
		if(!param.empty()) {
			auto u = getUser(param);

			if(u->getUser() == getMyIdentity().getUser()) {
				// u.getUser()->setFlag(User::AIRDCPLUSPLUS);
				if(isActive())
					u->getUser()->unsetFlag(User::PASSIVE);
				else
					u->getUser()->setFlag(User::PASSIVE);
			}

			if((getConnectState() == STATE_IDENTIFY || getConnectState() == STATE_VERIFY) && u->getUser() == getMyIdentity().getUser()) {
				setConnectState(STATE_NORMAL);
				updateCounts(false);
				fire(ClientListener::HubUpdated(), this);

				version();
				getNickList();
				myInfo(true);
			}

			fire(ClientListener::UserUpdated(), this, u);
		}
	} else if(cmd == "ForceMove") {
		disconnect(false);
		onRedirect(param);
	} else if(cmd == "HubIsFull") {
		fire(ClientListener::HubFull(), this);
	} else if(cmd == "ValidateDenide") {		// Mind the spelling...
		disconnect(false);
		statusMessage(STRING(NICK_TAKEN), LogMessage::SEV_ERROR);
	} else if(cmd == "UserIP") {
		if(!param.empty()) {
			OnlineUserList v;
			StringTokenizer<string> t(param, "$$", true);
			for(const auto& it: t.getTokens()) {
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

			for(const auto& it: sl) {
				v.push_back(getUser(it));
			}

			if(!(supportFlags & SUPPORTS_NOGETINFO)) {
				string tmp;
				// Let's assume 10 characters per nick...
				tmp.reserve(v.size() * (11 + 10 + getMyNick().length())); 
				string n = ' ' + fromUtf8(getMyNick()) + '|';
				for(const auto& i: v) {
					tmp += "$GetINFO ";
					tmp += fromUtf8(i->getIdentity().getNick());
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
			for(const auto& it: sl) {
				auto ou = getUser(it);
				ou->getIdentity().setOp(true);
				if(ou->getUser() == getMyIdentity().getUser()) {
					setMyIdentity(ou->getIdentity());
				}
				v.push_back(ou);
			}

			updateCounts(false);
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

		bool thirdPerson = false;
		auto text = unescape(param.substr(j + 2));
		if (Util::strnicmp(text, "/me ", 4) == 0) {
			thirdPerson = true;
			text = text.substr(4);
		}

		auto message = std::make_shared<ChatMessage>(text, findUser(fromNick), getUser(getMyNick()), findUser(rtNick));
		message->setThirdPerson(thirdPerson);

		if(!message->getReplyTo() || !message->getFrom()) {
			if(!message->getReplyTo()) {
				// Assume it's from the hub
				auto replyTo = getUser(rtNick);
				replyTo->getIdentity().setHub(true);
				replyTo->getIdentity().setHidden(true);
				fire(ClientListener::UserUpdated(), this, replyTo);
			}
			if(!message->getFrom()) {
				// Assume it's from the hub
				auto from = getUser(fromNick);
				from->getIdentity().setHub(true);
				from->getIdentity().setHidden(true);
				fire(ClientListener::UserUpdated(), this, from);
			}

			// Update pointers just in case they've been invalidated
			message->setReplyTo(findUser(rtNick));
			message->setFrom(findUser(fromNick));
		}

		onPrivateMessage(message);
	} else if(cmd == "GetPass") {
		auto& ou = *getUser(getMyNick());
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
		statusMessage(STRING(HUB_TOPIC) + "\t" + param, LogMessage::SEV_INFO, LogMessage::Type::SYSTEM);
	} else {
		dcdebug("NmdcHub::onLine Unknown command %s\n", aLine.c_str());
	} 
}

void NmdcHub::password(const string& aPass) noexcept {
	setPassword(aPass);
	send("$MyPass " + fromUtf8(aPass) + "|");
}

string NmdcHub::checkNick(const string& aNick) noexcept {
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
	
	bool connectSecure = CryptoManager::getInstance()->TLSOk() && aUser.getUser()->isSet(User::TLS);
	string ownPort = connectSecure ? ConnectionManager::getInstance()->getSecurePort() : ConnectionManager::getInstance()->getPort();
	send("$ConnectToMe " + nick + " " + localIp + ":" + ownPort + (connectSecure ? "S" : "") + "|");
}

void NmdcHub::revConnectToMe(const OnlineUser& aUser) {
	checkstate(); 
	dcdebug("NmdcHub::revConnectToMe %s\n", aUser.getIdentity().getNick().c_str());
	send("$RevConnectToMe " + fromUtf8(getMyNick()) + " " + fromUtf8(aUser.getIdentity().getNick()) + "|");
}

bool NmdcHub::hubMessage(const string& aMessage, bool aThirdPerson) noexcept { 
	send(fromUtf8( "<" + getMyNick() + "> " + escape(aThirdPerson ? "/me " + aMessage : aMessage) + "|" ) );
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
	


	status |= Identity::AIRDC;
	if (ActivityManager::getInstance()->isAway()) {
		status |= Identity::AWAY;
	}
	if (!isActive()) {
		status |= Identity::NAT;
	}

	
	if (CryptoManager::getInstance()->TLSOk()) {
		status |= Identity::TLS;
	}

	string uploadSpeed;
	auto upLimit = ThrottleManager::getInstance()->getUpLimit();
	if (upLimit > 0) {
		uploadSpeed = Util::toString(upLimit) + " KiB/s";
	} else {
		uploadSpeed = SETTING(UPLOAD_SPEED);
	}

	char myInfo[256];
	snprintf(myInfo, sizeof(myInfo), "$MyINFO $ALL %s %s<%s V:%s,M:%c,H:%ld/%ld/%ld,S:%d>$ $%s%c$%s$", fromUtf8(getMyNick()).c_str(),
		fromUtf8(escape(get(Description))).c_str(), APPNAME, VERSIONSTRING.c_str(), modeChar,
		getDisplayCount(COUNT_NORMAL), getDisplayCount(COUNT_REGISTERED), getDisplayCount(COUNT_OP),
		UploadManager::getInstance()->getSlots(), fromUtf8(uploadSpeed).c_str(), status, fromUtf8(escape(get(Email))).c_str());

	int64_t newBytesShared = ShareManager::getInstance()->getTotalShareSize(get(HubSettings::ShareProfile));
	if (strcmp(myInfo, lastMyInfo.c_str()) != 0 || alwaysSend || (newBytesShared != lastBytesShared && lastUpdate + 15*60*1000 < GET_TICK())) {
		dcdebug("MyInfo %s...\n", getMyNick().c_str());		
		send(string(myInfo) + Util::toString(newBytesShared) + "$|");
		
		lastMyInfo = myInfo;
		lastBytesShared = newBytesShared;
		lastUpdate = GET_TICK();
	}
}

void NmdcHub::search(const SearchPtr& s) noexcept {
	checkstate();
	if (s->aschOnly)
		return;

	auto [size, sizeMode] = s->parseLegacySize();
	char c1 = (sizeMode == Search::SIZE_DONTCARE || sizeMode == Search::SIZE_EXACT) ? 'F' : 'T';
	char c2 = (sizeMode == Search::SIZE_ATLEAST) ? 'F' : 'T';

	string tmp = ((s->fileType == Search::TYPE_TTH) ? "TTH:" + s->query : fromUtf8(escape(s->query)));
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

	string type = Util::toString((s->fileType == Search::TYPE_FILE ? Search::TYPE_ANY : s->fileType)+1);

	send("$Search " + tmp2 + ' ' + c1 + '?' + c2 + '?' + Util::toString(size) + '?' + type + '?' + tmp + '|');
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

	dcassert(Text::validateUtf8(tmp));
	return tmp;
}

void NmdcHub::privateMessage(const string& aNick, const string& aMessage, bool aThirdPerson) {
	send("$To: " + fromUtf8(aNick) + " From: " + fromUtf8(getMyNick()) + " $" + fromUtf8(escape("<" + getMyNick() + "> " + (aThirdPerson ? "/me " + aMessage : aMessage))) + "|");
}

bool NmdcHub::privateMessage(const OnlineUserPtr& aUser, const string& aMessage, string& error_, bool aThirdPerson, bool aEcho) noexcept {
	if(!stateNormal()) {
		error_ = STRING(CONNECTING_IN_PROGRESS);
		return false;
	}

	privateMessage(aUser->getIdentity().getNick(), aMessage, aThirdPerson);

	auto ou = findUser(getMyNick());
	if (!ou) {
		error_ = STRING(USER_OFFLINE);
		return false;
	}

	if (aEcho) {
		// Emulate a returning message...
		auto message = std::make_shared<ChatMessage>(aMessage, ou, aUser, ou);
		onPrivateMessage(message);
	}

	return true;
}

void NmdcHub::sendUserCmd(const UserCommand& command, const ParamMap& params) {
	checkstate();
	string cmd = Util::formatParams(command.getCommand(), params);
	if(command.isChat()) {
		if(command.getTo().empty()) {
			hubMessage(cmd);
		} else {
			privateMessage(command.getTo(), cmd, false);
		}
	} else {
		send(fromUtf8(cmd));
	}
}

void NmdcHub::on(Connected) noexcept {
	Client::on(Connected());

	if(getConnectState() != STATE_PROTOCOL) {
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

void NmdcHub::on(Second, uint64_t aTick) noexcept {
	Client::on(Second(), aTick);

	if(stateNormal() && (aTick > (getLastActivity() + 120*1000)) ) {
		send("|", 1);
	}
}

void NmdcHub::on(Minute, uint64_t /*aTick*/) noexcept {
	callAsync([this] { refreshLocalIp(); });
}

void NmdcHub::getUserList(OnlineUserList& list, bool aListHidden) const noexcept {
	RLock l(cs);
	for(auto& u: users | views::values) {
		if (!aListHidden && u->isHidden()) {
			continue;
		}

		list.push_back(u);
	}
}

size_t NmdcHub::getUserCount() const noexcept { 
	RLock l(cs); 
	size_t userCount = 0;
	for (const auto& [_, ou] : users) {
		if (!ou->isHidden()) {
			++userCount;
		}
	}
	return userCount;
}

} // namespace dcpp
