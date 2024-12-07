/*
* Copyright (C) 2011-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_GROUPED_SEARCHRESULT_H
#define DCPLUSPLUS_DCPP_GROUPED_SEARCHRESULT_H

#include "stdinc.h"

#include <airdcpp/core/types/GetSet.h>

#include <airdcpp/core/types/DupeType.h>
#include <airdcpp/core/types/Priority.h>
#include <airdcpp/search/SearchResult.h>

namespace dcpp {
	using GroupedResultToken = TTHValue;

	class GroupedSearchResult {
	public:
		using Ptr = shared_ptr<GroupedSearchResult>;
		struct RelevanceSort {
			bool operator()(const Ptr& left, const Ptr& right) const noexcept { return left->getTotalRelevance() > right->getTotalRelevance(); }
		};

		using List = vector<Ptr>;
		using Map = unordered_map<TTHValue, Ptr>;
		using Set = multiset<Ptr, RelevanceSort>;

		GroupedSearchResult(const SearchResultPtr& aSR, SearchResult::RelevanceInfo&& aRelevance);
		~GroupedSearchResult() { }

		bool hasUser(const UserPtr& aUser) const noexcept;
		bool addChildResult(const SearchResultPtr& aResult) noexcept;

		// Selects the best individual files to download and queues them
		// Throws if none of the children could not be queued
		BundleAddInfo downloadFileHooked(const string& aTargetDirectory, const string& aTargetName, Priority p, CallerPtr aCaller);

		// Selects the best individual folders to download and queues them
		// Throws if none of the children could not be queued
		DirectoryDownloadList downloadDirectoryHooked(const string& aTargetDirectory, const string& aTargetName, Priority p, CallerPtr aCaller) const;

		bool isDirectory() const noexcept {
			return baseResult->getType() == SearchResult::Type::DIRECTORY;
		}

		double getTotalRelevance() const noexcept;
		double getMatchRelevance() const noexcept;
		string getFileName() const noexcept;

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

		const string& getAdcPath() const noexcept {
			return baseResult->getAdcPath();
		}

		const HintedUser& getBaseUser() const noexcept {
			return baseResult->getUser();
		}

		const string& getBaseUserIP() const noexcept {
			return baseResult->getIP();
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

		// Selects the best child results for downloading
		SearchResultList pickDownloadResults() const noexcept;
	private:

		DupeType dupe;
		SearchResultList children;
		const SearchResultPtr baseResult;

		const SearchResult::RelevanceInfo relevanceInfo;

		static FastCriticalSection cs;
	};
}

#endif