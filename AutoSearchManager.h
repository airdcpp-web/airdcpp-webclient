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
#include "AutosearchManagerListener.h"
#include "Util.h"
#include "SearchResult.h"

namespace dcpp {
#define AUTOSEARCH_FILE "Autosearch.xml"

class Autosearch {
public:
	typedef vector<AutosearchPtr> List;

	Autosearch() { };

	Autosearch(bool aEnabled, const string& aSearchString, int aFileType, int aAction, bool aRemove, const string& aTarget)
		noexcept : enabled(aEnabled), searchString(aSearchString), fileType(aFileType), action(aAction), remove(aRemove), target(aTarget) { };

	GETSET(bool, enabled, Enabled);
	GETSET(string, searchString, SearchString);
	GETSET(int, action, Action);
	GETSET(int, fileType, FileType);
	GETSET(bool, remove, Remove); //remove after 1 hit
	GETSET(string, target, Target); //download to Target
};

class SimpleXML;

class AutoSearchManager :  public Singleton<AutoSearchManager>, public Speaker<AutosearchManagerListener>, private TimerManagerListener, private SearchManagerListener {
public:
	AutoSearchManager();
	~AutoSearchManager();

	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept;
	
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept {
		if(dirty && ((lastSave + 20 *1000) > aTick)) { //20 second delay between saves.
			lastSave = aTick;
			dirty = false;
			AutosearchSave();
		}
	}

	Autosearch* addAutosearch(bool en, const string& ss, int ft, int act, bool remove, const string& targ) {
		Lock l(acs);
		for(Autosearch::List::iterator i = as.begin(); i != as.end(); ++i) {
				if(stricmp((*i)->getSearchString(), ss) == 0)
					return NULL; //already exists
				}
		Autosearch* ipw = new Autosearch(en, ss, ft, act, remove, targ);
		as.push_back(ipw);
		dirty = true;
		fire(AutosearchManagerListener::AddItem(), ipw);
		return ipw;
	}
	Autosearch* getAutosearch(unsigned int index, Autosearch &ipw) {
		Lock l(acs);
		if(as.size() > index)
			ipw = *as[index];

		return NULL;
	}
	Autosearch* updateAutosearch(unsigned int index, Autosearch &ipw) {
		Lock l(acs);
		*as[index] = ipw;
		dirty = true;
		return NULL;
	}
	void removeAutosearch(AutosearchPtr a) {
		Lock l(acs);
		Autosearch::List::const_iterator i = find_if(as.begin(), as.end(), [&](AutosearchPtr& c) { return c == a; });

		if(i != as.end()) {	
			fire(AutosearchManagerListener::RemoveItem(), a->getSearchString());
			as.erase(i);
			dirty = true;
		}
	}
	
	Autosearch::List& getAutosearch() { 
		Lock l(acs);
		return as; 
	};

	void moveAutosearchUp(unsigned int id) {
		Lock l(acs);
		//hack =]
		if(as.size() > id) {
			swap(as[id], as[id-1]);
			dirty = true;
		}
	}

	void moveAutosearchDown(unsigned int id) {
		Lock l(acs);
		//hack =]
		if(as.size() > id) {
			swap(as[id], as[id+1]);
			dirty = true;
		}
	}

	void setActiveItem(unsigned int index, bool active) {
		Lock l(acs);
		Autosearch::List::iterator i = as.begin() + index;
		if(i < as.end()) {
			(*i)->setEnabled(active);
			dirty = true;
		}
	}

	void AutosearchLoad();
	void AutosearchSave();

	int temp;
private:
	CriticalSection cs, acs;

	Autosearch::List vs; //valid searches
	Autosearch::List as; //all searches

	void loadAutosearch(SimpleXML& aXml);

	StringList allowedHubs;
	int curPos;

	friend class Singleton<AutoSearchManager>;

	void removeRegExpFromSearches();
	void getAllowedHubs();
	string matchDirectory(const string& aFile, const string& aStrToMatch);

	GETSET(uint16_t, time, Time);

	bool dirty;
	void addToQueue(SearchResultPtr sr, bool pausePrio = false, const string& dTarget = Util::emptyString );
	SearchResultPtr sr;
	bool endOfList;
	uint16_t recheckTime;
	string curSearch;
	set<UserPtr> users;
	uint64_t lastSave;


};
}
#endif