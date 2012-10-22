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
#include "Client.h"

#include "BufferedSocket.h"

#include "FavoriteManager.h"
#include "TimerManager.h"
#include "ResourceManager.h"
#include "ClientManager.h"
#include "AirUtil.h"
#include "LogManager.h"

namespace dcpp {

atomic<long> Client::counts[COUNT_UNCOUNTED];

Client::Client(const string& hubURL, char separator_) : 
	myIdentity(ClientManager::getInstance()->getMe(), 0),
	reconnDelay(120), lastActivity(GET_TICK()), registered(false), autoReconnect(false),
	encoding(Text::systemCharset), state(STATE_DISCONNECTED), sock(0),
	separator(separator_),
	countType(COUNT_UNCOUNTED), availableBytes(0), seticons(0), favToken(0)
{
	setHubUrl(hubURL);
	TimerManager::getInstance()->addListener(this);
}

void Client::setHubUrl(const string& aUrl) {
	hubUrl = move(aUrl);
	secure = strnicmp("adcs://", aUrl.c_str(), 7) == 0 || strnicmp("nmdcs://", aUrl.c_str(), 8) == 0;

	string file, proto, query, fragment;
	Util::decodeUrl(hubUrl, proto, address, port, file, query, fragment);
	keyprint = Util::decodeQuery(query)["kp"];
}

Client::~Client() {
	dcassert(!sock);
	
	// In case we were deleted before we Failed
	FavoriteManager::getInstance()->removeUserCommand(getHubUrl());
	TimerManager::getInstance()->removeListener(this);
	updateCounts(true, false);
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
	FavoriteHubEntry* hub = FavoriteManager::getInstance()->getFavoriteHubEntry(getHubUrl());
	bool isAdcHub = AirUtil::isAdcHub(hubUrl);

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
		setStealth(!isAdcHub ? hub->getStealth() : false);
		setFavIp(hub->getIP());
		setFavNoPM(hub->getFavNoPM());
		setHubShowJoins(hub->getHubShowJoins()); //show joins
		setHubLogMainchat(hub->getHubLogMainchat());
		setChatNotify(hub->getChatNotify());

		//only set the token on the initial attempt. we may have other hubs in favs with the failover addresses but keep on using the initial list for now.
		if (favToken == 0)
			favToken = hub->getToken();

		if(!hub->getEncoding().empty())
			setEncoding(hub->getEncoding());
		
		if(hub->getSearchInterval() < 10)
			setSearchInterval(SETTING(MINIMUM_SEARCH_INTERVAL) * 1000);
		else
			setSearchInterval(hub->getSearchInterval() * 1000);

		if (isAdcHub) {
			setShareProfile(hub->getShareProfile()->getToken());
		} else {
			setShareProfile(hub->getShareProfile()->getToken() == SP_HIDDEN ? SP_HIDDEN : SP_DEFAULT);
		}
		
	} else {
		if(updateNick) {
			setCurrentNick(checkNick(SETTING(NICK)));
		}
		setCurrentDescription(SETTING(DESCRIPTION));
		setStealth(false);
		setFavIp(Util::emptyString);
		setFavNoPM(false);
		setHubShowJoins(false);
		setChatNotify(false);
		setHubLogMainchat(true);
		setSearchInterval(SETTING(MINIMUM_SEARCH_INTERVAL) * 1000);
		setPassword(Util::emptyString);
		//favToken = 0;

		if (!isAdcHub)
			setShareProfile(shareProfile == SP_HIDDEN ? SP_HIDDEN : SP_DEFAULT);
	}
}

bool Client::isActive() const {
	return ClientManager::getInstance()->isActive(hubUrl);
}

void Client::connect() {
	if(sock) {
		BufferedSocket::putSocket(sock);
		sock = 0;
	}

	setAutoReconnect(true);
	setReconnDelay(120 + Util::rand(0, 60));
	reloadSettings(true);
	setRegistered(false);
	setMyIdentity(Identity(ClientManager::getInstance()->getMe(), 0));
	setHubIdentity(Identity());

	state = STATE_CONNECTING;

	try {
		sock = BufferedSocket::getSocket(separator, v4only());
		sock->addListener(this);
		sock->connect(address, port, secure, BOOLSETTING(ALLOW_UNTRUSTED_HUBS), true);
	} catch(const Exception& e) {
		state = STATE_DISCONNECTED;
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
	COMMAND_DEBUG(aMessage, DebugManager::TYPE_HUB, DebugManager::OUTGOING, getIpPort());
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
	state = STATE_PROTOCOL;
	seticons = 0;
}

void Client::onPassword() {
	string newUrl = hubUrl;
	if (getPassword().empty() && FavoriteManager::getInstance()->blockFailOverUrl(favToken, newUrl)) {
		ClientManager::getInstance()->setClientUrl(hubUrl, newUrl);
		state = STATE_DISCONNECTED;
		sock->removeListener(this);
		fire(ClientListener::Failed(), this, STRING(FAILOVER_AUTH));
		return;
	}
	fire(ClientListener::GetPassword(), this);
}

void Client::on(Failed, const string& aLine) noexcept {
	string msg = aLine;
	if (state == STATE_CONNECTING || (state != STATE_NORMAL && FavoriteManager::getInstance()->isFailOverUrl(favToken, hubUrl))) {
		string newUrl = hubUrl;
		if (FavoriteManager::getInstance()->getFailOverUrl(favToken, newUrl) && !ClientManager::getInstance()->isConnected(newUrl)) {
			ClientManager::getInstance()->setClientUrl(hubUrl, newUrl);

			if (msg[msg.length()-1] != '.')
				msg += ".";
			msg += " Switching to an address " + hubUrl;
		}
	} else {
		//don't try failover addresses right after getting disconnected...
		FavoriteManager::getInstance()->removeUserCommand(hubUrl);
	}

	state = STATE_DISCONNECTED;

	//FavoriteManager::getInstance()->removeUserCommand(getHubUrl());
	sock->removeListener(this);
	fire(ClientListener::Failed(), this, msg);
}

void Client::disconnect(bool graceLess) {
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

bool Client::updateCounts(bool aRemove, bool updateIcons) {
	Lock l(cs); //prevent data race
	// We always remove the count and then add the correct one if requested...
	if(countType != COUNT_UNCOUNTED) {
		counts[countType]--;
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

		if(updateIcons && seticons < 2) { //set more than once due to some nmdc hubs
			fire(ClientListener::SetIcons(), this, countType);
			seticons++;
		}
		counts[countType]++;
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

uint64_t Client::queueSearch(Search* aSearch){
	dcdebug("Queue search %s\n", aSearch->query.c_str());
	return searchQueue.add(aSearch);
}

string Client::getCounts() {
	char buf[128];
	return string(buf, snprintf(buf, sizeof(buf), "%ld/%ld/%ld",
		counts[COUNT_NORMAL].load(), counts[COUNT_REGISTERED].load(), counts[COUNT_OP].load()));
}
 
void Client::on(Line, const string& aLine) noexcept {
	updateActivity();
	COMMAND_DEBUG(aLine, DebugManager::TYPE_HUB, DebugManager::INCOMING, getIpPort());
}

void Client::on(Second, uint64_t aTick) noexcept {
	if(state == STATE_DISCONNECTED && getAutoReconnect() && (aTick > (getLastActivity() + getReconnDelay() * 1000)) ) {
		// Try to reconnect...
		connect();
	}

	if(searchQueue.hasWaitingTime(aTick)) return;

	if(isConnected()){
		Search* s = searchQueue.pop();
		
		if(s){
			search(s);
		}
	}

}

} // namespace dcpp