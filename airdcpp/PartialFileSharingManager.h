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

#ifndef DCPLUSPLUS_DCPP_PARTIAL_FILE_SHARING_H
#define DCPLUSPLUS_DCPP_PARTIAL_FILE_SHARING_H

#include "QueueManagerListener.h"
#include "SearchManagerListener.h"
#include "TimerManagerListener.h"

#include "AdcCommand.h"
#include "CriticalSection.h"
#include "ProtocolCommandManager.h"
#include "Message.h"
#include "QueueItem.h"


namespace dcpp {

class PartialFileSharingManager : private TimerManagerListener, private SearchManagerListener, private ProtocolCommandManagerListener
{
public:
	void onPSR(const AdcCommand& cmd, UserPtr from, const string& remoteIp);

	PartialFileSharingManager();
	~PartialFileSharingManager();
	
	ADC_CMD(PSR, 'P', 'S', 'R');
private:
	class PartialFileSource : public FastAlloc<PartialFileSource> {
	public:
		PartialFileSource(const QueueItemPtr& aQI, const HintedUser& aUser, const string& aMyNick, const string& aHubIpPort, const string& aIp, const string& udp) :
			myNick(aMyNick), hubIpPort(aHubIpPort), ip(aIp), udpPort(udp), queueItem(aQI), hintedUser(aUser) {}

		~PartialFileSource() { }

		typedef std::shared_ptr<PartialFileSource> Ptr;
		typedef std::vector<PartialFileSource::Ptr> List;

		bool requestPartialSourceInfo(uint64_t aNow) const noexcept;
		bool isCurrentSource() const noexcept;

		GETSET(string, myNick, MyNick);			// for NMDC support only
		GETSET(string, hubIpPort, HubIpPort);
		GETSET(string, ip, Ip);
		GETSET(string, udpPort, UdpPort);
		IGETSET(uint64_t, nextQueryTime, NextQueryTime, 0);
		IGETSET(uint8_t, pendingQueryCount, PendingQueryCount, 0);
		GETSET(HintedUser, hintedUser, HintedUser);
		GETSET(QueueItemPtr, queueItem, QueueItem);

		struct Sort {
			bool operator()(const Ptr& a, const Ptr& b) const noexcept;
		};
	};

	AdcCommand toPSR(bool wantResponse, const string& myNick, const string& hubIpPort, const string& tth, const vector<uint16_t>& partialInfo) const;

	typedef vector<PartialFileSource::Ptr> PFSSourceList;
	PFSSourceList findPFSSources(int aMaxSources = 10) noexcept;

	string getPartsString(const PartsInfo& partsInfo) const;

	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept override;

	void on(SearchManagerListener::IncomingSearch, Client* aClient, const OnlineUserPtr& aUser, const SearchQuery& aQuery, const SearchResultList&, bool aIsUdpActive) noexcept override;

	void on(ProtocolCommandManagerListener::IncomingUDPCommand, const AdcCommand&, const string&) noexcept override;
	void on(ProtocolCommandManagerListener::IncomingHubCommand, const AdcCommand&, const Client&) noexcept override;

	void onIncomingSearch(const Client* aClient, const OnlineUserPtr& aUser, const SearchQuery& aQuery, bool aIsUdpActive) noexcept;

	bool handlePartialResultHooked(const QueueItemPtr& aQI, const PartialFileSource::Ptr& aPartialSource, const PartsInfo& aInPartialInfo) noexcept;
	void requestPartialSourceInfo(uint64_t aTick, uint64_t aNextQueryTime) noexcept;

	bool handlePartialSearch(const QueueItemPtr& aQI, PartsInfo& _outPartsInfo) noexcept;

	bool allowPartialSharing(const QueueItemPtr& aQI) const noexcept;

	void dbgMsg(const string& aMsg, LogMessage::Severity aSeverity) const noexcept;
	QueueItemPtr getQueueFile(const TTHValue& tth) const noexcept;

	void sendUDP(AdcCommand& aCmd, const UserPtr& aUser, const string& aHubUrl);

	mutable SharedMutex cs;

	set<PartialFileSource::Ptr, PartialFileSource::Sort> sources;
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_PARTIAL_FILE_SHARING_H)