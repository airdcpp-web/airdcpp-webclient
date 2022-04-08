/*
 * Copyright (C) 2001-2021 Jacek Sieka, arnetheduck on gmail point com
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
#include "version.h"

#include "ActivityManager.h"
#include "AdcCommand.h"
#include "AdcHub.h"
#include "Message.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "ConnectivityManager.h"
#include "CryptoManager.h"
#include "DebugManager.h"
#include "FavoriteManager.h"
#include "HashBloom.h"
#include "Localization.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "ScopedFunctor.h"
#include "SearchManager.h"
#include "ShareManager.h"
#include "SSLSocket.h"
#include "StringTokenizer.h"
#include "ThrottleManager.h"
#include "UploadManager.h"
#include "UserCommand.h"
#include "Util.h"

namespace dcpp {

const string AdcHub::CLIENT_PROTOCOL("ADC/1.0");
const string AdcHub::SECURE_CLIENT_PROTOCOL_TEST("ADCS/0.10");
const string AdcHub::ADCS_FEATURE("ADC0");
const string AdcHub::TCP4_FEATURE("TCP4");
const string AdcHub::TCP6_FEATURE("TCP6");
const string AdcHub::UDP4_FEATURE("UDP4");
const string AdcHub::UDP6_FEATURE("UDP6");
const string AdcHub::NAT0_FEATURE("NAT0");
const string AdcHub::SEGA_FEATURE("SEGA");
const string AdcHub::BASE_SUPPORT("ADBASE");
const string AdcHub::BAS0_SUPPORT("ADBAS0");
const string AdcHub::TIGR_SUPPORT("ADTIGR");
const string AdcHub::UCM0_SUPPORT("ADUCM0");
const string AdcHub::BLO0_SUPPORT("ADBLO0");
const string AdcHub::ZLIF_SUPPORT("ADZLIF");
const string AdcHub::SUD1_FEATURE("SUD1");
const string AdcHub::HBRI_SUPPORT("ADHBRI");
const string AdcHub::ASCH_FEATURE("ASCH");
const string AdcHub::CCPM_FEATURE("CCPM");

const vector<StringList> AdcHub::searchExtensions;

AdcHub::AdcHub(const string& aHubURL, const ClientPtr& aOldClient) :
	Client(aHubURL, '\n', aOldClient), udp(make_unique<Socket>(Socket::TYPE_UDP)) {

	TimerManager::getInstance()->addListener(this);
}

AdcHub::~AdcHub() {

}

void AdcHub::shutdown(ClientPtr& aClient, bool aRedirect) {
	stopValidation = true;
	if (hbriThread && hbriThread->joinable()) {
		hbriThread->join();
	}

	Client::shutdown(aClient, aRedirect);
	TimerManager::getInstance()->removeListener(this);
}

size_t AdcHub::getUserCount() const noexcept {
	size_t userCount = 0;

	RLock l(cs);
	for (const auto& u: users | map_values) {
		if (!u->isHidden()) {
			++userCount;
		}
	}
	return userCount;
}

OnlineUser& AdcHub::getUser(const uint32_t aSID, const CID& aCID) noexcept {
	auto ou = findUser(aSID);
	if(ou) {
		return *ou;
	}

	auto user = ClientManager::getInstance()->getUser(aCID);
	auto client = ClientManager::getInstance()->getClient(getHubUrl());

	{
		WLock l(cs);
		ou = users.emplace(aSID, new OnlineUser(user, client, aSID)).first->second;
		ou->inc();
	}

	return *ou;
}

OnlineUser* AdcHub::findUser(const uint32_t aSID) const noexcept {
	RLock l(cs);
	auto i = users.find(aSID);
	return i == users.end() ? nullptr : i->second;
}

OnlineUser* AdcHub::findUser(const CID& aCID) const noexcept {
	RLock l(cs);
	for(const auto& ou: users | map_values) {
		if(ou->getUser()->getCID() == aCID) {
			return ou;
		}
	}
	return 0;
}

void AdcHub::getUserList(OnlineUserList& list, bool aListHidden) const noexcept {
	RLock l(cs);
	for (const auto& i: users) {
		if (!aListHidden && i.second->isHidden()) {
			continue;
		}

		list.push_back(i.second);
	}
}

void AdcHub::putUser(const uint32_t aSID, bool aDisconnectTransfers) noexcept {
	OnlineUser* ou = nullptr;
	{
		WLock l(cs);
		auto i = users.find(aSID);
		if(i == users.end())
			return;

		ou = i->second;
		users.erase(i);

		availableBytes -= ou->getIdentity().getBytesShared();
	}

	onUserDisconnected(ou, aDisconnectTransfers);
	ou->dec();
}

void AdcHub::clearUsers() noexcept {
	SIDMap tmp;
	{
		WLock l(cs);
		users.swap(tmp);
		availableBytes = 0;
	}

	for (const auto& i: tmp) {
		if(i.first != AdcCommand::HUB_SID)
			ClientManager::getInstance()->putOffline(i.second, false);
		i.second->dec();
	}
}

void AdcHub::handle(AdcCommand::INF, AdcCommand& c) noexcept {
	if(c.getParameters().empty())
		return;

	string cid;

	OnlineUser* u = nullptr;
	bool newUser = false;
	if(c.getParam("ID", 0, cid)) {
		u = findUser(CID(cid));
		if (u) {
			if (u->getIdentity().getSID() != c.getFrom()) {
				// Same CID but different SID not allowed - buggy hub?
				string nick;
				if(!c.getParam("NI", 0, nick)) {
					nick = "[nick unknown]";
				}

				auto message = u->getIdentity().getNick() + " (" + u->getIdentity().getSIDString() +
					") has same CID {" + cid + "} as " + nick + " (" + AdcCommand::fromSID(c.getFrom()) + "), ignoring.";
				statusMessage(message, LogMessage::SEV_INFO, Util::emptyString, ClientListener::FLAG_IS_SPAM);
				return;
			}
		} else {
			u = &getUser(c.getFrom(), CID(cid));
			newUser = true;
		}
	} else if (c.getFrom() == AdcCommand::HUB_SID) {
		u = &getUser(c.getFrom(), CID());
	} else {
		u = findUser(c.getFrom());
	}

	if(!u) {
		dcdebug("AdcHub::INF Unknown user / no ID\n");
		return;
	}

	for (const auto& p: c.getParameters()) {
		if(p.length() < 2)
			continue;

		if(p.substr(0, 2) == "SS") {
			availableBytes -= u->getIdentity().getBytesShared();
			u->getIdentity().setBytesShared(p.substr(2));
			availableBytes += u->getIdentity().getBytesShared();
		} else {
			u->getIdentity().set(p.c_str(), p.substr(2));
		}
		
		if((p.substr(0, 2) == "VE") || (p.substr(0, 2) == "AP")) {
			if (p.find("AirDC++") != string::npos) {
				u->getUser()->setFlag(User::AIRDCPLUSPLUS);
			}
		}
	}

	if (u->getIdentity().isBot()) {
		u->getUser()->setFlag(User::BOT);
	} else {
		u->getUser()->unsetFlag(User::BOT);
	}

	if (u->getIdentity().supports(ADCS_FEATURE)) {
		u->getUser()->setFlag(User::TLS);

		//CCPM support flag is only sent if we support encryption too, so keep persistent here also...
		if (u->getIdentity().supports(CCPM_FEATURE)) {
			u->getUser()->setFlag(User::CCPM);
		}
	}

	if (u->getIdentity().supports(ASCH_FEATURE)) {
		u->getUser()->setFlag(User::ASCH);
	}

	if (u->getUser() == getMyIdentity().getUser()) {
		State oldState = getConnectState();
		if (oldState != STATE_NORMAL) {
			setConnectState(STATE_NORMAL);
			setAutoReconnect(true);
		}

		u->getIdentity().updateAdcConnectModes(u->getIdentity(), this);
		setMyIdentity(u->getIdentity());
		updateCounts(false);

		if (oldState != STATE_NORMAL) {
			if (u->getIdentity().getAdcConnectionSpeed(false) == 0) {
				statusMessage("WARNING: This hub is not displaying the connection speed fields, which prevents the client from choosing the best sources for downloads. Please advise the hub owner to fix this.", LogMessage::SEV_WARNING);
			}
		}

		//we have to update the modes in case our connectivity changed

		if (oldState != STATE_NORMAL || any_of(c.getParameters().begin(), c.getParameters().end(), [](const string& p) { return p.substr(0, 2) == "SU" || p.substr(0, 2) == "I4" || p.substr(0, 2) == "I6"; })) {
			fire(ClientListener::HubUpdated(), this);

			OnlineUserList ouList;

			{
				RLock l(cs);
				boost::algorithm::copy_if(users | map_values, back_inserter(ouList), [this](OnlineUser* ou) {
					return ou->getIdentity().getTcpConnectMode() != Identity::MODE_ME && ou->getIdentity().updateAdcConnectModes(getMyIdentity(), this);
				});
			}

			fire(ClientListener::UsersUpdated(), this, ouList);
		}
	} else if (stateNormal()) {
		u->getIdentity().updateAdcConnectModes(getMyIdentity(), this);
	}

	if (u->getIdentity().isHub()) {
		setHubIdentity(u->getIdentity());
		fire(ClientListener::HubUpdated(), this);
	} else if (!newUser) {
		fire(ClientListener::UserUpdated(), this, u);
	} else {
		onUserConnected(u);
	}
}

void AdcHub::handle(AdcCommand::SUP, AdcCommand& c) noexcept {
	if(getConnectState() != STATE_PROTOCOL) /** @todo SUP changes */
		return;

	bool baseOk = false;
	bool tigrOk = false;
	for(const auto& p: c.getParameters()) {
		if(p == BAS0_SUPPORT) {
			baseOk = true;
			tigrOk = true;
		} else if(p == BASE_SUPPORT) {
			baseOk = true;
		} else if(p == TIGR_SUPPORT) {
			tigrOk = true;
		} else if (p == HBRI_SUPPORT) {
			supportsHBRI = true;
		}
	}
	
	if(!baseOk) {
		statusMessage("Failed to negotiate base protocol", LogMessage::SEV_ERROR);
		disconnect(false);
		return;
	} else if(!tigrOk) {
		oldPassword = true;
		// Some hubs fake BASE support without TIGR support =/
		statusMessage("Hub probably uses an old version of ADC, please encourage the owner to upgrade", LogMessage::SEV_ERROR);
	}
}

void AdcHub::handle(AdcCommand::SID, AdcCommand& c) noexcept {
	if(getConnectState() != STATE_PROTOCOL) {
		dcdebug("Invalid state for SID\n");
		return;
	}

	if(c.getParameters().empty())
		return;

	sid = AdcCommand::toSID(c.getParam(0));

	setConnectState(STATE_IDENTIFY);
	infoImpl();
}

void AdcHub::handle(AdcCommand::MSG, AdcCommand& c) noexcept {
	if(c.getParameters().empty())
		return;

	auto message = std::make_shared<ChatMessage>(c.getParam(0), findUser(c.getFrom()));
	if(!message->getFrom())
		return;

	message->setThirdPerson(c.hasFlag("ME", 1));

	string temp;
	if (c.getParam("TS", 1, temp))
		message->setTime(Util::toTimeT(temp));

	if(c.getParam("PM", 1, temp)) { // add PM<group-cid> as well
		message->setTo(findUser(c.getTo()));
		if(!message->getTo())
			return;

		message->setReplyTo(findUser(AdcCommand::toSID(temp)));
		if(!message->getReplyTo())
			return;

		onPrivateMessage(message);
		return;
	}

	onChatMessage(message);
}

void AdcHub::handle(AdcCommand::GPA, AdcCommand& c) noexcept {
	if(c.getParameters().empty() || c.getFrom() != AdcCommand::HUB_SID)
		return;
	salt = c.getParam(0);

	onPassword();
}

void AdcHub::handle(AdcCommand::QUI, AdcCommand& c) noexcept {
	uint32_t s = AdcCommand::toSID(c.getParam(0));

	OnlineUser* victim = findUser(s);
	if(victim) {

		string tmp;
		if(c.getParam("MS", 1, tmp)) {
			OnlineUser* source = 0;
			string tmp2;
			if(c.getParam("ID", 1, tmp2)) {
				source = findUser(AdcCommand::toSID(tmp2));
			}
		
			if(source) {
				tmp = victim->getIdentity().getNick() + " was kicked by " +	source->getIdentity().getNick() + ": " + tmp;
			} else {
				tmp = victim->getIdentity().getNick() + " was kicked: " + tmp;
			}

			statusMessage(tmp, LogMessage::SEV_INFO, Util::emptyString, ClientListener::FLAG_IS_SPAM);
		}
	
		putUser(s, c.getParam("DI", 1, tmp)); 
	}
	
	if(s == sid) {
		// this QUI is directed to us

		string tmp;
		if(c.getParam("TL", 1, tmp)) {
			if(tmp == "-1") {
				setAutoReconnect(false);
			} else {
				setAutoReconnect(true);
				setReconnDelay(Util::toUInt32(tmp));
			}
		}

		if(!victim && c.getParam("MS", 1, tmp)) {
			statusMessage(tmp, LogMessage::SEV_INFO);
		}

		if(c.getParam("RD", 1, tmp)) {
			onRedirect(tmp);
		}
	}
}

void AdcHub::handle(AdcCommand::CTM, AdcCommand& c) noexcept {
	OnlineUser* u = findUser(c.getFrom());
	if(!u || u->getUser() == ClientManager::getInstance()->getMe())
		return;
	if(c.getParameters().size() < 3)
		return;

	const string& protocol = c.getParam(0);
	const string& remotePort = c.getParam(1);
	const string& token = c.getParam(2);

	bool allowSecure = false;
	if (!checkProtocol(*u, allowSecure, protocol, token))
		return;

	ConnectionManager::getInstance()->adcConnect(*u, remotePort, token, allowSecure);
}

void AdcHub::handle(AdcCommand::ZON, AdcCommand& c) noexcept {
	if (c.getFrom() != AdcCommand::HUB_SID)
		return;

	try {
		sock->setMode(BufferedSocket::MODE_ZPIPE);
	} catch (const Exception& e) {
		dcdebug("AdcHub::handleZON failed with error: %s\n", e.getError().c_str());
	}
}

void AdcHub::handle(AdcCommand::ZOF, AdcCommand& c) noexcept {
	if (c.getFrom() != AdcCommand::HUB_SID)
		return;

	try {
		sock->setMode(BufferedSocket::MODE_LINE);
	} catch (const Exception& e) {
		dcdebug("AdcHub::handleZOF failed with error: %s\n", e.getError().c_str());
	}
}

void AdcHub::handle(AdcCommand::RCM, AdcCommand& c) noexcept {
	if(c.getParameters().size() < 2) {
		return;
	}

	OnlineUser* u = findUser(c.getFrom());
	if(!u || u->getUser() == ClientManager::getInstance()->getMe())
		return;

	const string& protocol = c.getParam(0);
	const string& token = c.getParam(1);

	bool allowSecure = false;
	if (!checkProtocol(*u, allowSecure, protocol, token))
		return;

	if (getMyIdentity().isTcp4Active() || getMyIdentity().isTcp6Active()) {
		// We are active the other guy is not (and he wants to connect to us)
		connect(*u, token, allowSecure, true);
		return;
	}

	if (!u->getIdentity().supports(NAT0_FEATURE))
		return;

	// Attempt to traverse NATs and/or firewalls with TCP.
	// If they respond with their own, symmetric, RNT command, both
	// clients call ConnectionManager::adcConnect.
	send(AdcCommand(AdcCommand::CMD_NAT, u->getIdentity().getSID(), AdcCommand::TYPE_DIRECT).
		addParam(protocol).addParam(Util::toString(sock->getLocalPort())).addParam(token));
}

void AdcHub::handle(AdcCommand::CMD, AdcCommand& c) noexcept {
	if(c.getParameters().size() < 1)
		return;
	const string& name = c.getParam(0);
	bool rem = c.hasFlag("RM", 1);
	if(rem) {
		fire(ClientListener::HubUserCommand(), this, (int)UserCommand::TYPE_REMOVE, 0, name, Util::emptyString);
		return;
	}
	bool sep = c.hasFlag("SP", 1);
	string sctx;
	if(!c.getParam("CT", 1, sctx))
		return;
	int ctx = Util::toInt(sctx);
	if(ctx <= 0)
		return;
	if(sep) {
		fire(ClientListener::HubUserCommand(), this, (int)UserCommand::TYPE_SEPARATOR, ctx, name, Util::emptyString);
		return;
	}
	bool once = c.hasFlag("CO", 1);
	string txt;
	if(!c.getParam("TT", 1, txt))
		return;
	fire(ClientListener::HubUserCommand(), this, (int)(once ? UserCommand::TYPE_RAW_ONCE : UserCommand::TYPE_RAW), ctx, name, txt);
}

void AdcHub::sendUDP(const AdcCommand& cmd) noexcept {
	string command;
	string remoteIp;
	string remotePort;
	{
		RLock l(cs);
		auto i = users.find(cmd.getTo());
		if(i == users.end()) {
			dcdebug("AdcHub::sendUDP: invalid user\n");
			return;
		}
		OnlineUser& ou = *i->second;
		if (!ou.getIdentity().isUdpActive()) {
			return;
		}

		remoteIp = ou.getIdentity().getUdpIp();
		remotePort = ou.getIdentity().getUdpPort();
		command = cmd.toString(ou.getUser()->getCID());
	}

	try {
		udp->writeTo(remoteIp, remotePort, command);
	} catch(const SocketException& e) {
		dcdebug("AdcHub::sendUDP: write failed: %s\n", e.getError().c_str());
		udp->close();
	}
}

void AdcHub::handle(AdcCommand::STA, AdcCommand& c) noexcept {
	if(c.getParameters().size() < 2)
		return;

	OnlineUser* u = c.getFrom() == AdcCommand::HUB_SID ? &getUser(c.getFrom(), CID()) : findUser(c.getFrom());
	if(!u)
		return;

	int severity = Util::toInt(c.getParam(0).substr(0, 1));
	if(c.getParam(0).size() != 3) {
		return;
	}

	if (severity == AdcCommand::SUCCESS) {
		string fc;
		if(!c.getParam("FC", 1, fc) || fc.size() != 4)
			return;

		if (fc == "DSCH") {
			string token;
			if(!c.getParam("TO", 2, token))
				return;

			string resultCount;
			if(!c.getParam("RC", 2, resultCount))
				return;

			auto slash = token.find('/');
			if(slash != string::npos)
				ClientManager::getInstance()->fire(ClientManagerListener::DirectSearchEnd(), token.substr(slash+1), Util::toInt(resultCount));
		}
	} else {
		switch(Util::toInt(c.getParam(0).substr(1))) {

		case AdcCommand::ERROR_BAD_PASSWORD:
			{
				if (c.getFrom() == AdcCommand::HUB_SID)
					setPassword(Util::emptyString);
				break;
			}

		case AdcCommand::ERROR_COMMAND_ACCESS:
			{
				if (c.getFrom() == AdcCommand::HUB_SID) {
					string tmp;
					if(c.getParam("FC", 1, tmp) && tmp.size() == 4)
						forbiddenCommands.insert(AdcCommand::toFourCC(tmp.c_str()));
				}
				break;
			}

		case AdcCommand::ERROR_PROTOCOL_UNSUPPORTED:
			{
				string protocol;
				if(c.getParam("PR", 1, protocol)) {
					if(protocol == CLIENT_PROTOCOL) {
						u->getUser()->setFlag(User::NO_ADC_1_0_PROTOCOL);
					} else if(protocol == SECURE_CLIENT_PROTOCOL_TEST) {
						u->getUser()->setFlag(User::NO_ADCS_0_10_PROTOCOL);
						u->getUser()->unsetFlag(User::TLS);
					}
					// Try again...
					//ConnectionManager::getInstance()->force(u->getUser());
					string token;
					if (c.getParam("TO", 2, token))
						ConnectionManager::getInstance()->failDownload(token, STRING_F(REMOTE_PROTOCOL_UNSUPPORTED, protocol), true);
				}
				return;
			}

		case AdcCommand::ERROR_HBRI_TIMEOUT:
			{
				if (c.getFrom() == AdcCommand::HUB_SID && hbriThread && hbriThread->joinable()) {
					stopValidation = true;
					statusMessage(c.getParam(1), LogMessage::SEV_ERROR);
				}
				return;
			}
		case AdcCommand::ERROR_BAD_STATE:
			{
				string tmp;
				if (c.getParam("FC", 1, tmp) && tmp.size() == 4) {
					statusMessage(c.getParam(1) + " (command " + tmp + ", client state " + Util::toString(getConnectState()) + ")", LogMessage::SEV_ERROR);
					return;
				}
			}
		}

		auto message = std::make_shared<ChatMessage>(c.getParam(1), u);
		onChatMessage(message);
	}
}

void AdcHub::handle(AdcCommand::SCH, AdcCommand& c) noexcept {
	OnlineUser* ou = findUser(c.getFrom());
	if(!ou) {
		dcdebug("Invalid user in AdcHub::onSCH\n");
		return;
	}

	// Filter own searches
	ClientManager::getInstance()->fire(ClientManagerListener::IncomingADCSearch(), c);
	if(ou->getUser() == ClientManager::getInstance()->getMe())
		return;

	// No point to send results if downloads aren't possible
	auto mode = ou->getIdentity().getTcpConnectMode();
	if (!Identity::allowConnections(mode)) {
		return;
	}

	auto isUdpActive = Identity::isActiveMode(mode);
	SearchManager::getInstance()->respond(c, *ou, isUdpActive, getIpPort(), get(HubSettings::ShareProfile));
}

void AdcHub::handle(AdcCommand::RES, AdcCommand& c) noexcept {
	OnlineUser* ou = findUser(c.getFrom());
	if(!ou) {
		dcdebug("Invalid user in AdcHub::onRES\n");
		return;
	}
	SearchManager::getInstance()->onRES(c, ou->getUser(), ou->getIdentity().getUdpIp());
}

void AdcHub::handle(AdcCommand::PSR, AdcCommand& c) noexcept {
	OnlineUser* ou = findUser(c.getFrom());
	if(!ou) {
		dcdebug("Invalid user in AdcHub::onPSR\n");
		return;
	}
	SearchManager::getInstance()->onPSR(c, ou->getUser(), ou->getIdentity().getUdpIp());
}

void AdcHub::handle(AdcCommand::PBD, AdcCommand& c) noexcept {
	//LogManager::getInstance()->message("GOT PBD TCP: " + c.toString());
	OnlineUser* ou = findUser(c.getFrom());
	if(!ou) {
		dcdebug("Invalid user in AdcHub::onPBD\n");
		return;
	}
	SearchManager::getInstance()->onPBD(c, ou->getUser());
}


void AdcHub::handle(AdcCommand::UBD, AdcCommand& c) noexcept {
	UploadManager::getInstance()->onUBD(c);
}

void AdcHub::handle(AdcCommand::GET, AdcCommand& c) noexcept {
	if(c.getParameters().size() < 5) {
		if(c.getParameters().size() > 0) {
			if(c.getParam(0) == "blom") {
				send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_GENERIC,
					"Too few parameters for blom", AdcCommand::TYPE_HUB));
			} else {
				send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_TRANSFER_GENERIC,
					"Unknown transfer type", AdcCommand::TYPE_HUB));
			}
		} else {
			send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_GENERIC,
				"Too few parameters for GET", AdcCommand::TYPE_HUB));
		}
		return;
	}

	const string& type = c.getParam(0);
	string sk, sh;
	if(type == "blom" && c.getParam("BK", 4, sk) && c.getParam("BH", 4, sh))  {
		ByteVector v;
		size_t m = Util::toUInt32(c.getParam(3)) * 8;
		size_t k = Util::toUInt32(sk);
		size_t h = Util::toUInt32(sh);
				
		if(k > 8 || k < 1) {
			send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_TRANSFER_GENERIC, 
				"Unsupported k", AdcCommand::TYPE_HUB));
			return;
		}
		if(h > 64 || h < 1) {
			send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_TRANSFER_GENERIC, 
				"Unsupported h", AdcCommand::TYPE_HUB));
			return;
		}

		size_t n = 0;
		
		if (isSharingHub()) {
			if (SETTING(USE_PARTIAL_SHARING))
				n = QueueManager::getInstance()->getQueuedBundleFiles();

			int64_t tmp = 0;
			ShareManager::getInstance()->getProfileInfo(get(HubSettings::ShareProfile), tmp, n);
		}
		
		// Ideal size for m is n * k / ln(2), but we allow some slack
		// When h >= 32, m can't go above 2^h anyway since it's stored in a size_t.
		if(m > static_cast<size_t>((5 * Util::roundUp((int64_t)(n * k / log(2.)), (int64_t)64))) || (h < 32 && m > static_cast<size_t>(1ULL << h))) {
			send(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_TRANSFER_GENERIC,
				"Unsupported m", AdcCommand::TYPE_HUB));
			return;
		}
		
		if (m > 0) {
			dcdebug("Creating bloom filter, k=" SIZET_FMT ", m=" SIZET_FMT ", h=" SIZET_FMT "\n", k, m, h);

			HashBloom bloom;
			bloom.reset(k, m, h);
			ShareManager::getInstance()->getBloom(bloom);
			if (SETTING(USE_PARTIAL_SHARING))
				QueueManager::getInstance()->getBloom(bloom);
			bloom.copy_to(v);
		}
		AdcCommand cmd(AdcCommand::CMD_SND, AdcCommand::TYPE_HUB);
		cmd.addParam(c.getParam(0));
		cmd.addParam(c.getParam(1));
		cmd.addParam(c.getParam(2));
		cmd.addParam(c.getParam(3));
		cmd.addParam(c.getParam(4));
		send(cmd);
		if (m > 0) {
			send((char*)&v[0], v.size());
		}
	}
}

void AdcHub::handle(AdcCommand::NAT, AdcCommand& c) noexcept {
	OnlineUser* u = findUser(c.getFrom());
	if(!u || u->getUser() == ClientManager::getInstance()->getMe() || c.getParameters().size() < 3)
		return;

	const string& protocol = c.getParam(0);
	const string& remotePort = c.getParam(1);
	const string& token = c.getParam(2);

	bool allowSecure = false;
	if (!checkProtocol(*u, allowSecure, protocol, token))
		return;

	// Trigger connection attempt sequence locally ...
	auto localPort = Util::toString(sock->getLocalPort());
	dcdebug("triggering connecting attempt in NAT: remote port = %s, local IP = %s, local port = %d\n", remotePort.c_str(), sock->getLocalIp().c_str(), sock->getLocalPort());
	ConnectionManager::getInstance()->adcConnect(*u, remotePort, localPort, BufferedSocket::NAT_CLIENT, token, allowSecure);

	// ... and signal other client to do likewise.
	send(AdcCommand(AdcCommand::CMD_RNT, u->getIdentity().getSID(), AdcCommand::TYPE_DIRECT).addParam(protocol).
		addParam(localPort).addParam(token));
}

void AdcHub::handle(AdcCommand::RNT, AdcCommand& c) noexcept {
	// Sent request for NAT traversal cooperation, which
	// was acknowledged (with requisite local port information).
	OnlineUser* u = findUser(c.getFrom());
	if(!u || u->getUser() == ClientManager::getInstance()->getMe() || c.getParameters().size() < 3)
		return;

	const string& protocol = c.getParam(0);
	const string& remotePort = c.getParam(1);
	const string& token = c.getParam(2);

	bool allowSecure = false;
	if (!checkProtocol(*u, allowSecure, protocol, token))
		return;

	// Trigger connection attempt sequence locally
	dcdebug("triggering connecting attempt in RNT: remote port = %s, local IP = %s, local port = %d\n", remotePort.c_str(), sock->getLocalIp().c_str(), sock->getLocalPort());
	ConnectionManager::getInstance()->adcConnect(*u, remotePort, Util::toString(sock->getLocalPort()), BufferedSocket::NAT_SERVER, token, allowSecure);
}

void AdcHub::handle(AdcCommand::TCP, AdcCommand& c) noexcept {
	if (c.getType() != AdcCommand::TYPE_INFO)
		return;

	if (hbriThread && hbriThread->joinable()) {
		stopValidation = true;
		hbriThread->join();
	}

	// Validate the command
	if (c.getParameters().size() < 3 || c.getFrom() != AdcCommand::HUB_SID) {
		return;
	}

	string token;
	if (!c.getParam("TO", 2, token))
		return;

	string hbriHubUrl;
	bool v6 = !sock->isV6Valid();
	if (!c.getParam(v6 ? "I6" : "I4", 0, hbriHubUrl))
		return;

	string hbriPort;
	if (!c.getParam(v6 ? "P6" : "P4", 0, hbriPort))
		return;


	statusMessage(STRING_F(HBRI_VALIDATING_X, (v6 ? "IPv6" : "IPv4")), LogMessage::SEV_INFO);
	stopValidation = false;
	hbriThread.reset(new std::thread([=] { sendHBRI(hbriHubUrl, hbriPort, token, v6); }));
}

void AdcHub::sendHBRI(const string& aIP, const string& aPort, const string& aToken, bool v6) {
	// Construct the command we are going to send
	AdcCommand hbriCmd(AdcCommand::CMD_TCP, AdcCommand::TYPE_HUB);

	StringMap dummyMap;
	appendConnectivity(dummyMap, hbriCmd, !v6, v6);
	hbriCmd.addParam("TO", aToken);
	try {
		// Create the socket
		unique_ptr<Socket> hbri(isSocketSecure() ? (new SSLSocket(CryptoManager::SSL_CLIENT, SETTING(ALLOW_UNTRUSTED_HUBS), Util::emptyString)) : new Socket(Socket::TYPE_TCP));
		if (v6) {
			hbri->setLocalIp6(SETTING(BIND_ADDRESS6));
			hbri->setV4only(false);
		} else {
			hbri->setLocalIp4(SETTING(BIND_ADDRESS));
			hbri->setV4only(true);
		}

		auto snd = hbriCmd.toString(sid);
		COMMAND_DEBUG(snd, DebugManager::TYPE_HUB, DebugManager::OUTGOING, aIP + ":" + aPort);

		// Connect
		hbri->connect(AddressInfo(aIP, v6 ? AddressInfo::TYPE_V6 : AddressInfo::TYPE_V4), aPort);

		auto endTime = GET_TICK() + 10000;
		bool connSucceeded;
		while (!(connSucceeded = hbri->waitConnected(100)) && endTime >= GET_TICK()) {
			if (stopValidation) return;
		}

		if (connSucceeded) {

			// Send our command
			hbri->write(snd);

			// Wait for the hub to reply
			boost::scoped_array<char> buf(new char[8192]);

			while (endTime >= GET_TICK() && !stopValidation) {
				int read = hbri->read(&buf[0], 8192);
				if (read <= 0) {
					if (stopValidation) return;
					Thread::sleep(100);
					continue;
				}

				// We got our reply
				string l = string ((char*)&buf[0], read);
				COMMAND_DEBUG(l, DebugManager::TYPE_HUB, DebugManager::INCOMING, hbri->getIp() + ":" + aPort);

				AdcCommand response(l);
				if (response.getParameters().size() < 2) {
					statusMessage(STRING(INVALID_HUB_RESPONSE), LogMessage::SEV_ERROR);
					return;
				}

				int severity = Util::toInt(response.getParam(0).substr(0, 1));
				if (response.getParam(0).size() != 3) {
					statusMessage(STRING(INVALID_HUB_RESPONSE), LogMessage::SEV_ERROR);
					return;
				}

				if (severity == AdcCommand::SUCCESS) {
					statusMessage(STRING(VALIDATION_SUCCEEDED), LogMessage::SEV_INFO);
					return;
				} else {
					throw Exception(response.getParam(1));
				}
			}
		}
	} catch (const Exception& e) {
		statusMessage(STRING_F(HBRI_VALIDATION_FAILED, e.getError() % (v6 ? "IPv6" : "IPv4")), LogMessage::SEV_ERROR);
		return;
	}

	if (!stopValidation)
		statusMessage(STRING_F(HBRI_VALIDATION_FAILED, STRING(CONNECTION_TIMEOUT) % (v6 ? "IPv6" : "IPv4")), LogMessage::SEV_ERROR);
}

int AdcHub::connect(const OnlineUser& user, const string& token, string& lastError_) noexcept {
	bool allowSecure = CryptoManager::getInstance()->TLSOk() && user.getUser()->isSet(User::TLS);
	auto conn = allowConnect(user, allowSecure, lastError_, true);
	if (conn == AdcCommand::SUCCESS) {
		connect(user, token, allowSecure);
	}

	return conn;
}

bool AdcHub::checkProtocol(const OnlineUser& aUser, bool& secure_, const string& aRemoteProtocol, const string& aToken) noexcept {
	string failedProtocol;
	AdcCommand::Error errCode = AdcCommand::SUCCESS;

	if(aRemoteProtocol == CLIENT_PROTOCOL) {
		// Nothing special
	} else if(aRemoteProtocol == SECURE_CLIENT_PROTOCOL_TEST) {
		if (!CryptoManager::getInstance()->TLSOk())
			return false;
		secure_ = true;
	} else {
		errCode = AdcCommand::ERROR_PROTOCOL_UNSUPPORTED;
		failedProtocol = aRemoteProtocol;
	}


	if (errCode == AdcCommand::SUCCESS)
		errCode = allowConnect(aUser, secure_, failedProtocol, false);

	if (errCode != AdcCommand::SUCCESS) {
		if (errCode == AdcCommand::ERROR_TLS_REQUIRED) {
			send(AdcCommand(AdcCommand::SEV_FATAL, errCode, "TLS encryption required", AdcCommand::TYPE_DIRECT).setTo(aUser.getIdentity().getSID()));
		} else if (errCode == AdcCommand::ERROR_PROTOCOL_UNSUPPORTED) {
			AdcCommand cmd(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_UNSUPPORTED, failedProtocol + " protocol not supported", AdcCommand::TYPE_DIRECT);
			cmd.setTo(aUser.getIdentity().getSID());
			cmd.addParam("PR", failedProtocol);
			cmd.addParam("TO", aToken);

			send(cmd);
		}

		return false;
	}

	return true;
}

AdcCommand::Error AdcHub::allowConnect(const OnlineUser& aUser, bool aSecure, string& failedProtocol_, bool checkBase) const noexcept {
	//check the state
	if(!stateNormal())
		return AdcCommand::ERROR_BAD_STATE;

	if (checkBase) {
		//check the ADC protocol
		if(aSecure) {
			if(aUser.getUser()->isSet(User::NO_ADCS_0_10_PROTOCOL)) {
				failedProtocol_ = SECURE_CLIENT_PROTOCOL_TEST;
				return AdcCommand::ERROR_PROTOCOL_UNSUPPORTED;
			}
		} else {
			if(aUser.getUser()->isSet(User::NO_ADC_1_0_PROTOCOL)) {
				failedProtocol_ = CLIENT_PROTOCOL;
				return AdcCommand::ERROR_PROTOCOL_UNSUPPORTED;
			}
		}
	}

	//check TLS
	if (!aSecure && SETTING(TLS_MODE) == SettingsManager::TLS_FORCED) {
		return AdcCommand::ERROR_TLS_REQUIRED;
	}

	//check the passive mode
	if (aUser.getIdentity().getTcpConnectMode() == Identity::MODE_NOCONNECT_PASSIVE) {
		return AdcCommand::ERROR_FEATURE_MISSING;
	}

	//check the IP protocol
	if (aUser.getIdentity().getTcpConnectMode() == Identity::MODE_NOCONNECT_IP) {
		if (!getMyIdentity().getIp6().empty() && !Identity::allowV6Connections(aUser.getIdentity().getTcpConnectMode())) {
			failedProtocol_ = "IPv6";
			return AdcCommand::ERROR_PROTOCOL_UNSUPPORTED;
		}

		if (!getMyIdentity().getIp4().empty() && !Identity::allowV4Connections(aUser.getIdentity().getTcpConnectMode())) {
			failedProtocol_ = "IPv4";
			return AdcCommand::ERROR_PROTOCOL_UNSUPPORTED;
		}

		return AdcCommand::ERROR_PROTOCOL_GENERIC;
	}

	return AdcCommand::SUCCESS;
}


bool AdcHub::acceptUserConnections(const OnlineUser& aUser) noexcept {
	auto allowV4 = Identity::allowV4Connections(aUser.getIdentity().getTcpConnectMode()) && getMyIdentity().isTcp4Active();
	auto allowV6 = Identity::allowV6Connections(aUser.getIdentity().getTcpConnectMode()) && getMyIdentity().isTcp6Active();
	return allowV4 || allowV6;
}

void AdcHub::connect(const OnlineUser& aUser, const string& aToken, bool aSecure, bool aReplyingRCM) noexcept {
	const string* proto = aSecure ? &SECURE_CLIENT_PROTOCOL_TEST : &CLIENT_PROTOCOL;

	// Always let the other user connect if he requested that even if we haven't determined common connectivity
	if (aReplyingRCM || acceptUserConnections(aUser)) {
		const string& ownPort = aSecure ? ConnectionManager::getInstance()->getSecurePort() : ConnectionManager::getInstance()->getPort();
		if(ownPort.empty()) {
			// Oops?
			LogManager::getInstance()->message(STRING(NOT_LISTENING), LogMessage::SEV_ERROR, STRING(CONNECTIVITY));
			return;
		}

		if (send(AdcCommand(AdcCommand::CMD_CTM, aUser.getIdentity().getSID(), AdcCommand::TYPE_DIRECT).addParam(*proto).addParam(ownPort).addParam(aToken))) {
			//we are expecting an incoming connection from these, map so we know where its coming from.
			ConnectionManager::getInstance()->adcExpect(aToken, aUser.getUser()->getCID(), getHubUrl());
		}
	} else {
		send(AdcCommand(AdcCommand::CMD_RCM, aUser.getIdentity().getSID(), AdcCommand::TYPE_DIRECT).addParam(*proto).addParam(aToken));
	}
}

bool AdcHub::hubMessage(const string& aMessage, string& error_, bool aThirdPerson) noexcept {
	AdcCommand c(AdcCommand::CMD_MSG, AdcCommand::TYPE_BROADCAST);
	c.addParam(aMessage);
	if (aThirdPerson)
		c.addParam("ME", "1");

	if (!send(c)) {
		error_ = STRING(MAIN_PERMISSION_DENIED);
		return false;
	}

	return true;
}

bool AdcHub::privateMessage(const OnlineUserPtr& aUser, const string& aMessage, string& error_, bool aThirdPerson, bool aEcho) noexcept {
	if(!stateNormal()) {
		error_ = STRING(CONNECTING_IN_PROGRESS);
		return false;
	}

	AdcCommand c(AdcCommand::CMD_MSG, aUser->getIdentity().getSID(), aEcho ? AdcCommand::TYPE_ECHO : AdcCommand::TYPE_DIRECT);
	c.addParam(aMessage);
	if(aThirdPerson)
		c.addParam("ME", "1");
	c.addParam("PM", getMySID());
	if (!send(c)) {
		error_ = STRING(PM_PERMISSION_DENIED);
		return false;
	}

	return true;
}

void AdcHub::sendUserCmd(const UserCommand& command, const ParamMap& params) {
	if(!stateNormal())
		return;

	string cmd = Util::formatParams(command.getCommand(), params, escape);
	if(command.isChat()) {
		string error;
		if(command.getTo().empty()) {
			hubMessage(cmd, error);
		} else {
			auto ou = findUser(command.getTo());
			if (ou) {
				privateMessage(ou, cmd, error, false, false);
			}
		}
	} else {
		send(cmd);
	}
}

const vector<StringList>& AdcHub::getSearchExts() noexcept {
	if(!searchExtensions.empty())
		return searchExtensions;

	// the list is always immutable except for this function where it is initially being filled.
	const_cast<vector<StringList>&>(searchExtensions) = {
		// these extensions *must* be sorted alphabetically!
		{ "ape", "flac", "m4a", "mid", "mp3", "mpc", "ogg", "ra", "wav", "wma" },
		{ "7z", "ace", "arj", "bz2", "gz", "lha", "lzh", "rar", "tar", "z", "zip" },
		{ "doc", "docx", "htm", "html", "nfo", "odf", "odp", "ods", "odt", "pdf", "ppt", "pptx", "rtf", "txt", "xls", "xlsx", "xml", "xps" },
		{ "app", "bat", "cmd", "com", "dll", "exe", "jar", "msi", "ps1", "vbs", "wsf" },
		{ "bmp", "cdr", "eps", "gif", "ico", "img", "jpeg", "jpg", "png", "ps", "psd", "sfw", "tga", "tif", "webp" },
		{ "3gp", "asf", "asx", "avi", "divx", "flv", "mkv", "mov", "mp4", "mpeg", "mpg", "ogm", "pxp", "qt", "rm", "rmvb", "swf", "vob", "webm", "wmv" }
	};


	return searchExtensions;
}

StringList AdcHub::parseSearchExts(int flag) noexcept {
	StringList ret;
	const auto& searchExts = getSearchExts();
	for(auto i = searchExts.cbegin(), iend = searchExts.cend(); i != iend; ++i) {
		if(flag & (1 << (i - searchExts.cbegin()))) {
			ret.insert(ret.begin(), i->begin(), i->end());
		}
	}
	return ret;
}

bool AdcHub::directSearch(const OnlineUser& user, const SearchPtr& aSearch, string& error_) noexcept {
	if (!stateNormal()) {
		error_ = STRING(CONNECTING_IN_PROGRESS);
		return false;
	}

	AdcCommand c(AdcCommand::CMD_SCH, (user.getIdentity().getSID()), AdcCommand::TYPE_DIRECT);
	constructSearch(c, aSearch, true);

	if (user.getUser()->isSet(User::ASCH)) {
		if (!Util::isAdcRoot(aSearch->path)) {
			dcassert(Util::isAdcDirectoryPath(aSearch->path));
			c.addParam("PA", aSearch->path);
		}

		if (aSearch->requireReply) {
			c.addParam("RE", "1");
		}

		if (aSearch->returnParents) {
			c.addParam("PP", "1");
		}

		if (aSearch->matchType != Search::MATCH_PATH_PARTIAL) {
			c.addParam("MT", Util::toString(aSearch->matchType)); // name matches only
		}

		c.addParam("MR", Util::toString(aSearch->maxResults));
	}

	if (!send(c)) {
		error_ = STRING(PERMISSION_DENIED_HUB);
		return false;
	}

	return true;
}

void AdcHub::constructSearch(AdcCommand& c, const SearchPtr& aSearch, bool isDirect) noexcept {
	if(!aSearch->token.empty())
		c.addParam("TO", Util::toString(getToken()) + "/" + aSearch->token);

	if(aSearch->fileType == Search::TYPE_TTH) {
		c.addParam("TR", aSearch->query);

	} else {
		if(aSearch->sizeType == Search::SIZE_ATLEAST) {
			c.addParam("GE", Util::toString(aSearch->size));
		} else if(aSearch->sizeType == Search::SIZE_ATMOST) {
			c.addParam("LE", Util::toString(aSearch->size));
		} else if (aSearch->sizeType == Search::SIZE_EXACT) {
			c.addParam("GE", Util::toString(aSearch->size));
			c.addParam("LE", Util::toString(aSearch->size));
		}

		auto searchTokens = move(SearchQuery::parseSearchString(aSearch->query));
		for(const auto& t: searchTokens)
			c.addParam("AN", t);

		for(const auto& e: aSearch->excluded) {
			c.addParam("NO", e);
		}

		if(aSearch->fileType == Search::TYPE_DIRECTORY) {
			c.addParam("TY", "2");
		} else if (aSearch->fileType == Search::TYPE_FILE) {
			c.addParam("TY", "1");
		}

		if (aSearch->minDate) {
			c.addParam("NT", Util::toString(*aSearch->minDate));
		}

		if (aSearch->maxDate) {
			c.addParam("OT", Util::toString(*aSearch->maxDate));
		}

		if(aSearch->exts.size() > 2) {
			StringList exts = aSearch->exts;
			sort(exts.begin(), exts.end());

			uint8_t gr = 0;
			StringList rx;

			const auto& searchExts = getSearchExts();
			for(auto i = searchExts.cbegin(), iend = searchExts.cend(); i != iend; ++i) {
				const StringList& def = *i;

				// gather the exts not present in any of the lists
				StringList temp(def.size() + exts.size());
				temp = StringList(temp.begin(), set_symmetric_difference(def.begin(), def.end(),
					exts.begin(), exts.end(), temp.begin()));

				// figure out whether the remaining exts have to be added or removed from the set
				StringList rx_;
				bool ok = true;
				for(auto diff = temp.begin(); diff != temp.end();) {
					if(find(def.cbegin(), def.cend(), *diff) == def.cend()) {
						++diff; // will be added further below as an "EX"
					} else {
						if(rx_.size() == 2) {
							ok = false;
							break;
						}
						rx_.push_back(*diff);
						diff = temp.erase(diff);
					}
				}
				if(!ok) // too many "RX"s necessary - disregard this group
					continue;

				// let's include this group!
				gr += 1 << (i - searchExts.cbegin());

				exts = temp; // the exts to still add (that were not defined in the group)

				rx.insert(rx.begin(), rx_.begin(), rx_.end());

				if(exts.size() <= 2)
					break;
				// keep looping to see if there are more exts that can be grouped
			}

			if(gr) {
				auto appendGroupInfo = [rx, exts, gr] (AdcCommand& aCmd) -> void {
					for(const auto& ext: exts)
						aCmd.addParam("EX", ext);

					aCmd.addParam("GR", Util::toString(gr));
					for(const auto& i: rx)
						aCmd.addParam("RX", i);
				};

				if (isDirect) {
					// direct search always uses SEGA, just append the group information in the current command
					appendGroupInfo(c);
					return;
				} else {
					// some extensions can be grouped; let's send a command with grouped exts.
					AdcCommand c_gr(AdcCommand::CMD_SCH, AdcCommand::TYPE_FEATURE);
					c_gr.setFeatures('+' + SEGA_FEATURE);

					const auto& params = c.getParameters();
					for(const auto& p: params)
						c_gr.addParam(p);

					appendGroupInfo(c_gr);
					sendSearch(c_gr);

					// make sure users with the feature don't receive the search twice.
					c.setType(AdcCommand::TYPE_FEATURE);
					c.setFeatures('-' + SEGA_FEATURE);
				}
			}
		}

		for(const auto& ex: aSearch->exts)
			c.addParam("EX", ex);
	}
}

void AdcHub::search(const SearchPtr& s) noexcept {
	if(!stateNormal())
		return;

	AdcCommand c(AdcCommand::CMD_SCH, AdcCommand::TYPE_BROADCAST);

	constructSearch(c, s, false);

	if (!s->key.empty() && Util::strnicmp("adcs://", getHubUrl().c_str(), 7) == 0) {
		c.addParam("KY", s->key);
	}

	if (s->aschOnly) {
		c.setType(AdcCommand::TYPE_FEATURE);
		string features = c.getFeatures();
		c.setFeatures(features + '+' + ASCH_FEATURE);
	}

	sendSearch(c);
}

void AdcHub::sendSearch(AdcCommand& c) {
	if(isActive()) {
		send(c);
	} else {
		c.setType(AdcCommand::TYPE_FEATURE);
		string features = c.getFeatures();
		c.setFeatures(features + '+' + TCP4_FEATURE + '-' + NAT0_FEATURE);
		send(c);		
		c.setFeatures(features + "+" + NAT0_FEATURE);

		send(c);
	}
}

void AdcHub::password(const string& pwd) noexcept {
	if(getConnectState() != STATE_VERIFY)
		return;

	setPassword(pwd);
	if(!salt.empty()) {
		size_t saltBytes = salt.size() * 5 / 8;
		boost::scoped_array<uint8_t> buf(new uint8_t[saltBytes]);
		Encoder::fromBase32(salt.c_str(), &buf[0], saltBytes);
		TigerHash th;
		if(oldPassword) {
			CID cid = getMyIdentity().getUser()->getCID();
			th.update(cid.data(), CID::SIZE);
		}
		th.update(pwd.data(), pwd.length());
		th.update(&buf[0], saltBytes);
		send(AdcCommand(AdcCommand::CMD_PAS, AdcCommand::TYPE_HUB).addParam(Encoder::toBase32(th.finalize(), TigerHash::BYTES)));
		salt.clear();
	}
}

static void addParam(StringMap& lastInfoMap, AdcCommand& c, const string& var, const string& value) noexcept {
	auto i = lastInfoMap.find(var);
	if(i != lastInfoMap.end()) {
		if(i->second != value) {
			if(value.empty()) {
				lastInfoMap.erase(i);
			} else { 
				i->second = value;
			}
			c.addParam(var, value);
		}
	} else if(!value.empty()) {
		lastInfoMap.emplace(var, value);
		c.addParam(var, value);
	}
}

void AdcHub::appendConnectivity(StringMap& aLastInfoMap, AdcCommand& c, bool v4, bool v6) const noexcept {
	if (v4) {
		if(CONNSETTING(NO_IP_OVERRIDE) && !getUserIp4().empty()) {
			addParam(aLastInfoMap, c, "I4", Socket::resolve(getUserIp4(), AF_INET));
		} else {
			addParam(aLastInfoMap, c, "I4", "0.0.0.0");
		}

		if(isActiveV4()) {
			addParam(aLastInfoMap, c, "U4", SearchManager::getInstance()->getPort());
		} else {
			addParam(aLastInfoMap, c, "U4", "");
		}
	} else {
		addParam(aLastInfoMap, c, "I4", "");
		addParam(aLastInfoMap, c, "U4", "");
	}

	if (v6) {
		if (CONNSETTING(NO_IP_OVERRIDE6) && !getUserIp6().empty()) {
			addParam(aLastInfoMap, c, "I6", Socket::resolve(getUserIp6(), AF_INET6));
		} else {
			addParam(aLastInfoMap, c, "I6", "::");
		}

		if(isActiveV6()) {
			addParam(aLastInfoMap, c, "U6", SearchManager::getInstance()->getPort());
		} else {
			addParam(aLastInfoMap, c, "U6", "");
		}
	} else {
		addParam(aLastInfoMap, c, "I6", "");
		addParam(aLastInfoMap, c, "U6", "");
	}
}

void AdcHub::infoImpl() noexcept {
	if(getConnectState() != STATE_IDENTIFY && getConnectState() != STATE_NORMAL)
		return;

	reloadSettings(false);

	AdcCommand c(AdcCommand::CMD_INF, AdcCommand::TYPE_BROADCAST);

	if (stateNormal()) {
		if(!updateCounts(false))
			return;
	}

	addParam(lastInfoMap, c, "ID", ClientManager::getInstance()->getMyCID().toBase32());
	addParam(lastInfoMap, c, "PD", ClientManager::getInstance()->getMyPID().toBase32());
	addParam(lastInfoMap, c, "NI", get(Nick));
	addParam(lastInfoMap, c, "DE", get(Description));
	addParam(lastInfoMap, c, "SL", Util::toString(UploadManager::getInstance()->getSlots()));
	addParam(lastInfoMap, c, "FS", Util::toString(UploadManager::getInstance()->getFreeSlots()));

	size_t fileCount = 0;
	int64_t size = 0;
	if (isSharingHub()) {
		fileCount = SETTING(USE_PARTIAL_SHARING) ? QueueManager::getInstance()->getQueuedBundleFiles() : 0;
		ShareManager::getInstance()->getProfileInfo(get(HubSettings::ShareProfile), size, fileCount);
	}

	addParam(lastInfoMap, c, "SS", Util::toString(size));
	addParam(lastInfoMap, c, "SF", Util::toString(fileCount));

	addParam(lastInfoMap, c, "EM", get(Email));
	addParam(lastInfoMap, c, "HN", Util::toString(getDisplayCount(COUNT_NORMAL)));
	addParam(lastInfoMap, c, "HR", Util::toString(getDisplayCount(COUNT_REGISTERED)));
	addParam(lastInfoMap, c, "HO", Util::toString(getDisplayCount(COUNT_OP)));

	addParam(lastInfoMap, c, "VE", shortVersionString);
	addParam(lastInfoMap, c, "AW", ActivityManager::getInstance()->isAway() ? "1" : Util::emptyString);
	addParam(lastInfoMap, c, "LC", Localization::getLocale());

	int64_t limit = ThrottleManager::getInstance()->getDownLimit() * 1000;
	int64_t connSpeed = static_cast<int64_t>((Util::toDouble(SETTING(DOWNLOAD_SPEED)) * 1000.0 * 1000.0) / 8.0);
	addParam(lastInfoMap, c, "DS", Util::toString(limit > 0 ? min(limit, connSpeed) : connSpeed));

	limit = ThrottleManager::getInstance()->getUpLimit() * 1000;
	connSpeed = static_cast<int64_t>((Util::toDouble(SETTING(UPLOAD_SPEED)) * 1000.0 * 1000.0) / 8.0);
	addParam(lastInfoMap, c, "US", Util::toString(limit > 0 ? min(limit, connSpeed) : connSpeed));

	if(CryptoManager::getInstance()->TLSOk()) {
		auto &kp = CryptoManager::getInstance()->getKeyprint();
		addParam(lastInfoMap, c, "KP", CryptoManager::keyprintToString(kp));
	}

	bool addV4 = !sock->isV6Valid() || (get(HubSettings::Connection) != SettingsManager::INCOMING_DISABLED && supportsHBRI);
	bool addV6 = sock->isV6Valid() || (get(HubSettings::Connection6) != SettingsManager::INCOMING_DISABLED && supportsHBRI);


	// supports
	string su(SEGA_FEATURE);

	if(CryptoManager::getInstance()->TLSOk()) {
		su += "," + ADCS_FEATURE;
		su += "," + CCPM_FEATURE;
	}

	if (SETTING(ENABLE_SUDP))
		su += "," + SUD1_FEATURE;

	if(addV4 && isActiveV4()) {
		su += "," + TCP4_FEATURE;
		su += "," + UDP4_FEATURE;
	}

	if(addV6 && isActiveV6()) {
		su += "," + TCP6_FEATURE;
		su += "," + UDP6_FEATURE;
	}

	if ((addV6 && !isActiveV6() && get(HubSettings::Connection6) != SettingsManager::INCOMING_DISABLED) || 
		(addV4 && !isActiveV4() && get(HubSettings::Connection) != SettingsManager::INCOMING_DISABLED)) {
		su += "," + NAT0_FEATURE;
	}
	su += "," + ASCH_FEATURE;
	addParam(lastInfoMap, c, "SU", su);


	appendConnectivity(lastInfoMap, c, addV4, addV6);

	if(c.getParameters().size() > 0) {
		send(c);
	}
}

void AdcHub::refreshUserList(bool) noexcept {
	OnlineUserList v;

	{
		RLock l(cs);
		for (const auto& i : users) {
			if (i.first != AdcCommand::HUB_SID) {
				v.push_back(i.second);
			}
		}
	}

	fire(ClientListener::UsersUpdated(), this, v);
}

string AdcHub::checkNick(const string& aNick) noexcept {
	string tmp = aNick;
	for(size_t i = 0; i < aNick.size(); ++i) {
		if(static_cast<uint8_t>(tmp[i]) <= 32) {
			tmp[i] = '_';
		}
	}
	return tmp;
}

bool AdcHub::send(const AdcCommand& cmd) {
	if(forbiddenCommands.find(AdcCommand::toFourCC(cmd.getFourCC().c_str())) == forbiddenCommands.end()) {
		if(cmd.getType() == AdcCommand::TYPE_UDP)
			sendUDP(cmd);
		send(cmd.toString(sid));
		return true;
	}
	return false;
}

void AdcHub::on(Connected c) noexcept {
	Client::on(c);

	if(getConnectState() != STATE_PROTOCOL) {
		return;
	}

	lastInfoMap.clear();
	sid = 0;
	forbiddenCommands.clear();

	AdcCommand cmd(AdcCommand::CMD_SUP, AdcCommand::TYPE_HUB);
	cmd.addParam(BAS0_SUPPORT).addParam(BASE_SUPPORT).addParam(TIGR_SUPPORT);
	
	if(SETTING(HUB_USER_COMMANDS)) {
		cmd.addParam(UCM0_SUPPORT);
	}

	if(SETTING(BLOOM_MODE) == SettingsManager::BLOOM_ENABLED) {
		cmd.addParam(BLO0_SUPPORT);
	}

	cmd.addParam(ZLIF_SUPPORT);
	cmd.addParam(HBRI_SUPPORT);

	send(cmd);
}

void AdcHub::on(Line l, const string& aLine) noexcept {
	Client::on(l, aLine);

	if(!Text::validateUtf8(aLine)) {
		statusMessage(STRING(UTF_VALIDATION_ERROR) + "(" + Text::sanitizeUtf8(aLine) + ")", LogMessage::SEV_ERROR);
		return;
	}

	dispatch(aLine);
}

void AdcHub::on(Second s, uint64_t aTick) noexcept {
	Client::on(s, aTick);
	if(stateNormal() && (aTick > (getLastActivity() + 120*1000)) ) {
		send("\n", 1);
	}
}

OnlineUserPtr AdcHub::findUser(const string& aNick) const noexcept {
	RLock l(cs); 
	for(const auto& ou: users | map_values) { 
		if(ou->getIdentity().getNick() == aNick) { 
			return ou; 
		} 
	} 
	return nullptr; 
}

} // namespace dcpp