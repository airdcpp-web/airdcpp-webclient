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

#include "TimerManager.h"
#include "SearchManager.h"
#include "SearchManagerListener.h"


#include "Singleton.h"
#include "Util.h"
#include "SearchResult.h"

namespace dcpp {
#define AUTOSEARCH_FILE "Autosearch.xml"

class Autosearch {
public:

	typedef Autosearch* Ptr;
	typedef vector<Ptr> List;

	Autosearch() { };
	Autosearch(bool aEnabled, const string& aSearchString, int aFileType, int aAction)
		throw() : enabled(aEnabled), searchString(aSearchString), fileType(aFileType), action(aAction) { };

	GETSET(bool, enabled, Enabled);
	GETSET(string, searchString, SearchString);
	GETSET(int, action, Action);
	GETSET(int, fileType, FileType);
};

class SimpleXML;

class AutoSearchManager : public Singleton<AutoSearchManager>, private TimerManagerListener, private SearchManagerListener {
public:
	AutoSearchManager();
	~AutoSearchManager();

	void on(TimerManagerListener::Minute, uint64_t aTick) throw();
	void on(SearchManagerListener::SR, const SearchResultPtr&) throw();

	Autosearch* addAutosearch(bool en, const string& ss, int ft, int act) {
		Autosearch* ipw = new Autosearch(en, ss, ft, act);
		as.push_back(ipw);
		return ipw;
	}
	Autosearch* getAutosearch(unsigned int index, Autosearch &ipw) {
		if(as.size() > index)
			ipw = *as[index];
		return NULL;
	}
	Autosearch* updateAutosearch(unsigned int index, Autosearch &ipw) {
		*as[index] = ipw;
		return NULL;
	}
	Autosearch* removeAutosearch(unsigned int index) {
		if(as.size() > index)
			as.erase(as.begin() + index);
		return NULL;
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
		}
	}

	void moveAutosearchDown(unsigned int id) {
		Lock l(acs);
		//hack =]
		if(as.size() > id) {
			swap(as[id], as[id+1]);
		}
	}

	void setActiveItem(unsigned int index, bool active) {
		Autosearch::List::iterator i = as.begin() + index;
		if(i < as.end()) {
			(*i)->setEnabled(active);
		}
	}

	void AutosearchLoad();
	void AutosearchSave();
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

	void addToQueue(SearchResultPtr sr, bool pausePrio = false);
	SearchResultPtr sr;
	bool endOfList;
	uint16_t recheckTime;
	string curSearch;
	set<UserPtr> users;
};
}
#endif