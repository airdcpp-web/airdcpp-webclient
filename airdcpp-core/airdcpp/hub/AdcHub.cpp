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
#include <airdcpp/core/version.h>

#include <airdcpp/hub/activity/ActivityManager.h>
#include <airdcpp/protocol/AdcCommand.h>
#include <airdcpp/hub/AdcHub.h>
#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/connection/ConnectionManager.h>
#include <airdcpp/connectivity/ConnectivityManager.h>
#include <airdcpp/core/crypto/CryptoManager.h>
#include <airdcpp/favorites/FavoriteManager.h>
#include <airdcpp/hub/HBRIValidation.h>
#include <airdcpp/core/localization/Localization.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/message/Message.h>
#include <airdcpp/queue/partial_sharing/PartialSharingManager.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/core/classes/ScopedFunctor.h>
#include <airdcpp/search/SearchManager.h>
#include <airdcpp/search/SearchQuery.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/connection/ThrottleManager.h>
#include <airdcpp/transfer/upload/UploadManager.h>
#include <airdcpp/hub/user_command/UserCommand.h>
#include <airdcpp/util/Util.h>

namespace dcpp {

const string AdcHub::BASE_SUPPORT("BASE");
const string AdcHub::BAS0_SUPPORT("BAS0");
const string AdcHub::TIGR_SUPPORT("TIGR");
const string AdcHub::UCM0_SUPPORT("UCM0");
const string AdcHub::BLO0_SUPPORT("BLO0");
const string AdcHub::ZLIF_SUPPORT("ZLIF");
const string AdcHub::HBRI_SUPPORT("HBRI");

const vector<StringList> AdcHub::searchExtensions = {
	// these extensions *must* be sorted alphabetically!
	{ "ape", "flac", "m4a", "mid", "mp3", "mpc", "ogg", "ra", "wav", "wma" },
	{ "7z", "ace", "arj", "bz2", "gz", "lha", "lzh", "rar", "tar", "z", "zip" },
	{ "doc", "docx", "htm", "html", "nfo", "odf", "odp", "ods", "odt", "pdf", "ppt", "pptx", "rtf", "txt", "xls", "xlsx", "xml", "xps" },
	{ "app", "bat", "cmd", "com", "dll", "exe", "jar", "msi", "ps1", "vbs", "wsf" },
	{ "bmp", "cdr", "eps", "gif", "ico", "img", "jpeg", "jpg", "png", "ps", "psd", "sfw", "tga", "tif", "webp" },
	{ "3gp", "asf", "asx", "avi", "divx", "flv", "mkv", "mov", "mp4", "mpeg", "mpg", "ogm", "pxp", "qt", "rm", "rmvb", "swf", "vob", "webm", "wmv" }
};

AdcHub::AdcHub(const string& aHubURL, const ClientPtr& aOldClient) :
	Client(aHubURL, '\n', aOldClient) {

	TimerManager::getInstance()->addListener(this);
}

AdcHub::~AdcHub() {

}

void AdcHub::shutdown(ClientPtr& aClient, bool aRedirect) {
	resetHBRI();

	Client::shutdown(aClient, aRedirect);
	TimerManager::getInstance()->removeListener(this);
}

size_t AdcHub::getUserCount() const noexcept {
	size_t userCount = 0;

	RLock l(cs);
	for (const auto& u: users | views::values) {
		if (!u->isHidden()) {
			++userCount;
		}
	}
	return userCount;
}

OnlineUserPtr AdcHub::getUser(dcpp::SID aSID, const CID& aCID) noexcept {
	auto ou = findUser(aSID);
	if (ou) {
		return ou;
	}

	auto user = ClientManager::getInstance()->getUser(aCID);
	auto client = ClientManager::getInstance()->findClient(getHubUrl());

	{
		WLock l(cs);
		ou = users.emplace(aSID, std::make_shared<OnlineUser>(user, client, aSID)).first->second;
	}

	return ou;
}

OnlineUserPtr AdcHub::findUser(dcpp::SID aSID) const noexcept {
	RLock l(cs);
	auto i = users.find(aSID);
	return i == users.end() ? nullptr : i->second;
}

OnlineUserPtr AdcHub::findUser(const CID& aCID) const noexcept {
	RLock l(cs);
	for(const auto& ou: users | views::values) {
		if(ou->getUser()->getCID() == aCID) {
			return ou;
		}
	}
	return nullptr;
}

void AdcHub::getUserList(OnlineUserList& list, bool aListHidden) const noexcept {
	RLock l(cs);
	for (const auto& [_, user] : users) {
		if (!aListHidden && user->isHidden()) {
			continue;
		}

		list.push_back(user);
	}
}

void AdcHub::putUser(dcpp::SID aSID, bool aDisconnectTransfers) noexcept {
	OnlineUserPtr ou = nullptr;
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
}

void AdcHub::clearUsers() noexcept {
	SIDMap tmp;
	{
		WLock l(cs);
		users.swap(tmp);
		availableBytes = 0;
	}

	for (const auto& [sid, user] : tmp) {
		if(sid != AdcCommand::HUB_SID)
			ClientManager::getInstance()->putOffline(user, false);
	}
}

pair<OnlineUserPtr, bool> AdcHub::parseInfUser(const AdcCommand& c) noexcept {
	string cid;
	OnlineUserPtr u = nullptr;
	bool newUser = false;
	if (c.getParam("ID", 0, cid)) {
		u = findUser(CID(cid));
		if (u) {
			if (u->getIdentity().getSID() != c.getFrom()) {
				// Same CID but different SID not allowed - buggy hub?
				string nick;
				if (!c.getParam("NI", 0, nick)) {
					nick = "[nick unknown]";
				}

				auto message = u->getIdentity().getNick() + " (" + u->getIdentity().getSIDString() +
					") has same CID {" + cid + "} as " + nick + " (" + AdcCommand::fromSID(c.getFrom()) + "), ignoring.";
				statusMessage(message, LogMessage::SEV_VERBOSE);
				return { nullptr, false };
			}
		} else {
			u = getUser(c.getFrom(), CID(cid));
			newUser = true;
		}
	} else if (c.getFrom() == AdcCommand::HUB_SID) {
		u = getUser(c.getFrom(), CID());
	} else {
		u = findUser(c.getFrom());
	}

	return { u, newUser };
}

void AdcHub::updateInfUserProperties(const OnlineUserPtr& u, const StringList& aParams) noexcept {
	for (const auto& p: aParams) {
		if(p.length() < 2)
			continue;

		if(p.starts_with("SS")) {
			availableBytes -= u->getIdentity().getBytesShared();
			u->getIdentity().setBytesShared(p.substr(2));
			availableBytes += u->getIdentity().getBytesShared();
		} else if (p.starts_with("SU")) {
			u->getIdentity().setSupports(p.substr(2));
		} else {
			u->getIdentity().set(p.c_str(), p.substr(2));
		}
	}

	if (u->getIdentity().isBot()) {
		u->getUser()->setFlag(User::BOT);
	} else {
		u->getUser()->unsetFlag(User::BOT);
	}

	if (u->getIdentity().hasSupport(OnlineUser::ADCS_FEATURE)) {
		u->getUser()->setFlag(User::TLS);

		//CCPM support flag is only sent if we support encryption too, so keep persistent here also...
		if (u->getIdentity().hasSupport(OnlineUser::CCPM_FEATURE)) {
			u->getUser()->setFlag(User::CCPM);
		}
	}

	if (u->getIdentity().hasSupport(OnlineUser::ASCH_FEATURE)) {
		u->getUser()->setFlag(User::ASCH);
	}
}

void AdcHub::recalculateConnectModes() noexcept {
	fire(ClientListener::HubUpdated(), this);

	OnlineUserList ouList;

	{
		RLock l(cs);
		ranges::copy_if(users | views::values, back_inserter(ouList), [this](const OnlineUserPtr& ou) {
			return ou->getIdentity().getTcpConnectMode() != Identity::MODE_ME && ou->getIdentity().updateAdcConnectModes(getMyIdentity(), this);
		});
	}

	fire(ClientListener::UsersUpdated(), this, ouList);
}

void AdcHub::handle(AdcCommand::INF, AdcCommand& c) noexcept {
	if(c.getParameters().empty())
		return;

	auto [u, newUser] = parseInfUser(c);
	if (!u) {
		dcdebug("AdcHub::INF Unknown user / no ID\n");
		return;
	}

	updateInfUserProperties(u, c.getParameters());

	if (u->getUser() == getMyIdentity().getUser()) {
		auto oldState = getConnectState();
		if (oldState != STATE_NORMAL) {
			setConnectState(STATE_NORMAL);
			setAutoReconnect(true);

			if (u->getIdentity().getAdcConnectionSpeed(false) == 0) {
				statusMessage("WARNING: This hub is not displaying the connection speed fields, which prevents the client from choosing the best sources for downloads. Please advise the hub owner to fix this.", LogMessage::SEV_WARNING);
			}
		}

		u->getIdentity().updateAdcConnectModes(u->getIdentity(), this);
		setMyIdentity(u->getIdentity());
		updateCounts(false);

		// We have to update the modes in case our connectivity changed
		if (oldState != STATE_NORMAL || ranges::any_of(c.getParameters(), Identity::isConnectModeParam)) {
			resetHBRI();
			recalculateConnectModes();
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
	for (const auto& param: c.getParameters()) {
		if (param.size() != 6) {
			statusMessage("Invalid support " + param + " received from the hub", LogMessage::SEV_WARNING);
			continue;
		}

		auto adding = param.starts_with("AD");
		auto removing = param.starts_with("RM");
		if (!adding && !removing) {
			continue;
		}

		auto support = param.substr(2);
		if (adding) {
			supports.add(support);
		} else if (removing) {
			supports.remove(support);
		}
	}
	
	if (!supports.includes(BAS0_SUPPORT) && !supports.includes(BASE_SUPPORT)) {
		statusMessage("Failed to negotiate base protocol", LogMessage::SEV_ERROR);
		disconnect(false);
		return;
	} else if (!supports.includes(TIGR_SUPPORT)) {
		oldPassword = true;
		// Some hubs fake BASE support without TIGR support =/
		statusMessage("Hub probably uses an old version of ADC, please encourage the owner to upgrade", LogMessage::SEV_ERROR);
	}

	if (getConnectState() != STATE_PROTOCOL) {
		fire(ClientListener::HubUpdated(), this);
	}
}

void AdcHub::handle(AdcCommand::SID, AdcCommand& c) noexcept {
	if(getConnectState() != STATE_PROTOCOL) {
		dcdebug("Invalid state for SID\n");
		return;
	}

	if(c.getParameters().empty())
		return;

	mySID = AdcCommand::toSID(c.getParam(0));

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
	auto s = AdcCommand::toSID(c.getParam(0));

	auto victim = findUser(s);
	if (victim) {
		string tmp;
		if(c.getParam("MS", 1, tmp)) {
			OnlineUserPtr source;
			if (string tmp2; c.getParam("ID", 1, tmp2)) {
				source = findUser(AdcCommand::toSID(tmp2));
			}
		
			if(source) {
				tmp = victim->getIdentity().getNick() + " was kicked by " +	source->getIdentity().getNick() + ": " + tmp;
			} else {
				tmp = victim->getIdentity().getNick() + " was kicked: " + tmp;
			}

			statusMessage(tmp, LogMessage::SEV_VERBOSE);
		}
	
		putUser(s, c.getParam("DI", 1, tmp)); 
	}
	
	if(s == mySID) {
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

#define ASSERT_DIRECT_TO_ME(c) \
	if (c.getType() == AdcCommand::TYPE_DIRECT) { \
		if (c.getTo() != myIdentity.getSID()) { \
			statusMessage("SECURITY WARNING: received a " + c.getFourCC() + " message that should have been sent to a different user. This should never happen. Please inform the hub owner in order to get the security issue fixed.", LogMessage::SEV_WARNING); \
			return; \
		} \
	}

void AdcHub::handle(AdcCommand::CTM, AdcCommand& c) noexcept {
	auto ou = findUser(c.getFrom());
	if(c.getParameters().size() < 3)
		return;

	ASSERT_DIRECT_TO_ME(c)
	const string& protocol = c.getParam(0);
	const string& remotePort = c.getParam(1);
	const string& token = c.getParam(2);

	bool allowSecure = false;
	if (!validateConnectUser(ou, allowSecure, protocol, token, remotePort))
		return;

	SocketConnectOptions options(remotePort, allowSecure);
	ConnectionManager::getInstance()->adcConnect(*ou, options, token);
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
	if (c.getParameters().size() < 2) {
		return;
	}

	auto u = findUser(c.getFrom());
	if (!u || u->getUser() == ClientManager::getInstance()->getMe())
		return;

	ASSERT_DIRECT_TO_ME(c)
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

	if (!u->getIdentity().hasSupport(OnlineUser::NAT0_FEATURE))
		return;

	// Attempt to traverse NATs and/or firewalls with TCP.
	// If they respond with their own, symmetric, RNT command, both
	// clients call ConnectionManager::adcConnect.
	sendHooked(AdcCommand(AdcCommand::CMD_NAT, u->getIdentity().getSID(), AdcCommand::TYPE_DIRECT).
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

void AdcHub::handle(AdcCommand::STA, AdcCommand& c) noexcept {
	if(c.getParameters().size() < 2)
		return;

	auto u = c.getFrom() == AdcCommand::HUB_SID ? getUser(c.getFrom(), CID()) : findUser(c.getFrom());
	if (!u)
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
			if (slash != string::npos)
				ClientManager::getInstance()->fire(ClientManagerListener::DirectSearchEnd(), token.substr(slash+1), Util::toInt(resultCount));
		}
	} else {
		onErrorMessage(c, u);
	}
}

void AdcHub::onErrorMessage(const AdcCommand& c, const OnlineUserPtr& aSender) noexcept {
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
				if(protocol == OnlineUser::CLIENT_PROTOCOL) {
					aSender->getUser()->setFlag(User::NO_ADC_1_0_PROTOCOL);
				} else if(protocol == OnlineUser::SECURE_CLIENT_PROTOCOL_TEST) {
					aSender->getUser()->setFlag(User::NO_ADCS_0_10_PROTOCOL);
					aSender->getUser()->unsetFlag(User::TLS);
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
			if (c.getFrom() == AdcCommand::HUB_SID && hbriValidator) {
				resetHBRI();
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

	auto message = std::make_shared<ChatMessage>(c.getParam(1), aSender);
	onChatMessage(message);
}

void AdcHub::handle(AdcCommand::SCH, AdcCommand& c) noexcept {
	auto ou = findUser(c.getFrom());
	if (!ou) {
		dcdebug("Invalid user in AdcHub::onSCH\n");
		return;
	}

	{
		auto remotePort = ou->getIdentity().getUdpPort();
		auto target = !remotePort.empty() ? ou->getIdentity().getUdpIp() + ":" + remotePort : ou->getIdentity().getNick();
		if (!checkIncomingSearch(target, ou)) {
			return;
		}
	}

	ASSERT_DIRECT_TO_ME(c)

	// Filter own searches
	if(ou->getUser() == ClientManager::getInstance()->getMe())
		return;

	// No point to send results if downloads aren't possible
	auto mode = ou->getIdentity().getTcpConnectMode();
	if (!Identity::allowConnections(mode)) {
		return;
	}

	auto isUdpActive = Identity::isActiveMode(mode);
	SearchManager::getInstance()->respond(c, this, ou, isUdpActive, get(HubSettings::ShareProfile));
}

void AdcHub::handle(AdcCommand::RES, AdcCommand& c) noexcept {
	auto ou = findUser(c.getFrom());
	if (!ou) {
		dcdebug("Invalid user in AdcHub::onRES\n");
		return;
	}
	ASSERT_DIRECT_TO_ME(c)
	SearchManager::getInstance()->onRES(c, ou->getUser(), ou->getIdentity().getUdpIp());
}

void AdcHub::handle(AdcCommand::GET, AdcCommand& c) noexcept {
	if(c.getParameters().size() < 5) {
		if(c.getParameters().size() > 0) {
			if(c.getParam(0) == "blom") {
				sendHooked(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_GENERIC,
					"Too few parameters for blom", AdcCommand::TYPE_HUB));
			} else {
				sendHooked(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_TRANSFER_GENERIC,
					"Unknown transfer type", AdcCommand::TYPE_HUB));
			}
		} else {
			sendHooked(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_GENERIC,
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
			sendHooked(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_TRANSFER_GENERIC,
				"Unsupported k", AdcCommand::TYPE_HUB));
			return;
		}
		if(h > 64 || h < 1) {
			sendHooked(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_TRANSFER_GENERIC,
				"Unsupported h", AdcCommand::TYPE_HUB));
			return;
		}

		size_t n = ShareManager::getInstance()->getBloomFileCount(get(HubSettings::ShareProfile));
		
		// if (isSharingHub()) {
			// if (SETTING(USE_PARTIAL_SHARING))
			//	n = QueueManager::getInstance()->getQueuedBundleFiles();

			// int64_t tmp = 0;
			// ShareManager::getInstance()->getProfileInfo(get(HubSettings::ShareProfile), tmp, n);
		// }
		
		// Ideal size for m is n * k / ln(2), but we allow some slack
		// When h >= 32, m can't go above 2^h anyway since it's stored in a size_t.
		if(m > static_cast<size_t>((5 * Util::roundUp((int64_t)(n * k / log(2.)), (int64_t)64))) || (h < 32 && m > static_cast<size_t>(1ULL << h))) {
			sendHooked(AdcCommand(AdcCommand::SEV_FATAL, AdcCommand::ERROR_TRANSFER_GENERIC,
				"Unsupported m", AdcCommand::TYPE_HUB));
			return;
		}
		
		if (m > 0) {
			dcdebug("Creating bloom filter, k=" SIZET_FMT ", m=" SIZET_FMT ", h=" SIZET_FMT "\n", k, m, h);
			ShareManager::getInstance()->getBloom(get(HubSettings::ShareProfile), v, k, m, h);
		}
		AdcCommand cmd(AdcCommand::CMD_SND, AdcCommand::TYPE_HUB);
		cmd.addParam(c.getParam(0));
		cmd.addParam(c.getParam(1));
		cmd.addParam(c.getParam(2));
		cmd.addParam(c.getParam(3));
		cmd.addParam(c.getParam(4));
		sendHooked(cmd);
		if (m > 0) {
			send((char*)&v[0], v.size());
		}
	}
}

void AdcHub::handle(AdcCommand::NAT, AdcCommand& c) noexcept {
	ASSERT_DIRECT_TO_ME(c)
	auto u = findUser(c.getFrom());
	if (c.getParameters().size() < 3)
		return;

	const string& protocol = c.getParam(0);
	const string& remotePort = c.getParam(1);
	const string& token = c.getParam(2);

	bool allowSecure = false;
	if (!validateConnectUser(u, allowSecure, protocol, token, remotePort))
		return;

	// Trigger connection attempt sequence locally ...
	auto localPort = Util::toString(sock->getLocalPort());
	dcdebug("triggering connecting attempt in NAT: remote port = %s, local IP = %s, local port = %d\n", remotePort.c_str(), sock->getLocalIp().c_str(), sock->getLocalPort());

	SocketConnectOptions options(remotePort, allowSecure, NatRole::CLIENT);
	ConnectionManager::getInstance()->adcConnect(*u, options, localPort, token);

	// ... and signal other client to do likewise.
	sendHooked(AdcCommand(AdcCommand::CMD_RNT, u->getIdentity().getSID(), AdcCommand::TYPE_DIRECT).addParam(protocol).
		addParam(localPort).addParam(token));
}

void AdcHub::handle(AdcCommand::RNT, AdcCommand& c) noexcept {
	ASSERT_DIRECT_TO_ME(c)
	// Sent request for NAT traversal cooperation, which
	// was acknowledged (with requisite local port information).
	auto u = findUser(c.getFrom());
	if(c.getParameters().size() < 3)
		return;

	const string& protocol = c.getParam(0);
	const string& remotePort = c.getParam(1);
	const string& token = c.getParam(2);

	bool allowSecure = false;
	if (!validateConnectUser(u, allowSecure, protocol, token, remotePort))
		return;

	// Trigger connection attempt sequence locally
	dcdebug("triggering connecting attempt in RNT: remote port = %s, local IP = %s, local port = %d\n", remotePort.c_str(), sock->getLocalIp().c_str(), sock->getLocalPort());

	SocketConnectOptions options(remotePort, allowSecure, NatRole::SERVER);
	ConnectionManager::getInstance()->adcConnect(*u, options, Util::toString(sock->getLocalPort()), token);
}

void AdcHub::resetHBRI() noexcept {
	if (hbriValidator) {
		hbriValidator->stopAndWait();
		hbriValidator.reset();
	}
}

void AdcHub::handle(AdcCommand::TCP, AdcCommand& c) noexcept {
	if (c.getType() != AdcCommand::TYPE_INFO)
		return;

	resetHBRI();

	// Validate the command
	if (c.getParameters().size() < 3 || c.getFrom() != AdcCommand::HUB_SID) {
		return;
	}

	HBRIValidator::ConnectInfo connectInfo(!sock->isV6Valid(), isSocketSecure());

	string token;
	if (!c.getParam("TO", 2, token))
		return;

	if (!c.getParam(connectInfo.v6 ? "I6" : "I4", 0, connectInfo.ip))
		return;

	if (!c.getParam(connectInfo.v6 ? "P6" : "P4", 0, connectInfo.port))
		return;

	statusMessage(STRING_F(HBRI_VALIDATING_X, (connectInfo.v6 ? "IPv6" : "IPv4")), LogMessage::SEV_INFO);

	hbriValidator = make_unique<HBRIValidator>(
		connectInfo,
		getHBRIRequest(connectInfo.v6, token).toString(mySID),
		[this](auto... args) { statusMessage(args...); }
	);
}

AdcCommand AdcHub::getHBRIRequest(bool v6, const string& aToken) const noexcept {
	AdcCommand hbriCmd(AdcCommand::CMD_TCP, AdcCommand::TYPE_HUB);

	StringMap dummyMap;
	appendConnectivity(dummyMap, hbriCmd, !v6, v6);
	hbriCmd.addParam("TO", aToken);
	return hbriCmd;
}

int AdcHub::connect(const OnlineUser& user, const string& token, string& lastError_) noexcept {
	bool allowSecure = CryptoManager::getInstance()->TLSOk() && user.getUser()->isSet(User::TLS);
	auto conn = allowConnect(user, allowSecure, lastError_, true);
	if (conn == AdcCommand::SUCCESS) {
		connect(user, token, allowSecure);
	}

	return conn;
}


bool AdcHub::validateConnectUser(const OnlineUserPtr& aUser, bool& secure_, const string& aRemoteProtocol, const string& aToken, const string& aRemotePort) noexcept {
	if (!aUser || aUser->getUser() == ClientManager::getInstance()->getMe())
		return false;

	if (!checkProtocol(*aUser, secure_, aRemoteProtocol, aToken)) {
		return false;
	}

	auto target = aUser->getIdentity().getTcpConnectIp() + ":" + aRemotePort;
	if (!checkIncomingCTM(target, aUser)) {
		return false;
	}

	return true;
}

bool AdcHub::checkProtocol(const OnlineUser& aUser, bool& secure_, const string& aRemoteProtocol, const string& aToken) noexcept {
	string failedProtocol;
	AdcCommand::Error errCode = AdcCommand::SUCCESS;

	if(aRemoteProtocol == OnlineUser::CLIENT_PROTOCOL) {
		// Nothing special
	} else if(aRemoteProtocol == OnlineUser::SECURE_CLIENT_PROTOCOL_TEST) {
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
			sendHooked(AdcCommand(AdcCommand::SEV_FATAL, errCode, "TLS encryption required", AdcCommand::TYPE_DIRECT).setTo(aUser.getIdentity().getSID()));
		} else if (errCode == AdcCommand::ERROR_PROTOCOL_UNSUPPORTED) {
			AdcCommand cmd(AdcCommand::SEV_FATAL, AdcCommand::ERROR_PROTOCOL_UNSUPPORTED, failedProtocol + " protocol not supported", AdcCommand::TYPE_DIRECT);
			cmd.setTo(aUser.getIdentity().getSID());
			cmd.addParam("PR", failedProtocol);
			cmd.addParam("TO", aToken);

			sendHooked(cmd);
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
				failedProtocol_ = OnlineUser::SECURE_CLIENT_PROTOCOL_TEST;
				return AdcCommand::ERROR_PROTOCOL_UNSUPPORTED;
			}
		} else {
			if(aUser.getUser()->isSet(User::NO_ADC_1_0_PROTOCOL)) {
				failedProtocol_ = OnlineUser::CLIENT_PROTOCOL;
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


bool AdcHub::acceptUserConnections(const OnlineUser& aUser) const noexcept {
	auto allowV4 = Identity::allowV4Connections(aUser.getIdentity().getTcpConnectMode()) && getMyIdentity().isTcp4Active();
	auto allowV6 = Identity::allowV6Connections(aUser.getIdentity().getTcpConnectMode()) && getMyIdentity().isTcp6Active();
	return allowV4 || allowV6;
}

void AdcHub::connect(const OnlineUser& aUser, const string& aToken, bool aSecure, bool aReplyingRCM) noexcept {
	const string* proto = aSecure ? &OnlineUser::SECURE_CLIENT_PROTOCOL_TEST : &OnlineUser::CLIENT_PROTOCOL;

	// Always let the other user connect if he requested that even if we haven't determined common connectivity
	if (aReplyingRCM || acceptUserConnections(aUser)) {
		const string& ownPort = aSecure ? ConnectionManager::getInstance()->getSecurePort() : ConnectionManager::getInstance()->getPort();
		if(ownPort.empty()) {
			// Oops?
			LogManager::getInstance()->message(STRING(NOT_LISTENING), LogMessage::SEV_ERROR, STRING(CONNECTIVITY));
			return;
		}

		auto cmd = AdcCommand(AdcCommand::CMD_CTM, aUser.getIdentity().getSID(), AdcCommand::TYPE_DIRECT).addParam(*proto).addParam(ownPort).addParam(aToken);
		auto user = aUser.getUser();
		callAsync([=, this, cmd = std::move(cmd)] {
			if (sendHooked(cmd)) {
				// We are expecting an incoming connection from these, map so we know where its coming from.
				ConnectionManager::getInstance()->adcExpect(aToken, user->getCID(), getHubUrl());
			}
		});
	} else {
		auto cmd = AdcCommand(AdcCommand::CMD_RCM, aUser.getIdentity().getSID(), AdcCommand::TYPE_DIRECT).addParam(*proto).addParam(aToken);
		callAsync([this, cmd = std::move(cmd)] {
			sendHooked(cmd);
		});
	}
}

bool AdcHub::hubMessageHooked(const OutgoingChatMessage& aMessage, string& error_) noexcept {
	AdcCommand c(AdcCommand::CMD_MSG, AdcCommand::TYPE_BROADCAST);
	c.addParam(aMessage.text);
	if (aMessage.thirdPerson)
		c.addParam("ME", "1");

	if (!sendHooked(c, aMessage.owner, error_)) {
		return false;
	}

	return true;
}

bool AdcHub::privateMessageHooked(const OnlineUserPtr& aUser, const OutgoingChatMessage& aMessage, string& error_, bool aEcho) noexcept {
	if(!stateNormal()) {
		error_ = STRING(CONNECTING_IN_PROGRESS);
		return false;
	}

	AdcCommand c(AdcCommand::CMD_MSG, aUser->getIdentity().getSID(), aEcho ? AdcCommand::TYPE_ECHO : AdcCommand::TYPE_DIRECT);
	c.addParam(aMessage.text);
	if(aMessage.thirdPerson)
		c.addParam("ME", "1");
	c.addParam("PM", getMySID());

	if (!sendHooked(c, aMessage.owner, error_)) {
		return false;
	}

	return true;
}

void AdcHub::sendUserCmd(const UserCommand& aUserCommand, const ParamMap& params) {
	if(!stateNormal())
		return;

	string userCommandStr = Util::formatParams(aUserCommand.getCommand(), params, escape);
	if (aUserCommand.isChat()) {
		callAsync([aUserCommand, this, userCommandStr = std::move(userCommandStr)] {
			// This probably shouldn't trigger the message hooks...
			string error;
			OutgoingChatMessage c(userCommandStr, this, "user_command", false);
			if (aUserCommand.getTo().empty()) {
				hubMessageHooked(c, error);
			} else {
				auto ou = findUser(aUserCommand.getTo());
				if (ou) {
					privateMessageHooked(ou, c, error, false);
				}
			}
		});
	} else {
		send(userCommandStr);
	}
}

const vector<StringList>& AdcHub::getSearchExts() noexcept {
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

bool AdcHub::directSearchHooked(const OnlineUser& user, const SearchPtr& aSearch, string& error_) noexcept {
	if (!stateNormal()) {
		error_ = STRING(CONNECTING_IN_PROGRESS);
		return false;
	}

	AdcCommand c(AdcCommand::CMD_SCH, (user.getIdentity().getSID()), AdcCommand::TYPE_DIRECT);
	if (user.getUser()->isSet(User::ASCH)) {
		if (!PathUtil::isAdcRoot(aSearch->path)) {
			dcassert(PathUtil::isAdcDirectoryPath(aSearch->path));
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

	if (!sendSearchHooked(c, aSearch, &user)) {
		error_ = STRING(PERMISSION_DENIED_HUB);
		return false;
	}

	return true;
}

uint8_t AdcHub::groupExtensions(StringList& exts_, StringList& excluded_) noexcept {
	uint8_t gr = 0;
	const auto& searchExts = getSearchExts();
	for (auto i = searchExts.cbegin(), iend = searchExts.cend(); i != iend; ++i) {
		const StringList& def = *i;

		// gather the exts not present in any of the lists
		StringList temp(def.size() + exts_.size());
		temp = StringList(temp.begin(), set_symmetric_difference(def.begin(), def.end(),
			exts_.begin(), exts_.end(), temp.begin()));

		// figure out whether the remaining exts have to be added or removed from the set
		StringList rx_;
		bool ok = true;
		for (auto diff = temp.begin(); diff != temp.end();) {
			if (find(def.cbegin(), def.cend(), *diff) == def.cend()) {
				++diff; // will be added further below as an "EX"
			} else {
				if (rx_.size() == 2) {
					ok = false;
					break;
				}
				rx_.push_back(*diff);
				diff = temp.erase(diff);
			}
		}
		if (!ok) // too many "RX"s necessary - disregard this group
			continue;

		// let's include this group!
		gr += 1 << (i - searchExts.cbegin());

		exts_ = temp; // the exts to still add (that were not defined in the group)

		excluded_.insert(excluded_.begin(), rx_.begin(), rx_.end());

		if (exts_.size() <= 2)
			break;
		// keep looping to see if there are more exts that can be grouped
	}

	return gr;
}

void AdcHub::handleSearchExtensions(AdcCommand& c, const SearchPtr& aSearch, const OnlineUser* aDirectUser) noexcept {
	auto appendAllExtensions = [&aSearch](AdcCommand& aCmd) {
		for (const auto& ex : aSearch->exts)
			aCmd.addParam("EX", ex);
	};

	if (aSearch->exts.size() <= 2) {
		// No need for grouping
		appendAllExtensions(c);
		return;
	}

	StringList exts = aSearch->exts;
	sort(exts.begin(), exts.end());

	StringList excluded;
	auto group = groupExtensions(exts, excluded);

	if (!group) {
		return;
	}

	auto appendGroupInfo = [&excluded, &exts, group] (AdcCommand& aCmd) {
		for(const auto& ext: exts)
			aCmd.addParam("EX", ext);

		aCmd.addParam("GR", Util::toString(group));
		for(const auto& i: excluded)
			aCmd.addParam("RX", i);
	};

	if (aDirectUser) {
		if (aDirectUser->getIdentity().hasSupport(OnlineUser::SEGA_FEATURE)) {
			appendGroupInfo(c);
		} else {
			appendAllExtensions(c);
		}
	} else {
		// some extensions can be grouped; let's send a command with grouped exts.
		AdcCommand c_gr(AdcCommand::CMD_SCH, AdcCommand::TYPE_FEATURE);
		c_gr.setFeatures(c.getFeatures());
		c_gr.addFeature(OnlineUser::SEGA_FEATURE, AdcCommand::FeatureType::REQUIRED);

		const auto& params = c.getParameters();
		for(const auto& p: params)
			c_gr.addParam(p);

		appendGroupInfo(c_gr);
		sendSearch(c_gr);

		// make sure users with the feature don't receive the search twice.
		c.setType(AdcCommand::TYPE_FEATURE);
		c.addFeature(OnlineUser::SEGA_FEATURE, AdcCommand::FeatureType::EXCLUDED);

		appendAllExtensions(c);
	}
}

bool AdcHub::sendSearchHooked(AdcCommand& c, const SearchPtr& aSearch, const OnlineUser* aDirectUser) noexcept {
	if(!aSearch->token.empty())
		c.addParam("TO", Util::toString(getToken()) + "/" + aSearch->token);

	if(aSearch->fileType == Search::TYPE_TTH) {
		c.addParam("TR", aSearch->query);

	} else {
		if (aSearch->minSize) {
			c.addParam("GE", Util::toString(*aSearch->minSize));
		} 
		
		if (aSearch->maxSize) {
			c.addParam("LE", Util::toString(*aSearch->maxSize));
		}

		auto searchTokens = SearchQuery::parseSearchString(aSearch->query);
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

		if (!aSearch->key.empty() && isSocketSecure() && myIdentity.isUdpActive()) {
			c.addParam("KY", aSearch->key);
		}

		handleSearchExtensions(c, aSearch, aDirectUser);
	}

	return sendHooked(c);
}

void AdcHub::search(const SearchPtr& s) noexcept {
	if(!stateNormal())
		return;

	callAsync([this, s] {
		AdcCommand c(AdcCommand::CMD_SCH, AdcCommand::TYPE_BROADCAST);
		if (s->aschOnly) {
			c.setType(AdcCommand::TYPE_FEATURE);
			c.addFeature(OnlineUser::ASCH_FEATURE, AdcCommand::FeatureType::REQUIRED);
		}

		sendSearchHooked(c, s, nullptr);
	});
}

void AdcHub::sendSearch(AdcCommand& c) {
	if (isActive()) {
		sendHooked(c);
	} else {
		c.setType(AdcCommand::TYPE_FEATURE);
		string features = c.getFeatures();
		c.setFeatures(features + '+' + OnlineUser::TCP4_FEATURE + '-' + OnlineUser::NAT0_FEATURE);
		sendHooked(c);
		c.setFeatures(features + "+" + OnlineUser::NAT0_FEATURE);

		sendHooked(c);
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
		salt.clear();

		auto cmd = AdcCommand(AdcCommand::CMD_PAS, AdcCommand::TYPE_HUB).addParam(Encoder::toBase32(th.finalize(), TigerHash::BYTES));
		callAsync([this, cmd = std::move(cmd)] {
			sendHooked(cmd);
		});
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
		lastInfoMap.try_emplace(var, value);
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


void AdcHub::appendClientSupports(StringMap& aLastInfoMap, AdcCommand& c, bool v4, bool v6) const noexcept {
	string su(OnlineUser::SEGA_FEATURE);

	if (CryptoManager::getInstance()->TLSOk()) {
		su += "," + OnlineUser::ADCS_FEATURE;
		su += "," + OnlineUser::CCPM_FEATURE;
	}

	if (SETTING(ENABLE_SUDP)) {
		su += "," + OnlineUser::SUD1_FEATURE;
	}

	if (v4 && isActiveV4()) {
		su += "," + OnlineUser::TCP4_FEATURE;
		su += "," + OnlineUser::UDP4_FEATURE;
	}

	if (v6 && isActiveV6()) {
		su += "," + OnlineUser::TCP6_FEATURE;
		su += "," + OnlineUser::UDP6_FEATURE;
	}

	if ((v6 && !isActiveV6() && get(HubSettings::Connection6) != SettingsManager::INCOMING_DISABLED) ||
		(v4 && !isActiveV4() && get(HubSettings::Connection) != SettingsManager::INCOMING_DISABLED)) {
		su += "," + OnlineUser::NAT0_FEATURE;
	}
	su += "," + OnlineUser::ASCH_FEATURE;

	for (const auto& support: ClientManager::getInstance()->hubUserSupports.getAll()) {
		su += "," + support;
	}

	addParam(aLastInfoMap, c, "SU", su);
}

void AdcHub::appendConnectionSpeed(StringMap& aLastInfoMap, AdcCommand& c, const string& aParam, const string& aConnection, int64_t aLimitKbps) const noexcept {
	auto limit = aLimitKbps * 1000;
	auto connSpeed = static_cast<int64_t>((Util::toDouble(aConnection) * 1000.0 * 1000.0) / 8.0);
	addParam(aLastInfoMap, c, aParam, Util::toString(limit > 0 ? min(limit, connSpeed) : connSpeed));
}

void AdcHub::infoImpl() noexcept {
	if (getConnectState() != STATE_IDENTIFY && getConnectState() != STATE_NORMAL)
		return;

	reloadSettings(false);

	AdcCommand c(AdcCommand::CMD_INF, AdcCommand::TYPE_BROADCAST);

	if (stateNormal()) {
		if (!updateCounts(false))
			return;
	}

	addParam(lastInfoMap, c, "ID", ClientManager::getInstance()->getMyCID().toBase32());
	addParam(lastInfoMap, c, "PD", ClientManager::getInstance()->getMyPID().toBase32());
	addParam(lastInfoMap, c, "NI", get(Nick));
	addParam(lastInfoMap, c, "DE", get(Description));
	addParam(lastInfoMap, c, "SL", Util::toString(UploadManager::getInstance()->getSlots()));
	addParam(lastInfoMap, c, "FS", Util::toString(UploadManager::getInstance()->getFreeSlots()));

	addParam(lastInfoMap, c, "SS", Util::toString(ShareManager::getInstance()->getTotalShareSize(get(HubSettings::ShareProfile))));
	addParam(lastInfoMap, c, "SF", Util::toString(ShareManager::getInstance()->getBloomFileCount(get(HubSettings::ShareProfile))));

	addParam(lastInfoMap, c, "EM", get(Email));
	addParam(lastInfoMap, c, "HN", Util::toString(getDisplayCount(COUNT_NORMAL)));
	addParam(lastInfoMap, c, "HR", Util::toString(getDisplayCount(COUNT_REGISTERED)));
	addParam(lastInfoMap, c, "HO", Util::toString(getDisplayCount(COUNT_OP)));

	addParam(lastInfoMap, c, "VE", shortVersionString);
	addParam(lastInfoMap, c, "AW", ActivityManager::getInstance()->isAway() ? "1" : Util::emptyString);
	addParam(lastInfoMap, c, "LC", Localization::getLocale());

	appendConnectionSpeed(lastInfoMap, c, "US", SETTING(UPLOAD_SPEED), ThrottleManager::getUpLimit());
	appendConnectionSpeed(lastInfoMap, c, "DS", SETTING(DOWNLOAD_SPEED), ThrottleManager::getDownLimit());

	if (CryptoManager::getInstance()->TLSOk()) {
		auto &kp = CryptoManager::getInstance()->getKeyprint();
		addParam(lastInfoMap, c, "KP", CryptoManager::keyprintToString(kp));
	}

	auto supportsHBRI = supports.includes(HBRI_SUPPORT);
	bool addV4 = !sock->isV6Valid() || (get(HubSettings::Connection) != SettingsManager::INCOMING_DISABLED && supportsHBRI);
	bool addV6 = sock->isV6Valid() || (get(HubSettings::Connection6) != SettingsManager::INCOMING_DISABLED && supportsHBRI);

	appendClientSupports(lastInfoMap, c, addV4, addV6);
	appendConnectivity(lastInfoMap, c, addV4, addV6);

	if(c.getParameters().size() > 0) {
		sendHooked(c);
	}
}

void AdcHub::refreshUserList(bool) noexcept {
	OnlineUserList v;

	{
		RLock l(cs);
		for (const auto& [sid, user] : users) {
			if (sid != AdcCommand::HUB_SID) {
				v.push_back(user);
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

bool AdcHub::sendHooked(const AdcCommand& cmd, CallerPtr aOwner, string& error_) {
	if(!forbiddenCommands.contains(AdcCommand::toFourCC(cmd.getFourCC().c_str()))) {
		AdcCommand::ParamMap params;

		// Hooks
		try {
			auto results = ClientManager::getInstance()->outgoingHubCommandHook.runHooksDataThrow(aOwner, cmd, *this);
			params = ActionHook<AdcCommand::ParamMap>::normalizeMap(results);
		} catch (const HookRejectException& e) {
			error_ = ActionHookRejection::formatError(e.getRejection());
			return false;
		}

		// Listeners
		ProtocolCommandManager::getInstance()->fire(ProtocolCommandManagerListener::OutgoingHubCommand(), cmd, *this);

		// Send
		if (!params.empty()) {
			send(AdcCommand(cmd).addParams(params).toString(mySID));
		} else {
			send(cmd.toString(mySID));
		}

		return true;
	}

	error_ = STRING(HUB_PERMISSION_DENIED);
	return false;
}

void AdcHub::on(Connected c) noexcept {
	Client::on(c);

	if(getConnectState() != STATE_PROTOCOL) {
		return;
	}

	lastInfoMap.clear();
	mySID = 0;
	forbiddenCommands.clear();

	AdcCommand cmd(AdcCommand::CMD_SUP, AdcCommand::TYPE_HUB);
	appendHubSupports(cmd);
	sendHooked(cmd);
}

void AdcHub::appendHubSupports(AdcCommand& cmd) {
	cmd.addParam("AD" + BAS0_SUPPORT).addParam("AD" + BASE_SUPPORT).addParam("AD" + TIGR_SUPPORT);

	if (SETTING(HUB_USER_COMMANDS)) {
		cmd.addParam("AD" + UCM0_SUPPORT);
	}

	if (SETTING(BLOOM_MODE) == SettingsManager::BLOOM_ENABLED) {
		cmd.addParam("AD" + BLO0_SUPPORT);
	}

	cmd.addParam("AD" + ZLIF_SUPPORT);
	cmd.addParam("AD" + HBRI_SUPPORT);


	for (const auto& support: ClientManager::getInstance()->hubSupports.getAll()) {
		cmd.addParam("AD" + support);
	}
}

void AdcHub::on(Line l, const string& aLine) noexcept {
	Client::on(l, aLine);

	if(!Text::validateUtf8(aLine)) {
		statusMessage(STRING(UTF_VALIDATION_ERROR) + "(" + Text::sanitizeUtf8(aLine) + ")", LogMessage::SEV_ERROR);
		return;
	}

	dispatch(aLine, [this](const AdcCommand& aCmd) {
		ProtocolCommandManager::getInstance()->fire(ProtocolCommandManagerListener::IncomingHubCommand (), aCmd, *this);
	});
}

void AdcHub::on(TimerManagerListener::Second s, uint64_t aTick) noexcept {
	Client::on(s, aTick);
	if(stateNormal() && (aTick > (getLastActivity() + 120*1000)) ) {
		send("\n", 1);
	}
}

OnlineUserPtr AdcHub::findUser(const string& aNick) const noexcept {
	RLock l(cs); 
	for(const auto& ou: users | views::values) { 
		if(ou->getIdentity().getNick() == aNick) { 
			return ou; 
		} 
	} 
	return nullptr; 
}

} // namespace dcpp