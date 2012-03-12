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

namespace dcpp {
#define AUTOSEARCH_FILE "AutoSearch.xml"

class AutoSearch  : public intrusive_ptr_base<AutoSearch> {
public:

	AutoSearch() { };

	enum targetType {
		TARGET_PATH,
		TARGET_FAVORITE,
		TARGET_SHARE,
	};

	AutoSearch(bool aEnabled, const string& aSearchString, int aFileType, int aAction, bool aRemove, const string& aTarget, targetType aTargetType, const string& aUserMatch)
		noexcept : enabled(aEnabled), searchString(aSearchString), fileType(aFileType), action(aAction), remove(aRemove), target(aTarget), tType(aTargetType), userMatch(aUserMatch) { };

	GETSET(bool, enabled, Enabled);
	GETSET(string, searchString, SearchString);
	GETSET(int, action, Action);
	GETSET(int, fileType, FileType);
	GETSET(bool, remove, Remove); //remove after 1 hit
	GETSET(string, target, Target); //download to Target
	GETSET(targetType, tType, TargetType);
	GETSET(string, userMatch, UserMatch); //only results from users matching...
};

class SimpleXML;

class AutoSearchManager :  public Singleton<AutoSearchManager>, public Speaker<AutoSearchManagerListener>, private TimerManagerListener, private SearchManagerListener {
public:
	AutoSearchManager();
	~AutoSearchManager();

	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept;
	
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept {
		if(dirty && ((lastSave + 20 *1000) > aTick)) { //20 second delay between saves.
			lastSave = aTick;
			dirty = false;
			AutoSearchSave();
		}
	}

	bool addAutoSearch(bool en, const string& ss, int ft, int act, bool remove, const string& targ, AutoSearch::targetType aTargetType, const string& aUserMatch = Util::emptyString);
	void getAutoSearch(unsigned int index, AutoSearchPtr &ipw);
	void updateAutoSearch(unsigned int index, AutoSearchPtr &ipw);
	void removeAutoSearch(AutoSearchPtr a);
	
	AutoSearchList& getAutoSearch() { 
		Lock l(acs);
		return as; 
	};

	void moveAutoSearchUp(unsigned int id) {
		Lock l(acs);
		//hack =]
		if(as.size() > id) {
			swap(as[id], as[id-1]);
			dirty = true;
		}
	}

	void moveAutoSearchDown(unsigned int id) {
		Lock l(acs);
		//hack =]
		if(as.size() > id) {
			swap(as[id], as[id+1]);
			dirty = true;
		}
	}

	void setActiveItem(unsigned int index, bool active) {
		Lock l(acs);
		AutoSearchList::iterator i = as.begin() + index;
		if(i < as.end()) {
			(*i)->setEnabled(active);
			dirty = true;
		}
	}

	void AutoSearchLoad();
	void AutoSearchSave();

	int temp;
private:
	CriticalSection cs, acs;

	AutoSearchList vs; //valid searches
	AutoSearchList as; //all searches

	void loadAutoSearch(SimpleXML& aXml);

	int curPos;

	friend class Singleton<AutoSearchManager>;
	bool getTarget(const SearchResultPtr sr, const AutoSearchPtr as, string& target);

	void removeRegExpFromSearches();
	bool matchDirectory(const string& aFile, const string& aStrToMatch);

	GETSET(uint16_t, time, Time);

	bool dirty;
	void addToQueue(const SearchResultPtr sr, const AutoSearchPtr as);
	bool reportMainchat(const SearchResultPtr sr);

	SearchResultPtr sr;
	bool endOfList;
	uint16_t recheckTime;
	string curSearch;
	set<UserPtr> users;
	uint64_t lastSave;

};
}
#endif