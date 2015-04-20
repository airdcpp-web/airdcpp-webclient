/* 
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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
#include "ConnectionManager.h"

#include "AirUtil.h"
#include "ClientManager.h"
#include "ConnectivityManager.h"
#include "CryptoManager.h"
#include "DownloadManager.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "UploadManager.h"
#include "UserConnection.h"

namespace dcpp {
	FastCriticalSection TokenManager::cs;

string TokenManager::makeToken() const noexcept {
	string token;

	FastLock l(cs);
	do { token = Util::toString(Util::rand()); } while (tokens.find(token) != tokens.end());

	return token;
}


string TokenManager::getToken(ConnectionType aConnType) noexcept{
	string token = move(makeToken());
	FastLock l(cs);
	tokens.emplace(token, aConnType);
	return token;
}

bool TokenManager::addToken(const string& aToken, ConnectionType aConnType) noexcept{
	FastLock l(cs);
	const auto res = tokens.emplace(aToken, aConnType);
	return res.second;
}

bool TokenManager::hasToken(const string& aToken, ConnectionType aConnType) noexcept{
	FastLock l(cs);
	const auto res = tokens.find(aToken);
	return res != tokens.end() && res->second == aConnType;
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

ConnectionManager::ConnectionManager() : downloads(cqis[CONNECTION_TYPE_DOWNLOAD]), floodCounter(0), shuttingDown(false) {
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

	if (SETTING(USE_UPLOAD_BUNDLES))
		adcFeatures.push_back("AD" + UserConnection::FEATURE_ADC_UBN1);
}

void ConnectionManager::listen() {
	server.reset(new Server(false, Util::toString(CONNSETTING(TCP_PORT)), CONNSETTING(BIND_ADDRESS), CONNSETTING(BIND_ADDRESS6)));

	if(!CryptoManager::getInstance()->TLSOk()) {
		dcdebug("Skipping secure port: %d\n", CONNSETTING(TLS_PORT));
		return;
	}
	secureServer.reset(new Server(true, Util::toString(CONNSETTING(TLS_PORT)), CONNSETTING(BIND_ADDRESS), CONNSETTING(BIND_ADDRESS6)));
}

bool ConnectionQueueItem::allowNewConnections(int running) const {
	return (running < AirUtil::getSlotsPerUser(true) || AirUtil::getSlotsPerUser(true) == 0) && (running < maxConns || maxConns == 0);
}


/**
 * Request a connection for downloading.
 * DownloadManager::addConnection will be called as soon as the connection is ready
 * for downloading.
 * @param aUser The user to connect to.
 */
void ConnectionManager::getDownloadConnection(const HintedUser& aUser, bool smallSlot) {
	dcassert(aUser.user);
	bool supportMcn = false;

	if (!DownloadManager::getInstance()->checkIdle(aUser.user, smallSlot)) {
		ConnectionQueueItem* cqi = nullptr;
		int running = 0;

		{
			WLock l(cs);
			for(const auto& i: downloads) {
				cqi = i;
				if (cqi->getUser() == aUser.user && !cqi->isSet(ConnectionQueueItem::FLAG_REMOVE)) {
					if (cqi->isSet(ConnectionQueueItem::FLAG_MCN1)) {
						supportMcn = true;
						if (cqi->getState() != ConnectionQueueItem::RUNNING) {
							//already has a waiting item? small slot doesn't count
							if (!smallSlot) {
								// force in case we joined a new hub and there was a protocol error
								if (cqi->getLastAttempt() == -1) {
									cqi->setLastAttempt(0);
								}
								return;
							}
						} else {
							running++;
						}
					} else if (cqi->getDownloadType() == ConnectionQueueItem::TYPE_SMALL_CONF) {
						supportMcn = true;
						//no need to continue with small slot if an item with the same type exists already (no mather whether it's running or not)
						if (smallSlot) {
							// force in case we joined a new hub and there was a protocol error
							if (cqi->getLastAttempt() == -1) {
								cqi->setLastAttempt(0);
							}
							return;
						}
					} else {
						//no need to continue with non-MCN users
						return;
					}
				}
			}

			if (supportMcn && !smallSlot && !cqi->allowNewConnections(running)) {
				return;
			}

			//WLock l (cs);
			dcdebug("Get cqi");
			cqi = getCQI(aUser, CONNECTION_TYPE_DOWNLOAD);
			if (smallSlot)
				cqi->setDownloadType(supportMcn ? ConnectionQueueItem::TYPE_SMALL_CONF : ConnectionQueueItem::TYPE_SMALL);
		}
	}
}

ConnectionQueueItem* ConnectionManager::getCQI(const HintedUser& aUser, ConnectionType aConnType, const string& aToken) {
	auto& container = cqis[aConnType];
	auto cqi = new ConnectionQueueItem(aUser, aConnType, !aToken.empty() ? aToken : tokens.getToken(aConnType));
	container.emplace_back(cqi);

	fire(ConnectionManagerListener::Added(), cqi);
	return cqi;
}

void ConnectionManager::putCQI(ConnectionQueueItem* cqi) {
	//allways called from inside lock

	fire(ConnectionManagerListener::Removed(), cqi);
	
	auto& container = cqis[cqi->getConnType()];
	dcassert(find(container.begin(), container.end(), cqi) != container.end());
	container.erase(remove(container.begin(), container.end(), cqi), container.end());

	if (cqi->getConnType() == CONNECTION_TYPE_DOWNLOAD)
		delayedTokens[cqi->getToken()] = GET_TICK();

	tokens.removeToken(cqi->getToken());
	delete cqi;
}

UserConnection* ConnectionManager::getConnection(bool aNmdc, bool secure) noexcept {
	UserConnection* uc = new UserConnection(secure);
	uc->addListener(this);
	{
		WLock l(cs);
		userConnections.push_back(uc);
	}
	if(aNmdc)
		uc->setFlag(UserConnection::FLAG_NMDC);
	return uc;
}

void ConnectionManager::putConnection(UserConnection* aConn) {
	aConn->removeListener(this);
	aConn->disconnect(true);

	WLock l(cs);
	userConnections.erase(remove(userConnections.begin(), userConnections.end(), aConn), userConnections.end());
}

void ConnectionManager::onUserUpdated(const UserPtr& aUser) {
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
		for(auto& m: removedTokens) {
			auto s = find(downloads.begin(), downloads.end(), m);
			if (s != downloads.end()) {
				putCQI(*s);
			}
		}
	}
}

void ConnectionManager::attemptDownloads(uint64_t aTick, StringList& removedTokens) {
	RLock l(cs);
	int attemptLimit = SETTING(DOWNCONN_PER_SEC);
	uint16_t attempts = 0;
	for (auto cqi : downloads) {
		if (cqi->getState() != ConnectionQueueItem::ACTIVE && cqi->getState() != ConnectionQueueItem::RUNNING) {
			if (!cqi->getUser()->isOnline() || cqi->isSet(ConnectionQueueItem::FLAG_REMOVE)) {
				removedTokens.push_back(cqi->getToken());
				continue;
			}

			if (cqi->getErrors() == -1 && cqi->getLastAttempt() != 0) {
				// protocol error, don't reconnect except after a forced attempt
				continue;
			}

			if ((cqi->getLastAttempt() == 0 && attempts < attemptLimit * 2) || ((attemptLimit == 0 || attempts < attemptLimit) &&
				cqi->getLastAttempt() + 60 * 1000 * max(1, cqi->getErrors()) < aTick))
			{
				// TODO: no one can understand this code, fix!
				cqi->setLastAttempt(aTick);

				string bundleToken, lastError, hubHint = cqi->getHubUrl();
				bool allowUrlChange = true;
				bool hasDownload = false;

				auto type = cqi->getDownloadType() == ConnectionQueueItem::TYPE_SMALL || cqi->getDownloadType() == ConnectionQueueItem::TYPE_SMALL_CONF ? QueueItem::TYPE_SMALL : cqi->getDownloadType() == ConnectionQueueItem::TYPE_MCN_NORMAL ? QueueItem::TYPE_MCN_NORMAL : QueueItem::TYPE_ANY;

				//we'll also validate the hubhint (and that the user is online) before making any connection attempt
				auto startDown = QueueManager::getInstance()->startDownload(cqi->getUser(), hubHint, type, bundleToken, allowUrlChange, hasDownload, lastError);
				if (!hasDownload && cqi->getDownloadType() == ConnectionQueueItem::TYPE_SMALL && count_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem* aCQI) { return aCQI != cqi && aCQI->getUser() == cqi->getUser(); }) == 0) {
					//the small file finished already? try with any type
					cqi->setDownloadType(ConnectionQueueItem::TYPE_ANY);
					startDown = QueueManager::getInstance()->startDownload(cqi->getUser(), hubHint, QueueItem::TYPE_ANY,
						bundleToken, allowUrlChange, hasDownload, lastError);
				}
				else if (cqi->getDownloadType() == ConnectionQueueItem::TYPE_ANY && startDown.first == QueueItem::TYPE_SMALL &&
					count_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem* aCQI) {
					return aCQI->getUser() == cqi->getUser() && (cqi->getDownloadType() == ConnectionQueueItem::TYPE_SMALL || cqi->getDownloadType() == ConnectionQueueItem::TYPE_SMALL_CONF);
				}) == 0) {
					// a small file has been added after the CQI was created
					cqi->setDownloadType(ConnectionQueueItem::TYPE_SMALL);
				}


				if (!hasDownload) {
					removedTokens.push_back(cqi->getToken());
					continue;
				}

				cqi->setLastBundle(bundleToken);
				cqi->setHubUrl(hubHint);

				if (cqi->getState() == ConnectionQueueItem::WAITING) {
					if (startDown.second) {
						cqi->setState(ConnectionQueueItem::CONNECTING);
						bool protocolError = false;

						if (!ClientManager::getInstance()->connect(cqi->getUser(), cqi->getToken(), allowUrlChange, lastError, hubHint, protocolError)) {
							cqi->setState(ConnectionQueueItem::WAITING);
							cqi->setErrors(protocolError ? -1 : (cqi->getErrors() + 1)); // protocol error
							dcassert(!lastError.empty());
							fire(ConnectionManagerListener::Failed(), cqi, lastError);
						}
						else {
							cqi->setHubUrl(hubHint);
							fire(ConnectionManagerListener::StatusChanged(), cqi);
							attempts++;
						}
					}
					else {
						fire(ConnectionManagerListener::Failed(), cqi, lastError);
					}
				}
			}
			else if (cqi->getState() == ConnectionQueueItem::CONNECTING && cqi->getLastAttempt() + 50 * 1000 < aTick) {

				cqi->setErrors(cqi->getErrors() + 1);
				fire(ConnectionManagerListener::Failed(), cqi, STRING(CONNECTION_TIMEOUT));
				cqi->setState(ConnectionQueueItem::WAITING);
			}
		}
		else if (cqi->isSet(ConnectionQueueItem::FLAG_REMOVE)) {
			cqi->unsetFlag(ConnectionQueueItem::FLAG_REMOVE);
		}
	}
}


void ConnectionManager::addRunningMCN(const UserConnection *aSource) noexcept {
	{
		RLock l(cs);
		auto i = find(downloads.begin(), downloads.end(), aSource->getToken());
		if (i != downloads.end()) {
			ConnectionQueueItem* cqi = *i;
			cqi->setState(ConnectionQueueItem::RUNNING);
			//LogManager::getInstance()->message("Running downloads for the user: " + Util::toString(runningDownloads[aSource->getUser()]));

			if (!allowNewMCN(cqi))
				return;
		}
	}

	createNewMCN(aSource->getHintedUser());
}

bool ConnectionManager::allowNewMCN(const ConnectionQueueItem* aCQI) {
	//we need to check if we have queued something also while the small file connection was being established
	if (!aCQI->isSet(ConnectionQueueItem::FLAG_MCN1) && aCQI->getDownloadType() != ConnectionQueueItem::TYPE_SMALL_CONF)
		return false;

	//count the running MCN connections
	int running = 0;
	for(const auto& cqi: downloads) {
		if (cqi->getUser() == aCQI->getUser() && cqi->getDownloadType() != ConnectionQueueItem::TYPE_SMALL_CONF && !cqi->isSet(ConnectionQueueItem::FLAG_REMOVE)) {
			if (cqi->getState() != ConnectionQueueItem::RUNNING && cqi->getState() != ConnectionQueueItem::ACTIVE) {
				return false;
			}
			running++;
		}
	}

	if (running > 0 && aCQI->getDownloadType() == ConnectionQueueItem::TYPE_SMALL_CONF)
		return false;

	if (!aCQI->allowNewConnections(running) && !aCQI->isSet(ConnectionQueueItem::FLAG_REMOVE))
		return false;

	return true;
}

void ConnectionManager::createNewMCN(const HintedUser& aUser) {
	StringSet runningBundles;
	DownloadManager::getInstance()->getRunningBundles(runningBundles);

	string lastError;
	auto start = QueueManager::getInstance()->startDownload(aUser, runningBundles, 
		ClientManager::getInstance()->getHubSet(aUser.user->getCID()), QueueItem::TYPE_MCN_NORMAL, 0, lastError); // don't overlap...
	if (start) {
		WLock l (cs);
		ConnectionQueueItem* cqiNew = getCQI(aUser, CONNECTION_TYPE_DOWNLOAD);
		cqiNew->setFlag(ConnectionQueueItem::FLAG_MCN1);
		cqiNew->setDownloadType(ConnectionQueueItem::TYPE_MCN_NORMAL);
	}
}

void ConnectionManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	WLock l(cs);
	for(auto i = delayedTokens.begin(); i != delayedTokens.end();) {
		if((i->second + (90 * 1000)) < aTick) {
			delayedTokens.erase(i++);
		} else
			++i;
	}

	for(auto j: userConnections) {

		if (j->isSet(UserConnection::FLAG_PM)) {
			//Send a write check to the socket to detect half connected state, a good interval?
			if ((j->getLastActivity() + 180 * 1000) < aTick) {
				AdcCommand c(AdcCommand::CMD_PMI);
				c.addParam("\n");
				j->send(c);
			}
		}
		else if ((j->getLastActivity() + 180 * 1000) < aTick) {
			j->disconnect(true);
		}
	}
}

const string& ConnectionManager::getPort() const {
	return server.get() ? server->getPort() : Util::emptyString;
}

const string& ConnectionManager::getSecurePort() const {
	return secureServer.get() ? secureServer->getPort() : Util::emptyString;
}

static const uint32_t FLOOD_TRIGGER = 20000;
static const uint32_t FLOOD_ADD = 2000;

ConnectionManager::Server::Server(bool secure, const string& port_, const string& ipv4, const string& ipv6) :
sock(Socket::TYPE_TCP), secure(secure), die(false)
{
	sock.setLocalIp4(ipv4);
	sock.setLocalIp6(ipv6);
	sock.setV4only(false);
	port = sock.listen(port_);

	start();
}

static const uint32_t POLL_TIMEOUT = 250;

int ConnectionManager::Server::run() noexcept {
	while(!die) {
		try {
			while(!die) {
				auto ret = sock.wait(POLL_TIMEOUT, true, false);
				if(ret.first) {
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
					LogManager::getInstance()->message("Connectivity restored", LogManager::LOG_INFO);
					failed = false;
				}
				break;
			} catch(const SocketException& e) {
				dcdebug("ConnectionManager::Server::run Stopped listening: %s\n", e.getError().c_str());

				if(!failed) {
					LogManager::getInstance()->message(str(boost::format("Connectivity error: %1%") % e.getError()), LogManager::LOG_ERROR);
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

/**
 * Someone's connecting, accept the connection and wait for identification...
 * It's always the other fellow that starts sending if he made the connection.
 */
void ConnectionManager::accept(const Socket& sock, bool secure) noexcept {
	uint64_t now = GET_TICK();

	if(now > floodCounter) {
		floodCounter = now + FLOOD_ADD;
	} else {
		if(false && now + FLOOD_TRIGGER < floodCounter) {
			Socket s(Socket::TYPE_TCP);
			try {
				s.accept(sock);
			} catch(const SocketException&) {
				// ...
			}
			dcdebug("Connection flood detected!\n");
			return;
		} else {
			floodCounter += FLOOD_ADD;
		}
	}
	UserConnection* uc = getConnection(false, secure);
	uc->setFlag(UserConnection::FLAG_INCOMING);
	uc->setState(UserConnection::STATE_SUPNICK);
	uc->setLastActivity(GET_TICK());
	try {
		uc->accept(sock);
	} catch(const Exception&) {
		putConnection(uc);
		delete uc;
	}
}
	
void ConnectionManager::nmdcConnect(const string& aServer, const string& aPort, const string& aNick, const string& hubUrl, const string& encoding, bool stealth, bool secure) {
	nmdcConnect(aServer, aPort, Util::emptyString, BufferedSocket::NAT_NONE, aNick, hubUrl, encoding, stealth, secure);
}

void ConnectionManager::nmdcConnect(const string& aServer, const string& aPort, const string& localPort, BufferedSocket::NatRoles natRole, const string& aNick, const string& hubUrl, const string& encoding, bool /*stealth*/, bool secure) {
	if(shuttingDown)
		return;

	UserConnection* uc = getConnection(true, secure);
	uc->setToken(aNick);
	uc->setHubUrl(hubUrl);
	uc->setEncoding(encoding);
	uc->setState(UserConnection::STATE_CONNECT);
	uc->setFlag(UserConnection::FLAG_NMDC);
	try {
		uc->connect(aServer, aPort, localPort, natRole);
	} catch(const Exception&) {
		putConnection(uc);
		delete uc;
	}
}

void ConnectionManager::adcConnect(const OnlineUser& aUser, const string& aPort, const string& aToken, bool secure) {
	adcConnect(aUser, aPort, Util::emptyString, BufferedSocket::NAT_NONE, aToken, secure);
}

void ConnectionManager::adcConnect(const OnlineUser& aUser, const string& aPort, const string& localPort, BufferedSocket::NatRoles natRole, const string& aToken, bool secure) {
	if(shuttingDown)
		return;

	UserConnection* uc = getConnection(false, secure);
	uc->setEncoding(Text::utf8);
	uc->setState(UserConnection::STATE_CONNECT);
	uc->setHubUrl(aUser.getClient().getHubUrl());
	uc->setToken(aToken);
	if(aUser.getIdentity().isOp()) {
		uc->setFlag(UserConnection::FLAG_OP);
	}
	
	if (tokens.hasToken(aToken, CONNECTION_TYPE_PM)) {
		uc->setFlag(UserConnection::FLAG_PM);
	}

	try {
		// TODO: connect via both protocols when available
		uc->connect(aUser.getIdentity().getIp(), aPort, localPort, natRole);
		uc->setUser(aUser);
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
			} else if(feat == UserConnection::FEATURE_ADC_MCN1) {
				aSource->setFlag(UserConnection::FLAG_MCN1);
			} else if(feat == UserConnection::FEATURE_ADC_UBN1) {
				aSource->setFlag(UserConnection::FLAG_UBN1);
			} else if (feat == UserConnection::FEATURE_ADC_CPMI) {
				aSource->setFlag(UserConnection::FLAG_CPMI);
			}
		}
	}

	// TODO: better error
	if(!baseOk || !tigrOk) {
		aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_GENERIC, "Invalid SUP"));
		aSource->disconnect();
		return;
	}

	if(aSource->isSet(UserConnection::FLAG_INCOMING)) {
		StringList defFeatures = adcFeatures;
		if(SETTING(COMPRESS_TRANSFERS)) {
			defFeatures.push_back("AD" + UserConnection::FEATURE_ZLIB_GET);
		}
		aSource->sup(defFeatures);
	} else {
		aSource->inf(true, aSource->isSet(UserConnection::FLAG_MCN1) ? AirUtil::getSlotsPerUser(false) : 0);
	}
	aSource->setState(UserConnection::STATE_INF);
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
		aSource->myNick(aSource->getToken());
		aSource->lock(CryptoManager::getInstance()->getLock(), CryptoManager::getInstance()->getPk() + "Ref=" + aSource->getHubUrl());
	} else {
		StringList defFeatures = adcFeatures;
		if(SETTING(COMPRESS_TRANSFERS)) {
			defFeatures.push_back("AD" + UserConnection::FEATURE_ZLIB_GET);
		}
		aSource->sup(defFeatures);
		aSource->send(AdcCommand(AdcCommand::SEV_SUCCESS, AdcCommand::SUCCESS, Util::emptyString).addParam("RF", aSource->getHubUrl()));
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
		pair<string, string> i = expectedConnections.remove(aNick);
		if(i.second.empty()) {
			dcassert(i.first.empty());
			dcdebug("Unknown incoming connection from %s\n", aNick.c_str());
			putConnection(aSource);
			return;
		}
        aSource->setToken(i.first);	
		aSource->setHubUrl(i.second);
		aSource->setEncoding(ClientManager::getInstance()->findHubEncoding(i.second));
	}

	string nick = Text::toUtf8(aNick, aSource->getEncoding());
	CID cid = ClientManager::getInstance()->makeCid(nick, aSource->getHubUrl());

	// First, we try looking in the pending downloads...hopefully it's one of them...
	{
		RLock l(cs);
		for(auto cqi: downloads) {
			cqi->setErrors(0);
			if((cqi->getState() == ConnectionQueueItem::CONNECTING || cqi->getState() == ConnectionQueueItem::WAITING) && 
				cqi->getUser()->getCID() == cid)
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

	ClientManager::getInstance()->setIPUser(aSource->getUser(), aSource->getRemoteIp());

	if(ClientManager::getInstance()->isOp(aSource->getUser(), aSource->getHubUrl()))
		aSource->setFlag(UserConnection::FLAG_OP);

	if( aSource->isSet(UserConnection::FLAG_INCOMING) ) {
		aSource->myNick(aSource->getToken()); 
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

		aSource->supports(defFeatures);
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


void ConnectionManager::addPMConnection(UserConnection* uc) {
	{
		WLock l(cs);
		auto& container = cqis[CONNECTION_TYPE_PM];
		auto i = find(container.begin(), container.end(), uc->getUser());
		if (i == container.end()) {
			uc->setFlag(UserConnection::FLAG_ASSOCIATED);
			auto cqi = getCQI(uc->getHintedUser(), CONNECTION_TYPE_PM, uc->getToken());
			cqi->setState(ConnectionQueueItem::ACTIVE);
			uc->setToken(cqi->getToken());

			fire(ConnectionManagerListener::Connected(), cqi, uc);

			dcdebug("ConnectionManager::addPMConnection, PM handler\n");
			return;
		}
	}
	putConnection(uc);
}


void ConnectionManager::addDownloadConnection(UserConnection* uc) {
	dcassert(uc->isSet(UserConnection::FLAG_DOWNLOAD));
	bool addConn = false;
	{
		RLock l(cs);
		auto i = uc->isSet(UserConnection::FLAG_MCN1) ? std::find(downloads.begin(), downloads.end(), uc->getToken()) : std::find(downloads.begin(), downloads.end(), uc->getUser());
		if(i != downloads.end()) {
			ConnectionQueueItem* cqi = *i;
			if(cqi->getState() == ConnectionQueueItem::WAITING || cqi->getState() == ConnectionQueueItem::CONNECTING) {
				cqi->setState(ConnectionQueueItem::ACTIVE);
				if (uc->isSet(UserConnection::FLAG_MCN1)) {
					if (cqi->getDownloadType() == ConnectionQueueItem::TYPE_SMALL || cqi->getDownloadType() == ConnectionQueueItem::TYPE_SMALL_CONF) {
						uc->setFlag(UserConnection::FLAG_SMALL_SLOT);
						cqi->setDownloadType(ConnectionQueueItem::TYPE_SMALL_CONF);
					} else {
						cqi->setDownloadType(ConnectionQueueItem::TYPE_MCN_NORMAL);
						cqi->setFlag(ConnectionQueueItem::FLAG_MCN1);
					}
				}

				uc->setToken(cqi->getToken()); // sync for NMDC users
				uc->setHubUrl(cqi->getHubUrl()); //set the correct hint for the uc, it might not even have a hint at first.
				uc->setFlag(UserConnection::FLAG_ASSOCIATED);
				fire(ConnectionManagerListener::Connected(), cqi, uc);
				dcdebug("ConnectionManager::addDownloadConnection, leaving to downloadmanager\n");
				addConn = true;
			}
		}
	}

	if(addConn) {
		DownloadManager::getInstance()->addConnection(uc);
	} else {
		putConnection(uc);
	}
}

void ConnectionManager::addUploadConnection(UserConnection* uc) {
	dcassert(uc->isSet(UserConnection::FLAG_UPLOAD));
	bool allowAdd = true;

	{
		WLock l(cs);
		auto &uploads = cqis[CONNECTION_TYPE_UPLOAD];
		if (!uc->isSet(UserConnection::FLAG_MCN1) && find(uploads.begin(), uploads.end(), uc->getUser()) != uploads.end()) {
			//one connection per CID for non-mcn users
			allowAdd=false;
		}

		if (allowAdd) {
			allowAdd = tokens.addToken(uc->getToken(), CONNECTION_TYPE_UPLOAD);
			if (allowAdd) {
				uc->setFlag(UserConnection::FLAG_ASSOCIATED);
				ConnectionQueueItem* cqi = getCQI(uc->getHintedUser(), CONNECTION_TYPE_UPLOAD, uc->getToken());
				cqi->setState(ConnectionQueueItem::ACTIVE);
				fire(ConnectionManagerListener::Connected(), cqi, uc);
			}
		}
	}

	if (!allowAdd) {
		putConnection(uc);
		return;
	}

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
		aSource->setToken(Util::toString(Util::rand())); // set a random token instead of using the nick
		addUploadConnection(aSource);
	}
}

void ConnectionManager::on(AdcCommand::INF, UserConnection* aSource, const AdcCommand& cmd) noexcept {
	if(aSource->getState() != UserConnection::STATE_INF) {
		aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_GENERIC, "Expecting INF"));
		aSource->disconnect();
		return;
	}

	string token;

	if (aSource->isSet(UserConnection::FLAG_INCOMING)) {
		if (!cmd.getParam("TO", 0, token)) {
			aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_GENERIC, "TO missing"));
			putConnection(aSource);
			return;
		}

		aSource->setToken(token);

		// Incoming connections aren't associated with any user
		// Are we expecting this connection? Use the saved CID and hubUrl
		auto i = expectedConnections.remove(token);
		if (i.second.empty()) {
			aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_GENERIC, "Connection not expected"));
			putConnection(aSource);
			return;
		} 
		aSource->setHubUrl(i.second);

		auto user = ClientManager::getInstance()->findUser(CID(i.first));
		aSource->setUser(user);

		if (!aSource->getUser()) {
			dcdebug("CM::onINF: User not found");
			aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_GENERIC, "User not found"));
			putConnection(aSource);
			return;
		}

		// set the PM flag now in order to send a INF with PM1
		if ((tokens.hasToken(token, CONNECTION_TYPE_PM) || cmd.hasFlag("PM", 0)) && !aSource->isSet(UserConnection::FLAG_PM)) {
			aSource->setFlag(UserConnection::FLAG_PM);
		}

		aSource->inf(false, aSource->isSet(UserConnection::FLAG_MCN1) ? AirUtil::getSlotsPerUser(false) : 0);

	} else {
		dcassert(aSource->getUser());
		token = aSource->getToken();
	}

	if(!checkKeyprint(aSource)) {
		aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_GENERIC, "Keyprint validation failed"));
		putConnection(aSource);
		return;
	}
	//Cache the trusted state after keyprint verification
	if (aSource->isTrusted())
		aSource->setFlag(UserConnection::FLAG_TRUSTED);

	dcassert(!token.empty());

	bool delayedToken = false;
	{
		RLock l(cs);
		auto i = find(downloads.begin(), downloads.end(), token);
		if (i != downloads.end()) {
			ConnectionQueueItem* cqi = *i;
			if(aSource->isSet(UserConnection::FLAG_MCN1)) {
				string slots;
				if (cmd.getParam("CO", 0, slots)) {
					cqi->setMaxConns(Util::toInt(slots));
				}
			}
			cqi->setErrors(0);
			aSource->setFlag(UserConnection::FLAG_DOWNLOAD);
		} else {
			delayedToken = delayedTokens.find(token) != delayedTokens.end();
		}
	}

	if (aSource->isSet(UserConnection::FLAG_DOWNLOAD)) {
		addDownloadConnection(aSource);
	}
	else if (aSource->isSet(UserConnection::FLAG_PM) || cmd.hasFlag("PM", 0)) {
		if (!aSource->isSet(UserConnection::FLAG_PM)) 
			aSource->setFlag(UserConnection::FLAG_PM);
		addPMConnection(aSource);
	}
	else if (!delayedToken) {
		if (!aSource->isSet(UserConnection::FLAG_UPLOAD))
			aSource->setFlag(UserConnection::FLAG_UPLOAD);
		addUploadConnection(aSource);
	} else {
		putConnection(aSource);
	}
}

void ConnectionManager::force(const string& aToken) {
	RLock l(cs);
	auto i = find(downloads.begin(), downloads.end(), aToken);
	if (i != downloads.end()) {
		fire(ConnectionManagerListener::Forced(), *i);
		(*i)->setLastAttempt(0);
	}
}

bool ConnectionManager::checkKeyprint(UserConnection *aSource) {
	dcassert(aSource->getUser());

	if (!aSource->isSecure() || aSource->isTrusted())
		return true;

	string kp = ClientManager::getInstance()->getField(aSource->getUser()->getCID(), aSource->getHubUrl(), "KP");
	return aSource->verifyKeyprint(kp, SETTING(ALLOW_UNTRUSTED_CLIENTS));
}

void ConnectionManager::failDownload(const string& aToken, const string& aError, bool fatalError) {
	optional<HintedUser> mcnUser;

	{
		WLock l (cs); //this may flag other user connections as removed which would possibly cause threading issues
		auto i = find(downloads.begin(), downloads.end(), aToken);
		dcassert(i != downloads.end());
		if (i != downloads.end()) {
			ConnectionQueueItem* cqi = *i;
			if (cqi->getState() == ConnectionQueueItem::WAITING)
				return;


			if (cqi->isSet(ConnectionQueueItem::FLAG_MCN1) && !cqi->isSet(ConnectionQueueItem::FLAG_REMOVE)) {
				//remove an existing waiting item, if exists
				auto s = find_if(downloads.begin(), downloads.end(), [&](const ConnectionQueueItem* c) { 
					return c->getUser() == cqi->getUser() && c->getDownloadType() != ConnectionQueueItem::TYPE_SMALL_CONF && c->getDownloadType() != ConnectionQueueItem::TYPE_SMALL &&
						c->getState() != ConnectionQueueItem::RUNNING && c->getState() != ConnectionQueueItem::ACTIVE && c != cqi && !c->isSet(ConnectionQueueItem::FLAG_REMOVE);
				});

				if (s != downloads.end())
					(*s)->setFlag(ConnectionQueueItem::FLAG_REMOVE);
			} 
				
			if (cqi->getDownloadType() == ConnectionQueueItem::TYPE_SMALL_CONF && cqi->getState() == ConnectionQueueItem::ACTIVE) {
				//small slot item that was never used for downloading anything? check if we have normal files to download
				if (allowNewMCN(cqi))
					mcnUser = cqi->getHintedUser();
			}

			cqi->setState(ConnectionQueueItem::WAITING);

			cqi->setErrors(fatalError ? -1 : (cqi->getErrors() + 1));
			cqi->setLastAttempt(GET_TICK());
			fire(ConnectionManagerListener::Failed(), cqi, aError);
		}
	}

	if (mcnUser)
		createNewMCN(*mcnUser);
}

void ConnectionManager::failed(UserConnection* aSource, const string& aError, bool protocolError) {
	if(aSource->isSet(UserConnection::FLAG_ASSOCIATED)) {
		if(aSource->isSet(UserConnection::FLAG_DOWNLOAD)) {
			if (aSource->getState() == UserConnection::STATE_IDLE) {
				// don't remove the CQI if we are only out of downloading slots

				bool allowChange = false, hasDownload = false;
				string tmp, lastError;
				QueueManager::getInstance()->startDownload(aSource->getHintedUser(), tmp,
					aSource->isSet(UserConnection::FLAG_SMALL_SLOT) ? QueueItem::TYPE_SMALL : QueueItem::TYPE_ANY, 
					tmp, allowChange, hasDownload, lastError);

				if (hasDownload) {
					failDownload(aSource->getToken(), lastError, protocolError);
				} else {
					WLock l(cs);
					auto i = find(downloads.begin(), downloads.end(), aSource->getToken());
					dcassert(i != downloads.end());
					if (i != downloads.end()) {
						putCQI(*i);
					}
				}
			} else {
				failDownload(aSource->getToken(), aError, protocolError);
			}
		} else {

			auto type = aSource->isSet(UserConnection::FLAG_UPLOAD) ? CONNECTION_TYPE_UPLOAD :
				aSource->isSet(UserConnection::FLAG_PM) ? CONNECTION_TYPE_PM : CONNECTION_TYPE_LAST;

			if (type != CONNECTION_TYPE_LAST) {
				WLock l(cs);
				auto& container = cqis[type];
				auto i = type == CONNECTION_TYPE_PM ? find(container.begin(), container.end(), aSource->getUser()) :
					find(container.begin(), container.end(), aSource->getToken());
				dcassert(i != container.end());
				putCQI(*i);
			}

			if (type == CONNECTION_TYPE_UPLOAD)
				UploadManager::getInstance()->removeDelayUpload(*aSource);
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

void ConnectionManager::disconnect(const UserPtr& aUser) {
	RLock l(cs);
	for(auto uc: userConnections) {
		if(uc->getUser() == aUser)
			uc->disconnect(true);
	}
}

void ConnectionManager::disconnect(const string& token) {
	RLock l(cs);
	auto s = find_if(userConnections.begin(), userConnections.end(), [&](UserConnection* uc) { return compare(uc->getToken(), token) == 0; });
	if (s != userConnections.end())
		(*s)->disconnect(true);
}

void ConnectionManager::disconnect(const UserPtr& aUser, ConnectionType aConnType) {
	RLock l(cs);
	for(auto uc: userConnections) {
		if (uc->getUser() == aUser && uc->isSet(aConnType == CONNECTION_TYPE_DOWNLOAD ? UserConnection::FLAG_DOWNLOAD :
			aConnType == CONNECTION_TYPE_UPLOAD ? UserConnection::FLAG_UPLOAD : UserConnection::FLAG_PM)) {
			uc->disconnect(true);
		}
	}
}

bool ConnectionManager::setBundle(const string& token, const string& bundleToken) {
	RLock l (cs);
	auto s = find_if(userConnections.begin(), userConnections.end(), [&](UserConnection* uc) { return compare(uc->getToken(), token) == 0; });
	if (s != userConnections.end()) {
		(*s)->setLastBundle(bundleToken);
		return true;
	}
	return false;
}

void ConnectionManager::shutdown(function<void (float)> progressF) {
	TimerManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);
	shuttingDown = true;
	disconnect();

	int connections = 0;
	{
		RLock l(cs);
		connections = userConnections.size();
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
				progressF(static_cast<float>(userConnections.size()) / static_cast<float>(connections));
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
		} else if(i == UserConnection::FEATURE_AIRDC) {
			if(!conn->getUser()->isSet(User::AIRDCPLUSPLUS))
				conn->getUser()->setFlag(User::AIRDCPLUSPLUS);
		}
	}
}

} // namespace dcpp