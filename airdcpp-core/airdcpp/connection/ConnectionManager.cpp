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
#include <airdcpp/connection/ConnectionManager.h>

#include <airdcpp/util/AutoLimitUtil.h>
#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/connectivity/ConnectivityManager.h>
#include <airdcpp/core/crypto/CryptoManager.h>
#include <airdcpp/transfer/download/DownloadManager.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/queue/QueueManager.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/transfer/upload/UploadManager.h>
#include <airdcpp/connection/UserConnection.h>
#include <airdcpp/util/ValueGenerator.h>

namespace dcpp {
FastCriticalSection TokenManager::cs;


TokenManager::~TokenManager() {
	// dcassert(tokens.empty());
}

string TokenManager::createToken(ConnectionType aConnType) noexcept {
	string token;

	{
		FastLock l(cs);
		do { token = Util::toString(ValueGenerator::rand()); } while (tokens.contains(token));

		tokens.try_emplace(token, aConnType);
	}

	return token;
}

bool TokenManager::addToken(const string& aToken, ConnectionType aConnType) noexcept{
	FastLock l(cs);
	const auto [_, added] = tokens.try_emplace(aToken, aConnType);
	return added;
}

bool TokenManager::hasToken(const string& aToken, ConnectionType aConnType) const noexcept{
	FastLock l(cs);
	const auto res = tokens.find(aToken);
	return res != tokens.end() && (aConnType == CONNECTION_TYPE_LAST || res->second == aConnType);
}

void TokenManager::removeToken(const string& aToken) noexcept {
	FastLock l(cs);
#ifdef _DEBUG
	auto p = tokens.find(aToken);
	if (p != tokens.end())
		tokens.erase(p);
	else
		dcassert(0);
#else
	tokens.erase(aToken);
#endif
}

constexpr auto CONNECT_FLOOD_COUNT_NORMAL_MINOR = 30;
constexpr auto CONNECT_FLOOD_COUNT_NORMAL_SEVERE = 45;
constexpr auto CONNECT_FLOOD_COUNT_MCN = 100;
constexpr auto CONNECT_FLOOD_PERIOD = 30;

ConnectionManager::ConnectionManager() : floodCounter(CONNECT_FLOOD_PERIOD), downloads(cqis[CONNECTION_TYPE_DOWNLOAD]) {
	TimerManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);

	features = {
		UserConnection::FEATURE_MINISLOTS,
		UserConnection::FEATURE_XML_BZLIST,
		UserConnection::FEATURE_ADCGET,
		UserConnection::FEATURE_TTHL,
		UserConnection::FEATURE_TTHF 
	};

	adcFeatures = { 
		"AD" + UserConnection::FEATURE_ADC_BAS0,
		"AD" + UserConnection::FEATURE_ADC_BASE,
		"AD" + UserConnection::FEATURE_ADC_BZIP,
		"AD" + UserConnection::FEATURE_ADC_TIGR,
		"AD" + UserConnection::FEATURE_ADC_MCN1, 
		"AD" + UserConnection::FEATURE_ADC_CPMI
	};
}

void ConnectionManager::listen() {
	server.reset(new Server(false, Util::toString(CONNSETTING(TCP_PORT)), CONNSETTING(BIND_ADDRESS), CONNSETTING(BIND_ADDRESS6)));

	if(!CryptoManager::getInstance()->TLSOk()) {
		dcdebug("Skipping secure port: %d\n", CONNSETTING(TLS_PORT));
		return;
	}

	if (CONNSETTING(TCP_PORT) != 0 && CONNSETTING(TCP_PORT) == CONNSETTING(TLS_PORT)) {
		LogManager::getInstance()->message(STRING(ERROR_TLS_PORT), LogMessage::SEV_ERROR, STRING(CONNECTIVITY));
	}

	secureServer.reset(new Server(true, Util::toString(CONNSETTING(TLS_PORT)), CONNSETTING(BIND_ADDRESS), CONNSETTING(BIND_ADDRESS6)));
}

ConnectionQueueItem::ConnectionQueueItem(const HintedUser& aUser, ConnectionType aConntype, const string& aToken) : token(aToken),
	connType(aConntype), user(aUser) {

}

bool ConnectionQueueItem::allowNewConnections(int aRunning) const noexcept {
	if (maxRemoteConns != 0 && aRunning >= maxRemoteConns) {
		return false;
	}

	auto maxOwnConns = AutoLimitUtil::getSlotsPerUser(true);
	if (maxOwnConns != 0 && aRunning >= maxOwnConns) {
		return false;
	}

	return true;
}

bool ConnectionQueueItem::isSmallSlot() const noexcept {
	return downloadType == QueueDownloadType::SMALL;
}

bool ConnectionQueueItem::isActive() const noexcept {
	return state == State::ACTIVE;
}

bool ConnectionQueueItem::isRunning() const noexcept {
	return isSet(FLAG_RUNNING);
}

bool ConnectionQueueItem::isMcn() const noexcept {
	return isSet(FLAG_MCN);
}

bool ConnectionQueueItem::allowConnect(int aAttempts, int aAttemptLimit, uint64_t aTick) const noexcept {
	// No attempts?
	if (lastAttempt == 0 && aAttempts < aAttemptLimit * 2) {
		return true;
	}

	// Enough time ellapsed since the last attempt?
	return (aAttemptLimit == 0 || aAttempts < aAttemptLimit) &&
		lastAttempt + 60 * 1000 * max(1, errors) < aTick;
}

bool ConnectionQueueItem::isTimeout(uint64_t aTick) const noexcept {
	return state == ConnectionQueueItem::State::CONNECTING && lastAttempt + 50 * 1000 < aTick;
}

void ConnectionQueueItem::resetFatalError() noexcept {
	if (getLastAttempt() == -1) {
		setLastAttempt(0);
	}
}

/**
 * Request a connection for downloading.
 * DownloadManager::addConnection will be called as soon as the connection is ready
 * for downloading.
 * @param aUser The user to connect to.
 */
void ConnectionManager::getDownloadConnection(const HintedUser& aUser, bool aSmallSlot) noexcept {
	dcassert(aUser.user);

	if (DownloadManager::getInstance()->checkIdle(aUser.user, aSmallSlot)) {
		return;
	}

	{
		WLock l(cs);
		if (!allowNewMCNUnsafe(aUser, aSmallSlot, [](ConnectionQueueItem* aWaitingCQI) {
			// Force in case we joined a new hub and there was a protocol error
			aWaitingCQI->resetFatalError();
		})) {
			return;
		}

		auto cqi = getCQIUnsafe(aUser, CONNECTION_TYPE_DOWNLOAD);
		if (aSmallSlot) {
			cqi->setDownloadType(QueueDownloadType::SMALL);
		}

		dcdebug("DownloadManager::getDownloadConnection: created new item %s for user %s (small slot: %s)\n", cqi->getToken().c_str(), ClientManager::getInstance()->getFormattedNicks(aUser).c_str(), aSmallSlot ? "true" : "false");
	}
}

bool ConnectionManager::allowNewMCNUnsafe(const UserPtr& aUser, bool aSmallSlot, const ConnectionQueueItemCallback& aWaitingCallback) const noexcept {
	// We need to check if we have queued something also while the small file connection was being established
	ConnectionQueueItem* cqi = nullptr;
	int runningNormal = 0;
	auto supportMcn = false;

	for(const auto& i: downloads) {
		cqi = i;
		if (cqi->getUser() != aUser) {
			continue;
		}

		if (!cqi->isMcn()) {
			// We already have a connection, no need to continue
			return false;
		}

		supportMcn = true;

		if (cqi->getDownloadType() == QueueDownloadType::MCN_NORMAL) {
			if (!cqi->isRunning()) {
				// Already has a waiting item? Small slot doesn't count
				if (!aSmallSlot) {
					// Force in case we joined a new hub and there was a protocol error
					if (aWaitingCallback) {
						aWaitingCallback(cqi);
					}
					return false;
				}
			} else {
				runningNormal++;
			}
		} else if (cqi->getDownloadType() == QueueDownloadType::SMALL) {
			// No need to continue with small slot if an item with the same type exists already
			// (regardless of whether it's running or not)
			if (aSmallSlot) {
				// Force in case we joined a new hub and there was a protocol error
				if (!cqi->isRunning() && aWaitingCallback) {
					aWaitingCallback(cqi);
				}

				return false;
			}
		}
	}

	if (supportMcn && !aSmallSlot && !cqi->allowNewConnections(runningNormal)) {
		return false;
	}

	return true;
}


ConnectionQueueItem* ConnectionManager::getCQIUnsafe(const HintedUser& aUser, ConnectionType aConnType, const string& aToken) noexcept {
	auto& container = cqis[aConnType];
	auto cqi = new ConnectionQueueItem(aUser, aConnType, !aToken.empty() ? aToken : tokens.createToken(aConnType));
	container.emplace_back(cqi);
	dcassert(tokens.hasToken(cqi->getToken()));

	fire(ConnectionManagerListener::Added(), cqi);
	return cqi;
}

void ConnectionManager::putCQIUnsafe(ConnectionQueueItem* cqi) noexcept {
	fire(ConnectionManagerListener::Removed(), cqi);
	
	auto& container = cqis[cqi->getConnType()];
	dcassert(find(container.begin(), container.end(), cqi) != container.end());
	std::erase(container, cqi);

	if (cqi->getConnType() == CONNECTION_TYPE_DOWNLOAD && !cqi->isActive()) {
		removedDownloadTokens[cqi->getToken()] = GET_TICK();
	}

	tokens.removeToken(cqi->getToken());
	delete cqi;
}

UserConnection* ConnectionManager::getConnection(bool aNmdc) noexcept {
	auto uc = new UserConnection();
	uc->addListener(this);
	{
		WLock l(cs);
		userConnections.push_back(uc);
	}

	if (aNmdc) {
		uc->setFlag(UserConnection::FLAG_NMDC);
	}

	return uc;
}

bool ConnectionManager::findUserConnection(const string& aConnectToken, const UserConnectionCallback& aCallback) const noexcept {
	RLock l(cs);
	if (auto i = find(userConnections.begin(), userConnections.end(), aConnectToken); i != userConnections.end()) {
		aCallback(*i);
		return true;
	}

	return false;
}

bool ConnectionManager::findUserConnection(UserConnectionToken aToken, const UserConnectionCallback& aCallback) const noexcept {
	RLock l(cs);
	if (auto i = ranges::find_if(userConnections, [aToken](const UserConnectionPtr& uc) { return uc->getToken() == aToken; }); i != userConnections.end()) {
		aCallback(*i);
		return true;
	}

	return false;
}

bool ConnectionManager::isMCNUser(const UserPtr& aUser) const noexcept {
	RLock l(cs);

	auto s = ranges::find_if(userConnections, [&](const UserConnection* uc) {
		return uc->getUser() == aUser && uc->isMCN();
	});

	return s != userConnections.end();
}

void ConnectionManager::putConnection(UserConnection* aConn) noexcept {
	aConn->removeListener(this);
	aConn->disconnect(true);

	WLock l(cs);
	userConnections.erase(remove(userConnections.begin(), userConnections.end(), aConn), userConnections.end());
}

void ConnectionManager::onUserUpdated(const UserPtr& aUser) noexcept {
	RLock l(cs);
	for (const auto& cqi : downloads) {
		if (cqi->getUser() == aUser) {
			fire(ConnectionManagerListener::UserUpdated(), cqi);
		}
	}

	for (const auto& cqi : cqis[CONNECTION_TYPE_UPLOAD]) {
		if(cqi->getUser() == aUser) {
			fire(ConnectionManagerListener::UserUpdated(), cqi);
		}
	}
}

void ConnectionManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	StringList removedTokens;

	attemptDownloads(aTick, removedTokens);
	if (!removedTokens.empty()) {
		WLock l (cs);
		for (const auto& m: removedTokens) {
			auto s = find(downloads.begin(), downloads.end(), m);
			if (s != downloads.end()) {
				putCQIUnsafe(*s);
			}
		}
	}
}

void ConnectionManager::attemptDownloads(uint64_t aTick, StringList& removedTokens_) noexcept {
	int attemptLimit = SETTING(DOWNCONN_PER_SEC);
	int attempts = 0;

	RLock l(cs);
	for (auto cqi : downloads) {
		// Already active?
		if (cqi->isActive()) {
			continue;
		}

		// Removing?
		if (!cqi->getUser().user->isOnline()) {
			removedTokens_.push_back(cqi->getToken());
			continue;
		}

		// No attempts?
		if (cqi->getErrors() == -1 && cqi->getLastAttempt() != 0) {
			// protocol error, don't reconnect except after a forced attempt
			continue;
		}

		// Not enough time since the last attempt?
		if (!cqi->allowConnect(attempts, attemptLimit, aTick)) {
			if (cqi->isTimeout(aTick)) {
				cqi->setErrors(cqi->getErrors() + 1);
				fire(ConnectionManagerListener::Failed(), cqi, STRING(CONNECTION_TIMEOUT));
				cqi->setState(ConnectionQueueItem::State::WAITING);
			}

			continue;
		}

		// Try to connect 
		if (attemptDownloadUnsafe(cqi, removedTokens_)) {
			attempts++;
		}

		cqi->setLastAttempt(aTick);
	}
}

bool ConnectionManager::attemptDownloadUnsafe(ConnectionQueueItem* cqi, StringList& removedTokens_) noexcept {

	// We'll also validate the hubhint (and that the user is online) before making any connection attempt
	auto startResult = QueueManager::getInstance()->startDownload(cqi->getUser(), cqi->getDownloadType());
	if (!startResult.hasDownload && cqi->getDownloadType() == QueueDownloadType::SMALL && ranges::none_of(downloads, [&](const ConnectionQueueItem* aCQI) { return aCQI != cqi && aCQI->getUser() == cqi->getUser(); })) {
		// The small file finished already? Try with any type
		cqi->setDownloadType(QueueDownloadType::ANY);
		startResult = QueueManager::getInstance()->startDownload(cqi->getUser(), QueueDownloadType::ANY);
	} else if (
		cqi->getDownloadType() == QueueDownloadType::ANY &&
		startResult.downloadType == QueueDownloadType::SMALL &&
		ranges::none_of(downloads, [&](const ConnectionQueueItem* aCQI) {
			return aCQI->getUser() == cqi->getUser() && cqi->isSmallSlot();
		})
	) {
		// a Small file has been added after the CQI was created
		cqi->setDownloadType(QueueDownloadType::SMALL);
	}

	// No files to download from this user?
	if (!startResult.hasDownload) {
		dcdebug("ConnectionManager::attemptDownload: no downloads from user %s (conn %s), removing (small slot: %s)\n", ClientManager::getInstance()->getFormattedNicks(cqi->getUser()).c_str(), cqi->getToken().c_str(), cqi->isSmallSlot() ? "true" : "false");
		removedTokens_.push_back(cqi->getToken());
		return false;
	}

	cqi->setLastBundle(startResult.bundleToken ? Util::toString(*startResult.bundleToken) : Util::emptyString);
	cqi->setHubUrl(startResult.hubHint);

	if (cqi->getState() == ConnectionQueueItem::State::WAITING ||
		// Forcing the connection and it's not connected yet? Retry
		(cqi->getLastAttempt() == 0 && cqi->getState() == ConnectionQueueItem::State::CONNECTING && find(userConnections.begin(), userConnections.end(), cqi->getToken()) == userConnections.end())
	) {
		if (startResult.slotType) {
			return connectUnsafe(cqi, startResult.allowUrlChange);
		} else {
			// Download limits full or similar temporary error
			dcdebug("ConnectionManager::attemptDownload: can't start download from user %s (connection %s): %s (small slot: %s)\n", ClientManager::getInstance()->getFormattedNicks(cqi->getUser()).c_str(), cqi->getToken().c_str(), startResult.lastError.c_str(), cqi->isSmallSlot() ? "true" : "false");
			fire(ConnectionManagerListener::Failed(), cqi, startResult.lastError);
		}
	}

	return false;
}

bool ConnectionManager::connectUnsafe(ConnectionQueueItem* cqi, bool aAllowUrlChange) noexcept {
	cqi->setState(ConnectionQueueItem::State::CONNECTING);

	auto connectResult = ClientManager::getInstance()->connect(cqi->getUser(), cqi->getToken(), aAllowUrlChange);
	if (!connectResult.getIsSuccess()) {
		cqi->setState(ConnectionQueueItem::State::WAITING);
		cqi->setErrors(connectResult.getIsProtocolError() ? -1 : (cqi->getErrors() + 1)); // protocol error
		dcassert(!connectResult.getError().empty());
		fire(ConnectionManagerListener::Failed(), cqi, connectResult.getError());
		return false;
	}

	// Success
	cqi->setHubUrl(connectResult.getHubHint());
	fire(ConnectionManagerListener::Connecting(), cqi);
	return true;
}

void ConnectionManager::onDownloadRunning(const UserConnection *aSource) noexcept {
	{
		RLock l(cs);
		auto cqi = findDownloadUnsafe(aSource);
		if (!cqi || cqi->isSet(ConnectionQueueItem::FLAG_RUNNING)) {
			return;
		}

		cqi->setFlag(ConnectionQueueItem::FLAG_RUNNING);
		if (!cqi->isMcn()) {
			return;
		}

		if (!allowNewMCNUnsafe(cqi->getUser(), false)) {
			dcdebug("ConnectionManager::addRunningMCN: can't add new connections for user %s, conn %s (small slot: %s)\n", ClientManager::getInstance()->getFormattedNicks(aSource->getHintedUser()).c_str(), cqi->getToken().c_str(), cqi->isSmallSlot() ? "true" : "false");
			return;
		}
	}

	createNewMCN(aSource->getHintedUser());
}

void ConnectionManager::createNewMCN(const HintedUser& aUser) noexcept {
	auto result = QueueManager::getInstance()->startDownload(aUser, QueueDownloadType::MCN_NORMAL);
	if (!result.hasDownload) {
		dcdebug("ConnectionManager::createNewMCN: no downloads from user %s (type normal)\n", ClientManager::getInstance()->getFormattedNicks(aUser).c_str());
		return;
	}

	{
		WLock l (cs);
		auto cqiNew = getCQIUnsafe(aUser, CONNECTION_TYPE_DOWNLOAD);
		cqiNew->setDownloadType(QueueDownloadType::MCN_NORMAL);
		cqiNew->setFlag(ConnectionQueueItem::FLAG_MCN);

		dcdebug("ConnectionManager::createNewMCN: creating new connection for user %s\n", ClientManager::getInstance()->getFormattedNicks(aUser).c_str());
	}
}

constexpr auto MAX_UC_INACTIVITY_SECONDS = 180;
void ConnectionManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	WLock l(cs);
	for (auto i = removedDownloadTokens.begin(); i != removedDownloadTokens.end();) {
		if((i->second + (90 * 1000)) < aTick) {
			removedDownloadTokens.erase(i++);
		} else {
			++i;
		}
	}

	for(auto j: userConnections) {
		if (j->isSet(UserConnection::FLAG_PM)) {
			//Send a write check to the socket to detect half connected state, a good interval?
			if ((j->getLastActivity() + MAX_UC_INACTIVITY_SECONDS * 1000) < aTick) {
				AdcCommand c(AdcCommand::CMD_PMI);
				c.addParam("\n");
				j->sendHooked(c);
			}
		} else if ((j->getLastActivity() + MAX_UC_INACTIVITY_SECONDS * 1000) < aTick) {
			dcdebug("ConnectionManager::timer: disconnecting an inactive connection %s for user %s\n", j->getConnectToken().c_str(), ClientManager::getInstance()->getFormattedNicks(j->getHintedUser()).c_str());
			j->disconnect(true);
		}
	}
}

const string& ConnectionManager::getPort() const noexcept {
	return server.get() ? server->getPort() : Util::emptyString;
}

const string& ConnectionManager::getSecurePort() const noexcept {
	return secureServer.get() ? secureServer->getPort() : Util::emptyString;
}

ConnectionManager::Server::Server(bool aSecure, const string& aPort, const string& ipv4, const string& ipv6) :
sock(Socket::TYPE_TCP), secure(aSecure) {
	sock.setLocalIp4(ipv4);
	sock.setLocalIp6(ipv6);
	sock.setV4only(false);
	port = sock.listen(aPort);

	start();
}

static const uint32_t POLL_TIMEOUT = 250;

int ConnectionManager::Server::run() noexcept {
	while(!die) {
		try {
			while(!die) {
				auto [read, _] = sock.wait(POLL_TIMEOUT, true, false);
				if (read) {
					ConnectionManager::getInstance()->accept(sock, secure);
					
				}
			}
		} catch(const Exception& e) {
			dcdebug("ConnectionManager::Server::run Error: %s\n", e.getError().c_str());
		}

		bool failed = false;
		while(!die) {
			try {
				sock.disconnect();
				port = sock.listen(port);

				if(failed) {
					LogManager::getInstance()->message("Connectivity restored", LogMessage::SEV_INFO, STRING(CONNECTIVITY));
					failed = false;
				}
				break;
			} catch(const SocketException& e) {
				dcdebug("ConnectionManager::Server::run Stopped listening: %s\n", e.getError().c_str());

				if(!failed) {
					LogManager::getInstance()->message(str(boost::format("Connectivity error: %1%") % e.getError()), LogMessage::SEV_ERROR, STRING(CONNECTIVITY));
					failed = true;
				}

				// Spin for 60 seconds
				for(auto i = 0; i < 60 && !die; ++i) {
					Thread::sleep(1000);
				}
			}
		}
	}
	return 0;
}

FloodCounter::FloodLimits ConnectionManager::getIncomingConnectionLimits(const string& aIP) const noexcept {
	RLock l(cs);
	auto s = ranges::find_if(userConnections, [&](const UserConnection* uc) {
		return uc->getRemoteIp() == aIP && uc->isMCN();
	});

	if (s != userConnections.end()) {
		// Use higher limit for confirmed MCN users
		return {
			CONNECT_FLOOD_COUNT_MCN,
			CONNECT_FLOOD_COUNT_MCN,
		};
	}

	return {
		CONNECT_FLOOD_COUNT_NORMAL_MINOR,
		CONNECT_FLOOD_COUNT_NORMAL_SEVERE,
	};
}

/**
 * Someone's connecting, accept the connection and wait for identification...
 * It's always the other fellow that starts sending if he made the connection.
 */
void ConnectionManager::accept(const Socket& sock, bool aSecure) noexcept {
	auto uc = getConnection(false);
	uc->setFlag(UserConnection::FLAG_INCOMING);
	uc->setState(UserConnection::STATE_SUPNICK);
	uc->setLastActivity(GET_TICK());

	try {
		uc->accept(sock, aSecure, [&](const string& aIP) {
			auto floodResult = floodCounter.handleRequest(aIP, getIncomingConnectionLimits(aIP));
			if (floodResult.type == FloodCounter::FloodType::OK) {
				return true;
			}

			if (floodResult.type == FloodCounter::FloodType::FLOOD_SEVERE && floodResult.hitLimit) {
				LogManager::getInstance()->message(
					floodCounter.appendFloodRate(aIP, STRING_F(INCOMING_CONNECT_FLOOD_FROM, aIP), true), 
					LogMessage::SEV_WARNING,
					STRING(CONNECTIVITY)
				);
			}

			return false;
		});
	} catch(const Exception&) {
		putConnection(uc);
		delete uc;
	}
}
	
void ConnectionManager::nmdcConnect(const string& aServer, const SocketConnectOptions& aOptions, const string& aNick, const string& aHubUrl, const string& aEncoding) noexcept {
	nmdcConnect(aServer, aOptions, Util::emptyString, aNick, aHubUrl, aEncoding);
}

void ConnectionManager::nmdcConnect(const string& aServer, const SocketConnectOptions& aOptions, const string& aLocalPort, const string& aNick, const string& aHubUrl, const string& aEncoding) noexcept {
	if(shuttingDown)
		return;

	auto uc = getConnection(true);
	uc->setConnectToken(aNick);
	uc->setHubUrl(aHubUrl);
	uc->setEncoding(aEncoding);
	uc->setState(UserConnection::STATE_CONNECT);
	uc->setFlag(UserConnection::FLAG_NMDC);
	try {
		uc->connect(AddressInfo(aServer, AddressInfo::TYPE_V4), aOptions, aLocalPort);
	} catch(const Exception&) {
		putConnection(uc);
		delete uc;
	}
}

void ConnectionManager::adcConnect(const OnlineUser& aUser, const SocketConnectOptions& aOptions, const string& aToken) noexcept {
	adcConnect(aUser, aOptions, Util::emptyString, aToken);
}

void ConnectionManager::adcConnect(const OnlineUser& aUser, const SocketConnectOptions& aOptions, const string& aLocalPort, const string& aToken) noexcept {
	if(shuttingDown)
		return;

	auto uc = getConnection(false);
	uc->setEncoding(Text::utf8);
	uc->setState(UserConnection::STATE_CONNECT);
	uc->setHubUrl(aUser.getClient()->getHubUrl());
	uc->setConnectToken(aToken);
	/*if (aUser.getIdentity().isOp()) {
		uc->setFlag(UserConnection::FLAG_OP);
	}*/
	
	if (tokens.hasToken(aToken, CONNECTION_TYPE_PM)) {
		uc->setFlag(UserConnection::FLAG_PM);
	}

	try {
		if (aUser.getIdentity().getTcpConnectMode() == Identity::MODE_ACTIVE_DUAL) {
			uc->connect(AddressInfo(aUser.getIdentity().getIp4(), aUser.getIdentity().getIp6()), aOptions, aLocalPort, aUser);
		} else {
			auto ai = AddressInfo(aUser.getIdentity().getTcpConnectIp(), Identity::allowV6Connections(aUser.getIdentity().getTcpConnectMode()) ? AddressInfo::TYPE_V6 : AddressInfo::TYPE_V4);
			uc->connect(ai, aOptions, aLocalPort, aUser);
		}
	} catch(const Exception&) {
		putConnection(uc);
		delete uc;
	}
}

void ConnectionManager::disconnect() noexcept {
	server.reset();
	secureServer.reset();
}

void ConnectionManager::on(AdcCommand::SUP, UserConnection* aSource, const AdcCommand& cmd) noexcept {
	if(aSource->getState() != UserConnection::STATE_SUPNICK) {
		// Already got this once, ignore...@todo fix support updates
		dcdebug("CM::onSUP %p sent sup twice\n", (void*)aSource);
		return;
	}

	bool baseOk = false;
	bool tigrOk = false;

	StringList supports;
	for(auto& i: cmd.getParameters()) {
		if(i.compare(0, 2, "AD") == 0) {
			string feat = i.substr(2);
			if(feat == UserConnection::FEATURE_ADC_BASE || feat == UserConnection::FEATURE_ADC_BAS0) {
				baseOk = true;
				// For bas0 tiger is implicit
				if(feat == UserConnection::FEATURE_ADC_BAS0) {
					tigrOk = true;
				}
				// ADC clients must support all these...
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_ADCGET);
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_MINISLOTS);
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_TTHF);
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_TTHL);
				// For compatibility with older clients...
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_XML_BZLIST);
			} else if(feat == UserConnection::FEATURE_ZLIB_GET) {
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_ZLIB_GET);
			} else if(feat == UserConnection::FEATURE_ADC_BZIP) {
				aSource->setFlag(UserConnection::FLAG_SUPPORTS_XML_BZLIST);
			} else if(feat == UserConnection::FEATURE_ADC_TIGR) {
				tigrOk = true;
			}

			supports.push_back(feat);
		}
	}
	aSource->getSupports().replace(supports);

	// TODO: better error
	if(!baseOk || !tigrOk) {
		aSource->sendHooked(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_GENERIC, "Invalid SUP"));
		aSource->disconnect();
		return;
	}

	if (aSource->isSet(UserConnection::FLAG_INCOMING)) {
		aSource->sup(getAdcFeatures());
	} else {
		aSource->inf(true, aSource->isMCN() ? AutoLimitUtil::getSlotsPerUser(false) : 0);
	}

	aSource->setState(UserConnection::STATE_INF);
}

StringList ConnectionManager::getAdcFeatures() const noexcept {
	StringList defFeatures = adcFeatures;
	if (SETTING(COMPRESS_TRANSFERS)) {
		defFeatures.push_back("AD" + UserConnection::FEATURE_ZLIB_GET);
	}

	for (const auto& support: userConnectionSupports.getAll()) {
		defFeatures.push_back("AD" + support);
	}

	return defFeatures;
}

void ConnectionManager::on(AdcCommand::STA, UserConnection*, const AdcCommand& /*cmd*/) noexcept {
	
}

void ConnectionManager::on(UserConnectionListener::Connected, UserConnection* aSource) noexcept {

	if(SETTING(TLS_MODE) == SettingsManager::TLS_FORCED && !aSource->isSet(UserConnection::FLAG_NMDC) && !aSource->isSecure()) {
		putConnection(aSource);
		return;
	}

	dcassert(aSource->getState() == UserConnection::STATE_CONNECT);
	if(aSource->isSet(UserConnection::FLAG_NMDC)) {
		aSource->myNick(aSource->getConnectToken());
		aSource->lock(CryptoManager::getInstance()->getLock(), CryptoManager::getInstance()->getPk() + "Ref=" + aSource->getHubUrl());
	} else {
		aSource->sup(getAdcFeatures());
		aSource->sendHooked(AdcCommand(AdcCommand::SEV_SUCCESS, AdcCommand::SUCCESS, Util::emptyString).addParam("RF", aSource->getHubUrl()));
	}
	aSource->setState(UserConnection::STATE_SUPNICK);
}

void ConnectionManager::on(UserConnectionListener::MyNick, UserConnection* aSource, const string& aNick) noexcept {
	if(aSource->getState() != UserConnection::STATE_SUPNICK) {
		// Already got this once, ignore...
		dcdebug("CM::onMyNick %p sent nick twice\n", (void*)aSource);
		return;
	}

	dcassert(aNick.size() > 0);
	dcdebug("ConnectionManager::onMyNick %p, %s\n", (void*)aSource, aNick.c_str());
	dcassert(!aSource->getUser());

	if(aSource->isSet(UserConnection::FLAG_INCOMING)) {
		// Try to guess where this came from...
		auto [myNick, hubUrl] = expectedConnections.remove(aNick);
		if(hubUrl.empty()) {
			dcassert(myNick.empty());
			dcdebug("Unknown incoming connection from %s\n", aNick.c_str());
			putConnection(aSource);
			return;
		}
        aSource->setConnectToken(myNick);
		aSource->setHubUrl(hubUrl);
		aSource->setEncoding(ClientManager::getInstance()->findNmdcEncoding(hubUrl));
	}

	string nick = Text::toUtf8(aNick, aSource->getEncoding());
	CID cid = ClientManager::getInstance()->makeNmdcCID(nick, aSource->getHubUrl());

	// First, we try looking in the pending downloads...hopefully it's one of them...
	{
		RLock l(cs);
		for(auto cqi: downloads) {
			cqi->setErrors(0);
			if(!cqi->isActive() &&
				cqi->getUser().user->getCID() == cid)
			{
				aSource->setUser(cqi->getUser());
				// Indicate that we're interested in this file...
				aSource->setFlag(UserConnection::FLAG_DOWNLOAD);
				break;
			}
		}
	}

	if(!aSource->getUser()) {
		// Make sure we know who it is, i e that he/she is connected...

		aSource->setUser(ClientManager::getInstance()->findUser(cid));
		if(!aSource->getUser() || !aSource->getUser()->isOnline()) {
			dcdebug("CM::onMyNick Incoming connection from unknown user %s\n", nick.c_str());
			putConnection(aSource);
			return;
		}
		// We don't need this connection for downloading...make it an upload connection instead...
		aSource->setFlag(UserConnection::FLAG_UPLOAD);
	}

	ClientManager::getInstance()->setNmdcIPUser(aSource->getUser(), aSource->getRemoteIp());

	if( aSource->isSet(UserConnection::FLAG_INCOMING) ) {
		aSource->myNick(aSource->getConnectToken());
		aSource->lock(CryptoManager::getInstance()->getLock(), CryptoManager::getInstance()->getPk());
	}

	aSource->setState(UserConnection::STATE_LOCK);
}

void ConnectionManager::on(UserConnectionListener::CLock, UserConnection* aSource, const string& aLock) noexcept {
	if(aSource->getState() != UserConnection::STATE_LOCK) {
		dcdebug("CM::onLock %p received lock twice, ignoring\n", (void*)aSource);
		return;
	}

	if( CryptoManager::getInstance()->isExtended(aLock) ) {
		StringList defFeatures = features;
		if(SETTING(COMPRESS_TRANSFERS)) {
			defFeatures.push_back(UserConnection::FEATURE_ZLIB_GET);
		}

		aSource->sendSupports(defFeatures);
	}

	aSource->setState(UserConnection::STATE_DIRECTION);
	aSource->direction(aSource->getDirectionString(), aSource->getNumber());
	aSource->key(CryptoManager::getInstance()->makeKey(aLock));

}

void ConnectionManager::on(UserConnectionListener::Direction, UserConnection* aSource, const string& dir, const string& num) noexcept {
	if(aSource->getState() != UserConnection::STATE_DIRECTION) {
		dcdebug("CM::onDirection %p received direction twice, ignoring\n", (void*)aSource);
		return;
	}

	dcassert(aSource->isSet(UserConnection::FLAG_DOWNLOAD) ^ aSource->isSet(UserConnection::FLAG_UPLOAD));
	if(dir == "Upload") {
		// Fine, the other fellow want's to send us data...make sure we really want that...
		if(aSource->isSet(UserConnection::FLAG_UPLOAD)) {
			// Huh? Strange...disconnect...
			putConnection(aSource);
			return;
		}
	} else {
		if(aSource->isSet(UserConnection::FLAG_DOWNLOAD)) {
			int number = Util::toInt(num);
			// Damn, both want to download...the one with the highest number wins...
			if(aSource->getNumber() < number) {
				// Damn! We lost!
				aSource->unsetFlag(UserConnection::FLAG_DOWNLOAD);
				aSource->setFlag(UserConnection::FLAG_UPLOAD);
			} else if(aSource->getNumber() == number) {
				putConnection(aSource);
				return;
			}
		}
	}

	dcassert(aSource->isSet(UserConnection::FLAG_DOWNLOAD) ^ aSource->isSet(UserConnection::FLAG_UPLOAD));

	aSource->setState(UserConnection::STATE_KEY);
}


void ConnectionManager::addPMConnection(UserConnection* uc) noexcept {
	dcassert(uc->isSet(UserConnection::FLAG_PM));

	{
		WLock l(cs);
		auto& container = cqis[CONNECTION_TYPE_PM];
		auto i = find(container.begin(), container.end(), uc->getUser());
		if (i == container.end()) {
			dcassert(!uc->getConnectToken().empty());
			uc->setFlag(UserConnection::FLAG_ASSOCIATED);

			auto cqi = getCQIUnsafe(uc->getHintedUser(), CONNECTION_TYPE_PM, uc->getConnectToken());
			cqi->setState(ConnectionQueueItem::State::ACTIVE);

			fire(ConnectionManagerListener::Connected(), cqi, uc);

			dcdebug("ConnectionManager::addPMConnection, PM handler\n");
			return;
		}
	}

	putConnection(uc);
}


void ConnectionManager::addDownloadConnection(UserConnection* uc) noexcept {
	dcassert(uc->isSet(UserConnection::FLAG_DOWNLOAD));
	bool addConn = false;
	{
		RLock l(cs);
		auto cqi = findDownloadUnsafe(uc);
		if (cqi && !cqi->isActive()) {
			cqi->setState(ConnectionQueueItem::State::ACTIVE);
			if (uc->isMCN()) {
				if (cqi->isSmallSlot()) {
					uc->setFlag(UserConnection::FLAG_SMALL_SLOT);
				} else {
					cqi->setDownloadType(QueueDownloadType::MCN_NORMAL);
				}

				cqi->setFlag(ConnectionQueueItem::FLAG_MCN);
			}

			uc->setConnectToken(cqi->getToken()); // sync for NMDC users
			uc->setHubUrl(cqi->getHubUrl()); //set the correct hint for the uc, it might not even have a hint at first.
			uc->setFlag(UserConnection::FLAG_ASSOCIATED);
			fire(ConnectionManagerListener::Connected(), cqi, uc);
			dcdebug("ConnectionManager::addDownloadConnection, leaving to downloadmanager\n");
			addConn = true;
		}
	}

	if(addConn) {
		DownloadManager::getInstance()->addConnection(uc);
	} else {
		putConnection(uc);
	}
}

void ConnectionManager::addUploadConnection(UserConnection* uc) noexcept {
	dcassert(uc->isSet(UserConnection::FLAG_UPLOAD));
	bool allowAdd = true;

	{
		WLock l(cs);
		auto& uploads = cqis[CONNECTION_TYPE_UPLOAD];
		if (!uc->isMCN() && find(uploads.begin(), uploads.end(), uc->getUser()) != uploads.end()) {
			// One connection per CID for non-mcn users
			allowAdd = false;
		}

		if (allowAdd) {
			allowAdd = tokens.addToken(uc->getConnectToken(), CONNECTION_TYPE_UPLOAD);
			if (allowAdd) {
				uc->setFlag(UserConnection::FLAG_ASSOCIATED);

				auto cqi = getCQIUnsafe(uc->getHintedUser(), CONNECTION_TYPE_UPLOAD, uc->getConnectToken());
				cqi->setState(ConnectionQueueItem::State::ACTIVE);
				fire(ConnectionManagerListener::Connected(), cqi, uc);
			}
		}
	}

	if (!allowAdd) {
		putConnection(uc);
		return;
	}

	uc->setThreadPriority(Thread::IDLE);
	dcdebug("ConnectionManager::addUploadConnection, leaving to uploadmanager\n");
	UploadManager::getInstance()->addConnection(uc);
}

void ConnectionManager::on(UserConnectionListener::Key, UserConnection* aSource, const string&/* aKey*/) noexcept {
	if(aSource->getState() != UserConnection::STATE_KEY) {
		dcdebug("CM::onKey Bad state, ignoring");
		return;
	}

	dcassert(aSource->getUser());

	if(aSource->isSet(UserConnection::FLAG_DOWNLOAD)) {
		// this will be synced to use CQI's random token
		addDownloadConnection(aSource);
	} else {
		aSource->setConnectToken(Util::toString(ValueGenerator::rand())); // set a random token instead of using the nick
		addUploadConnection(aSource);
	}
}

void ConnectionManager::on(AdcCommand::INF, UserConnection* aSource, const AdcCommand& cmd) noexcept {
	if(aSource->getState() != UserConnection::STATE_INF) {
		aSource->sendHooked(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_GENERIC, "Expecting INF"));
		aSource->disconnect(true);
		return;
	}

	string token;

	auto fail = [&, this](AdcCommand::Error aCode, const string& aStr) {
		aSource->sendHooked(AdcCommand(AdcCommand::SEV_FATAL, aCode, aStr));
		aSource->disconnect(true);
	};

	if (aSource->isSet(UserConnection::FLAG_INCOMING)) {
		if (!cmd.getParam("TO", 0, token)) {
			aSource->sendHooked(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_GENERIC, "TO missing"));
			putConnection(aSource);
			return;
		}

		aSource->setConnectToken(token);

		// Incoming connections aren't associated with any user
		// Are we expecting this connection? Use the saved CID and hubUrl
		auto [cid, hubUrl] = expectedConnections.remove(token);
		if (hubUrl.empty()) {
			fail(AdcCommand::ERROR_GENERIC, "Connection not expected");
			return;
		} 

		// Hub URL
		aSource->setHubUrl(hubUrl);

		// User
		auto user = ClientManager::getInstance()->findUser(CID(cid));
		aSource->setUser(user);
		if (!aSource->getUser()) {
			dcdebug("CM::onINF: User not found");
			fail(AdcCommand::ERROR_GENERIC, "User not found");
			return;
		}

		// set the PM flag now in order to send a INF with PM1
		if ((tokens.hasToken(token, CONNECTION_TYPE_PM) || cmd.hasFlag("PM", 0))) {
			if (!aSource->isSet(UserConnection::FLAG_PM)) {
				aSource->setFlag(UserConnection::FLAG_PM);
			}

			if (!aSource->getUser()->isSet(User::TLS)) {
				fail(AdcCommand::ERROR_GENERIC, "Unencrypted PM connections not allowed");
				return;
			}

		}

		aSource->inf(false, aSource->isMCN() ? AutoLimitUtil::getSlotsPerUser(false) : 0);

	} else {
		dcassert(aSource->getUser());
		token = aSource->getConnectToken();
	}

	if(!checkKeyprint(aSource)) {
		fail(AdcCommand::ERROR_GENERIC, "Keyprint validation failed");
		return;
	}

	//Cache the trusted state after keyprint verification
	if (aSource->isTrusted())
		aSource->setFlag(UserConnection::FLAG_TRUSTED);

	dcassert(!token.empty());

	{
		RLock l(cs);
		auto i = find(downloads.begin(), downloads.end(), token);
		if (i != downloads.end()) {
			auto cqi = *i;
			if (aSource->isMCN()) {
				string slots;
				if (cmd.getParam("CO", 0, slots)) {
					cqi->setMaxRemoteConns(static_cast<uint8_t>(Util::toInt(slots)));
				}
			}
			cqi->setErrors(0);
			aSource->setFlag(UserConnection::FLAG_DOWNLOAD);
		} else if (removedDownloadTokens.contains(token)) {
			aSource->disconnect(true);
			return;
		}
	}

	if (aSource->isSet(UserConnection::FLAG_DOWNLOAD)) {
		addDownloadConnection(aSource);
	} else if (aSource->isSet(UserConnection::FLAG_PM) || cmd.hasFlag("PM", 0)) {
		// Token
		if (!aSource->isSet(UserConnection::FLAG_PM)) {
			if (!tokens.addToken(token, CONNECTION_TYPE_PM)) {
				dcassert(0);
				fail(AdcCommand::ERROR_GENERIC, "Duplicate token");
				return;
			}

			aSource->setFlag(UserConnection::FLAG_PM);
		} else {
			dcassert(tokens.hasToken(token));
		}

		addPMConnection(aSource);
	} else {
		if (!aSource->isSet(UserConnection::FLAG_UPLOAD))
			aSource->setFlag(UserConnection::FLAG_UPLOAD);
		addUploadConnection(aSource);
	}
}

void ConnectionManager::force(const string& aToken) noexcept {
	if (DownloadManager::getInstance()->checkIdle(aToken)) {
		dcdebug("ConnectionManager::force: idler %s\n", aToken.c_str());
		return;
	}

	RLock l(cs);
	auto i = find(downloads.begin(), downloads.end(), aToken);
	if (i != downloads.end()) {
		fire(ConnectionManagerListener::Forced(), *i);
		(*i)->setLastAttempt(0);
		dcdebug("ConnectionManager::force: download %s\n", aToken.c_str());
	}
}

bool ConnectionManager::checkKeyprint(UserConnection* aSource) noexcept {
	dcassert(aSource->getUser());

	if (!aSource->isSecure() || aSource->isTrusted())
		return true;

	string kp = ClientManager::getInstance()->getField(aSource->getUser()->getCID(), aSource->getHubUrl(), "KP");
	return aSource->verifyKeyprint(kp, SETTING(ALLOW_UNTRUSTED_CLIENTS));
}

void ConnectionManager::failDownload(const string& aToken, const string& aError, bool aFatalError) noexcept {
	optional<HintedUser> mcnUser;

	{
		WLock l (cs); //this may flag other user connections as removed which would possibly cause threading issues
		auto i = find(downloads.begin(), downloads.end(), aToken);
		if (i == downloads.end()) {
			return;
		}

		auto cqi = *i;
		if (cqi->isMcn()) {
			removeExtraMCNUnsafe(cqi);

			if (cqi->isSmallSlot() && cqi->getState() == ConnectionQueueItem::State::ACTIVE) {
				// Small slot item that was never used for downloading anything? Check if we have normal files to download
				if (allowNewMCNUnsafe(cqi->getUser(), false)) {
					mcnUser = cqi->getUser();
				}
			}
		}

		if (cqi->getState() != ConnectionQueueItem::State::WAITING) {
			cqi->setState(ConnectionQueueItem::State::WAITING);

			cqi->setErrors(aFatalError ? -1 : (cqi->getErrors() + 1));
			cqi->setLastAttempt(GET_TICK());
		}

		cqi->unsetFlag(ConnectionQueueItem::FLAG_RUNNING);
		fire(ConnectionManagerListener::Failed(), cqi, aError);
	}

	if (mcnUser) {
		createNewMCN(*mcnUser);
	}
}

void ConnectionManager::onIdle(const UserConnection* aSource) noexcept {
	WLock l(cs); //this may flag other user connections as removed which would possibly cause threading issues
	auto cqi = findDownloadUnsafe(aSource);
	if (!cqi || !cqi->isSet(ConnectionQueueItem::FLAG_RUNNING)) {
		return;
	}

	cqi->unsetFlag(ConnectionQueueItem::FLAG_RUNNING);
	removeExtraMCNUnsafe(cqi);
}

void ConnectionManager::removeExtraMCNUnsafe(const ConnectionQueueItem* aFailedCQI) noexcept {
	if (!aFailedCQI->isMcn()) {
		return;
	}

	if (aFailedCQI->getDownloadType() != QueueDownloadType::MCN_NORMAL) {
		return;
	}

	// Remove an existing waiting item (if exists)
	auto s = find_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem* c) {
		return c->getUser() == aFailedCQI->getUser() && !c->isSmallSlot() &&
			!c->isActive() && c != aFailedCQI;
	});

	if (s != downloads.end()) {
		// (*s)->setFlag(ConnectionQueueItem::FLAG_REMOVE);
		dcdebug("ConnectionManager::disconnectExtraMCN: removing an existing inactive MCN item %s\n", (*s)->getToken().c_str());
		putCQIUnsafe(*s);
	}
}

ConnectionQueueItem* ConnectionManager::findDownloadUnsafe(const UserConnection* aSource) noexcept {
	// Token may not be synced for NMDC users
	auto i = aSource->isMCN() ? std::find(downloads.begin(), downloads.end(), aSource->getConnectToken()) : std::find(downloads.begin(), downloads.end(), aSource->getUser());
	if (i == downloads.end()) {
		return nullptr;
	}

	return *i;
}

void ConnectionManager::on(UserConnectionListener::State, UserConnection* aSource) noexcept {
	if (aSource->getState() == UserConnection::STATE_IDLE) {
		onIdle(aSource);
	} else if (aSource->isSet(UserConnection::FLAG_DOWNLOAD) && aSource->getState() == UserConnection::STATE_RUNNING) {
		onDownloadRunning(aSource);
	}
}

ConnectionType toConnectionType(const UserConnection* aSource) {
	if (aSource->isSet(UserConnection::FLAG_UPLOAD)) {
		return CONNECTION_TYPE_UPLOAD;
	}

	if (aSource->isSet(UserConnection::FLAG_DOWNLOAD)) {
		return CONNECTION_TYPE_DOWNLOAD;
	}

	if (aSource->isSet(UserConnection::FLAG_PM)) {
		return CONNECTION_TYPE_PM;
	}

	return CONNECTION_TYPE_LAST;
}

void ConnectionManager::putCQI(UserConnection* aSource) noexcept {
	auto type = toConnectionType(aSource);
	if (type == CONNECTION_TYPE_LAST) {
		return;
	}

	WLock l(cs);
	auto& container = cqis[type];
	auto i = type == CONNECTION_TYPE_PM ? find(container.begin(), container.end(), aSource->getUser()) :
		find(container.begin(), container.end(), aSource->getConnectToken());
	dcassert(i != container.end());
	putCQIUnsafe(*i);
}

void ConnectionManager::failed(UserConnection* aSource, const string& aError, bool aProtocolError) noexcept {
	if(aSource->isSet(UserConnection::FLAG_ASSOCIATED)) {
		if (aSource->isSet(UserConnection::FLAG_DOWNLOAD)) {
			if (aSource->getState() == UserConnection::STATE_IDLE) {
				// Remove finished idle connections instantly instead of putting them in the "Disconnected" state
				// (unless we are only out of downloading slots)
				auto startResult = QueueManager::getInstance()->startDownload(aSource->getHintedUser(), aSource->getDownloadType());
				if (startResult.hasDownload) {
					failDownload(aSource->getConnectToken(), startResult.lastError, aProtocolError);
				} else {
					putCQI(aSource);
				}
			} else {
				failDownload(aSource->getConnectToken(), aError, aProtocolError);
			}

			dcdebug("ConnectionManager::failed: download %s failed\n", aSource->getConnectToken().c_str());
		} else {
			putCQI(aSource);
		}
	}
	putConnection(aSource);
}

void ConnectionManager::on(UserConnectionListener::Failed, UserConnection* aSource, const string& aError) noexcept {
	failed(aSource, aError, false);
}

void ConnectionManager::on(UserConnectionListener::ProtocolError, UserConnection* aSource, const string& aError) noexcept {
	failed(aSource, aError, true);
}

void ConnectionManager::disconnect(const UserPtr& aUser) noexcept {
	RLock l(cs);
	for(auto uc: userConnections) {
		if(uc->getUser() == aUser)
			uc->disconnect(true);
	}
}

void ConnectionManager::disconnect(const string& aToken) const noexcept {
	findUserConnection(aToken, [](auto uc) {
		uc->disconnect(true);
	});
}

void ConnectionManager::shutdown(const ProgressFunction& progressF) noexcept {
	TimerManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);
	shuttingDown = true;
	disconnect();

	size_t initialConnectionCount = 0;
	{
		RLock l(cs);
		initialConnectionCount = userConnections.size();
		for(auto uc: userConnections) {
			uc->disconnect(true);
		}
	}

	// Wait until all connections have died out...
	while(true) {
		{
			RLock l(cs);
			if(userConnections.empty()) {
				break;
			}

			if (progressF)
				progressF(static_cast<float>(userConnections.size()) / static_cast<float>(initialConnectionCount));
		}
		Thread::sleep(50);
	}
}		

// UserConnectionListener
void ConnectionManager::on(UserConnectionListener::Supports, UserConnection* conn, const StringList& feat) noexcept {
	string sup = Util::emptyString;
	for(auto& i: feat) {
		sup += i + " ";
		if(i == UserConnection::FEATURE_MINISLOTS) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_MINISLOTS);
		} else if(i == UserConnection::FEATURE_XML_BZLIST) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_XML_BZLIST);
		} else if(i == UserConnection::FEATURE_ADCGET) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_ADCGET);
		} else if(i == UserConnection::FEATURE_ZLIB_GET) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_ZLIB_GET);
		} else if(i == UserConnection::FEATURE_TTHL) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_TTHL);
		} else if(i == UserConnection::FEATURE_TTHF) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_TTHF);
		}
	}
}

void ConnectionManager::on(UserConnectionListener::UserSet, UserConnection* aUserConnection) noexcept {
	fire(ConnectionManagerListener::UserSet(), aUserConnection);
}

} // namespace dcpp