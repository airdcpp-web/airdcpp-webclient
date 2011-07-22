/* 
 * Copyright (C) 2001-2010 Jacek Sieka, arnetheduck on gmail point com
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
#include "DCPlusPlus.h"

#include "ConnectionManager.h"

#include "ResourceManager.h"
#include "DownloadManager.h"
#include "UploadManager.h"
#include "CryptoManager.h"
#include "ClientManager.h"
#include "QueueManager.h"
#include "LogManager.h"

#include "UserConnection.h"

namespace dcpp {

uint16_t ConnectionManager::iConnToMeCount = 0;

ConnectionManager::ConnectionManager() : floodCounter(0), server(0), secureServer(0), shuttingDown(false) {
	TimerManager::getInstance()->addListener(this);

	features.push_back(UserConnection::FEATURE_MINISLOTS);
	features.push_back(UserConnection::FEATURE_XML_BZLIST);
	features.push_back(UserConnection::FEATURE_ADCGET);
	features.push_back(UserConnection::FEATURE_TTHL);
	features.push_back(UserConnection::FEATURE_TTHF);
	features.push_back(UserConnection::FEATURE_AIRDC);

	adcFeatures.push_back("AD" + UserConnection::FEATURE_ADC_BAS0);
	adcFeatures.push_back("AD" + UserConnection::FEATURE_ADC_BASE);
	adcFeatures.push_back("AD" + UserConnection::FEATURE_ADC_TIGR);
	adcFeatures.push_back("AD" + UserConnection::FEATURE_ADC_BZIP);
	adcFeatures.push_back("AD" + UserConnection::FEATURE_ADC_MCN1);
}

void ConnectionManager::listen() throw(SocketException){
	disconnect();
	uint16_t port;

	if (BOOLSETTING(AUTO_DETECT_CONNECTION)) {
		server = new Server(false, 0, Util::emptyString);
	} else {
		port = static_cast<uint16_t>(SETTING(TCP_PORT));

		server = new Server(false, port, SETTING(BIND_ADDRESS));
	}

	if(!CryptoManager::getInstance()->TLSOk()) {
		dcdebug("Skipping secure port: %d\n", SETTING(USE_TLS));
		return;
	}

	if (BOOLSETTING(AUTO_DETECT_CONNECTION)) {
		secureServer = new Server(true, 0, Util::emptyString);
	} else {
		port = static_cast<uint16_t>(SETTING(TLS_PORT));

		secureServer = new Server(true, port, SETTING(BIND_ADDRESS));
	}
}

/**
 * Request a connection for downloading.
 * DownloadManager::addConnection will be called as soon as the connection is ready
 * for downloading.
 * @param aUser The user to connect to.
 */
void ConnectionManager::getDownloadConnection(const HintedUser& aUser, bool smallSlot) {
	bool found=false,supportMcn=false;
	dcassert((bool)aUser.user);
	{
	if (!DownloadManager::getInstance()->checkIdle(aUser.user, smallSlot)) {
		for(ConnectionQueueItem::Iter i = downloads.begin(); i != downloads.end(); ++i) {
			ConnectionQueueItem* cqi = *i;
			if (cqi->getUser().user == aUser.user) {
				found=true;
				if (cqi->isSet(ConnectionQueueItem::FLAG_MCN1)) {
					supportMcn=true;
				}
				if ((!smallSlot && cqi->isSet(ConnectionQueueItem::FLAG_SMALL_CONF)) || (smallSlot && !cqi->isSet(ConnectionQueueItem::FLAG_SMALL_CONF))) {
					continue;
				}
				if (supportMcn)
					checkWaitingMCN();
				dcdebug("Returning1!");
				return;
			}
		}
		if (smallSlot && !supportMcn && found) {
			dcdebug("Returning2!");
			return;
		}

		dcdebug("Get cqi");
		ConnectionQueueItem* cqi = getCQI(aUser, true);
		if (smallSlot)
			cqi->setFlag(ConnectionQueueItem::FLAG_SMALL);
	}
	}
}

ConnectionQueueItem* ConnectionManager::getCQI(const HintedUser& aUser, bool download, string token) {
	ConnectionQueueItem* cqi = new ConnectionQueueItem(aUser, download, token);
	if(download) {
		downloads.push_back(cqi);
	} else {
		uploads.push_back(cqi);
	}

	fire(ConnectionManagerListener::Added(), cqi);
	return cqi;
}

void ConnectionManager::putCQI(ConnectionQueueItem* cqi) {
	fire(ConnectionManagerListener::Removed(), cqi);
	if(cqi->getDownload()) {
		dcassert(find(downloads.begin(), downloads.end(), cqi) != downloads.end());
		downloads.erase(remove(downloads.begin(), downloads.end(), cqi), downloads.end());
	} else {
		UploadManager::getInstance()->removeDelayUpload(cqi->getUser());
		dcassert(find(uploads.begin(), uploads.end(), cqi) != uploads.end());
		uploads.erase(remove(uploads.begin(), uploads.end(), cqi), uploads.end());
	}
	delete cqi;
}

UserConnection* ConnectionManager::getConnection(bool aNmdc, bool secure) throw() {
	UserConnection* uc = new UserConnection(secure);
	uc->addListener(this);
	{
		Lock l(cs);
		userConnections.push_back(uc);
	}
	if(aNmdc)
		uc->setFlag(UserConnection::FLAG_NMDC);
	return uc;
}

void ConnectionManager::putConnection(UserConnection* aConn) {
	aConn->removeListener(this);
	aConn->disconnect();

	Lock l(cs);
	userConnections.erase(remove(userConnections.begin(), userConnections.end(), aConn), userConnections.end());
}

void ConnectionManager::on(TimerManagerListener::Second, uint64_t aTick) throw() {
	UserList passiveUsers;
	ConnectionQueueItem::List removed;
	{
		Lock l(cs);

		uint16_t attempts = 0;

		for(ConnectionQueueItem::Iter i = downloads.begin(); i != downloads.end(); ++i) {
			ConnectionQueueItem* cqi = *i;
			if(cqi == NULL)
				continue;

			if(cqi->getState() != ConnectionQueueItem::ACTIVE) {
				if(!cqi->getUser().user->isOnline()) {
					// Not online anymore...remove it from the pending...
					removed.push_back(cqi);
					continue;
				}

				if(cqi->getErrors() == -1 && cqi->getLastAttempt() != 0) {
					// protocol error, don't reconnect except after a forced attempt
					continue;
				}

				if(cqi->getLastAttempt() == 0 || ((SETTING(DOWNCONN_PER_SEC) == 0 || attempts < SETTING(DOWNCONN_PER_SEC)) &&
					cqi->getLastAttempt() + 60 * 1000 * max(1, cqi->getErrors()) < aTick))
				{
					cqi->setLastAttempt(aTick);

					bool smallSlot=false;
					if (cqi->isSet(ConnectionQueueItem::FLAG_SMALL))
						smallSlot=true;

					QueueItem::Priority prio = QueueManager::getInstance()->hasDownload(cqi->getUser(), smallSlot);

					if(prio == QueueItem::PAUSED) {
						removed.push_back(cqi);
						continue;
					}

					bool startDown = DownloadManager::getInstance()->startDownload(prio);

					if(cqi->getState() == ConnectionQueueItem::WAITING) {
						if(startDown) {
							cqi->setState(ConnectionQueueItem::CONNECTING);							
							ClientManager::getInstance()->connect(cqi->getUser(), cqi->getToken());
							fire(ConnectionManagerListener::StatusChanged(), cqi);
							attempts++;
						} else {
							cqi->setState(ConnectionQueueItem::NO_DOWNLOAD_SLOTS);
							fire(ConnectionManagerListener::Failed(), cqi, STRING(ALL_DOWNLOAD_SLOTS_TAKEN));
						}
					} else if(cqi->getState() == ConnectionQueueItem::NO_DOWNLOAD_SLOTS && startDown) {
						cqi->setState(ConnectionQueueItem::WAITING);
					}
				} else if(cqi->getState() == ConnectionQueueItem::CONNECTING && cqi->getLastAttempt() + 50*1000 < aTick) {
					

					cqi->setErrors(cqi->getErrors() + 1);
					fire(ConnectionManagerListener::Failed(), cqi, STRING(CONNECTION_TIMEOUT));
					cqi->setState(ConnectionQueueItem::WAITING);
				}
			}
		}

		for(ConnectionQueueItem::Iter m = removed.begin(); m != removed.end(); ++m) {
			putCQI(*m);
		}

	}

	for(UserList::iterator ui = passiveUsers.begin(); ui != passiveUsers.end(); ++ui) {
		QueueManager::getInstance()->removeSource(*ui, QueueItem::Source::FLAG_PASSIVE);
	}
}

void ConnectionManager::checkWaitingMCN() throw() {
	ConnectionQueueItem::List waitingMultiConn;
	ConnectionQueueItem::List multiUsers;
	ConnectionQueueItem::List removed;
	MultiConnMap mcnConnections;
	{

		Lock l(cs);

		for(ConnectionQueueItem::Iter i = downloads.begin(); i != downloads.end(); ++i) {
			ConnectionQueueItem* cqi = *i;
			if (cqi->isSet(ConnectionQueueItem::FLAG_MCN1)) {
				if(cqi->getState() == ConnectionQueueItem::ACTIVE && isUCRunning(cqi->getToken(), false)) {
					if(!cqi->getUser().user->isOnline()) {
						// No new connections for offline users...
						continue;
					}
					ConnectionQueueItem::Iter u = find(multiUsers.begin(), multiUsers.end(), cqi->getUser());
					if(u == multiUsers.end())
						multiUsers.push_back(cqi);
				} else {
					ConnectionQueueItem::Iter u = find(waitingMultiConn.begin(), waitingMultiConn.end(), cqi->getUser());
					if(u == waitingMultiConn.end()) {
						waitingMultiConn.push_back(cqi);
					} else {
						//there should be only one cqi waiting
						if (cqi->getState() != ConnectionQueueItem::CONNECTING && !isUCRunning(cqi->getToken(), true)) {
							removed.push_back(cqi);
						}
					}
				}
				MultiConnIter y = mcnConnections.find(cqi->getUser().user->getCID());
				if (y == mcnConnections.end()) {
					mcnConnections[cqi->getUser().user->getCID()] = 1;
				} else {
					y->second++;
				}
			}
		}

		for(ConnectionQueueItem::Iter k = multiUsers.begin(); k != multiUsers.end(); ++k) {
			ConnectionQueueItem* cqi = *k;
			ConnectionQueueItem::Iter u = std::find(waitingMultiConn.begin(), waitingMultiConn.end(), cqi->getUser());
			if (u == waitingMultiConn.end()) {
				//no connection waiting, check if we can create a new one
				MultiConnIter y = mcnConnections.find(cqi->getUser().user->getCID());
				if (y != mcnConnections.end()) {
					if (y->second >= SETTING(MAX_MCN_DOWNLOADS) && SETTING(MAX_MCN_DOWNLOADS) != 0) {
						continue;
					}
					if (y->second >= cqi->getMaxConns() && cqi->getMaxConns() != 0) {
						continue;
					}
				}

				QueueItem::Priority prio = QueueManager::getInstance()->hasDownload(cqi->getUser(), false);
				bool startDown = DownloadManager::getInstance()->startDownload(prio, true);
				if(prio != QueueItem::PAUSED && startDown) {
					ConnectionQueueItem* cqiNew = getCQI(cqi->getUser(),true);
					cqiNew->setFlag(ConnectionQueueItem::FLAG_MCN1);
				}
			}
		}

		for(ConnectionQueueItem::Iter m = removed.begin(); m != removed.end(); ++m) {
			ConnectionQueueItem* cqi1 = *m;
			dcdebug("uc+cqi remove!");
			disconnect(cqi1->getToken());
			putCQI(cqi1);
		}
	}
}

bool ConnectionManager::isUCRunning(const string token, bool requesting) {
	Lock l(cs);
	for(UserConnectionList::const_iterator i = userConnections.begin(); i != userConnections.end(); ++i) {
		UserConnection* uc = *i;
		if(uc->getToken() == token) {
			if(uc->getState() == UserConnection::STATE_RUNNING) {
				return true;
			} else if (requesting && (uc->getState() == UserConnection::STATE_SND || uc->getState() == UserConnection::STATE_IDLE)) {
				return true;
			} else {
				return false;
			}
		}
	}
	return false;
}

void ConnectionManager::on(TimerManagerListener::Minute, uint64_t aTick) throw() {
	Lock l(cs);

	for(UserConnectionList::const_iterator j = userConnections.begin(); j != userConnections.end(); ++j) {
		if(((*j)->getLastActivity() + 180*1000) < aTick) {
			(*j)->disconnect(true);
		}
	}
}

static const uint32_t FLOOD_TRIGGER = 20000;
static const uint32_t FLOOD_ADD = 2000;

ConnectionManager::Server::Server(bool secure_, uint16_t aPort, const string& ip_ /* = "0.0.0.0" */) : port(0), secure(secure_), die(false) {
	sock.create();
	sock.setSocketOpt(SO_REUSEADDR, 1);
	ip = ip_;
	port = sock.bind(aPort, ip);
	sock.listen();

	start();
}

static const uint32_t POLL_TIMEOUT = 250;

int ConnectionManager::Server::run() throw() {
	while(!die) {
	try {
			while(!die) {
				if(sock.wait(POLL_TIMEOUT, Socket::WAIT_READ) == Socket::WAIT_READ) {
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
				sock.create();
				sock.bind(port, ip);
				sock.listen();
				if(failed) {
					LogManager::getInstance()->message("Connectivity restored"); // TODO: translate
					failed = false;
				}
				break;
			} catch(const SocketException& e) {
				dcdebug("ConnectionManager::Server::run Stopped listening: %s\n", e.getError().c_str());

				if(!failed) {
					LogManager::getInstance()->message("Connectivity error: " + e.getError());
					failed = true;
				}

				// Spin for 60 seconds
				for(int i = 0; i < 60 && !die; ++i) {
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
void ConnectionManager::accept(const Socket& sock, bool secure) throw() {
	uint64_t now = GET_TICK();

	if(iConnToMeCount > 0)
		iConnToMeCount--;

	if(now > floodCounter) {
		floodCounter = now + FLOOD_ADD;
	} else {
		if(now + FLOOD_TRIGGER < floodCounter) {
			Socket s;
			try {
				s.accept(sock);
			} catch(const SocketException&) {
				// ...
			}
			dcdebug("Connection flood detected!\n");
			return;
		} else {
			if(iConnToMeCount <= 0)
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

bool ConnectionManager::checkIpFlood(const string& aServer, uint16_t aPort, const string& userInfo) {
	Lock l(cs);

	// Temporary fix to avoid spamming
	if(aPort == 80 || aPort == 2501) {
		LogManager::getInstance()->message("Someone (" + userInfo + ") is trying to use your client to spam " + aServer + ":" + Util::toString(aPort) + ", please urge hub owner to fix this");
		return true;
	}	
	
	// We don't want to be used as a flooding instrument
	uint8_t count = 0;
	for(UserConnectionList::const_iterator j = userConnections.begin(); j != userConnections.end(); ++j) {
		
		const UserConnection& uc = **j;
		
		if(uc.socket == NULL || !uc.socket->hasSocket())
			continue;

		if (uc.isSet(UserConnection::FLAG_MCN1)) {
			//yes, these may have more than 5 connections..
			continue;
		}

		if(uc.getRemoteIp() == aServer && uc.getPort() == aPort) {
			count++;
			if(count >= 5) {
				// More than 5 outbound connections to the same addr/port? Can't trust that..
				dcdebug("ConnectionManager::connect Tried to connect more than 5 times to %s:%hu, connect dropped\n", aServer.c_str(), aPort);
				return true;
			}
		}
	}
	return false;
} 
	
void ConnectionManager::nmdcConnect(const string& aServer, uint16_t aPort, const string& aNick, const string& hubUrl, string* encoding, bool stealth, bool secure) {
	nmdcConnect(aServer, aPort, 0, BufferedSocket::NAT_NONE, aNick, hubUrl, encoding, stealth, secure);
}

void ConnectionManager::nmdcConnect(const string& aServer, uint16_t aPort, uint16_t localPort, BufferedSocket::NatRoles natRole, const string& aNick, const string& hubUrl, string* encoding, bool stealth, bool secure) {
	if(shuttingDown)
		return;
		
	if(checkIpFlood(aServer, aPort, "NMDC Hub: " + hubUrl))
		return;

	UserConnection* uc = getConnection(true, secure);
	uc->setToken(aNick);
	uc->setHubUrl(hubUrl);
	uc->setEncoding(encoding);
	uc->setState(UserConnection::STATE_CONNECT);
	uc->setFlag(UserConnection::FLAG_NMDC);
	if(stealth) {
		uc->setFlag(UserConnection::FLAG_STEALTH);
	}
	try {
		uc->connect(aServer, aPort, localPort, natRole);
	} catch(const Exception&) {
		putConnection(uc);
		delete uc;
	}
}

void ConnectionManager::adcConnect(const OnlineUser& aUser, uint16_t aPort, const string& aToken, bool secure) {
	adcConnect(aUser, aPort, 0, BufferedSocket::NAT_NONE, aToken, secure);
}

void ConnectionManager::adcConnect(const OnlineUser& aUser, uint16_t aPort, uint16_t localPort, BufferedSocket::NatRoles natRole, const string& aToken, bool secure) {
	if(shuttingDown)
		return;

	if(checkIpFlood(aUser.getIdentity().getIp(), aPort, "ADC Nick: " + aUser.getIdentity().getNick() + ", Hub: " + aUser.getClientBase().getHubName()))
		return;

	UserConnection* uc = getConnection(false, secure);
	uc->setEncoding(const_cast<string*>(&Text::utf8));
	uc->setState(UserConnection::STATE_CONNECT);
	uc->setHubUrl(aUser.getClient().getHubUrl());
	uc->setToken(aToken);
	if(aUser.getIdentity().isOp()) {
		uc->setFlag(UserConnection::FLAG_OP);
	}
	try {
		uc->connect(aUser.getIdentity().getIp(), aPort, localPort, natRole);
	} catch(const Exception&) {
		putConnection(uc);
		delete uc;
	}
}

void ConnectionManager::disconnect() throw() {
	delete server;
	delete secureServer;

	server = secureServer = 0;
}

void ConnectionManager::on(AdcCommand::SUP, UserConnection* aSource, const AdcCommand& cmd) throw() {
	if(aSource->getState() != UserConnection::STATE_SUPNICK) {
		// Already got this once, ignore...@todo fix support updates
		dcdebug("CM::onSUP %p sent sup twice\n", (void*)aSource);
		return;
	}

	bool baseOk = false;
	bool tigrOk = false;

	for(StringIterC i = cmd.getParameters().begin(); i != cmd.getParameters().end(); ++i) {
		if(i->compare(0, 2, "AD") == 0) {
			string feat = i->substr(2);
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
			if(feat == UserConnection::FEATURE_ADC_MCN1) {
				aSource->setFlag(UserConnection::FLAG_MCN1);
			}
		}
	}

	if(!baseOk) {
		aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_GENERIC, "Invalid SUP"));
		aSource->disconnect();
		return;
	}

	int mcn = 0;
	if(aSource->isSet(UserConnection::FLAG_MCN1)) {
		int slots = 0;
		slots = UploadManager::getInstance()->getSlotsPerUser();
		if (slots != 0)
			mcn=slots;
	}

	if(aSource->isSet(UserConnection::FLAG_INCOMING)) {
		StringList defFeatures = adcFeatures;
		if(BOOLSETTING(COMPRESS_TRANSFERS)) {
			defFeatures.push_back("AD" + UserConnection::FEATURE_ZLIB_GET);
		}
		aSource->sup(defFeatures);
		aSource->inf(false, mcn);
	} else {
		aSource->inf(true, mcn);
	}
	aSource->setState(UserConnection::STATE_INF);
}

void ConnectionManager::on(AdcCommand::STA, UserConnection*, const AdcCommand& /*cmd*/) throw() {
	
}

void ConnectionManager::on(UserConnectionListener::Connected, UserConnection* aSource) throw() {
	if(aSource->isSecure() && !aSource->isTrusted() && !BOOLSETTING(ALLOW_UNTRUSTED_CLIENTS)) {
		putConnection(aSource);
		QueueManager::getInstance()->removeSource(aSource->getUser(), QueueItem::Source::FLAG_UNTRUSTED);
		return;
	}

	dcassert(aSource->getState() == UserConnection::STATE_CONNECT);
	if(aSource->isSet(UserConnection::FLAG_NMDC)) {
		aSource->myNick(aSource->getToken());
		aSource->lock(CryptoManager::getInstance()->getLock(), CryptoManager::getInstance()->getPk() + "Ref=" + aSource->getHubUrl());
	} else {
		StringList defFeatures = adcFeatures;
		if(BOOLSETTING(COMPRESS_TRANSFERS)) {
			defFeatures.push_back("AD" + UserConnection::FEATURE_ZLIB_GET);
		}
		aSource->sup(defFeatures);
		aSource->send(AdcCommand(AdcCommand::SEV_SUCCESS, AdcCommand::SUCCESS, Util::emptyString).addParam("RF", aSource->getHubUrl()));
	}
	aSource->setState(UserConnection::STATE_SUPNICK);
}

void ConnectionManager::on(UserConnectionListener::MyNick, UserConnection* aSource, const string& aNick) throw() {
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
		aSource->setEncoding(const_cast<string*>(&ClientManager::getInstance()->findHubEncoding(i.second)));
	}

	string nick = Text::toUtf8(aNick, *(aSource->getEncoding()));
	CID cid = ClientManager::getInstance()->makeCid(nick, aSource->getHubUrl());

	// First, we try looking in the pending downloads...hopefully it's one of them...
	{
		Lock l(cs);
		for(ConnectionQueueItem::Iter i = downloads.begin(); i != downloads.end(); ++i) {
			ConnectionQueueItem* cqi = *i;
			cqi->setErrors(0);
			if((cqi->getState() == ConnectionQueueItem::CONNECTING || cqi->getState() == ConnectionQueueItem::WAITING) && 
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

	if(ClientManager::getInstance()->isStealth(aSource->getHubUrl()))
		aSource->setFlag(UserConnection::FLAG_STEALTH);

	ClientManager::getInstance()->setIPUser(aSource->getUser(), aSource->getRemoteIp());

	if(ClientManager::getInstance()->isOp(aSource->getUser(), aSource->getHubUrl()))
		aSource->setFlag(UserConnection::FLAG_OP);

	if( aSource->isSet(UserConnection::FLAG_INCOMING) ) {
		aSource->myNick(aSource->getToken());
		aSource->lock(CryptoManager::getInstance()->getLock(), CryptoManager::getInstance()->getPk());
	}

	aSource->setState(UserConnection::STATE_LOCK);
}

void ConnectionManager::on(UserConnectionListener::CLock, UserConnection* aSource, const string& aLock) throw() {
	if(aSource->getState() != UserConnection::STATE_LOCK) {
		dcdebug("CM::onLock %p received lock twice, ignoring\n", (void*)aSource);
		return;
	}

	if( CryptoManager::getInstance()->isExtended(aLock) ) {
		// Alright, we have an extended protocol, set a user flag for this user and refresh his info...
	//	if( (aPk.find("DCPLUSPLUS") != string::npos) && aSource->getUser() && !aSource->getUser()->isSet(User::DCPLUSPLUS)) {
		//	aSource->getUser()->setFlag(User::DCPLUSPLUS);
		//}
		StringList defFeatures = features;
		if(BOOLSETTING(COMPRESS_TRANSFERS)) {
			defFeatures.push_back(UserConnection::FEATURE_ZLIB_GET);
		}

		aSource->supports(defFeatures);
	}

	aSource->setState(UserConnection::STATE_DIRECTION);
	aSource->direction(aSource->getDirectionString(), aSource->getNumber());
	aSource->key(CryptoManager::getInstance()->makeKey(aLock));


}

void ConnectionManager::on(UserConnectionListener::Direction, UserConnection* aSource, const string& dir, const string& num) throw() {
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

void ConnectionManager::addDownloadConnection(UserConnection* uc) {
	dcassert(uc->isSet(UserConnection::FLAG_DOWNLOAD));
	bool addConn = false;
	Lock l(cs);
	if (!uc->isSet(UserConnection::FLAG_MCN1)) {
		ConnectionQueueItem::Iter i = std::find(downloads.begin(), downloads.end(), uc->getUser());
		if(i != downloads.end()) {
			ConnectionQueueItem* cqi = *i;
			if(cqi->getState() == ConnectionQueueItem::WAITING || cqi->getState() == ConnectionQueueItem::CONNECTING) {
				cqi->setState(ConnectionQueueItem::ACTIVE);
				uc->setToken(cqi->getToken());
				uc->setFlag(UserConnection::FLAG_ASSOCIATED);
				fire(ConnectionManagerListener::Connected(), cqi);
				dcdebug("ConnectionManager::addDownloadConnection, leaving to downloadmanager\n");
				addConn = true;
			}
		}
	} else {
		for(ConnectionQueueItem::Iter i = downloads.begin(); i != downloads.end(); ++i) {
			ConnectionQueueItem* cqi = *i;
			if (cqi->getToken() == uc->getToken()) {
				if(cqi->getState() == ConnectionQueueItem::WAITING || cqi->getState() == ConnectionQueueItem::CONNECTING) {
					cqi->setState(ConnectionQueueItem::ACTIVE);
					if (cqi->isSet(ConnectionQueueItem::FLAG_SMALL)) {
						uc->setFlag(UserConnection::FLAG_SMALL_SLOT);
						cqi->setFlag(ConnectionQueueItem::FLAG_SMALL_CONF);
					} else {
						cqi->setFlag(ConnectionQueueItem::FLAG_MCN1);
					}
					uc->setFlag(UserConnection::FLAG_ASSOCIATED);
					fire(ConnectionManagerListener::Connected(), cqi);
				
					dcdebug("ConnectionManager::addDownloadConnection MCN, leaving to downloadmanager\n");
					addConn = true;
				}
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

	bool addConn = false;
	{
		Lock l(cs);

		ConnectionQueueItem::Iter i = find(uploads.begin(), uploads.end(), uc->getUser());
		if(i == uploads.end() || uc->isSet(UserConnection::FLAG_MCN1)) {
			string token = uc->getToken();
			ConnectionQueueItem* cqi = getCQI(uc->getHintedUser(), false, token);

			cqi->setState(ConnectionQueueItem::ACTIVE);
			uc->setFlag(UserConnection::FLAG_ASSOCIATED);

			fire(ConnectionManagerListener::Connected(), cqi);

			dcdebug("ConnectionManager::addUploadConnection, leaving to uploadmanager\n");
			addConn = true;
		}
	}

	if(addConn) {
		UploadManager::getInstance()->addConnection(uc);
	} else {
		putConnection(uc);
	}
}

void ConnectionManager::on(UserConnectionListener::Key, UserConnection* aSource, const string&/* aKey*/) throw() {
	if(aSource->getState() != UserConnection::STATE_KEY) {
		dcdebug("CM::onKey Bad state, ignoring");
		return;
	}

	aSource->setToken(Util::toString(Util::rand()));
	dcassert(aSource->getUser());

	if(aSource->isSet(UserConnection::FLAG_DOWNLOAD)) {
		addDownloadConnection(aSource);
	} else {
		addUploadConnection(aSource);
	}
}

void ConnectionManager::on(AdcCommand::INF, UserConnection* aSource, const AdcCommand& cmd) throw() {
	if(aSource->getState() != UserConnection::STATE_INF) {
		aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_GENERIC, "Expecting INF"));
		aSource->disconnect();
		return;
	}

	string cid;
	if(!cmd.getParam("ID", 0, cid)) {
		aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_INF_MISSING, "ID missing").addParam("FL", "ID"));
		dcdebug("CM::onINF missing ID\n");
		aSource->disconnect();
		return;
	}

	aSource->setUser(ClientManager::getInstance()->findUser(CID(cid)));

	if(!aSource->getUser()) {
		dcdebug("CM::onINF: User not found");
		aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_GENERIC, "User not found"));
		putConnection(aSource);
		return;
	}
	string token;
	if(aSource->isSet(UserConnection::FLAG_INCOMING)) {
		if(!cmd.getParam("TO", 0, token)) {
			aSource->send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_GENERIC, "TO missing"));
			putConnection(aSource);
			return;
		}
	} else {
		token = aSource->getToken();
	}

	bool down = false;
	{
		Lock l(cs);
		for(ConnectionQueueItem::Iter i = downloads.begin(); i != downloads.end(); ++i) {
			ConnectionQueueItem* cqi = *i;
			const string& to = cqi->getToken();
			if(to == token) {
				if(aSource->isSet(UserConnection::FLAG_MCN1)) {
					string slots;
					cmd.getParam("CO", 0, slots);
					if (Util::toInt(slots) > 0) {
						//dcdebug("DownloadSlots: %s", Util::toInt(slots));
						cqi->setMaxConns(Util::toInt(slots));
					}
				}
				cqi->setErrors(0);
				aSource->setToken(token);
				down = true;
				break;
			}
		}
		/** @todo check tokens for upload connections */
	}

	if(down) {
		aSource->setFlag(UserConnection::FLAG_DOWNLOAD);
		addDownloadConnection(aSource);
	} else {
		dcdebug("inf uppi");
		aSource->setFlag(UserConnection::FLAG_UPLOAD);
		addUploadConnection(aSource);
	}
}

void ConnectionManager::force(const string aToken) {
	Lock l(cs);

	for(ConnectionQueueItem::Iter i = downloads.begin(); i != downloads.end(); ++i) {
		ConnectionQueueItem* cqi = *i;
		if (cqi->getToken() == aToken)
			(*i)->setLastAttempt(0);
	}

	return;
}

void ConnectionManager::force(const UserPtr& aUser) {
	Lock l(cs);

	ConnectionQueueItem::Iter i = find(downloads.begin(), downloads.end(), aUser);
	if(i == downloads.end()) {
		return;
	}

	(*i)->setLastAttempt(0);
}


void ConnectionManager::failed(UserConnection* aSource, const string& aError, bool protocolError) {
	Lock l(cs);
	if(aSource->isSet(UserConnection::FLAG_ASSOCIATED)) {
		if(aSource->isSet(UserConnection::FLAG_DOWNLOAD) && !downloads.empty()) {
			for(ConnectionQueueItem::Iter i = downloads.begin(); i != downloads.end(); ++i) {
				ConnectionQueueItem* cqi = *i;
				if (aSource->getToken() == cqi->getToken()) {
					dcassert(i != downloads.end());
					cqi->setState(ConnectionQueueItem::WAITING);
					cqi->setLastAttempt(GET_TICK());
					cqi->setErrors(protocolError ? -1 : (cqi->getErrors() + 1));
					fire(ConnectionManagerListener::Failed(), cqi, aError);
					break;
				}
			}
		} else if(aSource->isSet(UserConnection::FLAG_UPLOAD) && !uploads.empty()) {
			ConnectionQueueItem::List removed;
			for(ConnectionQueueItem::Iter i = uploads.begin(); i != uploads.end(); ++i) {
				ConnectionQueueItem* cqi = *i;
				if (aSource->getToken() == cqi->getToken()) {
					removed.push_back(cqi);
					break;
				}
			}
			for(ConnectionQueueItem::Iter i = removed.begin(); i != removed.end(); ++i) {
				putCQI(*i);
			}
		}
	}
	checkWaitingMCN();
	putConnection(aSource);
}

void ConnectionManager::on(UserConnectionListener::Failed, UserConnection* aSource, const string& aError) throw() {
	failed(aSource, aError, false);
}

void ConnectionManager::on(UserConnectionListener::ProtocolError, UserConnection* aSource, const string& aError) throw() {
	failed(aSource, aError, true);
}

void ConnectionManager::disconnect(const UserPtr& aUser) {
	Lock l(cs);
	for(UserConnectionList::const_iterator i = userConnections.begin(); i != userConnections.end(); ++i) {
		UserConnection* uc = *i;
		if(uc->getUser() == aUser)
			uc->disconnect(true);
	}
}

void ConnectionManager::disconnect(const string token) {
	Lock l(cs);
	for(UserConnectionList::const_iterator i = userConnections.begin(); i != userConnections.end(); ++i) {
		UserConnection* uc = *i;
		if(uc->getToken() == token) {
			uc->disconnect(true);
			break;
		}
	}
}

void ConnectionManager::disconnect(const UserPtr& aUser, int isDownload) {
	Lock l(cs);
	for(UserConnectionList::const_iterator i = userConnections.begin(); i != userConnections.end(); ++i) {
		UserConnection* uc = *i;
		if(uc->getUser() == aUser && uc->isSet((Flags::MaskType)(isDownload ? UserConnection::FLAG_DOWNLOAD : UserConnection::FLAG_UPLOAD))) {
			uc->disconnect(true);
			break;
		}
	}
}

void ConnectionManager::shutdown() {
	TimerManager::getInstance()->removeListener(this);
	shuttingDown = true;
	disconnect();
	{
		Lock l(cs);
		for(UserConnectionList::const_iterator j = userConnections.begin(); j != userConnections.end(); ++j) {
			(*j)->disconnect(true);
		}
	}
	// Wait until all connections have died out...
	while(true) {
		{
			Lock l(cs);
			if(userConnections.empty()) {
				break;
			}
		}
		Thread::sleep(50);
	}
}		

// UserConnectionListener
void ConnectionManager::on(UserConnectionListener::Supports, UserConnection* conn, const StringList& feat) throw() {
	string sup = Util::emptyString;
	for(StringList::const_iterator i = feat.begin(); i != feat.end(); ++i) {
		sup += (string)*i + " ";
		if(*i == UserConnection::FEATURE_MINISLOTS) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_MINISLOTS);
		} else if(*i == UserConnection::FEATURE_XML_BZLIST) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_XML_BZLIST);
		} else if(*i == UserConnection::FEATURE_ADCGET) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_ADCGET);
		} else if(*i == UserConnection::FEATURE_ZLIB_GET) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_ZLIB_GET);
		} else if(*i == UserConnection::FEATURE_TTHL) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_TTHL);
		} else if(*i == UserConnection::FEATURE_TTHF) {
			conn->setFlag(UserConnection::FLAG_SUPPORTS_TTHF);
		} else if(*i == UserConnection::FEATURE_AIRDC) {
			if(!conn->getUser()->isSet(User::AIRDCPLUSPLUS))
				conn->getUser()->setFlag(User::AIRDCPLUSPLUS);
		}
	}


}

} // namespace dcpp

/**
 * @file
 * $Id: ConnectionManager.cpp 536 2010-07-28 14:40:14Z bigmuscle $
 */
