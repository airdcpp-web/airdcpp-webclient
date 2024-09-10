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
#include "PartialFileSharingManager.h"

#include "ClientManager.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "SearchManager.h"
#include "SearchQuery.h"
#include "StringTokenizer.h"

const auto ENABLE_DEBUG = false;

namespace dcpp {

PartialFileSharingManager::PartialFileSharingManager() {
	TimerManager::getInstance()->addListener(this);
	SearchManager::getInstance()->addListener(this);
	ProtocolCommandManager::getInstance()->addListener(this);
}

PartialFileSharingManager::~PartialFileSharingManager() {
	TimerManager::getInstance()->removeListener(this);
	SearchManager::getInstance()->removeListener(this);
	ProtocolCommandManager::getInstance()->removeListener(this);
}

void PartialFileSharingManager::dbgMsg(const string& aMsg, LogMessage::Severity aSeverity) const noexcept {
	if (ENABLE_DEBUG) {
		LogManager::getInstance()->message(aMsg, aSeverity, "PSR");
	} else if (aSeverity == LogMessage::SEV_WARNING || aSeverity == LogMessage::SEV_ERROR) {
#ifdef _DEBUG
		LogManager::getInstance()->message(aMsg, aSeverity, "PSR");
		dcdebug("PSR: %s\n", aMsg.c_str());
#endif
	}
}

QueueItemPtr PartialFileSharingManager::getQueueFile(const TTHValue& tth) const noexcept {
	auto ql = QueueManager::getInstance()->findFiles(tth);
	if (ql.empty()) {
		return nullptr;
	}

	return ql.front();
}


// NMDC/ADC
void PartialFileSharingManager::onPSR(const AdcCommand& aCmd, UserPtr from, const string& aRemoteIp) {
	if (!SETTING(USE_PARTIAL_SHARING)) {
		return;
	}

	string udpPort;
	uint32_t partialCount = 0;
	string tth;
	string hubIpPort;
	string nick;
	PartsInfo partialInfo;

	for (auto& str: aCmd.getParameters()) {
		if (str.compare(0, 2, "U4") == 0) {
			udpPort = str.substr(2);
		} else if (str.compare(0, 2, "NI") == 0) {
			nick = str.substr(2);
		} else if (str.compare(0, 2, "HI") == 0) {
			hubIpPort = str.substr(2);
		} else if (str.compare(0, 2, "TR") == 0) {
			tth = str.substr(2);
		} else if (str.compare(0, 2, "PC") == 0) {
			partialCount = Util::toUInt32(str.substr(2))*2;
		} else if (str.compare(0, 2, "PI") == 0) {
			StringTokenizer<string> tok(str.substr(2), ',');
			for (auto& i: tok.getTokens()) {
				partialInfo.push_back((uint16_t)Util::toInt(i));
			}
		}
	}

	auto qi = getQueueFile(TTHValue(tth));
	if (!qi) {
		return;
	}

	string hubUrl;
	/*if (!from || from == ClientManager::getInstance()->getMe()) {
		// for NMDC support
		if (nick.empty() || hubIpPort.empty()) {
			dbgMsg("NMDC nick/hub ip:port empty", LogMessage::SEV_WARNING);
			return;
		}
		
		auto u = ClientManager::getInstance()->getNmdcSearchHintedUserUtf8(nick, hubIpPort, aRemoteIp);
		if (!u) {
			dbgMsg("result from an unknown NMDC user", LogMessage::SEV_WARNING);
			return;
		}

		dcassert(!u.hint.empty());
		from = u.user;
		hubUrl = u.hint;
	} else {*/
		// ADC
		hubUrl = ClientManager::getInstance()->getADCSearchHubUrl(from->getCID(), hubIpPort);
		if (hubUrl.empty()) {
			dbgMsg("result from an unknown ADC hub", LogMessage::SEV_WARNING);
			return;
		}
	// }

	if (partialInfo.size() != partialCount) {
		dbgMsg("invalid size", LogMessage::SEV_WARNING);
		// what to do now ? just ignore partial search result :-/
		return;
	}

	auto hintedUser = HintedUser(from, hubUrl);
	auto myNick = from->isNMDC() ? ClientManager::getInstance()->getMyNick(hubUrl) : Util::emptyString;
	auto partialSource = make_shared<PartialFileSource>(qi, hintedUser, myNick, hubIpPort, aRemoteIp, udpPort);

	handlePartialResultHooked(qi, partialSource, partialInfo);

	PartsInfo outPartialInfo;
	if (handlePartialSearch(qi, outPartialInfo) && Util::toInt(udpPort) > 0) {
		try {
			AdcCommand cmd = toPSR(false, partialSource->getMyNick(), hubIpPort, tth, outPartialInfo);
			sendUDP(cmd, from, hubUrl);
			dbgMsg("reply sent", LogMessage::SEV_WARNING);
		} catch (const Exception& e) {
			dbgMsg("failed to send reply (" + string(e.what()) + ")", LogMessage::SEV_WARNING);
		}
	}

}

bool PartialFileSharingManager::handlePartialResultHooked(const QueueItemPtr& aQI, const PartialFileSource::Ptr& aPartialSource, const PartsInfo& aInPartialInfo) noexcept {
	// Don't add sources to finished files
	// This could happen when "Keep finished files in queue" is enabled
	if (aQI->isDownloaded()) {
		return false;
	}

	// Check min size
	if (aQI->getSize() < PARTIAL_SHARE_MIN_SIZE) {
		dcassert(0);
		return false;
	}


	// Add our source
	if (QueueManager::getInstance()->addPartialSourceHooked(aPartialSource->getHintedUser(), aQI, aInPartialInfo)) {
		{
			WLock l(cs);
			sources.insert(aPartialSource);
		}

		dbgMsg("added partial source", LogMessage::SEV_VERBOSE);
	} else {
		dbgMsg("could not add partial source", LogMessage::SEV_WARNING);
	}

	return true;
}


string PartialFileSharingManager::getPartsString(const PartsInfo& partsInfo) const {
	string ret;

	for(auto i = partsInfo.begin(); i < partsInfo.end(); i+=2){
		ret += Util::toString(*i) + "," + Util::toString(*(i+1)) + ",";
	}

	return ret.substr(0, ret.size()-1);
}

AdcCommand PartialFileSharingManager::toPSR(bool aWantResponse, const string& aMyNick, const string& aHubIpPort, const string& aTTH, const vector<uint16_t>& aPartialInfo) const {
	AdcCommand cmd(PartialFileSharingManager::CMD_PSR, AdcCommand::TYPE_UDP);
		
	if (!aMyNick.empty()) {
		// NMDC
		auto hubUrl = ClientManager::getInstance()->findHub(aHubIpPort, true);
		cmd.addParam("NI", Text::fromUtf8(aMyNick, ClientManager::getInstance()->findHubEncoding(hubUrl)));
	}
		
	cmd.addParam("HI", aHubIpPort);
	cmd.addParam("U4", aWantResponse ? SearchManager::getInstance()->getPort() : "0");
	cmd.addParam("TR", aTTH);
	cmd.addParam("PC", Util::toString(aPartialInfo.size() / 2));
	cmd.addParam("PI", getPartsString(aPartialInfo));
	
	return cmd;
}

void PartialFileSharingManager::on(ProtocolCommandManagerListener::IncomingUDPCommand, const AdcCommand& aCmd, const string& aRemoteIp) noexcept {
	if (aCmd.getCommand() != PartialFileSharingManager::CMD_PSR) {
		return;
	}

	if (!SETTING(USE_PARTIAL_SHARING)) {
		return;
	}

	if (aCmd.getParameters().empty())
		return;

	const auto cid = aCmd.getParam(0);
	if (cid.size() != 39)
		return;

	auto user = ClientManager::getInstance()->findUser(CID(cid));
	if (!user) {
		return;
	}

	// when user == NULL then it is probably NMDC user, check it later

	// Remove the CID
	// c.getParameters().erase(c.getParameters().begin());
	onPSR(aCmd, user, aRemoteIp);
}

void PartialFileSharingManager::on(ProtocolCommandManagerListener::IncomingHubCommand, const AdcCommand& aCmd, const Client& aClient) noexcept {
	if (aCmd.getCommand() != PartialFileSharingManager::CMD_PSR) {
		return;
	}

	OnlineUser* ou = aClient.findUser(aCmd.getFrom());
	if (!ou) {
		dcdebug("Invalid user in AdcHub::onPBD\n");
		return;
	}

	onPSR(aCmd, ou->getUser(), ou->getIdentity().getUdpIp());
}


void PartialFileSharingManager::onIncomingSearch(const Client* aClient, const OnlineUserPtr& aUser, const SearchQuery& aQuery, bool aIsUdpActive) noexcept {
	if (!aUser) {
		return;
	}

	if (!aQuery.root) {
		return;
	}

	auto qi = getQueueFile(*aQuery.root);
	if (!qi) {
		return;
	}

	/*if (!aIsUdpActive || !aQuery.root) {
		return;
	}

	if (SETTING(EXTRA_PARTIAL_SLOTS) == 0) //disable partial uploads by setting 0
		return;

	PartsInfo partialInfo;
	if (!handlePartialSearch(nullptr, *aQuery.root, partialInfo)) {
		return;
	}

	string ip, port;
	Util::parseIpPort(aSeeker, ip, port);

	if (port.empty())
		return;

	try {
		auto cmd = toPSR(true, aClient->getMyNick(), aClient->getIpPort(), (*aQuery.root).toBase32(), partialInfo);
		auto data = cmd.toString(ClientManager::getInstance()->getMe()->getCID());

		ClientManager::getInstance()->sendUDP(data, ip, port);
	} catch (...) {
		dcdebug("Partial search caught error\n");
	}*/

	PartsInfo partialInfo;
	if (handlePartialSearch(qi, partialInfo)) {
		AdcCommand cmd = toPSR(aIsUdpActive, Util::emptyString, aClient->getIpPort(), *aQuery.root, partialInfo);
		sendUDP(cmd, aUser->getUser(), aUser->getHubUrl());
		dbgMsg("partial file info not empty, response sent", LogMessage::SEV_VERBOSE);

		/*try {
			AdcCommand cmd = toPSR(false, ps->getMyNick(), hubIpPort, tth, outPartialInfo);
			ClientManager::getInstance()->sendUDP(cmd, from->getCID(), false, true, Util::emptyString, hubUrl);
		} catch (const Exception& e) {
			dbgMsg("PSR: failed to send reply (" + string(e.what()) + ")", LogMessage::SEV_WARNING);
		}*/
	}
}

void PartialFileSharingManager::on(SearchManagerListener::IncomingSearch, Client* aClient, const OnlineUserPtr& aUser, const SearchQuery& aQuery, const SearchResultList& aResults, bool aIsUdpActive) noexcept {
	if (aResults.empty() && SETTING(USE_PARTIAL_SHARING) && aClient->get(HubSettings::ShareProfile) != SP_HIDDEN) {
		onIncomingSearch(aClient, aUser, aQuery, aIsUdpActive);
	}
}

bool PartialFileSharingManager::allowPartialSharing(const QueueItemPtr& aQI) const noexcept {
	if (aQI->isSet(QueueItem::FLAG_PRIVATE)) {
		dbgMsg("partial sharing disabled for file " + aQI->getTarget() + " (private file)", LogMessage::SEV_VERBOSE);
		return false;
	}

	// do we have a file to send?
	if (!aQI->hasPartialSharingTarget()) {
		dbgMsg("partial sharing disabled for file " + aQI->getTarget() + " (no file on disk)", LogMessage::SEV_VERBOSE);
		return false;
	}

	if (aQI->getSize() < PARTIAL_SHARE_MIN_SIZE) {
		dbgMsg("partial sharing disabled for file " + aQI->getTarget() + " (file too small)", LogMessage::SEV_VERBOSE);
		return false;
	}

	return true;
}

bool PartialFileSharingManager::handlePartialSearch(const QueueItemPtr& aQI, PartsInfo& outPartialInfo_) noexcept {
	dcassert(outPartialInfo_.empty());
	if (!allowPartialSharing(aQI)) {
		return false;
	}

	QueueManager::getInstance()->getPartialInfo(aQI, outPartialInfo_);
	return !outPartialInfo_.empty();
}

bool PartialFileSharingManager::PartialFileSource::requestPartialSourceInfo(uint64_t aNow) const noexcept {
	return getNextQueryTime() <= aNow &&
		getPendingQueryCount() < 10 && !getUdpPort().empty();
};

bool PartialFileSharingManager::PartialFileSource::Sort::operator()(const Ptr& a, const Ptr& b) const noexcept {
	return compare(a->getHintedUser().user->getCID(), b->getHintedUser().user->getCID()) < 0 && compare(a->getQueueItem()->getToken(), b->getQueueItem()->getToken()) < 0;
}

bool PartialFileSharingManager::PartialFileSource::isCurrentSource() const noexcept {
	// File finished?
	if (queueItem->isDownloaded()) {
		return false;
	}

	// Source removed?
	auto fileSources = QueueManager::getInstance()->getSources(queueItem);
	if (find(fileSources.begin(), fileSources.end(), hintedUser.user) == fileSources.end()) {
		return false;
	}

	return true;
}


PartialFileSharingManager::PFSSourceList PartialFileSharingManager::findPFSSources(int aMaxSources) noexcept {
	typedef multimap<time_t, PartialFileSource::Ptr> SortedBuffer;
	SortedBuffer buffer;
	uint64_t now = GET_TICK();

	// Collect the sources pending updates
	{
		RLock l(cs);
		for (const auto& partialSource: sources) {
			if (!partialSource->requestPartialSourceInfo(now)) {
				continue;
			}

			buffer.emplace(partialSource->getNextQueryTime(), partialSource);
		}
	}

	// Remove obsolete sources
	for (auto i = buffer.begin(); i != buffer.end();) {
		auto partialSource = i->second;
		if (!partialSource->isCurrentSource()) {
			{
				WLock l(cs);
				sources.erase(partialSource);
			}

			dbgMsg("removing obsolete partial source " + partialSource->getHintedUser().user->getCID().toBase32() + " for file " + partialSource->getQueueItem()->getTarget(), LogMessage::SEV_VERBOSE);
			i = buffer.erase(i);
			continue;
		} else {
			++i;
		}
	}

	// Pick the oldest matches
	PFSSourceList sourceList;
	sourceList.reserve(aMaxSources);
	for (auto i = buffer.begin(); i != buffer.end() && sourceList.size() < static_cast<size_t>(aMaxSources); i++) {
		sourceList.emplace_back(i->second);
	}

	return sourceList;
}

struct PartsInfoReqParam{
	PartsInfo	parts;
	string		tth;
	string		myNick;
	string		hubIpPort;
	string		ip;
	string		udpPort;
	HintedUser  user;
};

void PartialFileSharingManager::requestPartialSourceInfo(uint64_t aTick, uint64_t aNextQueryTime) noexcept {
	BundlePtr bundle;
	vector<const PartsInfoReqParam*> params;

	{
		//find max 10 pfs sources to exchange parts
		//the source basis interval is 5 minutes
		auto sourceList = findPFSSources();
		for (auto& source : sourceList) {
			const auto& qi = source->getQueueItem();

			PartsInfoReqParam* param = new PartsInfoReqParam;

			QueueManager::getInstance()->getPartialInfo(qi, param->parts);

			param->tth = qi->getTTH().toBase32();
			param->ip = source->getIp();
			param->udpPort = source->getUdpPort();
			param->myNick = source->getMyNick();
			param->hubIpPort = source->getHubIpPort();
			param->user = source->getHintedUser();


			params.push_back(param);

			source->setPendingQueryCount((uint8_t)(source->getPendingQueryCount() + 1));
			source->setNextQueryTime(aTick + aNextQueryTime);

			dbgMsg("requesting partial information for file " + qi->getTarget() + " from user " + source->getHintedUser().user->getCID().toBase32(), LogMessage::SEV_VERBOSE);
		}
	}

	// Request parts info from partial file sharing sources
	for (auto& param : params) {
		dcassert(!param->udpPort.empty());
		try {
			AdcCommand cmd = toPSR(true, param->myNick, param->hubIpPort, param->tth, param->parts);
			// ClientManager::getInstance()->sendUDP(cmd.toString(), param->ip, param->udpPort);
			sendUDP(cmd, param->user.user, param->user.hint);
		} catch (const Exception& e) {
			dbgMsg("failed to send info request: " + e.getError(), LogMessage::SEV_WARNING);
			dcdebug("Partial search caught error\n");
		}

		delete param;
	}
}

void PartialFileSharingManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	requestPartialSourceInfo(aTick, 300000); // 5 minutes
}

void PartialFileSharingManager::sendUDP(AdcCommand& aCmd, const UserPtr& aUser, const string& aHubUrl) {
	SearchManager::getInstance()->getUdpServer().addTask([=, this] {
		auto cmd = aCmd;
		auto success = ClientManager::getInstance()->sendUDPHooked(cmd, aUser->getCID(), false, true, Util::emptyString, aHubUrl);
		if (!success) {
			dbgMsg("failed to send UDP message", LogMessage::SEV_WARNING);
		}
	});
}


} // namespace dcpp
