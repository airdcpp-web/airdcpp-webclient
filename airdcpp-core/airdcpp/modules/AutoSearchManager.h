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

#ifndef DCPLUSPLUS_DCPP_AUTOSEARCH_MANAGER_H
#define DCPLUSPLUS_DCPP_AUTOSEARCH_MANAGER_H

#include <airdcpp/forward.h>

#include "AutoSearchManagerListener.h"
#include "AutoSearchQueue.h"

#include <airdcpp/DirectoryListingManagerListener.h>
#include <airdcpp/SearchManagerListener.h>
#include <airdcpp/QueueManagerListener.h>

#include <airdcpp/DelayedEvents.h>
#include <airdcpp/GetSet.h>
#include <airdcpp/Message.h>
#include <airdcpp/Singleton.h>
#include <airdcpp/Speaker.h>
#include <airdcpp/TimerManagerListener.h>


namespace dcpp {

class AutoSearchManager : public Singleton<AutoSearchManager>, public Speaker<AutoSearchManagerListener>, 
	private TimerManagerListener, private SearchManagerListener, private QueueManagerListener, private DirectoryListingManagerListener {
public:
	enum SearchType {
		TYPE_MANUAL_FG,
		TYPE_MANUAL_BG,
		TYPE_NEW,
		TYPE_NORMAL
	};

	AutoSearchManager() noexcept;
	~AutoSearchManager() noexcept;

	//AutoSearchPtr getNameDupe(const string& aName, bool report, const AutoSearchPtr& thisSearch = nullptr) const noexcept;
	bool addFailedBundle(const BundlePtr& aBundle) noexcept;
	void addAutoSearch(AutoSearchPtr aAutoSearch, bool search, bool loading = false) noexcept;
	AutoSearchPtr addAutoSearch(const string& ss, const string& targ, bool isDirectory, AutoSearch::ItemType asType, bool aRemove = true, bool aSearch = true, int aExpiredays = 0) noexcept;
	bool validateAutoSearchStr(const string& aStr) const noexcept;
	AutoSearchList getSearchesByBundle(const BundlePtr& aBundle) const noexcept;
	AutoSearchList getSearchesByString(const string& aSearchString, const AutoSearchPtr& ignoredSearch = nullptr) const noexcept;

	string getBundleStatuses(const AutoSearchPtr& as) const noexcept;
	void clearPaths(AutoSearchPtr as) noexcept;

	void getMenuInfo(const AutoSearchPtr& as, BundleList& bundleInfo, AutoSearch::FinishedPathMap& finishedPaths) const noexcept;
	bool updateAutoSearch(AutoSearchPtr& ipw) noexcept;
	void removeAutoSearch(AutoSearchPtr& a) noexcept;
	bool searchItem(AutoSearchPtr& as, SearchType aType) noexcept;

	void changeNumber(AutoSearchPtr as, bool increase) noexcept;
	
	AutoSearchMap& getSearchItems() noexcept {
		return searchItems.getItems(); 
	};

	bool setItemActive(AutoSearchPtr& as, bool active) noexcept;

	time_t getNextSearch() const noexcept { return nextSearch; }

	void load() noexcept;
	void save() noexcept;

	void logMessage(const string& aMsg, LogMessage::Severity aSeverity) const noexcept;

	void onBundleCreated(const BundlePtr& aBundle, const void* aSearch) noexcept;
	void onBundleError(const void* aSearch, const string& aError, const string& aBundleName, const HintedUser& aUser) noexcept;

	vector<string> getGroups() { RLock l(cs);  return groups; }
	void setGroups(vector<string>& newGroups) { WLock l(cs);  groups = newGroups; }
	void moveItemToGroup(AutoSearchPtr& as, const string& aGroupName);
	bool hasGroup(const string& aGroup) { RLock l(cs);  return (find(groups.begin(), groups.end(), aGroup) != groups.end()); }
	int getGroupIndex(const AutoSearchPtr& as);

	void maybePopSearchItem(uint64_t aTick, bool aIgnoreSearchTimes);

	SharedMutex& getCS() { return cs; }
private:
	enum {
		RECALCULATE_SEARCH,
		SEARCH_ITEM
	};

	mutable SharedMutex cs;

	//Delayed events used to collect search results and calculate search times.
	DelayedEvents<int> delayEvents;
	
	vector<string> groups;


	void performSearch(AutoSearchPtr& as, StringList& aHubs, SearchType aType, uint64_t aTick = GET_TICK()) noexcept;
	//count minutes to be more accurate than comparing ticks every minute.
	void checkItems() noexcept;
	Searches searchItems;

	void loadAutoSearch(SimpleXML& aXml);

	AutoSearchPtr loadItemFromXml(SimpleXML& aXml);
	uint64_t lastSave = 0;
	bool dirty = false;
	time_t nextSearch = 0;

	bool endOfListReached = false;

	unordered_map<ProfileToken, SearchResultList> searchResults;
	void pickNameMatch(AutoSearchPtr as) noexcept;
	void downloadList(SearchResultList& sr, AutoSearchPtr& as, int64_t minWantedSize) noexcept;
	void handleAction(const SearchResultPtr& sr, AutoSearchPtr& as) noexcept;

	void updateStatus(AutoSearchPtr& as, bool setTabDirty) noexcept;
	void clearError(AutoSearchPtr& as) noexcept;
	void resetSearchTimes(uint64_t aTick, bool aRecalculate = true) noexcept;

	/* Listeners */
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept override;

	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept override;
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept override;

	void on(QueueManagerListener::BundleRemoved, const BundlePtr& aBundle) noexcept override { onRemoveBundle(aBundle, false); }
	void on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept override;

	void on(DirectoryListingManagerListener::DirectoryDownloadProcessed, const DirectoryDownloadPtr& aDirectoryInfo, const DirectoryBundleAddResult& aQueueInfo, const string& aError) noexcept override;
	void on(DirectoryDownloadFailed, const DirectoryDownloadPtr&, const string&) noexcept override;

	//bool onBundleStatus(BundlePtr& aBundle, const ProfileTokenSet& aSearches);
	void onRemoveBundle(const BundlePtr& aBundle, bool finished) noexcept;
	void handleExpiredItems(AutoSearchList& asList) noexcept;

	time_t toTimeT(uint64_t& aValue, uint64_t currentTick = GET_TICK());
};
}
#endif