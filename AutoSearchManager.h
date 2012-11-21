/*
 * Copyright (C) 2011-2012 AirDC++ Project
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

#include <bitset>

#include "forward.h"

#include "AutoSearchManagerListener.h"
#include "GetSet.h"
#include "Pointer.h"
#include "SearchManagerListener.h"
#include "Singleton.h"
#include "Speaker.h"
#include "StringMatch.h"
#include "TargetUtil.h"
#include "TimerManager.h"
#include "Util.h"

namespace dcpp {
#define AUTOSEARCH_FILE "AutoSearch.xml"

struct SearchTime {
	uint16_t hour;
	uint16_t minute;
		
	explicit SearchTime(bool end=false) { 
		minute = end ? 59 : 0;
		hour = end ? 23 : 0;
	}
	explicit SearchTime(uint16_t aHours, uint16_t aMinutes) : hour(aHours), minute(aMinutes) { }
	explicit SearchTime(const string& aTime) {
		/*auto s = aTime.find(",");
		if (s != aTime.end()) {
			hours = 
		}*/
		if (aTime.length() != 4) {
			hour = 0;
			minute = 0;
			return;
		}

		hour = Util::toUInt(aTime.substr(0, 2));
		minute = Util::toUInt(aTime.substr(2, 2));
	}

	string toString() {
		string hourStr = Util::toString(hour);
		if (hourStr.length() == 1)
			hourStr = "0" + hourStr;
		string minuteStr = Util::toString(minute);
		if (minuteStr.length() == 1)
			minuteStr = "0" + minuteStr;
		return hourStr+minuteStr;
	}
};

class AutoSearch  : public intrusive_ptr_base<AutoSearch>, public StringMatch {

public:
	enum ActionType {
		ACTION_DOWNLOAD,
		ACTION_QUEUE,
		ACTION_REPORT
	};

	enum StatusType {
		STATUS_SEARCHING,
		STATUS_LIST,
		STATUS_QUEUED_OK,
		STATUS_FAILED_MISSING,
		STATUS_FAILED_EXTRAS
	};

	AutoSearch(bool aEnabled, const string& aSearchString, const string& aFileType, ActionType aAction, bool aRemove, const string& aTarget, TargetUtil::TargetType aTargetType, 
		StringMatch::Method aMatcherType, const string& aMatcherString, const string& aUserMatch, int aSearchInterval, time_t aExpireTime, bool aCheckAlreadyQueued, 
		bool aCheckAlreadyShared, bool matchFullPath, ProfileToken aToken = 0) noexcept;

	~AutoSearch();

	GETSET(bool, enabled, Enabled);
	GETSET(string, searchString, SearchString);
	GETSET(ActionType, action, Action);
	GETSET(string, fileType, FileType);
	GETSET(bool, remove, Remove); //remove after 1 hit
	GETSET(string, target, Target); //download to Target
	GETSET(TargetUtil::TargetType, tType, TargetType);
	GETSET(time_t, lastSearch, LastSearch);
	GETSET(int, searchInterval, SearchInterval);
	GETSET(time_t, expireTime, ExpireTime);
	bitset<7> searchDays;
	GETSET(bool, checkAlreadyQueued, CheckAlreadyQueued);
	GETSET(bool, checkAlreadyShared, CheckAlreadyShared);
	GETSET(bool, manualSearch, ManualSearch);
	GETSET(bool, matchFullPath, MatchFullPath);
	GETSET(ProfileToken, token, Token);
	GETSET(OrderedStringSet, bundleTokens, BundleTokens);
	GETSET(OrderedStringSet, finishedPaths, FinishedPaths);
	GETSET(StatusType, status, Status);

	GETSET(int, curNumber, CurNumber);
	GETSET(int, maxNumber, MaxNumber);
	GETSET(int, numberLen, NumberLen);
	GETSET(bool, useParams, UseParams);

	SearchTime startTime;
	SearchTime endTime;

	bool matchNick(const string& aStr) { return userMatcher.match(aStr); }
	const string& getNickPattern() const { return userMatcher.pattern; }

	string getDisplayType();
	bool allowNewItems();
	void updatePattern();
	void increaseNumber();

	void addBundle(const string& aToken);
	void removeBundle(const string& aToken);
	void addPath(const string& aPath);
	void clearPaths() { finishedPaths.clear(); }
private:
	StringMatch userMatcher;
};

class SimpleXML;

class AutoSearchManager :  public Singleton<AutoSearchManager>, public Speaker<AutoSearchManagerListener>, private TimerManagerListener, private SearchManagerListener {
public:
	AutoSearchManager();
	~AutoSearchManager();

	bool addAutoSearch(AutoSearchPtr aAutoSearch);
	AutoSearchPtr addAutoSearch(const string& ss, const string& targ, TargetUtil::TargetType aTargetType, bool isDirectory, bool aRemove = true);
	AutoSearchPtr getSearchByIndex(unsigned int index) const;
	AutoSearchPtr getSearchByToken(ProfileToken aToken) const;

	void setItemStatus(AutoSearchPtr as, AutoSearch::StatusType aStatus);
	string getStatus(AutoSearchPtr as);
	void clearPaths(AutoSearchPtr as);

	void getMenuInfo(AutoSearchPtr as, BundleList& bundleInfo, OrderedStringSet& finishedPaths) const;
	bool updateAutoSearch(unsigned int index, AutoSearchPtr &ipw);
	void removeAutoSearch(AutoSearchPtr a);
	void searchItem(AutoSearchPtr as, bool manual = false);
	
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

	void setActiveItem(unsigned int index, bool active) {
		RLock l(cs);
		auto i = searchItems.begin() + index;
		if(i < searchItems.end()) {
			(*i)->setEnabled(active);
			dirty = true;
		}
	}

	void checkSearches();

	void AutoSearchLoad();
	void AutoSearchSave();

	void logMessage(const string& aMsg, bool error);
private:
	mutable SharedMutex cs;

	uint64_t searchItem(AutoSearchPtr as, StringList& aHubs, bool report, bool manual);
	//count minutes to be more accurate than comparing ticks every minute.
	unsigned int lastSearch;
	unsigned int recheckTime;
	unsigned int curPos;

	bool endOfListReached;
	bool hasEnabledItems();
	AutoSearchList searchItems;

	void loadAutoSearch(SimpleXML& aXml);
	GETSET(uint64_t, lastSave, LastSave);
	bool dirty;

	void handleAction(const SearchResultPtr sr, AutoSearchPtr as);

	/* Listeners */
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept;

	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;

	void on(SearchManagerListener::SearchTypeRenamed, const string& oldName, const string& newName) noexcept;

	void onAddBundle(const BundlePtr aBundle);
	void onRemoveBundle(const BundlePtr aBundle, bool finished);
	void onBundleScanFailed(const BundlePtr aBundle, bool noMissing, bool noExtras);
};
}
#endif