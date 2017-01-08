/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_GROUPED_SEARCHRESULT_H
#define DCPLUSPLUS_DCPP_GROUPED_SEARCHRESULT_H

#include "stdinc.h"

#include <airdcpp/GetSet.h>

#include <airdcpp/DupeType.h>
#include <airdcpp/Priority.h>
#include <airdcpp/SearchResult.h>

namespace dcpp {
	//typedef uint64_t ResultToken;
	typedef TTHValue GroupedResultToken;

	class GroupedSearchResult {
	public:
		typedef shared_ptr<GroupedSearchResult> Ptr;
		struct RelevanceSort {
			bool operator()(const Ptr& left, const Ptr& right) const noexcept { return left->getTotalRelevance() > right->getTotalRelevance(); }
		};

		typedef vector<Ptr> List;
		typedef unordered_map<TTHValue, Ptr> Map;
		typedef set<Ptr, RelevanceSort> Set;

		GroupedSearchResult(const SearchResultPtr& aSR, SearchResult::RelevanceInfo&& aRelevance);
		~GroupedSearchResult() { }

		bool hasUser(const UserPtr& aUser) const noexcept;
		bool addChildResult(const SearchResultPtr& aResult) noexcept;

		// Selects the best individual files to download and queues them
		// Throws if none of the children could not be queued
		BundleAddInfo downloadFile(const string& aTargetDirectory, const string& aTargetName, Priority p);

		// Selects the best individual folders to download and queues them
		// Throws if none of the children could not be queued
		DirectoryDownloadList downloadDirectory(const string& aTargetDirectory, const string& aTargetName, Priority p);

		bool isDirectory() const noexcept {
			return baseResult->getType() == SearchResult::TYPE_DIRECTORY;
		}

		double getTotalRelevance() const noexcept;
		double getMatchRelevance() const noexcept;

		/*GroupedResultToken getToken() const noexcept {
			return token;
		}*/

		string getToken() const noexcept {
			return baseResult->getTTH().toBase32();
		}

		const TTHValue& getTTH() const noexcept {
			return baseResult->getTTH();
		}

		DupeType getDupe() const noexcept {
			return dupe;
		}

		int64_t getSize() const noexcept {
			return baseResult->getSize();
		}

		const string& getPath() const noexcept {
			return baseResult->getPath();
		}

		string getFileName() const noexcept {
			return baseResult->getFileName();
		}

		const HintedUser& getBaseUser() const noexcept {
			return baseResult->getUser();
		}

		int getHits() const noexcept;

		double getConnectionSpeed() const noexcept;

		struct SlotInfo {
			const int free;
			const int total;
		};
		SlotInfo getSlots() const noexcept;

		const DirectoryContentInfo& getContentInfo() const noexcept;

		time_t getOldestDate() const noexcept;

		SearchResultList getChildren() const noexcept;
	private:
		// Selects the best child results for downloading
		SearchResultList pickDownloadResults() const noexcept;

		DupeType dupe;
		SearchResultList children;
		const SearchResultPtr baseResult;

		const SearchResult::RelevanceInfo relevanceInfo;

		//const GroupedResultToken token;

		static FastCriticalSection cs;
	};
}

#endif