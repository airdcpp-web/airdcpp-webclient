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
#include <airdcpp/queue/partial_sharing/PartialBundleSharingManager.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/queue/QueueManager.h>
#include <airdcpp/search/SearchManager.h>
#include <airdcpp/search/SearchQuery.h>
#include <airdcpp/util/text/StringTokenizer.h>

const auto ENABLE_DEBUG = false;

namespace dcpp {

PartialBundleSharingManager::PartialBundleSharingManager() {
	SearchManager::getInstance()->addListener(this);
	QueueManager::getInstance()->addListener(this);
	ProtocolCommandManager::getInstance()->addListener(this);
}

PartialBundleSharingManager::~PartialBundleSharingManager() {
	SearchManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);
	ProtocolCommandManager::getInstance()->removeListener(this);
}

void PartialBundleSharingManager::dbgMsg(const string& aMsg, LogMessage::Severity aSeverity) const noexcept {
	if (ENABLE_DEBUG) {
		LogManager::getInstance()->message(aMsg, aSeverity, "PBD");
	} else if (aSeverity == LogMessage::SEV_WARNING || aSeverity == LogMessage::SEV_ERROR) {
#ifdef _DEBUG
		LogManager::getInstance()->message(aMsg, aSeverity, "PBD");
		dcdebug("PBD: %s\n", aMsg.c_str());
#endif
	}
}

// Partial bundle sharing (ADC)
void PartialBundleSharingManager::onPBD(const AdcCommand& aCmd, const UserPtr& from) {
	dcassert(!!from);

	string remoteBundle;
	string hubIpPort;
	string tth;
	bool add = false, update = false, reply = false, notify = false, remove = false;

	for (auto& str: aCmd.getParameters()) {
		if (str.compare(0, 2, "HI") == 0) {
			hubIpPort = str.substr(2);
		} else if (str.compare(0, 2, "BU") == 0) {
			remoteBundle = str.substr(2);
		} else if (str.compare(0, 2, "TH") == 0) {
			tth = str.substr(2);
		} else if (str.compare(0, 2, "UP") == 0) { //new notification of a finished TTH
			update = true;
		} else if (str.compare(0, 2, "AD") == 0) { //add TTHList
			add = true;
		} else if (str.compare(0, 2, "RE") == 0) { //require reply (and add remote notifications)
			notify = true;
			reply = true;
		} else if (str.compare(0, 2, "NO") == 0) { //add remote notifications
			notify = true;
		} else if (str.compare(0, 2, "RM") == 0) { //remove remote notifications for a selected user and bundle
			remove = true;
		} else {
			dbgMsg("unknown param " + str, LogMessage::SEV_WARNING);
		}
	}

	if (remove && !remoteBundle.empty()) {
		dbgMsg("remove remote notifications for bundle " + remoteBundle, LogMessage::SEV_VERBOSE);
		// Local bundle really...
		handleRemoveRemoteBundleNotify(from, Util::toUInt32(remoteBundle));
		return;
	}

	if (tth.empty()) {
		dbgMsg("TTH param missing", LogMessage::SEV_WARNING);
		return;
	}

	auto hubUrl = ClientManager::getInstance()->getADCSearchHubUrl(from->getCID(), hubIpPort);
	if (hubUrl.empty()) {
		dbgMsg("no online hubs for a CID" + from->getCID().toBase32(), LogMessage::SEV_WARNING);
		return;
	}

	if (update) {
		dbgMsg("add user " + from->getCID().toBase32() +  " as a source for file " + tth, LogMessage::SEV_VERBOSE);
		QueueManager::getInstance()->addSourceHooked(HintedUser(from, hubUrl), TTHValue(tth));
		return;
	} else if (remoteBundle.empty()) {
		dbgMsg("remote bundle param missing", LogMessage::SEV_WARNING);
		return;
	}

	// New user sequence
	auto bundle = QueueManager::getInstance()->findBundle(TTHValue(tth));
	if (!bundle) {
		dbgMsg("can't process new user notifications, bundle not found for TTH " + tth, LogMessage::SEV_WARNING);
		return;
	}

	auto u = HintedUser(from, hubUrl);
	if (notify) {
		dbgMsg("add finished notify (the remote bundle is still incomplete)", LogMessage::SEV_VERBOSE);
		handleAddRemoteNotifyUser(u, bundle, remoteBundle);
	}
	
	if (reply) {
		// Similar to notify but we just let the other user know whether we have finished files
		dbgMsg("reply requested", LogMessage::SEV_VERBOSE);

		// Params
		string localBundle;
		bool sendNotify = false, sendAdd = false;
		handleGetReplyParams(bundle, localBundle, sendNotify, sendAdd);

		// Send
		AdcCommand cmd = toPBD(hubIpPort, localBundle, tth, false, sendAdd, sendNotify);
		sendUDP(cmd, from, hubUrl);
	}

	if (add) {
		// The remote user has finished files
		try {
			QueueManager::getInstance()->addBundleTTHListHooked(u, bundle, remoteBundle);
			dbgMsg("TTH list queued", LogMessage::SEV_VERBOSE);
		} catch (const Exception& e) {
			dbgMsg("error when queueing TTH list: " + string(e.what()), LogMessage::SEV_WARNING);
		}
	}
}

AdcCommand PartialBundleSharingManager::toPBD(const string& hubIpPort, const string& bundle, const string& aTTH, bool reply, bool add, bool notify) const {
	AdcCommand cmd(PartialBundleSharingManager::CMD_PBD, AdcCommand::TYPE_UDP);

	cmd.addParam("HI", hubIpPort);
	cmd.addParam("BU", bundle);
	cmd.addParam("TH", aTTH);
	if (notify) {
		cmd.addParam("NO1");
	} else if (reply) {
		cmd.addParam("RE1");
	}

	if (add) {
		cmd.addParam("AD1");
	}
	return cmd;
}

void PartialBundleSharingManager::sendFileCompletionNotifications(const QueueItemPtr& qi) noexcept {
	dcassert(qi->getBundle());
	HintedUserList notified;

	{
		RLock l (cs);

		//collect the users that don't have this file yet
		auto bundleFNL = getRemoteBundleNotificationsUnsafe(qi->getBundle());
		if (bundleFNL) {
			for (const auto& [user, _] : *bundleFNL) {
				if (!qi->isSource(user)) {
					notified.push_back(user);
				}
			}
		}
	}

	//send the notifications
	for(auto& u: notified) {
		AdcCommand cmd(PartialBundleSharingManager::CMD_PBD, AdcCommand::TYPE_UDP);

		cmd.addParam("UP1");
		//cmd.addParam("HI", u.hint); update adds sources, so ip port needed here...
		cmd.addParam("TH", qi->getTTH().toBase32());
		sendUDP(cmd, u.user, u.hint);
	}
}

void PartialBundleSharingManager::sendRemovePBD(const HintedUser& aUser, const string& aRemoteToken) noexcept {
	AdcCommand cmd(PartialBundleSharingManager::CMD_PBD, AdcCommand::TYPE_UDP);

	cmd.addParam("BU", aRemoteToken);
	cmd.addParam("RM1");
	sendUDP(cmd, aUser, aUser.hint);
}

void PartialBundleSharingManager::handleGetReplyParams(const BundlePtr& aBundle, string& _bundleToken, bool& _notify, bool& _add) noexcept {
	_bundleToken = aBundle->getStringToken();
	_add = !aBundle->getFinishedFiles().empty();

	if (!aBundle->isDownloaded()) {
		_notify = true;
	}
}

void PartialBundleSharingManager::handleAddRemoteNotifyUser(const HintedUser& aUser, const BundlePtr& aBundle, const string& remoteBundle) noexcept {
	if (!aBundle->isDownloaded()) {
		addRemoteNotify(aBundle, aUser, remoteBundle);
	} else {
		dbgMsg("can't add finished notifications for a complete bundle", LogMessage::SEV_VERBOSE);
	}
}

void PartialBundleSharingManager::sendBundleCompletedNotifications(const BundlePtr& aBundle) noexcept {
	FinishedNotifyList fnl;
	clearRemoteNotifications(aBundle, fnl);

	for (const auto& [user, bundleToken] : fnl) {
		sendRemovePBD(user, bundleToken);
	}
}

void PartialBundleSharingManager::on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept {
	if (aBundle->getStatus() == Bundle::STATUS_COMPLETED) {
		sendBundleCompletedNotifications(aBundle);
	}
}

void PartialBundleSharingManager::on(QueueManagerListener::ItemStatus, const QueueItemPtr& aQI) noexcept {
	if (aQI->getStatus() == QueueItem::STATUS_COMPLETED && aQI->getBundle()) {
		sendFileCompletionNotifications(aQI);
	}
}

bool PartialBundleSharingManager::matchIncomingSearch(const UserPtr& aUser, const TTHValue& tth, string& _bundle, bool& _reply, bool& _add) const noexcept {
	QueueItemPtr qi = nullptr;
	{
		// Locate target QueueItem in download queue
		auto ql = QueueManager::getInstance()->findFiles(tth);
		if (ql.empty()) {
			return false;
		}

		qi = ql.front();

		//don't share files download from private chat
		if (qi->isSet(QueueItem::FLAG_PRIVATE))
			return false;

		auto b = qi->getBundle();
		if (b) {
			_bundle = b->getStringToken();

			//should we notify the other user about finished item?
			_reply = !b->isDownloaded() && !isRemoteNotified(b, aUser);

			//do we have finished files that the other guy could download?
			_add = QueueManager::getInstance()->getFinishedItemCount(b) > 0;

			return true;
		}
	}

	return false;
}

void PartialBundleSharingManager::on(SearchManagerListener::IncomingSearch, Client* aClient, const OnlineUserPtr& aAdcUser, const SearchQuery& aQuery, const SearchResultList& aResults, bool aIsUdpActive) noexcept {
	if (!aAdcUser) {
		return;
	}

	if (aResults.empty() && SETTING(USE_PARTIAL_SHARING) && aClient->get(HubSettings::ShareProfile) != SP_HIDDEN) {
		onIncomingSearch(aAdcUser, aQuery, aIsUdpActive);
	}
}

void PartialBundleSharingManager::onIncomingSearch(const OnlineUserPtr& aUser, const SearchQuery& aQuery, bool /*aIsUdpActive*/) noexcept {
	if (!aQuery.root) {
		return;
	}

	string bundle;
	bool reply = false, add = false;
	matchIncomingSearch(aUser->getUser(), *aQuery.root, bundle, reply, add);
	if (!bundle.empty()) {
		AdcCommand cmd = toPBD(aUser->getClient()->getIpPort(), bundle, *aQuery.root, reply, add);
		sendUDP(cmd, aUser->getUser(), aUser->getHubUrl());
		dbgMsg("matching bundle in queue for an incoming search, PBD response sent", LogMessage::SEV_VERBOSE);
	}
}

void PartialBundleSharingManager::sendUDP(AdcCommand& aCmd, const UserPtr& aUser, const string& aHubUrl) {
	SearchManager::getInstance()->getUdpServer().addTask([=, this] {
		auto cmd = aCmd;

		ClientManager::OutgoingUDPCommandOptions options(this, true);
		string error;
		auto success = ClientManager::getInstance()->sendUDPHooked(cmd, HintedUser(aUser, aHubUrl), options, error);
		if (!success) {
			dbgMsg("failed to send UDP message to an user " + aUser->getCID().toBase32() + ": " + error, LogMessage::SEV_WARNING);
		}
	});
}

void PartialBundleSharingManager::on(ProtocolCommandManagerListener::IncomingUDPCommand, const AdcCommand& aCmd, const string&) noexcept {
	if (aCmd.getCommand() != PartialBundleSharingManager::CMD_PBD) {
		return;
	}

	if (!SETTING(USE_PARTIAL_SHARING)) {
		return;
	}

	if (aCmd.getParameters().empty())
		return;

	const auto& cid = aCmd.getParam(0);
	if (cid.size() != 39)
		return;

	const auto user = ClientManager::getInstance()->findUser(CID(cid));
	if (!user) {
		return;
	}

	onPBD(aCmd, user);
}

void PartialBundleSharingManager::on(ProtocolCommandManagerListener::IncomingHubCommand, const AdcCommand& aCmd, const Client& aClient) noexcept {
	if (aCmd.getCommand() != PartialBundleSharingManager::CMD_PBD) {
		return;
	}

	auto ou = aClient.findUser(aCmd.getFrom());
	if (!ou) {
		dcdebug("Invalid user in AdcHub::onPBD\n");
		return;
	}

	onPBD(aCmd, ou->getUser());
}

const PartialBundleSharingManager::FinishedNotifyList* PartialBundleSharingManager::getRemoteBundleNotificationsUnsafe(const BundlePtr& aBundle) const noexcept {
	auto i = remoteBundleNotifications.find(aBundle);
	return i != remoteBundleNotifications.end() ? &i->second : nullptr;
}

bool PartialBundleSharingManager::isRemoteNotified(const BundlePtr& aBundle, const UserPtr& aUser) const noexcept {
	RLock l(cs);
	auto bundleFNL = getRemoteBundleNotificationsUnsafe(aBundle);
	if (!bundleFNL) {
		return false;
	}

	return ranges::find_if(*bundleFNL, [&aUser](const UserBundlePair& ubp) { return ubp.first.user == aUser; }) != (*bundleFNL).end();
}

void PartialBundleSharingManager::addRemoteNotify(const BundlePtr& aBundle, const HintedUser& aUser, const string& remoteBundle) noexcept {
	if (!isRemoteNotified(aBundle, aUser.user) /* && !isBadSource(aUser)*/) {
		{
			WLock l(cs);
			remoteBundleNotifications[aBundle].emplace_back(aUser, remoteBundle);
		}

		dbgMsg("remote notification added for an user " + aUser.user->getCID().toBase32(), LogMessage::SEV_VERBOSE);
	} else {
		dbgMsg("remote notifications exist for an user " + aUser.user->getCID().toBase32() + ", skip adding", LogMessage::SEV_VERBOSE);
	}
}

void PartialBundleSharingManager::handleRemoveRemoteBundleNotify(const UserPtr& aUser, QueueToken aBundleToken) noexcept {
	auto bundle = QueueManager::getInstance()->findBundle(aBundleToken);
	if (bundle) {
		removeRemoteBundleNotify(aUser, bundle);
	} else {
		dbgMsg("could not remove removed notifications for an user " + aUser->getCID().toBase32() + ", local bundle " + Util::toString(aBundleToken) + " not found", LogMessage::SEV_WARNING);
		return;
	}
}

void PartialBundleSharingManager::removeRemoteBundleNotify(const UserPtr& aUser, const BundlePtr& aBundle) noexcept {
	WLock l(cs);
	auto mainIter = remoteBundleNotifications.find(aBundle);
	if (mainIter == remoteBundleNotifications.end()) {
		dbgMsg("could not remove removed notifications for an user " + aUser->getCID().toBase32() + ", bundle notifications not found", LogMessage::SEV_WARNING);
		return;
	}

	auto& bundleFNL = mainIter->second;
	auto userBundleIter = ranges::find_if(bundleFNL, [&aUser](const UserBundlePair& ubp) { return ubp.first.user == aUser; });
	if (userBundleIter != bundleFNL.end()) {
		bundleFNL.erase(userBundleIter);
		dbgMsg("remote notification removed for an user " + aUser->getCID().toBase32(), LogMessage::SEV_VERBOSE);
	} else {
		dbgMsg("could not remove removed notifications for an user " + aUser->getCID().toBase32() + ", user not found for the bundle", LogMessage::SEV_WARNING);
	}

	if (bundleFNL.empty()) {
		remoteBundleNotifications.erase(aBundle);
	}
}

void PartialBundleSharingManager::clearRemoteNotifications(const BundlePtr& aBundle, FinishedNotifyList& fnl_) noexcept {
	{
		WLock l(cs);
		auto i = remoteBundleNotifications.find(aBundle);
		if (i == remoteBundleNotifications.end()) {
			return;
		}

		fnl_.swap(i->second);
		remoteBundleNotifications.erase(aBundle);
	}

	dbgMsg("remote notification cleared for a bundle " + aBundle->getName(), LogMessage::SEV_VERBOSE);
}


} // namespace dcpp
