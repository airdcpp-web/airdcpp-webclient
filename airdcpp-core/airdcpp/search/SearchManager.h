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

#ifndef DCPLUSPLUS_DCPP_SEARCH_MANAGER_H
#define DCPLUSPLUS_DCPP_SEARCH_MANAGER_H

#include <airdcpp/search/SearchManagerListener.h>
#include <airdcpp/core/timer/TimerManagerListener.h>

#include <airdcpp/core/ActionHook.h>
#include <airdcpp/protocol/AdcCommand.h>
#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/core/types/GetSet.h>
#include <airdcpp/message/Message.h>
#include <airdcpp/core/Singleton.h>
#include <airdcpp/core/Speaker.h>
#include <airdcpp/connection/UDPServer.h>
#include <airdcpp/util/Util.h>


namespace dcpp {

class SearchTypes;
class SocketException;
class UDPServer;

struct SearchQueueInfo {
	StringSet queuedHubUrls;
	uint64_t queueTime;
	string error;
};

class SearchManager : public Speaker<SearchManagerListener>, public Singleton<SearchManager>, private TimerManagerListener
{
public:
	ActionHook<nullptr_t, const SearchResultPtr&> incomingSearchResultHook;

	SearchQueueInfo search(const SearchPtr& aSearch) noexcept;
	SearchQueueInfo search(const StringList& aHubUrls, const SearchPtr& aSearch, void* aOwner = nullptr) noexcept;
	
	void respond(const AdcCommand& cmd, Client* aClient, const OnlineUserPtr& aUser, bool aIsUdpActive, ProfileToken aProfile) noexcept;
	void respond(Client* aClient, const string& aSeeker, int aSearchType, int64_t aSize, int aFileType, const string& aString, bool aIsPassive) noexcept;

	const string& getPort() const;

	void listen();
	void disconnect() noexcept;
	void onSR(const string& aLine, const string& aRemoteIP = Util::emptyString);

	void onRES(const AdcCommand& cmd, const UserPtr& aFrom, const string& aRemoteIp);

	bool decryptPacket(string& x, size_t aLen, const ByteVector& aBuf);

	SearchInstancePtr createSearchInstance(const string& aOwnerId, uint64_t aExpirationTick = 0) noexcept;
	SearchInstancePtr removeSearchInstance(SearchInstanceToken aToken) noexcept;
	SearchInstancePtr getSearchInstance(SearchInstanceToken aToken) const noexcept;
	SearchInstanceList getSearchInstances() const noexcept;

	SearchTypes& getSearchTypes() noexcept {
		return *searchTypes.get();
	}

	UDPServer& getUdpServer() noexcept {
		return *udpServer.get();
	}
private:
	vector<pair<std::unique_ptr<uint8_t[]>, uint64_t>> searchKeys;

	string generateSUDPKey();

	mutable SharedMutex cs;

	friend class Singleton<SearchManager>;

	SearchManager();

	static std::string normalizeWhitespace(const std::string& aString);

	~SearchManager() override;
	
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept override;

	const unique_ptr<SearchTypes> searchTypes;
	const unique_ptr<UDPServer> udpServer;

	using SearchInstanceMap = map<SearchInstanceToken, SearchInstancePtr>;
	SearchInstanceMap searchInstances;
};

} // namespace dcpp

#endif // !defined(SEARCH_MANAGER_H)