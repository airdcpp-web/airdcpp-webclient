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
		StringMatcher::Type aMatcherType, const string& aMatcherString, const string& aUserMatch, int aSearchInterval) noexcept;

	~AutoSearch();

	GETSET(bool, enabled, Enabled);
	GETSET(string, searchString, SearchString);
	GETSET(ActionType, action, Action);
	GETSET(SearchManager::TypeModes, fileType, FileType);
	GETSET(bool, remove, Remove); //remove after 1 hit
	GETSET(string, target, Target); //download to Target
	GETSET(TargetType, tType, TargetType);
	GETSET(string, userMatch, UserMatch); //only results from users matching...
	GETSET(uint64_t, lastSearch, LastSearch);
	GETSET(int, searchInterval, SearchInterval);

	int8_t getType() { return matcher->getType(); }
	bool match(const string& aStr) { return matcher->match(aStr); }
	const string& getPattern() const { return matcher->getPattern(); }
private:
	StringMatcher* matcher;
};

class SimpleXML;

class AutoSearchManager :  public Singleton<AutoSearchManager>, public Speaker<AutoSearchManagerListener>, private TimerManagerListener, private SearchManagerListener {
public:
	AutoSearchManager();
	~AutoSearchManager();

	bool addAutoSearch(bool en, const string& ss, SearchManager::TypeModes ft, AutoSearch::ActionType aAction, bool remove, const string& targ, AutoSearch::TargetType aTargetType, 
		StringMatcher::Type aMatcherType, const string& aMatcherString, int aSearchInterval, const string& aUserMatch = Util::emptyString);
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

	AutoSearchList searchItems;

	void loadAutoSearch(SimpleXML& aXml);
	GETSET(uint64_t, lastSave, LastSave);
	bool dirty;

	void handleAction(const SearchResultPtr sr, AutoSearchPtr as);

	bool getTarget(const SearchResultPtr sr, const AutoSearchPtr as, string& target);

	/* Listeners */
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept;

	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept;
};
}
#endif