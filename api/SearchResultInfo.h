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

#ifndef DCPLUSPLUS_DCPP_SEARCHRESULTINFO_H
#define DCPLUSPLUS_DCPP_SEARCHRESULTINFO_H

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/GetSet.h>

#include <airdcpp/DupeType.h>
#include <airdcpp/Priority.h>
#include <airdcpp/SearchResult.h>
#include <airdcpp/TargetUtil.h>

namespace webserver {
	typedef uint32_t ResultToken;

	class SearchResultInfo {
	public:
		typedef shared_ptr<SearchResultInfo> Ptr;
		struct RelevanceSort {
			bool operator()(const Ptr& left, const Ptr& right) const noexcept { return left->getTotalRelevance() > right->getTotalRelevance(); }
		};

		typedef vector<Ptr> List;
		typedef unordered_map<TTHValue, Ptr> Map;
		typedef set<Ptr, RelevanceSort> Set;

		SearchResultInfo(const SearchResultPtr& aSR, SearchResult::RelevanceInfo&& aRelevance);
		~SearchResultInfo() {	}

		const UserPtr& getUser() const noexcept { return sr->getUser().user; }
		const string& getHubUrl() const noexcept { return sr->getUser().hint; }

		bool hasUser(const UserPtr& aUser) const noexcept;
		bool addChildResult(const SearchResultPtr& aResult) noexcept;

		// Selects the best individual to download and queues them
		// Throws if none of the children could not be queued
		json download(const string& aTargetDirectory, const string& aTargetName, Priority p);

		bool isDirectory() const noexcept {
			return sr->getType() == SearchResult::TYPE_DIRECTORY;
		}

		const SearchResultPtr sr;

		double getTotalRelevance() const noexcept;
		double getMatchRelevance() const noexcept;

		ResultToken getToken() const noexcept {
			return token;
		}

		DupeType getDupe() const noexcept {
			return dupe;
		}

		int getHits() const noexcept {
			return hits;
		}

		double getConnectionSpeed() const noexcept;
		void getSlots(int& free_, int& total_) const noexcept;
		time_t getOldestDate() const noexcept;

		SearchResultList getChildren() const noexcept;
	private:
		DupeType dupe;
		SearchResultList children;

		const SearchResult::RelevanceInfo relevanceInfo;

		const ResultToken token;
		int hits = 0;

		static FastCriticalSection cs;
	};

	typedef SearchResultInfo::Ptr SearchResultInfoPtr;
}

#endif