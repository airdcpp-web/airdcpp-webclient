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
#include "Client.h"

#include "BufferedSocket.h"

#include "FavoriteManager.h"
#include "TimerManager.h"
#include "ResourceManager.h"
#include "ClientManager.h"
#include "AirUtil.h"

namespace dcpp {

atomic<long> Client::counts[COUNT_UNCOUNTED];

Client::Client(const string& hubURL, char separator_, bool secure_) : 
	myIdentity(ClientManager::getInstance()->getMe(), 0),
	reconnDelay(120), lastActivity(GET_TICK()), registered(false), autoReconnect(false),
	encoding(const_cast<string*>(&Text::systemCharset)), state(STATE_DISCONNECTED), sock(0),
	hubUrl(hubURL), port(0), separator(separator_),
	secure(secure_), countType(COUNT_UNCOUNTED), availableBytes(0), seticons(0)
{
	string file, proto, query, fragment;
	Util::decodeUrl(hubURL, proto, address, port, file, query, fragment);

	if(!query.empty()) {
		auto q = Util::decodeQuery(query);
		auto kp = q.find("kp");
		if(kp != q.end()) {
			keyprint = kp->second;
		}
	}

	TimerManager::getInstance()->addListener(this);
}

Client::~Client() {
	dcassert(!sock);
	
	// In case we were deleted before we Failed
	FavoriteManager::getInstance()->removeUserCommand(getHubUrl());
	TimerManager::getInstance()->removeListener(this);
	updateCounts(true);
}

void Client::reconnect() {
	disconnect(true);
	setAutoReconnect(true);
	setReconnDelay(0);
}

void Client::shutdown() {
	if(sock) {
		BufferedSocket::putSocket(sock);
		sock = 0;
	}
}

void Client::reloadSettings(bool updateNick) {
	const FavoriteHubEntry* hub = FavoriteManager::getInstance()->getFavoriteHubEntry(getHubUrl());
	if(hub) {
		if(updateNick) {
			setCurrentNick(checkNick(hub->getNick(true)));
		}		

		if(!hub->getUserDescription().empty()) {
			setCurrentDescription(hub->getUserDescription());
		} else {
			setCurrentDescription(SETTING(DESCRIPTION));
		}
		if(!hub->getPassword().empty())
			setPassword(hub->getPassword());
		setStealth(hub->getStealth());
		setFavIp(hub->getIP());
		setHideShare(hub->getHideShare()); // Hide Share Mod
		setFavNoPM(hub->getFavNoPM());
		setHubShowJoins(hub->getHubShowJoins()); //show joins
		setHubLogMainchat(hub->getHubLogMainchat());
		setChatNotify(hub->getChatNotify());
		if(!hub->getEncoding().empty())
			setEncoding(const_cast<string*>(&hub->getEncoding()));
		
		if(hub->getSearchInterval() < 10)
			setSearchInterval(SETTING(MINIMUM_SEARCH_INTERVAL) * 1000);
		else
			setSearchInterval(hub->getSearchInterval() * 1000);
	} else {
		if(updateNick) {
			setCurrentNick(checkNick(SETTING(NICK)));
		}
		setCurrentDescription(SETTING(DESCRIPTION));
		setStealth(false);
		setFavIp(Util::emptyString);
		setHideShare(false); // Hide Share Mod
		setFavNoPM(false);
		setHubShowJoins(false);
		setChatNotify(false);
		setHubLogMainchat(true);
		setSearchInterval(SETTING(MINIMUM_SEARCH_INTERVAL) * 1000);
	}
}

bool Client::isActive() const {
	return ClientManager::getInstance()->isActive(hubUrl);
}

void Client::connect() {
	if(sock)
		BufferedSocket::putSocket(sock);

	availableBytes = 0;

	setAutoReconnect(true);
	setReconnDelay(120 + Util::rand(0, 60));
	reloadSettings(true);
	setRegistered(false);
	setMyIdentity(Identity(ClientManager::getInstance()->getMe(), 0));
	setHubIdentity(Identity());

	state = STATE_CONNECTING;

	try {
		sock = BufferedSocket::getSocket(separator);
		sock->addListener(this);
		sock->connect(address, port, secure, BOOLSETTING(ALLOW_UNTRUSTED_HUBS), true);
	} catch(const Exception& e) {
		shutdown();
		/// @todo at this point, this hub instance is completely useless
		fire(ClientListener::Failed(), this, e.getError());
	}
	updateActivity();
}

void Client::send(const char* aMessage, size_t aLen) {
	if(!isReady() || !sock) {
		dcassert(0);
		return;
	}
	updateActivity();
	sock->write(aMessage, aLen);
	COMMAND_DEBUG(aMessage, DebugManager::HUB_OUT, getIpPort());
}

void Client::on(Connected) noexcept {
	updateActivity(); 
	ip = sock->getIp();
	localIp = sock->getLocalIp();

	if(sock->isSecure() && keyprint.compare(0, 7, "SHA256/") == 0) {
		auto kp = sock->getKeyprint();
		if(!kp.empty()) {
			vector<uint8_t> kp2v(kp.size());
			Encoder::fromBase32(keyprint.c_str() + 7, &kp2v[0], kp2v.size());
			if(!std::equal(kp.begin(), kp.end(), kp2v.begin())) {
				state = STATE_DISCONNECTED;
				sock->removeListener(this);
				fire(ClientListener::Failed(), this, "Keyprint mismatch");
				return;
			}
		}
	}
	
	fire(ClientListener::Connected(), this);
	seticons = 0;
	state = STATE_PROTOCOL;
	
}

void Client::on(Failed, const string& aLine) noexcept {
	state = STATE_DISCONNECTED;
	FavoriteManager::getInstance()->removeUserCommand(getHubUrl());
	sock->removeListener(this);
	fire(ClientListener::Failed(), this, aLine);
}

void Client::disconnect(bool graceLess) {
	state = STATE_DISCONNECTED;
	if(sock) 
		sock->disconnect(graceLess);
}

bool Client::isSecure() const {
	return isReady() && sock->isSecure();
}

bool Client::isTrusted() const {
	return isReady() && sock->isTrusted();
}

std::string Client::getCipherName() const {
	return isReady() ? sock->getCipherName() : Util::emptyString;
}

vector<uint8_t> Client::getKeyprint() const {
	return isReady() ? sock->getKeyprint() : vector<uint8_t>();
}

bool Client::updateCounts(bool aRemove) {
	// We always remove the count and then add the correct one if requested...
	if(countType != COUNT_UNCOUNTED) {
		--counts[countType];
		countType = COUNT_UNCOUNTED;
	}

	if(!aRemove) {
		if(getMyIdentity().isOp()) {
			countType = COUNT_OP;
		} else if(getMyIdentity().isRegistered()) {
			countType = COUNT_REGISTERED;
		} else {
				//disconnect before the hubcount is updated.
			if(BOOLSETTING(DISALLOW_CONNECTION_TO_PASSED_HUBS)) {
				fire(ClientListener::AddLine(), this, STRING(HUB_NOT_PROTECTED));
				disconnect(true);
				setAutoReconnect(false);
				return false;
				}

			countType = COUNT_NORMAL;
		}
		if(seticons < 2) { //set more than once due to some nmdc hubs
			fire(ClientListener::SetIcons(), this);
			seticons++;
		}

		++counts[countType];
	}
	return true;
}

string Client::getLocalIp() const {
	// Favorite hub Ip
	if(!getFavIp().empty())
		return Socket::resolve(getFavIp());

	// Best case - the server detected it
	if((!BOOLSETTING(NO_IP_OVERRIDE) || SETTING(EXTERNAL_IP).empty()) && !getMyIdentity().getIp().empty()) {
		return getMyIdentity().getIp();
	}

	if(!SETTING(EXTERNAL_IP).empty()) {
		return Socket::resolve(SETTING(EXTERNAL_IP));
	}

	if(localIp.empty()) {
		return AirUtil::getLocalIp();
	}

	return localIp;
}

uint64_t Client::search(int aSizeMode, int64_t aSize, int aFileType, const string& aString, const string& aToken, const StringList& aExtList, void* owner){
	dcdebug("Queue search %s\n", aString.c_str());

	if(searchQueue.interval) {
		Search s;
		s.fileType = aFileType;
		s.size     = aSize;
		s.query    = aString;
		s.sizeType = aSizeMode;
		s.token    = aToken;
		s.exts	   = aExtList;
		s.owners.insert(owner);

		searchQueue.add(s);

		return searchQueue.getSearchTime(owner) - GET_TICK();
	}

	search(aSizeMode, aSize, aFileType , aString, aToken, aExtList);
	return 0;

}
 
void Client::on(Line, const string& aLine) noexcept {
	updateActivity();
	COMMAND_DEBUG(aLine, DebugManager::HUB_IN, getIpPort());
}

void Client::on(Second, uint64_t aTick) noexcept {
	if(state == STATE_DISCONNECTED && getAutoReconnect() && (aTick > (getLastActivity() + getReconnDelay() * 1000)) ) {
		// Try to reconnect...
		connect();
	}

	if(!searchQueue.interval) return;

	if(isConnected()){
		Search s;
		
		if(searchQueue.pop(s)){
			search(s.sizeType, s.size, s.fileType , s.query, s.token, s.exts);
		}
	}

}

} // namespace dcpp

/**
 * @file
 * $Id: Client.cpp 568 2011-07-24 18:28:43Z bigmuscle $
 */
