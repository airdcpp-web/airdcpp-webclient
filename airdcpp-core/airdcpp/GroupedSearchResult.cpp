/*
* Copyright (C) 2011-2021 AirDC++ Project
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

#include "stdinc.h"
#include "GroupedSearchResult.h"

#include <airdcpp/AirUtil.h>
#include <airdcpp/GeoManager.h>
#include <airdcpp/DirectoryListingManager.h>
#include <airdcpp/QueueManager.h>

#include <boost/range/numeric.hpp>


namespace dcpp {
	FastCriticalSection GroupedSearchResult::cs;

	GroupedSearchResult::GroupedSearchResult(const SearchResultPtr& aSR, SearchResult::RelevanceInfo&& aRelevance) :
		/*token(Util::rand()),*/ baseResult(aSR), relevanceInfo(move(aRelevance)) {

		// check the dupe
		if (SETTING(DUPE_SEARCH)) {
			if (baseResult->getType() == SearchResult::TYPE_DIRECTORY) {
				dupe = AirUtil::checkAdcDirectoryDupe(baseResult->getAdcPath(), baseResult->getSize());
			} else {
				dupe = AirUtil::checkFileDupe(baseResult->getTTH());
			}
		}

		children.push_back(aSR);
	}

	bool GroupedSearchResult::addChildResult(const SearchResultPtr& aResult) noexcept {
		// No duplicate results for the same user that are received via different hubs
		if (hasUser(aResult->getUser())) {
			return false;
		}

		FastLock l(cs);
		children.push_back(aResult);
		return true;
	}

	SearchResultList GroupedSearchResult::getChildren() const noexcept {
		FastLock l(cs);
		return children;
	}

	bool GroupedSearchResult::hasUser(const UserPtr& aUser) const noexcept {
		FastLock l(cs);
		return boost::find_if(children, [&](const SearchResultPtr& aResult) { return aResult->getUser() == aUser; }) != children.end();
	}

	double GroupedSearchResult::getConnectionSpeed() const noexcept {
		FastLock l(cs);
		return static_cast<double>(boost::accumulate(children, static_cast<int64_t>(0), [](int64_t total, const SearchResultPtr& aSR) {
			return total + aSR->getConnectionInt();
		}));
	}

	int GroupedSearchResult::getHits() const noexcept {
		return static_cast<int>(children.size());
	}

	GroupedSearchResult::SlotInfo GroupedSearchResult::getSlots() const noexcept {
		int free = 0, total = 0;

		{
			FastLock l(cs);
			for (const auto& c : children) {
				free += c->getFreeSlots();
				total += c->getTotalSlots();
			}
		}

		return { free, total };
	}

	const DirectoryContentInfo& GroupedSearchResult::getContentInfo() const noexcept {
		{
			FastLock l(cs);

			// Attempt to find a user that provides this information
			auto i = boost::find_if(children, [&](const SearchResultPtr& aResult) { return Util::hasContentInfo(aResult->getContentInfo()); });
			if (i != children.end()) {
				return (*i)->getContentInfo();
			}
		}


		return baseResult->getContentInfo();
	}

	time_t GroupedSearchResult::getOldestDate() const noexcept {
		FastLock l(cs);
		auto min = min_element(children.begin(), children.end(), SearchResult::DateOrder());
		return (*min)->getDate();
	}

	double GroupedSearchResult::getTotalRelevance() const noexcept {
		return (getHits() * relevanceInfo.sourceScoreFactor) + relevanceInfo.matchRelevance;
	}

	double GroupedSearchResult::getMatchRelevance() const noexcept {
		return relevanceInfo.matchRelevance;
	}

	SearchResultList GroupedSearchResult::pickDownloadResults() const noexcept {
		SearchResultList results;
		{
			FastLock l(cs);
			results = children;
		}

		SearchResult::pickResults(results, SETTING(MAX_AUTO_MATCH_SOURCES));
		return results;
	}

	BundleAddInfo GroupedSearchResult::downloadFileHooked(const string& aTargetDirectory, const string& aTargetName, Priority aPrio, const void* aCaller) {
		string lastError;
		optional<BundleAddInfo> bundleAddInfo;

		boost::for_each(pickDownloadResults(), [&](const SearchResultPtr& aSR) {
			try {
				auto fileInfo = BundleFileAddData(aTargetName, aSR->getTTH(), aSR->getSize(), aPrio, aSR->getDate());
				auto options = BundleAddOptions(aTargetDirectory, aSR->getUser(), aCaller);
				auto curInfo = QueueManager::getInstance()->createFileBundleHooked(options, fileInfo);
				if (!bundleAddInfo) {
					bundleAddInfo = curInfo;
				}
			} catch (const Exception& e) {
				lastError = e.getError();
			}
		});

		if (!bundleAddInfo) {
			throw Exception(lastError);
		}

		return *bundleAddInfo;
	}

	DirectoryDownloadList GroupedSearchResult::downloadDirectoryHooked(const string& aTargetDirectory, const string& aTargetName, Priority aPrio, const void* aCaller) {
		string lastError;
		DirectoryDownloadList directoryDownloads;

		boost::for_each(pickDownloadResults(), [&](const SearchResultPtr& aSR) {
			try {
				auto listData = FilelistAddData(aSR->getUser(), aCaller, aSR->getAdcFilePath());
				auto directoryDownload = DirectoryListingManager::getInstance()->addDirectoryDownloadHookedThrow(listData, aTargetName, aTargetDirectory, aPrio, DirectoryDownload::ErrorMethod::LOG);
				directoryDownloads.push_back(directoryDownload);
			} catch (const Exception& e) {
				lastError = e.getError();
			}
		});

		if (directoryDownloads.empty()) {
			throw Exception(lastError);
		}

		return directoryDownloads;
	}
}