/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#include <airdcpp/AirUtil.h>
//#include <airdcpp/UserInfoBase.h>
#include <airdcpp/SearchResult.h>
#include <airdcpp/QueueItemBase.h>
#include <airdcpp/TargetUtil.h>

namespace webserver {
	typedef uint32_t ResultToken;

	class SearchResultInfo {
	public:
		typedef shared_ptr<SearchResultInfo> Ptr;
		typedef vector<Ptr> List;
		typedef unordered_map<TTHValue, Ptr> Map;

		SearchResultInfo(const SearchResultPtr& aSR, const SearchQuery& aSearch);
		~SearchResultInfo() {	}

		const UserPtr& getUser() const { return sr->getUser().user; }
		const string& getHubUrl() const { return sr->getUser().hint; }

		size_t hits = 0;

		void addItem(const SearchResultInfo::Ptr& aResult) noexcept;
		api_return download(const string& aTargetDirectory, const string& aTargetName, TargetUtil::TargetType aTargetType, QueueItemBase::Priority p);

		SearchResultInfo* parent = nullptr;
		SearchResultInfo::List children;

		/*struct CheckTTH {
		CheckTTH() : op(true), firstHubs(true), firstPath(true), firstTTH(true) { }

		void operator()(SearchInfo* si);
		bool firstHubs;
		StringList hubs;
		bool op;

		bool firstTTH;
		bool firstPath;
		optional<TTHValue> tth;
		optional<string> path;
		};*/

		bool isDupe() const { return dupe != DUPE_NONE; }
		bool isShareDupe() const { return dupe == DUPE_SHARE || dupe == DUPE_SHARE_PARTIAL; }
		bool isQueueDupe() const { return dupe == DUPE_QUEUE || dupe == DUPE_FINISHED; }
		//StringList getDupePaths() const;

		SearchResultPtr sr;
		IGETSET(DupeType, dupe, Dupe, DUPE_NONE);

		double getTotalRelevancy() const;
		double getMatchRelevancy() const { return matchRelevancy; }

		ResultToken getToken() const noexcept {
			return token;
		}

		const string& getCountry() const noexcept {
			return country;
		}

		double getConnectionSpeed() const noexcept;
		void getSlots(int& free_, int& total_) const noexcept;
		string getSlotStr() const noexcept;
	private:
		string country;
		double matchRelevancy = 0;
		double sourceScoreFactor = 0.01;
		ResultToken token;

		static FastCriticalSection cs;
	};

	typedef SearchResultInfo::Ptr SearchResultInfoPtr;
}

#endif