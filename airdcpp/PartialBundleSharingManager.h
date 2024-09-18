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

#ifndef DCPLUSPLUS_DCPP_PARTIAL_BUNDLE_SHARING_H
#define DCPLUSPLUS_DCPP_PARTIAL_BUNDLE_SHARING_H

#include "QueueManagerListener.h"
#include "SearchManagerListener.h"
#include "TimerManagerListener.h"

#include "AdcCommand.h"
#include "CriticalSection.h"
#include "ProtocolCommandManager.h"
#include "Message.h"


namespace dcpp {

class PartialBundleSharingManager : private QueueManagerListener, private SearchManagerListener, private ProtocolCommandManagerListener
{
public:
	void onPBD(const AdcCommand& cmd, const UserPtr& from);
	AdcCommand toPBD(const string& hubIpPort, const string& bundle, const string& aTTH, bool reply, bool add, bool notify = false) const;

	PartialBundleSharingManager();
	~PartialBundleSharingManager();

	ADC_CMD(PBD, 'P','B','D');
private:
	typedef pair<HintedUser, string> UserBundlePair;
	typedef vector<UserBundlePair> FinishedNotifyList;

	mutable SharedMutex cs;

	void sendFileCompletionNotifications(const QueueItemPtr& q) noexcept;
	void sendRemovePBD(const HintedUser& aUser, const string& aRemoteToken) noexcept;

	static void handleGetReplyParams(const BundlePtr& aBundle, string& _bundleToken, bool& _notify, bool& _add) noexcept;
	void handleAddRemoteNotifyUser(const HintedUser& aUser, const BundlePtr& aBundle, const string& aRemoteBundle) noexcept;

	// Remove user from a notify list of the local bundle
	void handleRemoveRemoteBundleNotify(const UserPtr& aUser, QueueToken aBundleToken) noexcept;
	void sendBundleCompletedNotifications(const BundlePtr& aBundle) noexcept;

	void on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept override;
	void on(QueueManagerListener::ItemStatus, const QueueItemPtr& aQI) noexcept override;

	void on(SearchManagerListener::IncomingSearch, Client* aClient, const OnlineUserPtr& aAdcUser, const SearchQuery& aQuery, const SearchResultList&, bool aIsUdpActive) noexcept override;

	void on(ProtocolCommandManagerListener::IncomingUDPCommand, const AdcCommand&, const string&) noexcept override;
	void on(ProtocolCommandManagerListener::IncomingHubCommand, const AdcCommand&, const Client&) noexcept override;

	void onIncomingSearch(const OnlineUserPtr& aUser, const SearchQuery& aQuery, bool aIsUdpActive) noexcept;

	void dbgMsg(const string& aMsg, LogMessage::Severity aSeverity) const noexcept;

	bool matchIncomingSearch(const UserPtr& aUser, const TTHValue& tth, string& _bundle, bool& _reply, bool& _add) const noexcept;

	const FinishedNotifyList* getRemoteBundleNotificationsUnsafe(const BundlePtr& aBundle) const noexcept;

	void clearRemoteNotifications(const BundlePtr& aBundle, FinishedNotifyList& fnl) noexcept;
	bool isRemoteNotified(const BundlePtr& aBundle, const UserPtr& aUser) const noexcept;
	void addRemoteNotify(const BundlePtr& aBundle, const HintedUser& aUser, const string& remoteBundle) noexcept;
	void removeRemoteBundleNotify(const UserPtr& aUser, const BundlePtr& aBundle) noexcept;

	void sendUDP(AdcCommand& aCmd, const UserPtr& aUser, const string& aHubUrl);

	std::unordered_map<BundlePtr, FinishedNotifyList> remoteBundleNotifications;
};

} // namespace dcpp

#endif // !defined(SEARCH_MANAGER_H)