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

	AutoSearchManager();
	~AutoSearchManager();

	bool hasNameDupe(const string& aName, bool report, const AutoSearchPtr& thisSearch = nullptr) const;
	bool addFailedBundle(const BundlePtr& aBundle);
	void addAutoSearch(AutoSearchPtr aAutoSearch, bool search);
	AutoSearchPtr addAutoSearch(const string& ss, const string& targ, TargetUtil::TargetType aTargetType, bool isDirectory, bool aRemove = true);
	AutoSearchPtr getSearchByIndex(unsigned int index) const;
	AutoSearchPtr getSearchByToken(ProfileToken aToken) const;

	string getBundleStatuses(const AutoSearchPtr& as) const;
	void clearPaths(AutoSearchPtr as);

	void getMenuInfo(const AutoSearchPtr& as, BundleList& bundleInfo, AutoSearch::FinishedPathMap& finishedPaths) const;
	bool updateAutoSearch(AutoSearchPtr& ipw);
	void removeAutoSearch(AutoSearchPtr& a);
	bool searchItem(AutoSearchPtr& as, SearchType aType);

	void changeNumber(AutoSearchPtr as, bool increase);
	
	AutoSearchList& getSearchItems() { 
		RLock l(cs);
		return searchItems; 
	};

	void moveAutoSearchUp(unsigned int id) {
		WLock l(cs);
		//hack =]
		if(searchItems.size() > id) {
			swap(searchItems[id], searchItems[id-1]);
			dirty = true;
		}
	}

	void moveAutoSearchDown(unsigned int id) {
		WLock l(cs);
		//hack =]
		if(searchItems.size() > id) {
			swap(searchItems[id], searchItems[id+1]);
			dirty = true;
		}
	}

	bool setItemActive(AutoSearchPtr& as, bool active);

	void runSearches();

	void AutoSearchLoad();
	void AutoSearchSave();

	void logMessage(const string& aMsg, bool error) const;

	void onBundleCreated(const BundlePtr& aBundle, const ProfileToken aSearch);
	void onBundleError(const ProfileToken aSearch, const string& aError, const string& aDir, const HintedUser& aUser);
private:
	mutable SharedMutex cs;

	DelayedEvents<ProfileToken> resultCollector;

	void performSearch(AutoSearchPtr& as, StringList& aHubs, SearchType aType);
	//count minutes to be more accurate than comparing ticks every minute.
	bool checkItems();
	AutoSearchList searchItems;

	void loadAutoSearch(SimpleXML& aXml);

	uint64_t lastSave = 0;
	bool dirty = false;
	time_t lastSearch;
	time_t recheckTime;
	uint32_t curPos = 0;

	bool endOfListReached = false;

	unordered_map<ProfileToken, SearchResultList> searchResults;
	void pickMatch(AutoSearchPtr as);
	void handleAction(const SearchResultPtr& sr, AutoSearchPtr& as);

	void updateStatus(AutoSearchPtr& as, bool setTabDirty);
	void clearError(AutoSearchPtr& as);

	/* Listeners */
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept;

	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;

	void on(SearchManagerListener::SearchTypeRenamed, const string& oldName, const string& newName) noexcept;

	virtual void on(QueueManagerListener::BundleRemoved, const BundlePtr& aBundle) noexcept { onRemoveBundle(aBundle, false); }
	virtual void on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept;

	//bool onBundleStatus(BundlePtr& aBundle, const ProfileTokenSet& aSearches);
	void onRemoveBundle(const BundlePtr& aBundle, bool finished);
};
}
#endif