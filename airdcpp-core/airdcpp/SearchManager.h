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

#ifndef DCPLUSPLUS_DCPP_SEARCH_MANAGER_H
#define DCPLUSPLUS_DCPP_SEARCH_MANAGER_H

#include "ResourceManager.h"
#include "SearchManagerListener.h"
#include "SettingsManagerListener.h"
#include "TimerManagerListener.h"

#include "AdcCommand.h"
#include "CriticalSection.h"
#include "GetSet.h"
#include "Message.h"
#include "Search.h"
#include "Singleton.h"
#include "Speaker.h"
#include "UDPServer.h"
#include "Util.h"


namespace dcpp {

#define SEARCH_TYPE_ANY "0"
#define SEARCH_TYPE_DIRECTORY "7"
#define SEARCH_TYPE_TTH "8"
#define SEARCH_TYPE_FILE "9"

class SocketException;

struct SearchQueueInfo {
	StringSet queuedHubUrls;
	uint64_t queueTime;
	string error;
};

class SearchType {
public:
	SearchType(const string& aId, const string& aName, const StringList& aExtensions) :
		id(aId), name(aName), extensions(aExtensions) {

	}

	string getDisplayName() const noexcept;
	bool isDefault() const noexcept;
	Search::TypeModes getTypeMode() const noexcept;

	GETSET(string, id, Id);
	GETSET(string, name, Name);
	GETSET(StringList, extensions, Extensions);
};

class SearchManager : public Speaker<SearchManagerListener>, public Singleton<SearchManager>, private TimerManagerListener, private SettingsManagerListener
{
public:
	typedef map<string, SearchTypePtr> SearchTypes;
private:
	static ResourceManager::Strings types[Search::TYPE_LAST];
public:
	static const string& getTypeStr(int aType) noexcept;
	static bool isDefaultTypeStr(const string& aType) noexcept;
	
	SearchQueueInfo search(const SearchPtr& aSearch) noexcept;
	SearchQueueInfo search(StringList& aHubUrls, const SearchPtr& aSearch, void* aOwner = nullptr) noexcept;
	
	void respond(const AdcCommand& cmd, OnlineUser& aUser, bool isUdpActive, const string& hubIpPort, ProfileToken aProfile);

	const string& getPort() const;

	void listen();
	void disconnect() noexcept;
	void onSR(const string& aLine, const string& aRemoteIP=Util::emptyString);

	void onRES(const AdcCommand& cmd, const UserPtr& from, const string& remoteIp);
	void onPSR(const AdcCommand& cmd, UserPtr from, const string& remoteIp);
	void onPBD(const AdcCommand& cmd, const UserPtr& from);
	AdcCommand toPSR(bool wantResponse, const string& myNick, const string& hubIpPort, const string& tth, const vector<uint16_t>& partialInfo) const;
	AdcCommand toPBD(const string& hubIpPort, const string& bundle, const string& aTTH, bool reply, bool add, bool notify = false) const;


	// Search types
	static void validateSearchTypeName(const string& aName);
	void setSearchTypeDefaults();
	SearchTypePtr addSearchType(const string& aName, const StringList& aExtensions);
	void delSearchType(const string& aId);
	SearchTypePtr modSearchType(const string& aId, const optional<string>& aName, const optional<StringList>& aExtensions);

	SearchTypeList getSearchTypes() const noexcept;

	void getSearchType(int aPos, Search::TypeModes& type_, StringList& extList_, string& typeId_);
	void getSearchType(const string& aId, Search::TypeModes& type_, StringList& extList_, string& name_);

	SearchTypePtr getSearchType(const string& aId) const;
	string getTypeIdByExtension(const string& aExtension, bool aDefaultsOnly = false) const noexcept;

	bool decryptPacket(string& x, size_t aLen, const ByteVector& aBuf);

	SearchInstancePtr createSearchInstance(const string& aOwnerId, uint64_t aExpirationTick = 0) noexcept;
	SearchInstancePtr removeSearchInstance(SearchInstanceToken aToken) noexcept;
	SearchInstancePtr getSearchInstance(SearchInstanceToken aToken) const noexcept;
	SearchInstanceList getSearchInstances() const noexcept;
private:
	vector<pair<uint8_t*, uint64_t>> searchKeys;

	mutable SharedMutex cs;

	friend class Singleton<SearchManager>;

	SearchManager();

	static std::string normalizeWhitespace(const std::string& aString);

	~SearchManager();

	string getPartsString(const PartsInfo& partsInfo) const;
	
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;

	void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept;
	void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept;

	// Search types
	SearchTypes searchTypes; // name, extlist

	UDPServer udpServer;

	typedef map<SearchInstanceToken, SearchInstancePtr> SearchInstanceMap;
	SearchInstanceMap searchInstances;

	void dbgMsg(const string& aMsg, LogMessage::Severity aSeverity) noexcept;
};

} // namespace dcpp

#endif // !defined(SEARCH_MANAGER_H)