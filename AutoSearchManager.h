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

#include <bitset>

#include "forward.h"

#include "DelayedEvents.h"
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

	enum Status {
		STATUS_DISABLED,
		STATUS_MANUAL,
		STATUS_SEARCHING,
		STATUS_COLLECTING,
		STATUS_WAITING,
		STATUS_POSTSEARCH,
		STATUS_QUEUED_OK,
		STATUS_FAILED_MISSING,
		STATUS_FAILED_EXTRAS
	};

	AutoSearch(bool aEnabled, const string& aSearchString, const string& aFileType, ActionType aAction, bool aRemove, const string& aTarget, TargetUtil::TargetType aTargetType, 
		StringMatch::Method aMatcherType, const string& aMatcherString, const string& aUserMatch, time_t aExpireTime, bool aCheckAlreadyQueued, 
		bool aCheckAlreadyShared, bool matchFullPath, const string& aExcluded, ProfileToken aToken = 0) noexcept;

	AutoSearch() noexcept;
	~AutoSearch();
	typedef map<BundlePtr, Status> BundleStatusMap;
	typedef map<string, time_t> FinishedPathMap;

	GETSET(bool, enabled, Enabled);
	GETSET(string, searchString, SearchString);
	GETSET(string, excludedString, ExcludedString);
	GETSET(string, matcherString, MatcherString);
	GETSET(ActionType, action, Action);
	GETSET(string, fileType, FileType);
	GETSET(bool, remove, Remove); //remove after 1 hit
	GETSET(TargetUtil::TargetType, tType, TargetType);
	GETSET(time_t, lastSearch, LastSearch);
	GETSET(time_t, expireTime, ExpireTime);
	bitset<7> searchDays;
	GETSET(bool, checkAlreadyQueued, CheckAlreadyQueued);
	GETSET(bool, checkAlreadyShared, CheckAlreadyShared);
	GETSET(bool, manualSearch, ManualSearch);
	GETSET(bool, matchFullPath, MatchFullPath);
	GETSET(ProfileToken, token, Token);
	GETSET(BundleStatusMap, bundles, Bundles);
	GETSET(FinishedPathMap, finishedPaths, FinishedPaths);
	GETSET(Status, status, Status);

	GETSET(int, curNumber, CurNumber);
	GETSET(int, maxNumber, MaxNumber);
	GETSET(int, numberLen, NumberLen);
	GETSET(bool, useParams, UseParams);
	GETSET(time_t, lastIncFinish, LastIncFinish);

	time_t nextAllowedSearch();
	SearchTime startTime;
	SearchTime endTime;

	bool matchNick(const string& aStr) { return userMatcher.match(aStr); }
	const string& getNickPattern() const { return userMatcher.pattern; }
	string getDisplayName();

	string getDisplayType() const;
	string getSearchingStatus() const;
	string getExpiration() const;

	bool allowNewItems() const;
	void updatePattern();
	void changeNumber(bool increase);
	bool updateSearchTime();
	void updateStatus();

	void removeBundle(BundlePtr& aBundle);
	//returns true if the status has changed
	bool setBundleStatus(BundlePtr& aBundle, Status aStatus);
	void addPath(const string& aPath, time_t aFinishTime);
	void clearPaths() { finishedPaths.clear(); }
	bool usingIncrementation() const;
	static string formatParams(const AutoSearchPtr& as, const string& aString);
	void setUserMatcher(const string& aPattern) { userMatcher.pattern = aPattern; }
	void prepareUserMatcher() { userMatcher.prepare(); }
	string getTarget() { return target; }
	void setTarget(const string& aTarget);
	bool removePostSearch();
	bool isExcluded(const string& aString);
	void updateExcluded();
private:
	StringMatch userMatcher;
	time_t nextSearchChange;
	bool nextIsDisable;
	string target;
	StringSearch::List excluded;
};

class SimpleXML;

class AutoSearchManager :  public Singleton<AutoSearchManager>, public Speaker<AutoSearchManagerListener>, private TimerManagerListener, private SearchManagerListener {
public:
	enum SearchType {
		TYPE_MANUAL,
		TYPE_NEW,
		TYPE_NORMAL
	};

	AutoSearchManager();
	~AutoSearchManager();

	bool addFailedBundle(BundlePtr& aBundle, ProfileToken aToken);
	bool addAutoSearch(AutoSearchPtr aAutoSearch, bool search);
	AutoSearchPtr addAutoSearch(const string& ss, const string& targ, TargetUtil::TargetType aTargetType, bool isDirectory, bool aRemove = true);
	AutoSearchPtr getSearchByIndex(unsigned int index) const;
	AutoSearchPtr getSearchByToken(ProfileToken aToken) const;

	string getBundleStatuses(const AutoSearchPtr& as) const;
	void clearPaths(AutoSearchPtr as);

	void getMenuInfo(const AutoSearchPtr& as, AutoSearch::BundleStatusMap& bundleInfo, AutoSearch::FinishedPathMap& finishedPaths) const;
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

	void setActiveItem(unsigned int index, bool active);

	void runSearches();

	void AutoSearchLoad();
	void AutoSearchSave();

	void logMessage(const string& aMsg, bool error);
private:
	mutable SharedMutex cs;

	DelayedEvents<ProfileToken> resultCollector;

	void performSearch(AutoSearchPtr& as, StringList& aHubs, SearchType aType);
	//count minutes to be more accurate than comparing ticks every minute.
	unsigned int lastSearch;
	unsigned int recheckTime;
	unsigned int curPos;

	bool endOfListReached;
	bool checkItems();
	AutoSearchList searchItems;

	void loadAutoSearch(SimpleXML& aXml);
	GETSET(uint64_t, lastSave, LastSave);
	bool dirty;

	unordered_map<ProfileToken, SearchResultList> searchResults;
	void pickMatch(AutoSearchPtr as);
	void handleAction(const SearchResultPtr& sr, AutoSearchPtr& as);

	/* Listeners */
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept;

	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;

	void on(SearchManagerListener::SearchTypeRenamed, const string& oldName, const string& newName) noexcept;

	bool onBundleStatus(BundlePtr& aBundle, const ProfileTokenSet& aSearches, AutoSearch::Status aStatus);
	void onRemoveBundle(BundlePtr& aBundle, const ProfileTokenSet& aSearches, bool finished);
};
}
#endif