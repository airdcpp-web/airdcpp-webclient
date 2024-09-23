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

#include "stdinc.h"

#include "DirectoryListingSearch.h"

#include "DirectSearch.h"

#include <airdcpp/ShareManager.h>
#include <airdcpp/SearchQuery.h>


namespace dcpp {

using ranges::for_each;
using ranges::find_if;

DirectoryListingSearch::DirectoryListingSearch(const DirectoryListingPtr& aList, FailedCallback&& aFailedHandler) : list(aList), failedHandler(std::move(aFailedHandler))
{

}

DirectoryListingSearch::~DirectoryListingSearch() {
	dcdebug("Filelist deleted\n");

	TimerManager::getInstance()->removeListener(this);
}

bool DirectoryListingSearch::supportsASCH() const noexcept {
	return !list->getPartialList() || list->getIsOwnList() || list->getUser()->isSet(User::ASCH);
}

void DirectoryListingSearch::addSearchTask(const SearchPtr& aSearch) noexcept {
	dcassert(aSearch && PathUtil::isAdcDirectoryPath(aSearch->path));
	list->addAsyncTask([aSearch, this] { searchImpl(aSearch); });
}

void DirectoryListingSearch::searchImpl(const SearchPtr& aSearch) noexcept {
	searchResults.clear();

	curSearch.reset(SearchQuery::getSearch(aSearch));
	if (list->getIsOwnList() && list->getPartialList()) {
		SearchResultList results;

		ShareSearch shareSearch(*curSearch, list->getShareProfile(), nullptr, aSearch->path);
		try {
			ShareManager::getInstance()->search(results, shareSearch);
		} catch (...) {}

		for (const auto& sr : results)
			searchResults.insert(sr->getAdcPath());

		endSearch(false);
	} else if (list->getPartialList() && !list->getUser()->isNMDC()) {
		TimerManager::getInstance()->addListener(this);

		directSearch.reset(new DirectSearch(list->getHintedUser(), aSearch));
	} else {
		const auto dir = list->findDirectoryUnsafe(aSearch->path);
		if (dir) {
			searchRecursive(dir, searchResults, *curSearch);
		}

		endSearch(false);
	}
}

void DirectoryListingSearch::on(TimerManagerListener::Second, uint64_t /*aTick*/) noexcept {
	if (directSearch && directSearch->finished()) {
		endSearch(directSearch->hasTimedOut());
	}
}

void DirectoryListingSearch::endSearch(bool aTimedOut /*false*/) noexcept {
	if (directSearch) {
		directSearch->getAdcPaths(searchResults, true);
		directSearch.reset(nullptr);
	}

	if (searchResults.size() == 0) {
		curSearch = nullptr;
		failedHandler(aTimedOut);
	} else {
		curResult = searchResults.begin();
		list->addDirectoryChangeTask(*curResult, DirectoryListing::DirectoryLoadType::CHANGE_NORMAL);
	}
}

bool DirectoryListingSearch::nextResult(bool aPrev) noexcept {
	if (aPrev) {
		if (curResult == searchResults.begin()) {
			return false;
		}
		advance(curResult, -1);
	} else {
		if (static_cast<size_t>(distance(searchResults.begin(), curResult)) == searchResults.size()-1) {
			return false;
		}
		advance(curResult, 1);
	}

	list->addDirectoryChangeTask(*curResult, DirectoryListing::DirectoryLoadType::CHANGE_NORMAL);
	return true;
}

bool DirectoryListingSearch::isCurrentSearchPath(const string& aPath) const noexcept {
	if (searchResults.empty())
		return false;

	return Util::stricmp(*curResult, aPath) == 0;
}

string DirectoryListingSearch::getCurrentSearchPath() const noexcept {
	if (searchResults.empty())
		return Util::emptyString;

	return *curResult;
}

void DirectoryListingSearch::searchRecursive(const DirectoryListing::DirectoryPtr& aDir, OrderedStringSet& aResults, SearchQuery& aStrings) const noexcept {
	if (aDir->isVirtual())
		return;

	if (aStrings.matchesDirectory(aDir->getName())) {
		auto path = aDir->getParent() ? aDir->getParent()->getAdcPathUnsafe() : ADC_ROOT_STR;
		auto res = ranges::find(aResults, path);
		if (res == aResults.end() && aStrings.matchesSize(aDir->getTotalSize(false))) {
			aResults.insert(path);
		}
	}

	for (const auto& f: aDir->files) {
		if (aStrings.matchesFile(f->getName(), f->getSize(), f->getRemoteDate(), f->getTTH())) {
			aResults.insert(aDir->getAdcPathUnsafe());
			break;
		}
	}

	for (const auto& d : aDir->directories | views::values) {
		searchRecursive(d, aResults, aStrings);
		if (aResults.size() >= aStrings.maxResults) return;
	}
}


} // namespace dcpp
