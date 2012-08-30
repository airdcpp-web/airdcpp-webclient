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

#include "stdinc.h"
#include "DCPlusPlus.h"

#include "AutoSearchManager.h"

#include "ClientManager.h"
#include "LogManager.h"
#include "ShareManager.h"
#include "QueueManager.h"
#include "StringTokenizer.h"
#include "Pointer.h"
#include "SearchResult.h"
#include "SimpleXML.h"
#include "User.h"
#include "Wildcards.h"
#include "format.h"
#include "ScopedFunctor.h"
#include "DirectoryListingManager.h"

#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm_ext/for_each.hpp>

namespace dcpp {

using boost::range::for_each;


AutoSearch::AutoSearch(bool aEnabled, const string& aSearchString, const string& aFileType, ActionType aAction, bool aRemove, const string& aTarget, 
	TargetUtil::TargetType aTargetType, StringMatch::Method aMethod, const string& aMatcherString, const string& aUserMatch, int aSearchInterval, time_t aExpireTime,
	bool aCheckAlreadyQueued, bool aCheckAlreadyShared ) noexcept : 
	enabled(aEnabled), searchString(aSearchString), fileType(aFileType), action(aAction), remove(aRemove), target(aTarget), tType(aTargetType), 
		searchInterval(aSearchInterval), expireTime(aExpireTime), lastSearch(0), checkAlreadyQueued(aCheckAlreadyQueued), checkAlreadyShared(aCheckAlreadyShared),
		manualSearch(false) {

	setMethod(aMethod);
	pattern = aMatcherString.empty() ? aSearchString : aMatcherString;
	prepare();

	/*if (aFileType == SEARCH_TYPE_TTH)
		resultMatcher = new TTHMatcher(matchPattern);
	else if (aMatcherType == StringMatcher::MATCHER_STRING)
		resultMatcher = new TokenMatcher(matchPattern);
	else if (aMatcherType == StringMatcher::MATCHER_REGEX)
		resultMatcher = new RegExMatcher(matchPattern);
	else if (aMatcherType == StringMatcher::MATCHER_WILDCARD)
		resultMatcher = new WildcardMatcher(matchPattern);*/

	userMatcher.setMethod(StringMatch::WILDCARD);
	userMatcher.pattern = aUserMatch;
	userMatcher.prepare();
};

AutoSearch::~AutoSearch() { 
};

void AutoSearch::search(StringList& aHubs) {
	StringList extList;
	int ftype = 0;
	try {
		SearchManager::getInstance()->getSearchType(fileType, ftype, extList, true);
	} catch(const SearchTypeException&) {
		//reset to default
		fileType = SEARCH_TYPE_ANY;
		ftype = SearchManager::TYPE_ANY;
	}

	uint64_t searchTime = SearchManager::getInstance()->search(aHubs, searchString, 0, (SearchManager::TypeModes)ftype, SearchManager::SIZE_DONTCARE, "as", extList, manualSearch? Search::MANUAL : Search::AUTO_SEARCH);

	if (searchTime == 0) {
		LogManager::getInstance()->message(str(boost::format("Autosearch: %s has been searched for") %
			searchString), LogManager::LOG_INFO);
	} else {
		LogManager::getInstance()->message(str(boost::format("Autosearch: %s will be searched in %d seconds") %
			searchString %
			(searchTime / 1000)), LogManager::LOG_INFO);
	}
}

string AutoSearch::getDisplayType() {
	return SearchManager::isDefaultTypeStr(fileType) ? SearchManager::getTypeStr(fileType[0]-'0') : fileType;
}


AutoSearchManager::AutoSearchManager() : 
	lastSave(0), 
	dirty(false), 
	lastSearch(SETTING(AUTOSEARCH_EVERY)-2), //start searching after 2 minutes.
	curPos(0), 
	endOfListReached(false), 
	recheckTime(SETTING(AUTOSEARCH_RECHECK_TIME)) 
{
	TimerManager::getInstance()->addListener(this);
	SearchManager::getInstance()->addListener(this);
}

AutoSearchManager::~AutoSearchManager() {
	SearchManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
}

/* For external use */
AutoSearchPtr AutoSearchManager::addAutoSearch(const string& ss, const string& aTarget, TargetUtil::TargetType aTargetType, bool isDirectory) {
	if (ss.length() <= 5) {
		LogManager::getInstance()->message(str(boost::format(STRING(AUTO_SEARCH_ADD_FAILED)) % ss) + " " + STRING(LINE_EMPTY_OR_TOO_SHORT), LogManager::LOG_ERROR);
		return nullptr;
	}

	auto as = new AutoSearch(true, ss, isDirectory ? SEARCH_TYPE_DIRECTORY : SEARCH_TYPE_ANY, AutoSearch::ACTION_DOWNLOAD, true, aTarget, aTargetType, 
		StringMatch::PARTIAL, Util::emptyString, Util::emptyString, 0, SETTING(AUTOSEARCH_EXPIRE_DAYS) > 0 ? GET_TIME() + (SETTING(AUTOSEARCH_EXPIRE_DAYS)*24*60*60) : 0, false, false);

	as->startTime = SearchTime();
	as->endTime = SearchTime(true);
	as->searchDays = bitset<7>("1111111");

	if (addAutoSearch(as)) {
		LogManager::getInstance()->message(CSTRING(SEARCH_ADDED) + ss, LogManager::LOG_INFO);
		SearchNow(as);
		return as;
	} else {
		LogManager::getInstance()->message(str(boost::format(STRING(AUTO_SEARCH_ADD_FAILED)) % ss) + " " + STRING(AUTO_SEARCH_EXISTS), LogManager::LOG_ERROR);
		return nullptr;
	}
}

void AutoSearchManager::on(SearchManagerListener::SearchTypeRenamed, const string& oldName, const string& newName) noexcept {
	RLock l(cs);
	for(auto i = searchItems.begin(); i != searchItems.end(); ++i) {
		if ((*i)->getFileType() == oldName) {
			(*i)->setFileType(newName);
			fire(AutoSearchManagerListener::UpdateItem(), *i, distance(searchItems.begin(), i));
		}
	}
}

bool AutoSearchManager::addAutoSearch(AutoSearchPtr aAutoSearch) {
	{
		WLock l(cs);
		if (find_if(searchItems.begin(), searchItems.end(),
			[aAutoSearch](AutoSearchPtr as)  { return as->getSearchString() == aAutoSearch->getSearchString(); }) != searchItems.end()) return false;
		searchItems.push_back(aAutoSearch);
	}
	dirty = true;
	fire(AutoSearchManagerListener::AddItem(), aAutoSearch);
	return true;
}

AutoSearchPtr AutoSearchManager::getAutoSearch(unsigned int index) {
	RLock l(cs);
	if(searchItems.size() > index)
		return searchItems[index];
	return nullptr;
}

bool AutoSearchManager::updateAutoSearch(unsigned int index, AutoSearchPtr &ipw) {
	WLock l(cs);
	for(auto i = searchItems.begin(); i != searchItems.end(); ++i) {
		if ((*i)->getSearchString() == ipw->getSearchString() && distance(searchItems.begin(), i) != index)
			return false;
	}

	searchItems[index] = ipw;
	dirty = true;
	return true;
}

void AutoSearchManager::removeAutoSearch(AutoSearchPtr aItem) {
	WLock l(cs);
	auto i = find(searchItems.begin(), searchItems.end(), aItem);
	if(i != searchItems.end()) {

		if(distance(searchItems.begin(), i) < curPos) //dont skip a search if we remove before the last search.
			curPos--;

		fire(AutoSearchManagerListener::RemoveItem(), aItem->getSearchString());
		searchItems.erase(i);
		dirty = true;
	}
}

void AutoSearchManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	if(dirty && (lastSave + (20*1000) < aTick)) { //20 second delay between saves.
		lastSave = aTick;
		dirty = false;
		AutoSearchSave();
	}
}

void AutoSearchManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {

	lastSearch++;

	if(endOfListReached) {
		recheckTime++;
		if(recheckTime >= SETTING(AUTOSEARCH_RECHECK_TIME)) {
			curPos = 0;
			endOfListReached = false;
		} else {
			return;
		}
	}
	if(lastSearch >= (SETTING(AUTOSEARCH_EVERY))) {
		if(hasEnabledItems())
			checkSearches(false, aTick);
	}
}

bool AutoSearchManager::hasEnabledItems() {
	auto curTime = GET_TIME();
	AutoSearchList expired;
	bool result = false;
	{
		RLock l(cs);
		
		if(searchItems.empty()){
			curPos = 0; //list got empty, start from 0 with new items.
			return result;
		}

		for(auto i = searchItems.begin(); i != searchItems.end(); ++i) {
			AutoSearchPtr as = *i;
			
			//check expired, and remove them.
			if (as->getExpireTime() > 0 && as->getExpireTime() < curTime) {
				expired.push_back(as);
				continue;
			}
			if (!as->getEnabled())
				continue;

			result = true;
		}
	}

	for_each(expired, [&](AutoSearchPtr as) {
		LogManager::getInstance()->message("An expired autosearch has been removed: " + as->getSearchString(), LogManager::LOG_INFO); 
		removeAutoSearch(as);
	});

	if(!result) //if no enabled items, start checking from the beginning with newly enabled ones.
		curPos = 0;

	return result;
}

void AutoSearchManager::checkSearches(bool force, uint64_t aTick /* = GET_TICK() */) {
	StringList allowedHubs;
	ClientManager::getInstance()->getOnlineClients(allowedHubs);
	//no hubs? no fun...
	if(allowedHubs.empty()) {
		return;
	}
	
	auto curTime = GET_TIME();
	tm _tm;
	localtime_s(&_tm, &curTime);

	AutoSearchPtr as = nullptr;
	{
		RLock l (cs);
		
		//we have waited for search time, and we are at the end of list. wait for recheck time. so time between searches "autosearch every" + "recheck time" 
		if(curPos >= searchItems.size()) { 
			LogManager::getInstance()->message("Autosearch: End of list reached. Recheck Items, next search after " + Util::toString(SETTING(AUTOSEARCH_RECHECK_TIME)) + " minutes", LogManager::LOG_INFO);
			curPos = 0;
			endOfListReached = true;
			recheckTime = 0;
			return;
		}

		for(auto i = searchItems.begin() + curPos; i != searchItems.end(); ++i) {
			
			curPos++; //move to next one, even if we skip something, dont check the same ones again until list has gone thru.

			if (!(*i)->getEnabled())
				continue;
			//check the weekday
			if (!(*i)->searchDays[_tm.tm_wday])
				continue;
			//check the hours
			if ((*i)->startTime.hour > _tm.tm_hour || (*i)->endTime.hour < _tm.tm_hour)
				continue;
			//check the minutes
			if ((*i)->startTime.hour == _tm.tm_hour) {
				if ((*i)->startTime.minute > _tm.tm_min)
					continue;
			}
			if ((*i)->endTime.hour == _tm.tm_hour) {
				if ((*i)->endTime.minute < _tm.tm_min)
					continue;
			}

			as = *i;
			as->setLastSearch(curTime);
			lastSearch = 0;
			fire(AutoSearchManagerListener::UpdateItem(), as, distance(searchItems.begin(), i));
			break;
		}
	}
	
	if(as != nullptr) {
		as->search(allowedHubs);
	}
}

void AutoSearchManager::SearchNow(AutoSearchPtr as) {
	StringList allowedHubs;
	ClientManager::getInstance()->getOnlineClients(allowedHubs);
	//no hubs? no fun...
	if(allowedHubs.empty()) {
		return;
	}
	as->setManualSearch(true);
	as->search(allowedHubs);
}

void AutoSearchManager::on(SearchManagerListener::SR, const SearchResultPtr& sr) noexcept {
	//don't match bundle searches
	if (stricmp(sr->getToken(), "qa") == 0)
		return;

	AutoSearchList matches;

	{
		RLock l (cs);
		for(auto i = searchItems.begin(); i != searchItems.end(); ++i) {
			AutoSearchPtr as = *i;
			if (!as->getEnabled() && !as->getManualSearch())
				continue;
			
			as->setManualSearch(false);

			//match
			if (as->getFileType() == SEARCH_TYPE_TTH) {
				if (!as->match(sr->getTTH().toBase32()))
					continue;
			} else {
				/* Check the type */
				if(as->getFileType() == SEARCH_TYPE_DIRECTORY) {
					if (sr->getType() != SearchResult::TYPE_DIRECTORY)
						continue;
				} /*else if(!ShareManager::getInstance()->checkType(sr->getFile(), (*i)->getFileType())) {
					continue;
				}*/

				if (!as->match(sr->getType() == SearchResult::TYPE_DIRECTORY ? Util::getLastDir(sr->getFile()) : sr->getFileName()))
					continue;
			}

			//check the nick
			if(!as->getNickPattern().empty()) {
				StringList nicks = ClientManager::getInstance()->getNicks(sr->getUser()->getCID(), sr->getHubURL());
				if (find_if(nicks.begin(), nicks.end(), [&](const string& aNick) { return as->matchNick(aNick); }) == nicks.end())
					continue;
			}

			//we have a valid result
			matches.push_back(as);
		}
	}

	for_each(matches, [&](AutoSearchPtr as) { handleAction(sr, as); });
}

void AutoSearchManager::handleAction(const SearchResultPtr sr, AutoSearchPtr as) {
	if (as->getAction() == AutoSearch::ACTION_QUEUE || as->getAction() == AutoSearch::ACTION_DOWNLOAD) {

		if(as->getFileType() == SEARCH_TYPE_DIRECTORY) {
			string dir = Util::getLastDir(sr->getFile());
			//check shared.
			if(as->getCheckAlreadyShared()) {
				if(ShareManager::getInstance()->isDirShared(dir))
					return;
			}
			//check Queued
			if(as->getCheckAlreadyQueued()) {
				if(QueueManager::getInstance()->isDirQueued(dir))
					return;
			}
		}

		try {
			if(sr->getType() == SearchResult::TYPE_DIRECTORY) {
				DirectoryListingManager::getInstance()->addDirectoryDownload(sr->getFile(), HintedUser(sr->getUser(), sr->getHubURL()), as->getTarget(), as->getTargetType(), REPORT_SYSLOG, 
					(as->getAction() == AutoSearch::ACTION_QUEUE) ? QueueItem::PAUSED : QueueItem::DEFAULT);
			} else {
				TargetUtil::TargetInfo ti;
				bool hasSpace = TargetUtil::getVirtualTarget(as->getTarget(), as->getTargetType(), ti, sr->getSize());
				if (!hasSpace)
					TargetUtil::reportInsufficientSize(ti, sr->getSize());

				QueueManager::getInstance()->add(as->getTarget() + sr->getFileName(), sr->getSize(), sr->getTTH(), HintedUser(sr->getUser(), sr->getHubURL()), 0, true, 
					((as->getAction() == AutoSearch::ACTION_QUEUE) ? QueueItem::PAUSED : QueueItem::DEFAULT));
			}
		} catch(const Exception& /*e*/) {
			//LogManager::getInstance()->message("AutoSearch failed to queue " + sr->getFileName() + " (" + e.getError() + ")");
			return;
		}
	} else if (as->getAction() == AutoSearch::ACTION_REPORT) {
		ClientManager* c = ClientManager::getInstance();
		c->lockRead();
		ScopedFunctor([c] { c->unlockRead(); });
		OnlineUser* u = c->findOnlineUser(sr->getUser()->getCID(), sr->getHubURL(), false);

		if(u) {
			Client* client = &u->getClient();
			if(client && client->isConnected()) {
				client->Message("AutoSearch found a file: " + sr->getFile() + " from an user " + u->getIdentity().getNick());
			}
		} else {
			return;
		}
	}

	if(as->getRemove()) {
		removeAutoSearch(as);
	}
}

void AutoSearchManager::AutoSearchSave() {
	try {
		dirty = false;
		SimpleXML xml;

		xml.addTag("Autosearch");
		xml.addChildAttrib("LastPosition", curPos);
		xml.stepIn();
		xml.addTag("Autosearch");
		xml.stepIn();

		{
			RLock l(cs);
			for(auto i = searchItems.begin(); i != searchItems.end(); ++i) {
				AutoSearchPtr as = *i;
				xml.addTag("Autosearch");
				xml.addChildAttrib("Enabled", as->getEnabled());
				xml.addChildAttrib("SearchString", as->getSearchString());
				xml.addChildAttrib("FileType", as->getFileType());
				xml.addChildAttrib("Action", as->getAction());
				xml.addChildAttrib("Remove", as->getRemove());
				xml.addChildAttrib("Target", as->getTarget());
				xml.addChildAttrib("TargetType", as->getTargetType());
				xml.addChildAttrib("MatcherType", as->getMethod()),
				xml.addChildAttrib("MatcherString", as->pattern),
				xml.addChildAttrib("SearchInterval", as->getSearchInterval()),
				xml.addChildAttrib("UserMatch", (*i)->getNickPattern());
				xml.addChildAttrib("ExpireTime", (*i)->getExpireTime());
				xml.addChildAttrib("CheckAlreadyQueued", as->getCheckAlreadyQueued());
				xml.addChildAttrib("CheckAlreadyShared", as->getCheckAlreadyShared());
				xml.addChildAttrib("SearchDays", (*i)->searchDays.to_string());
				xml.addChildAttrib("StartTime", (*i)->startTime.toString());
				xml.addChildAttrib("EndTime", (*i)->endTime.toString());
				xml.addChildAttrib("LastSearchTime", Util::toString(as->getLastSearch()));
			}
		}

		xml.stepOut();
		xml.stepOut();
		
		string fname = Util::getPath(Util::PATH_USER_CONFIG) + AUTOSEARCH_FILE;

		File f(fname + ".tmp", File::WRITE, File::CREATE | File::TRUNCATE);
		f.write(SimpleXML::utf8Header);
		f.write(xml.toXML());
		f.close();
		File::deleteFile(fname);
		File::renameFile(fname + ".tmp", fname);
		
	} catch(const Exception& e) {
		dcdebug("FavoriteManager::recentsave: %s\n", e.getError().c_str());
	}
}

void AutoSearchManager::loadAutoSearch(SimpleXML& aXml) {
	aXml.resetCurrentChild();
	if(aXml.findChild("Autosearch")) {
		aXml.stepIn();
		while(aXml.findChild("Autosearch")) {
			auto as = new AutoSearch(aXml.getBoolChildAttrib("Enabled"),
				aXml.getChildAttrib("SearchString"), 
				aXml.getChildAttrib("FileType"), 
				(AutoSearch::ActionType)aXml.getIntChildAttrib("Action"),
				aXml.getBoolChildAttrib("Remove"),
				aXml.getChildAttrib("Target"),
				(TargetUtil::TargetType)aXml.getIntChildAttrib("TargetType"),
				(StringMatch::Method)aXml.getIntChildAttrib("MatcherType"),
				aXml.getChildAttrib("MatcherString"),
				aXml.getChildAttrib("UserMatch"),
				aXml.getIntChildAttrib("SearchInterval"),
				aXml.getIntChildAttrib("ExpireTime"),
				aXml.getBoolChildAttrib("CheckAlreadyQueued"),
				aXml.getBoolChildAttrib("CheckAlreadyShared"));

			as->setExpireTime(aXml.getIntChildAttrib("ExpireTime"));

			auto searchDays = aXml.getChildAttrib("SearchDays");
			if(!searchDays.empty()) {
				as->searchDays =  bitset<7>(searchDays);
			} else {
				as->searchDays = bitset<7>("1111111");
			}

			auto startTime = aXml.getChildAttrib("StartTime");
			if(!startTime.empty()) {
				as->startTime = SearchTime(startTime);
			} else {
				as->startTime = SearchTime();
			}

			auto endTime = aXml.getChildAttrib("EndTime");
			if(!endTime.empty()) {
				as->endTime = SearchTime(endTime);
			} else {
				as->endTime = SearchTime(true);
			}

			as->setLastSearch(aXml.getIntChildAttrib("LastSearchTime"));
			addAutoSearch(as);
		}
		aXml.stepOut();
	}
}

void AutoSearchManager::AutoSearchLoad() {
	try {
		SimpleXML xml;
		xml.fromXML(File(Util::getPath(Util::PATH_USER_CONFIG) + AUTOSEARCH_FILE, File::READ, File::OPEN).read());
		if(xml.findChild("Autosearch")) {
			curPos = xml.getIntChildAttrib("LastPosition");
			xml.stepIn();
			loadAutoSearch(xml);
			xml.stepOut();
		}
		if(curPos >= searchItems.size())
			curPos = 0;
	} catch(const Exception& e) {
		dcdebug("AutoSearchManager::load: %s\n", e.getError().c_str());
	}	
}
}