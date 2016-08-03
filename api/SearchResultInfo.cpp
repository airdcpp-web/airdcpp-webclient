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

#include <api/SearchApi.h>
#include <api/SearchUtils.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/GeoManager.h>
#include <airdcpp/DirectoryListingManager.h>
#include <airdcpp/QueueManager.h>

#include <boost/range/numeric.hpp>


namespace webserver {
	FastCriticalSection SearchResultInfo::cs;

	SearchResultInfo::SearchResultInfo(const SearchResultPtr& aSR, SearchResult::RelevanceInfo&& aRelevance) :
		token(Util::rand()), sr(aSR), relevanceInfo(move(aRelevance)) {

		// check the dupe
		if (SETTING(DUPE_SEARCH)) {
			if (sr->getType() == SearchResult::TYPE_DIRECTORY)
				dupe = AirUtil::checkDirDupe(sr->getPath(), sr->getSize());
			else
				dupe = AirUtil::checkFileDupe(sr->getTTH());
		}

		//get the ip info
		country = GeoManager::getInstance()->getCountry(sr->getIP());
	}

	void SearchResultInfo::addChildResult(const SearchResultInfo::Ptr& aResult) noexcept {
		FastLock l(cs);
		children.push_back(aResult);
		aResult->parent = this;
		hits++;
	}

	bool SearchResultInfo::hasUser(const UserPtr& aUser) const noexcept {
		if (getUser() == aUser) {
			return true;
		}

		FastLock l(cs);
		return boost::find_if(children, [&](const SearchResultInfoPtr& aResult) { return aResult->getUser() == aUser; }) != children.end();
	}

	double SearchResultInfo::getConnectionSpeed() const noexcept {
		FastLock l(cs);
		return static_cast<double>(boost::accumulate(children, sr->getConnectionInt(), [](int64_t total, const SearchResultInfo::Ptr& aSI) {
			return total + aSI->sr->getConnectionInt();
		}));
	}

	void SearchResultInfo::getSlots(int& free_, int& total_) const noexcept {
		free_ += sr->getFreeSlots();
		total_ += sr->getSlots();

		FastLock l(cs);
		for (const auto& si : children) {
			free_ += si->sr->getFreeSlots();
			total_ += si->sr->getSlots();
		}
	}

	string SearchResultInfo::getSlotStr() const noexcept {
		int free = 0, total = 0;
		getSlots(free, total);
		return Util::toString(free) + '/' + Util::toString(total);
	}

	double SearchResultInfo::getTotalRelevance() const noexcept {
		return (hits * relevanceInfo.sourceScoreFactor) + relevanceInfo.matchRelevance;
	}

	double SearchResultInfo::getMatchRelevance() const noexcept {
		return relevanceInfo.matchRelevance; 
	}

	api_return SearchResultInfo::download(const string& aTargetDirectory, const string& aTargetName, TargetUtil::TargetType aTargetType, QueueItemBase::Priority aPrio) {
		bool fileDownload = sr->getType() == SearchResult::TYPE_FILE;

		auto download = [&](const SearchResultPtr& aSR) {
			if (fileDownload) {
				QueueManager::getInstance()->createFileBundle(aTargetDirectory + aTargetName, sr->getSize(), sr->getTTH(), sr->getUser(), sr->getDate(), 0, aPrio);
			} else {
				DirectoryListingManager::getInstance()->addDirectoryDownload(aSR->getFilePath(), aTargetName, aSR->getUser(), aTargetDirectory, aTargetType,
					false, aPrio, false, 0, false, false);
			}
		};

		SearchResultList results = { sr };
		if (hits >= 1) {
			FastLock l(cs);
			for (auto si : children)
				results.push_back(si->sr);
		}

		SearchResult::pickResults(results, SETTING(MAX_AUTO_MATCH_SOURCES));
		boost::for_each(results, download);

		return websocketpp::http::status_code::ok;
	}
}