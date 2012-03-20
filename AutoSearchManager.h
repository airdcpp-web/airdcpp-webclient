/*
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
#include "TimerManager.h"
#include "SearchManager.h"
#include "SearchManagerListener.h"
#include "Speaker.h"
#include "Singleton.h"
#include "AutoSearchManagerListener.h"
#include "Util.h"
#include "SearchResult.h"
#include "StringMatcher.h"

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

class AutoSearch  : public intrusive_ptr_base<AutoSearch> {

public:
	enum TargetType {
		TARGET_PATH,
		TARGET_FAVORITE,
		TARGET_SHARE,
	};

	enum ActionType {
		ACTION_DOWNLOAD,
		ACTION_QUEUE,
		ACTION_REPORT,
	};

	bool operator==(const AutoSearchPtr as) const {
		return stricmp(searchString, as->getSearchString()) == 0;
	}

	AutoSearch(bool aEnabled, const string& aSearchString, SearchManager::TypeModes aFileType, ActionType aAction, bool aRemove, const string& aTarget, TargetType aTargetType, 
		StringMatcher::Type aMatcherType, const string& aMatcherString, const string& aUserMatch, int aSearchInterval, time_t aExpireTime) noexcept;

	~AutoSearch();

	GETSET(bool, enabled, Enabled);
	GETSET(string, searchString, SearchString);
	GETSET(ActionType, action, Action);
	GETSET(SearchManager::TypeModes, fileType, FileType);
	GETSET(bool, remove, Remove); //remove after 1 hit
	GETSET(string, target, Target); //download to Target
	GETSET(TargetType, tType, TargetType);
	GETSET(time_t, lastSearch, LastSearch);
	GETSET(int, searchInterval, SearchInterval);
	GETSET(time_t, expireTime, ExpireTime);
	bitset<7> searchDays;

	SearchTime startTime;
	SearchTime endTime;

	int8_t getMatcherType() { return resultMatcher->getType(); }
	bool matchNick(const string& aStr) { return userMatcher->match(aStr); }
	bool match(const string& aStr) { return resultMatcher->match(aStr); }
	bool match(const TTHValue& aTTH) { return resultMatcher->match(aTTH); }
	const string& getPattern() const { return resultMatcher->getPattern(); }
	const string& getNickPattern() const { return userMatcher->getPattern(); }
private:
	StringMatcher* resultMatcher;
	StringMatcher* userMatcher;
};

class SimpleXML;

class AutoSearchManager :  public Singleton<AutoSearchManager>, public Speaker<AutoSearchManagerListener>, private TimerManagerListener, private SearchManagerListener {
public:
	AutoSearchManager();
	~AutoSearchManager();

	bool addAutoSearch(AutoSearchPtr aAutoSearch);
	AutoSearchPtr addAutoSearch(const string& ss, const string& targ, AutoSearch::TargetType aTargetType);
	AutoSearchPtr getAutoSearch(unsigned int index);
	bool updateAutoSearch(unsigned int index, AutoSearchPtr &ipw);
	void removeAutoSearch(AutoSearchPtr a);
	
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

	void checkSearches(bool force, uint64_t aTick = GET_TICK());

	void AutoSearchLoad();
	void AutoSearchSave();

private:
	mutable SharedMutex cs;

	//count minutes to be more accurate than comparing ticks every minute.
	size_t lastSearch;
	size_t recheckTime;

	unsigned int curPos;

	bool endOfListReached;

	bool hasEnabledItems();
	AutoSearchList searchItems;

	void loadAutoSearch(SimpleXML& aXml);
	GETSET(uint64_t, lastSave, LastSave);
	bool dirty;

	void handleAction(const SearchResultPtr sr, AutoSearchPtr as);

	int64_t getTarget(const string& aTarget, AutoSearch::TargetType targetType, string& newTarget);

	/* Listeners */
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept;

	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;
};
}
#endif