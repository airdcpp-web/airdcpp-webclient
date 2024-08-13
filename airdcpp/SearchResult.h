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

#ifndef DCPLUSPLUS_DCPP_SEARCHRESULT_H
#define DCPLUSPLUS_DCPP_SEARCHRESULT_H

#include "DirectoryContentInfo.h"
#include "DupeType.h"
#include "GetSet.h"
#include "forward.h"
#include "FastAlloc.h"
#include "HintedUser.h"
#include "MerkleTree.h"
#include "Util.h"

namespace dcpp {

typedef uint64_t SearchResultId;
class SearchResult : public FastAlloc<SearchResult> {
public:	
	enum Types {
		TYPE_FILE,
		TYPE_DIRECTORY
	};

	//outgoing result (direct)
	SearchResult(const string& name);

	//outgoing result (normal)
	SearchResult(Types aType, int64_t aSize, const string& name, const TTHValue& aTTH, time_t aDate, const DirectoryContentInfo& aContentInfo);

	//incoming results
	SearchResult(const HintedUser& aUser, Types aType, uint8_t aTotalSlots, uint8_t aFreeSlots, 
		int64_t aSize, const string& aFilePath, const string& ip, TTHValue aTTH, const string& aToken, time_t aDate, const string& connection, const DirectoryContentInfo& aContentInfo);

	string getFileName() const noexcept;
	string toSR(const Client& client) const noexcept;
	AdcCommand toRES(char type) const noexcept;

	const HintedUser& getUser() const noexcept { return user; }

	static string formatSlots(size_t aFree, size_t aTotal) noexcept;
	string getSlotString() const noexcept;

	string getAdcFilePath() const noexcept;
	const string& getAdcPath() const noexcept { return path; }

	int64_t getSize() const noexcept { return size; }
	Types getType() const noexcept { return type; }
	size_t getTotalSlots() const noexcept { return totalSlots; }
	size_t getFreeSlots() const noexcept { return freeSlots; }
	const TTHValue& getTTH() const noexcept { return tth; }
	
	const string& getConnectionStr() const noexcept { return connection; }
	int64_t getConnectionInt() const noexcept;
	int64_t getSpeedPerSlot() const noexcept;

	const string& getIP() const noexcept { return IP; }
	const string& getSearchToken() const noexcept { return searchToken; }
	SearchResultId getId() const noexcept { return id; }
	time_t getDate() const noexcept { return date; }
	const CID& getCID() const noexcept;
	bool isNMDC() const noexcept;

	static void pickResults(SearchResultList& aResults, int aMaxCount) noexcept;
	struct SpeedSortOrder {
		bool operator()(const SearchResultPtr& left, const SearchResultPtr& right) const noexcept;
	};

	// The oldest date will go first (min element)
	// Results without a date will go last
	struct DateOrder {
		bool operator()(const SearchResultPtr& a, const SearchResultPtr& b) const noexcept;
	};

	struct RelevanceInfo {
		double matchRelevance;
		double sourceScoreFactor;
	};

	// Matches result against the current search and returns relevance information
	// Non-mandatory validity checks are skipped if no search token is provided
	bool getRelevance(SearchQuery& aQuery, RelevanceInfo& relevance_, const string& aSearchToken = Util::emptyString) const noexcept;

	const DirectoryContentInfo& getContentInfo() const noexcept { return contentInfo; }
	DupeType getDupe() const noexcept;
private:
	bool matches(SearchQuery& aQuery, const string& aSearchToken) const noexcept;

	friend class SearchManager;

	SearchResult();

	const TTHValue tth;
	
	const string path;
	const string IP;

	const string searchToken;
	const SearchResultId id;
	
	const int64_t size = 0;
	
	const size_t totalSlots = 0;
	const size_t freeSlots = 0;

	const DirectoryContentInfo contentInfo;
	
	const HintedUser user;
	const Types type;

	const time_t date = 0;
	const string connection;
};

}

#endif
