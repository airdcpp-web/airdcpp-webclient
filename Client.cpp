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
#include "Client.h"

#include "AirUtil.h"
#include "BufferedSocket.h"
#include "ClientManager.h"
#include "ConnectivityManager.h"
#include "DebugManager.h"
#include "FavoriteManager.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "ThrottleManager.h"
#include "TimerManager.h"

namespace dcpp {

atomic<long> Client::counts[COUNT_UNCOUNTED];
uint32_t idCounter = 0;

Client::Client(const string& hubURL, char separator_) : 
	myIdentity(ClientManager::getInstance()->getMe(), 0), uniqueId(++idCounter),
	reconnDelay(120), lastActivity(GET_TICK()), registered(false), autoReconnect(false),
	state(STATE_DISCONNECTED), sock(0),
	separator(separator_),
	countType(COUNT_UNCOUNTED), availableBytes(0), seticons(0), favToken(0)
{
	setHubUrl(hubURL);
	TimerManager::getInstance()->addListener(this);
}

void Client::setHubUrl(const string& aUrl) {
	hubUrl = aUrl;
	secure = Util::strnicmp("adcs://", aUrl.c_str(), 7) == 0 || Util::strnicmp("nmdcs://", aUrl.c_str(), 8) == 0;

	string file, proto, query, fragment;
	Util::decodeUrl(hubUrl, proto, address, port, file, query, fragment);
	keyprint = Util::decodeQuery(query)["kp"];
}

Client::~Client() {
	updateCounts(true, false);
}

void Client::reconnect() {
	disconnect(true);
	setAutoReconnect(true);
	setReconnDelay(0);
}

void Client::setActive() {
	fire(ClientListener::SetActive(), this);
}

void Client::shutdown() {
	FavoriteManager::getInstance()->removeUserCommand(getHubUrl());
	TimerManager::getInstance()->removeListener(this);

	if(sock) {
		BufferedSocket::putSocket(sock, [this] { delete this; }); //delete in its own thread to allow safely using async calls
	} else {
		delete this;
	}
}

string Client::getDescription() const {
	string ret = get(HubSettings::Description);

	int upLimit = ThrottleManager::getInstance()->getUpLimit();
	if(upLimit > 0)
		ret = "[L:" + Util::toString(upLimit) + "KB] " + ret;
	return ret;
}

void Client::reloadSettings(bool updateNick) {
	/// @todo update the nick in ADC hubs?
	string prevNick;
	if(!updateNick)
		prevNick = get(Nick);

	auto fav = FavoriteManager::getInstance()->getFavoriteHubEntry(getHubUrl());

	*static_cast<HubSettings*>(this) = SettingsManager::getInstance()->getHubSettings();

	bool isAdcHub = AirUtil::isAdcHub(hubUrl);

	if(fav) {
		FavoriteManager::getInstance()->mergeHubSettings(fav, *this);
		if(!fav->getPassword().empty())
			setPassword(fav->getPassword());

		setStealth(!isAdcHub ? fav->getStealth() : false);
		setFavNoPM(fav->getFavNoPM());

		//only set the token on the initial attempt. we may have other hubs in favs with failover addresses but keep on using the initial list for now.
		if (favToken == 0)
			favToken = fav->getToken();

		if (isAdcHub) {
			setShareProfile(fav->getShareProfile()->getToken());
		} else {
			setShareProfile(fav->getShareProfile()->getToken() == SP_HIDDEN ? SP_HIDDEN : SETTING(DEFAULT_SP));
		}
		
	} else {
		setStealth(false);
		setFavNoPM(false);
		setPassword(Util::emptyString);

		if (!isAdcHub)
			setShareProfile(shareProfile == SP_HIDDEN ? SP_HIDDEN : SETTING(DEFAULT_SP));
	}

	searchQueue.minInterval = get(HubSettings::SearchInterval);
	if(updateNick)
		checkNick(get(Nick));
	else
		get(Nick) = prevNick;
}

bool Client::changeBoolHubSetting(HubSettings::HubBoolSetting aSetting) {
	auto newValue = !get(aSetting);
	get(aSetting) = newValue;

	//save for a favorite hub if needed
	if (favToken > 0) {
		FavoriteManager::getInstance()->setHubSetting(hubUrl, aSetting, newValue);
	}
	return newValue;
}

void Client::updated(const OnlineUserPtr& aUser) { 
	fire(ClientListener::UserUpdated(), this, aUser); 
}

void Client::updated(OnlineUserList& users) {
	//std::for_each(users.begin(), users.end(), [](OnlineUser* user) { UserMatchManager::getInstance()->match(*user); });

	fire(ClientListener::UsersUpdated(), this, users);
}

const string& Client::getUserIp4() const {
	if(!get(UserIp).empty()) {
		return get(UserIp);
	}
	return CONNSETTING(EXTERNAL_IP);
}

const string& Client::getUserIp6() const {
	if(!get(UserIp6).empty()) {
		return get(UserIp6);
	}
	return CONNSETTING(EXTERNAL_IP6);
}

bool Client::isActive() const {
	return isActiveV4() || isActiveV6();
}

bool Client::isActiveV4() const {
	return get(HubSettings::Connection) != SettingsManager::INCOMING_PASSIVE && get(HubSettings::Connection) != SettingsManager::INCOMING_DISABLED;
}

bool Client::isActiveV6() const {
	return !v4only() && get(HubSettings::Connection6) != SettingsManager::INCOMING_PASSIVE && get(HubSettings::Connection6) != SettingsManager::INCOMING_DISABLED;
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
		sock->connect(address, port, secure, SETTING(ALLOW_UNTRUSTED_HUBS), true);
	} catch(const Exception& e) {
		state = STATE_DISCONNECTED;
		fire(ClientListener::Failed(), hubUrl, e.getError());
	}
	updateActivity();
}

void Client::info() {
	callAsync([this] { infoImpl(); });
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
				fire(ClientListener::Failed(), hubUrl, "Keyprint mismatch");
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
		state = STATE_DISCONNECTED;
		sock->removeListener(this);
		fire(ClientListener::Failed(), hubUrl, STRING(FAILOVER_AUTH));
		ClientManager::getInstance()->setClientUrl(hubUrl, newUrl);
		return;
	}
	fire(ClientListener::GetPassword(), this);
}

void Client::on(Failed, const string& aLine) noexcept {
	string msg = aLine;
	string oldUrl = hubUrl;
	if (state == STATE_CONNECTING || (state != STATE_NORMAL && FavoriteManager::getInstance()->isFailOverUrl(favToken, hubUrl))) {
		auto newUrl = FavoriteManager::getInstance()->getFailOverUrl(favToken, hubUrl);
		if (newUrl && !ClientManager::getInstance()->isConnected(*newUrl)) {
			ClientManager::getInstance()->setClientUrl(hubUrl, *newUrl);

			if (msg[msg.length()-1] != '.')
				msg += ".";
			msg += " " + STRING_F(SWITCHING_TO_ADDRESS, hubUrl);
		}
	} else {
		//don't try failover addresses right after getting disconnected...
		FavoriteManager::getInstance()->removeUserCommand(hubUrl);
	}

	state = STATE_DISCONNECTED;

	sock->removeListener(this);
	fire(ClientListener::Failed(), oldUrl, msg);
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
			if(SETTING(DISALLOW_CONNECTION_TO_PASSED_HUBS)) {
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

uint64_t Client::queueSearch(SearchPtr aSearch){
	dcdebug("Queue search %s\n", aSearch->query.c_str());
	return searchQueue.add(move(aSearch));
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
		auto s = move(searchQueue.pop());
		if(s){
			search(move(s));
		}
	}

}

} // namespace dcpp