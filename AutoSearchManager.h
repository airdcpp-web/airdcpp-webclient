/*
 * Copyright (C) 2011-2013 AirDC++ Project
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

#ifndef AUTO_SEARCH_MANAGER_H
#define AUTO_SEARCH_MANAGER_H

#include "forward.h"

#include "AutoSearch.h"
#include "AutoSearchManagerListener.h"
#include "SearchManagerListener.h"
#include "QueueManagerListener.h"

#include "DelayedEvents.h"
#include "GetSet.h"
#include "Singleton.h"
#include "Speaker.h"
#include "TargetUtil.h"
#include "TimerManager.h"
#include "Util.h"

namespace dcpp {

class SimpleXML;

class AutoSearchManager :  public Singleton<AutoSearchManager>, public Speaker<AutoSearchManagerListener>, private TimerManagerListener, private SearchManagerListener, private QueueManagerListener {
public:
	enum SearchType {
		TYPE_MANUAL_FG,
		TYPE_MANUAL_BG,
		TYPE_NEW,
		TYPE_NORMAL
	};

	AutoSearchManager() noexcept;
	~AutoSearchManager() noexcept;

	bool hasNameDupe(const string& aName, bool report, const AutoSearchPtr& thisSearch = nullptr) const noexcept;
	bool addFailedBundle(const BundlePtr& aBundle) noexcept;
	void addAutoSearch(AutoSearchPtr aAutoSearch, bool search) noexcept;
	AutoSearchPtr addAutoSearch(const string& ss, const string& targ, TargetUtil::TargetType aTargetType, bool isDirectory, bool aRemove = true) noexcept;
	AutoSearchPtr getSearchByIndex(unsigned int index) const noexcept;
	AutoSearchPtr getSearchByToken(ProfileToken aToken) const noexcept;
	AutoSearchList getSearchesByBundle(const BundlePtr& aBundle) const noexcept;
	AutoSearchList getSearchesByString(const string& aSearchString) const noexcept;

	string getBundleStatuses(const AutoSearchPtr& as) const noexcept;
	void clearPaths(AutoSearchPtr as) noexcept;

	void getMenuInfo(const AutoSearchPtr& as, BundleList& bundleInfo, AutoSearch::FinishedPathMap& finishedPaths) const noexcept;
	bool updateAutoSearch(AutoSearchPtr& ipw) noexcept;
	void removeAutoSearch(AutoSearchPtr& a) noexcept;
	bool searchItem(AutoSearchPtr& as, SearchType aType) noexcept;

	void changeNumber(AutoSearchPtr as, bool increase) noexcept;
	
	AutoSearchList& getSearchItems() noexcept {
		RLock l(cs);
		return searchItems; 
	};

	void moveAutoSearchUp(unsigned int id) noexcept {
		WLock l(cs);
		//hack =]
		if(searchItems.size() > id) {
			swap(searchItems[id], searchItems[id-1]);
			dirty = true;
		}
	}

	void moveAutoSearchDown(unsigned int id) noexcept {
		WLock l(cs);
		//hack =]
		if(searchItems.size() > id) {
			swap(searchItems[id], searchItems[id+1]);
			dirty = true;
		}
	}

	bool setItemActive(AutoSearchPtr& as, bool active) noexcept;

	void runSearches() noexcept;

	void AutoSearchLoad();
	void AutoSearchSave() noexcept;

	void logMessage(const string& aMsg, bool error) const noexcept;

	void onBundleCreated(const BundlePtr& aBundle, const ProfileToken aSearch) noexcept;
	void onBundleError(const ProfileToken aSearch, const string& aError, const string& aDir, const HintedUser& aUser) noexcept;
private:
	mutable SharedMutex cs;

	DelayedEvents<ProfileToken> resultCollector;

	void performSearch(AutoSearchPtr& as, StringList& aHubs, SearchType aType) noexcept;
	//count minutes to be more accurate than comparing ticks every minute.
	bool checkItems() noexcept;
	AutoSearchList searchItems;

	void loadAutoSearch(SimpleXML& aXml);

	uint64_t lastSave = 0;
	bool dirty = false;
	time_t lastSearch;
	time_t recheckTime;
	uint32_t curPos = 0;

	bool endOfListReached = false;

	unordered_map<ProfileToken, SearchResultList> searchResults;
	void pickMatch(AutoSearchPtr as) noexcept;
	void handleAction(const SearchResultPtr& sr, AutoSearchPtr& as) noexcept;

	void updateStatus(AutoSearchPtr& as, bool setTabDirty) noexcept;
	void clearError(AutoSearchPtr& as) noexcept;

	/* Listeners */
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept;

	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;

	void on(SearchManagerListener::SearchTypeRenamed, const string& oldName, const string& newName) noexcept;

	void on(QueueManagerListener::BundleRemoved, const BundlePtr& aBundle) noexcept { onRemoveBundle(aBundle, false); }
	void on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept;

	//bool onBundleStatus(BundlePtr& aBundle, const ProfileTokenSet& aSearches);
	void onRemoveBundle(const BundlePtr& aBundle, bool finished) noexcept;
	void handleExpiredItems(AutoSearchList& asList) noexcept;
};
}
#endif