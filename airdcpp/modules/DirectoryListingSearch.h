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

#ifndef DCPLUSPLUS_DCPP_DIRECTORY_LISTING_SEARCH_H
#define DCPLUSPLUS_DCPP_DIRECTORY_LISTING_SEARCH_H

#include <airdcpp/forward.h>
#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/filelist/DirectoryListingDirectory.h>
#include <airdcpp/core/timer/TimerManagerListener.h>

namespace dcpp {

class DirectSearch;
class SearchQuery;

class DirectoryListingSearch : private TimerManagerListener
{
public:
	using FailedCallback = std::function<void(bool)>;

	DirectoryListingSearch(const DirectoryListingPtr& aList, FailedCallback&& aFailedHandler);
	~DirectoryListingSearch() override;

	void addSearchTask(const SearchPtr& aSearch) noexcept;

	bool nextResult(bool prev) noexcept;

	unique_ptr<SearchQuery> curSearch;

	bool isCurrentSearchPath(const string& aPath) const noexcept;
	size_t getResultCount() const noexcept { return searchResults.size(); }

	bool supportsASCH() const noexcept;
	string getCurrentSearchPath() const noexcept;
private:
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept override;

	void endSearch(bool timedOut = false) noexcept;

	OrderedStringSet searchResults;
	OrderedStringSet::iterator curResult;

	void searchImpl(const SearchPtr& aSearch) noexcept;

	unique_ptr<DirectSearch> directSearch;

	DirectoryListingPtr list;

	void searchRecursive(const DirectoryListing::DirectoryPtr& aDir, OrderedStringSet& aResults, SearchQuery& aStrings) const noexcept;

	FailedCallback failedHandler;
};

} // namespace dcpp

#endif // !defined(DIRECTORY_LISTING_H)
