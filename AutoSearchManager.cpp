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
#include "ScopedFunctor.h"
#include "DirectoryListingManager.h"

#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm_ext/for_each.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/algorithm/max_element.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace dcpp {

using boost::range::copy;
using boost::adaptors::map_values;
using boost::adaptors::map_keys;
using boost::range::for_each;
using boost::range::find_if;
using boost::range::max_element;
using namespace boost::posix_time;
using namespace boost::gregorian;


AutoSearch::AutoSearch(bool aEnabled, const string& aSearchString, const string& aFileType, ActionType aAction, bool aRemove, const string& aTarget, 
	TargetUtil::TargetType aTargetType, StringMatch::Method aMethod, const string& aMatcherString, const string& aUserMatch, time_t aExpireTime,
	bool aCheckAlreadyQueued, bool aCheckAlreadyShared, bool aMatchFullPath, ProfileToken aToken /*rand*/) noexcept : 
	enabled(aEnabled), searchString(aSearchString), fileType(aFileType), action(aAction), remove(aRemove), tType(aTargetType), 
		expireTime(aExpireTime), lastSearch(0), checkAlreadyQueued(aCheckAlreadyQueued), checkAlreadyShared(aCheckAlreadyShared),
		manualSearch(false), token(aToken), matchFullPath(aMatchFullPath), useParams(false), curNumber(1), maxNumber(0), numberLen(2), matcherString(aMatcherString),
		nextSearchChange(0), nextIsDisable(false), status(STATUS_SEARCHING), noDelay(false) {

	if (token == 0)
		token = Util::randInt(10);

	setTarget(aTarget);
	setMethod(aMethod);
	userMatcher.setMethod(StringMatch::WILDCARD);
	userMatcher.pattern = aUserMatch;
	userMatcher.prepare();

	startTime = SearchTime();
	endTime = SearchTime(true);
	searchDays = bitset<7>("1111111");
};

AutoSearch::~AutoSearch() { };

bool AutoSearch::allowNewItems() const {
	if (!enabled)
		return false;

	if (status < STATUS_QUEUED_OK)
		return true;

	if (status == STATUS_FAILED_MISSING)
		return true;

	return !remove && !usingIncrementation();
}

void AutoSearch::changeNumber(bool increase) {
	if (usingIncrementation()) {
		noDelay = true;
		increase ? curNumber++ : curNumber--;
		updatePattern();
	}
}

string AutoSearch::formatParams(const AutoSearchPtr as, const string& aString) {
	ParamMap params;
	if (as->usingIncrementation()) {
		auto num = Util::toString(as->getCurNumber());
		if (num.length() < as->getNumberLen()) {
			//prepend the zeroes
			num.insert(num.begin(), as->getNumberLen() - num.length(), '0');
		}
		params["inc"] = num;
	}

	return Util::formatParams(aString, params);
}

string AutoSearch::getDisplayName() {
	if (!useParams)
		return searchString;

	auto formated = formatParams(this, searchString);
	return formated + " (" + searchString + ")";
}

void AutoSearch::setTarget(const string& aTarget) {
	target = Util::validateFileName(aTarget);
	if(tType == TargetUtil::TARGET_PATH && !target.empty() && target[target.size() - 1] != PATH_SEPARATOR) {
		target += PATH_SEPARATOR;
	}
}

void AutoSearch::updatePattern() {
	if (matcherString.empty())
		matcherString = searchString;

	if (useParams) {
		pattern = formatParams(this, matcherString);
	} else {
		pattern = matcherString;
	}
	prepare();
}

string AutoSearch::getDisplayType() const {
	return SearchManager::isDefaultTypeStr(fileType) ? SearchManager::getTypeStr(fileType[0]-'0') : fileType;
}

void AutoSearch::setBundleStatus(BundlePtr aBundle, Status aStatus) {
	bundles[aBundle] = aStatus;
	updateStatus();
}

void AutoSearch::removeBundle(BundlePtr aBundle) { 
	bundles.erase(aBundle);
}

void AutoSearch::addPath(const string& aPath, time_t aFinishTime) { 
	finishedPaths[aPath] = aFinishTime;
}

bool AutoSearch::usingIncrementation() const {
	return useParams && searchString.find("%[inc]") != string::npos;
}

string AutoSearch::getSearchingStatus() const {
	if (status == STATUS_DISABLED) {
		return STRING(DISABLED);
	} else if (status == STATUS_MANUAL) {
		return STRING(MATCHING_MANUAL);
	} else if (status == STATUS_COLLECTING) {
		return STRING(COLLECTING_RESULTS);
	} else if (status == STATUS_POSTSEARCH) {
		return STRING(POST_SEARCHING);
	} else if (status == STATUS_WAITING) {
		auto time = GET_TIME();
		if (nextSearchChange > time) {
			auto timeStr = Util::formatTime(nextSearchChange - time, true, true);
			return nextIsDisable ? STRING_F(ACTIVE_FOR, timeStr) : STRING_F(WAITING_LEFT, timeStr);
		}
	} else if (remove || usingIncrementation()) {
		if (status == STATUS_QUEUED_OK) {
			return STRING(INACTIVE_QUEUED);
		} else if (status == STATUS_FAILED_MISSING) {
			return STRING_F(BUNDLE_X_FILES_MISSING, STRING(ACTIVE));
		} else if (status == STATUS_FAILED_MISSING) {
			return STRING(INACTIVE_QUEUED);
		}
	}

	return STRING(ACTIVE);
}

string AutoSearch::getExpiration() const {
	if (expireTime == 0)
		return STRING(NEVER);

	auto curTime = GET_TIME();
	if (expireTime <= curTime) {
		return STRING(EXPIRED);
	} else {
		return Util::formatTime(expireTime - curTime, true, true);
	}
}

void AutoSearch::updateStatus() {
	if (!enabled) {
		status = manualSearch ? AutoSearch::STATUS_MANUAL : AutoSearch::STATUS_DISABLED;
		return;
	}

	if (nextAllowedSearch() > GET_TIME()) {
		status = AutoSearch::STATUS_WAITING;
		return;
	}

	if (bundles.empty()) {
		if (!noDelay && usingIncrementation() && !finishedPaths.empty()) {
			if (*max_element(finishedPaths | map_values) + SETTING(AS_DELAY_HOURS)+60*60 > GET_TIME()) {
				status = AutoSearch::STATUS_POSTSEARCH;
				return;
			}
		}
		status = AutoSearch::STATUS_SEARCHING;
		return;
	}

	status = *boost::max_element(bundles | map_values);
}

bool AutoSearch::removePostSearch() {
	if (!noDelay && usingIncrementation() && !finishedPaths.empty() && SETTING(AS_DELAY_HOURS) > 0) {
		auto max = *max_element(finishedPaths | map_values);
		if (max > 0 && max + SETTING(AS_DELAY_HOURS)+60*60 <= GET_TIME()) {
			updateStatus();
			return true;
		}
	}

	return false;
}


time_t AutoSearch::nextAllowedSearch() {
	if (nextSearchChange == 0 || nextIsDisable)
		return 0;

	return nextSearchChange;
}

bool AutoSearch::updateSearchTime() {
	if (!searchDays.all() || startTime.hour != 0 || endTime.minute != 59 || endTime.hour != 23 || startTime.minute != 0) {
		//get the current time from the clock -- one second resolution
		ptime now = second_clock::local_time();
		ptime nextSearch(now);

		//have we already passed the end time from this day?
		if (endTime.hour < nextSearch.time_of_day().hours() || 
			(endTime.hour == nextSearch.time_of_day().hours() && endTime.minute < nextSearch.time_of_day().minutes())) {

			nextSearch = ptime(nextSearch.date() + days(1));
		}

		auto addTime = [this, &nextSearch] (bool toEnabled) -> void {
			//check the next weekday when the searching can be done (or when it's being disabled)
			if (searchDays[nextSearch.date().day_of_week()] != toEnabled) {
				//get the next day when we can search for it
				int p = nextSearch.date().day_of_week();
				if (!toEnabled)
					p++; //start from the next day as we know already that we are able to search today

				int d = 0;

				for (d = 0; d < 6; d++) {
					if(p == 7)
						p = 0;

					if (searchDays[p++] == toEnabled)
						break;
				}

				nextSearch = ptime(nextSearch.date() + days(d)); // start from the midnight
			}

			//add the start (or end) hours and minutes (if needed)
			auto& timeStruct = toEnabled ? startTime : endTime;
			if (timeStruct.hour > nextSearch.time_of_day().hours()) {
				nextSearch += (hours(timeStruct.hour) + minutes(timeStruct.minute)) - (hours(nextSearch.time_of_day().hours()) + minutes(nextSearch.time_of_day().minutes()));
			} else if ((timeStruct.hour == nextSearch.time_of_day().hours() && timeStruct.minute > nextSearch.time_of_day().minutes())) {
				nextSearch += minutes(timeStruct.minute - nextSearch.time_of_day().minutes());
			}
		};

		addTime(true);

		if (nextSearch == now) {
			//we are allowed to search already, check when it's going to be disabled
			addTime(false);
			nextIsDisable = true;
		} else {
			nextIsDisable = false;
		}

		tm td_tm = to_tm(nextSearch);
		time_t next = mktime(&td_tm);
		if (next != nextSearchChange) {
			updateStatus();
			nextSearchChange = next;
		}

		return true;
	}

	nextSearchChange = 0;
	return false;
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

void AutoSearchManager::logMessage(const string& aMsg, bool error) {
	LogManager::getInstance()->message(STRING(AUTO_SEARCH_SMALL) + ": " +  aMsg, error ? LogManager::LOG_ERROR : LogManager::LOG_INFO);
}

/* Adding new items for external use */
AutoSearchPtr AutoSearchManager::addAutoSearch(const string& ss, const string& aTarget, TargetUtil::TargetType aTargetType, bool isDirectory, bool aRemove/*true*/) {
	if (ss.length() <= 5) {
		logMessage(STRING_F(AUTOSEARCH_ADD_FAILED, ss % STRING(LINE_EMPTY_OR_TOO_SHORT)), true);
		return nullptr;
	}

	auto as = new AutoSearch(true, ss, isDirectory ? SEARCH_TYPE_DIRECTORY : SEARCH_TYPE_ANY, AutoSearch::ACTION_DOWNLOAD, aRemove, aTarget, aTargetType, 
		StringMatch::EXACT, Util::emptyString, Util::emptyString, SETTING(AUTOSEARCH_EXPIRE_DAYS) > 0 ? GET_TIME() + (SETTING(AUTOSEARCH_EXPIRE_DAYS)*24*60*60) : 0, false, false, false);

	return addAutoSearch(as, true) ? as : nullptr;
}


/* List changes */
bool AutoSearchManager::addAutoSearch(AutoSearchPtr aAutoSearch, bool search) {
	aAutoSearch->updatePattern();
	aAutoSearch->updateSearchTime();
	aAutoSearch->updateStatus();

	{
		WLock l(cs);
		if (find_if(searchItems,
			[aAutoSearch](AutoSearchPtr as)  { return as->getSearchString() == aAutoSearch->getSearchString(); }) != searchItems.end()) { 
				logMessage(STRING_F(AUTOSEARCH_ADD_FAILED, aAutoSearch->getSearchString() % STRING(ITEM_NAME_EXISTS)), true);
				return false;
		};
		searchItems.push_back(aAutoSearch);
	}

	dirty = true;
	fire(AutoSearchManagerListener::AddItem(), aAutoSearch);
	if (search && !searchItem(aAutoSearch, TYPE_NEW)) {
		//no hubs
		logMessage(CSTRING_F(AUTOSEARCH_ADDED, aAutoSearch->getSearchString()), false);
	}

	return true;
}

void AutoSearchManager::setActiveItem(unsigned int index, bool active) {
	RLock l(cs);
	auto i = searchItems.begin() + index;
	if(i < searchItems.end()) {
		(*i)->setEnabled(active);
		(*i)->updateStatus();
		fire(AutoSearchManagerListener::UpdateItem(), *i, true);
		dirty = true;
	}
}

bool AutoSearchManager::updateAutoSearch(AutoSearchPtr ipw) {
	WLock l(cs);
	ipw->prepareUserMatcher();
	ipw->updatePattern();
	ipw->updateSearchTime();
	ipw->updateStatus();

	//if (find_if(searchItems, [ipw](const AutoSearchPtr as) { return as->getSearchString() == ipw->getSearchString() && compare(ipw->getToken(), as->getToken()) != 0; }) != searchItems.end())
	//	return false;

	dirty = true;
	return true;
}

void AutoSearchManager::changeNumber(AutoSearchPtr as, bool increase) {
	WLock l(cs);
	as->changeNumber(increase);
	as->updateStatus();
	fire(AutoSearchManagerListener::UpdateItem(), as, true);
}

void AutoSearchManager::removeAutoSearch(AutoSearchPtr aItem) {
	WLock l(cs);
	auto i = find(searchItems.begin(), searchItems.end(), aItem);
	if(i != searchItems.end()) {

		if(distance(searchItems.begin(), i) < curPos) //dont skip a search if we remove before the last search.
			curPos--;

		fire(AutoSearchManagerListener::RemoveItem(), aItem);
		searchItems.erase(i);
		dirty = true;
	}
}


/* Item lookup */
AutoSearchPtr AutoSearchManager::getSearchByIndex(unsigned int index) const {
	RLock l(cs);
	if(searchItems.size() > index)
		return searchItems[index];
	return nullptr;
}

AutoSearchPtr AutoSearchManager::getSearchByToken(ProfileToken aToken) const {
	RLock l(cs);
	auto p = find_if(searchItems, [&aToken](const AutoSearchPtr as)  { return compare(as->getToken(), aToken) == 0; });
	return p != searchItems.end() ? *p : nullptr;
}


/* GUI things */
void AutoSearchManager::getMenuInfo(const AutoSearchPtr as, AutoSearch::BundleStatusMap& bundleInfo, AutoSearch::FinishedPathMap& finishedPaths) const {
	{
		RLock l(cs);
		finishedPaths = as->getFinishedPaths();
		bundleInfo = as->getBundles();
	}
}

void AutoSearchManager::clearPaths(AutoSearchPtr as) {
	{
		WLock l (cs);
		as->clearPaths();
	}

	fire(AutoSearchManagerListener::UpdateItem(), as, true);
	dirty = true;
}

string AutoSearchManager::getBundleStatuses(const AutoSearchPtr as) const {
	string statusString;
	{
		RLock l (cs);
		int bundleCount = as->getBundles().size();
		int finishedCount = as->getFinishedPaths().size();

		if (bundleCount == 0 && finishedCount == 0) {
			return STRING(NONE);
		} 

		if (bundleCount > 0) {
			if (bundleCount == 1) {
				auto bsp = *as->getBundles().begin();
				if (bsp.second == AutoSearch::STATUS_QUEUED_OK) {
					statusString += STRING_F(BUNDLE_X_QUEUED, bsp.first->getName());
				} else if (bsp.second == AutoSearch::STATUS_FAILED_MISSING) {
					statusString += STRING_F(BUNDLE_X_FILES_MISSING, bsp.first->getName());
				} else if (bsp.second == AutoSearch::STATUS_FAILED_EXTRAS) {
					statusString += STRING_F(BUNDLE_X_EXTRA_FILES, bsp.first->getName());
				}
			} else {
				statusString += STRING_F(X_BUNDLES_QUEUED, bundleCount);
			}
		}

		if (finishedCount > 0) {
			if (bundleCount > 0)
				statusString += ", ";
			statusString += STRING_F(X_FINISHED_BUNDLES, finishedCount);
		}
	}
	return statusString;
}


/* Bundle updates */
bool AutoSearchManager::onBundleStatus(BundlePtr aBundle, const ProfileTokenSet& aSearches, AutoSearch::Status aStatus) {
	bool found = false;
	for(auto i = aSearches.begin(); i != aSearches.end(); ++i) {
		auto as = getSearchByToken(*i);
		if (as) {
			found = true;
			{
				WLock l (cs);
				as->setBundleStatus(aBundle, aStatus);
			}

			fire(AutoSearchManagerListener::UpdateItem(), as, true);
		}
	}

	return found;
}

void AutoSearchManager::onRemoveBundle(BundlePtr aBundle, const ProfileTokenSet& aSearches, bool finished) {
	for(auto i = aSearches.begin(); i != aSearches.end(); ++i) {
		auto as = getSearchByToken(*i);
		if (as) {
			bool removeAs = (as->getRemove() || (as->getUseParams() && as->getCurNumber() >= as->getMaxNumber() && as->getMaxNumber() > 0)) && finished;
			{
				WLock l (cs);
				as->removeBundle(aBundle);
				if (!as->getBundles().empty())
					removeAs = false;

				if (!removeAs) {
					dirty = true;
					if (finished) {
						as->addPath(aBundle->getTarget(), GET_TIME());
						if (SETTING(AS_DELAY_HOURS) > 0) {
							as->setNoDelay(false);
							as->setStatus(AutoSearch::STATUS_POSTSEARCH);
						} else {
							as->changeNumber(true);
							as->updateStatus();
						}
					}
				}
			}

			if (removeAs) {
				removeAutoSearch(as);
			} else {
				fire(AutoSearchManagerListener::UpdateItem(), as, true);
			}
		}
	}
}

bool AutoSearchManager::addFailedBundle(BundlePtr aBundle, ProfileToken aToken) {
	auto as = new AutoSearch(true, aBundle->getName(), SEARCH_TYPE_DIRECTORY, AutoSearch::ACTION_DOWNLOAD, true, Util::getParentDir(aBundle->getTarget()), TargetUtil::TARGET_PATH, 
		StringMatch::EXACT, Util::emptyString, Util::emptyString, SETTING(AUTOSEARCH_EXPIRE_DAYS) > 0 ? GET_TIME() + (SETTING(AUTOSEARCH_EXPIRE_DAYS)*24*60*60) : 0, false, false, false, aToken);
	as->setBundleStatus(aBundle, AutoSearch::STATUS_FAILED_MISSING);
	return addAutoSearch(as, true);
}

/* Item searching */
void AutoSearchManager::performSearch(AutoSearchPtr as, StringList& aHubs, SearchType aType) {

	//Get the search type
	StringList extList;
	int ftype = 0;
	try {
		SearchManager::getInstance()->getSearchType(as->getFileType(), ftype, extList, true);
	} catch(const SearchTypeException&) {
		//reset to default
		as->setFileType(SEARCH_TYPE_ANY);
		ftype = SearchManager::TYPE_ANY;
	}


	//Update the item
	{
		WLock l(cs);
		as->updatePattern();
	}
	as->setLastSearch(GET_TIME());
	if (aType == TYPE_MANUAL && !as->getEnabled()) {
		as->setManualSearch(true);
		as->setStatus(AutoSearch::STATUS_MANUAL);
	}
	fire(AutoSearchManagerListener::UpdateItem(), as, false);


	//Run the search
	string searchWord = as->getUseParams() ? AutoSearch::formatParams(as, as->getSearchString()) : as->getSearchString();
	uint64_t searchTime = SearchManager::getInstance()->search(aHubs, searchWord, 0, (SearchManager::TypeModes)ftype, SearchManager::SIZE_DONTCARE, 
		"as", extList, StringList(), aType == TYPE_MANUAL ? Search::MANUAL : Search::AUTO_SEARCH);


	//Report
	if (searchTime == 0) {
		logMessage(aType == TYPE_NEW ? CSTRING_F(AUTOSEARCH_ADDED_SEARCHED, searchWord) : STRING_F(ITEM_SEARCHED, searchWord), false);
	} else {
		auto time = searchTime / 1000;
		logMessage(aType == TYPE_NEW ? CSTRING_F(AUTOSEARCH_ADDED_SEARCHED_IN, searchWord % time) : STRING_F(ITEM_SEARCHED_IN, searchWord % time), false);
	}
}


bool AutoSearchManager::searchItem(AutoSearchPtr as, SearchType aType) {
	StringList allowedHubs;
	ClientManager::getInstance()->getOnlineClients(allowedHubs);
	//no hubs? no fun...
	if(allowedHubs.empty()) {
		return false;
	}

	performSearch(as, allowedHubs, aType);
	return true;
}


/* Timermanager */
void AutoSearchManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	if(dirty && (lastSave + (20*1000) < aTick)) { //20 second delay between saves.
		lastSave = aTick;
		dirty = false;
		AutoSearchSave();
	}
}

void AutoSearchManager::on(TimerManagerListener::Minute, uint64_t /*aTick*/) noexcept {

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
		runSearches();
	}
}

/* Scheduled searching */
bool AutoSearchManager::checkItems() {
	AutoSearchList expired;
	AutoSearchList il;
	bool result = false;
	auto curTime = GET_TIME();

	{
		RLock l(cs);
		
		if(searchItems.empty()){
			curPos = 0; //list got empty, start from 0 with new items.
			return false;
		}

		for(auto i = searchItems.begin(); i != searchItems.end(); ++i) {
			bool search = true;
			if (!(*i)->allowNewItems())
				search = false;
			
			//check expired, and remove them.
			if ((*i)->getExpireTime() > 0 && (*i)->getExpireTime() <= curTime && (*i)->getBundles().empty()) {
				expired.push_back(*i);
				search = false;
			}

			if ((*i)->removePostSearch())
				il.push_back(*i);

			if ((*i)->updateSearchTime() || (*i)->getExpireTime() > 0)
				fire(AutoSearchManagerListener::UpdateItem(), *i, false);


			if (search && (*i)->nextAllowedSearch() <= curTime)
				result = true;
		}
	}

	for_each(expired, [&](AutoSearchPtr as) {
		LogManager::getInstance()->message("An expired autosearch has been removed: " + as->getSearchString(), LogManager::LOG_INFO); 
		removeAutoSearch(as);
	});

	if (!il.empty()) {
		WLock l(cs);
		for_each(il, [&](AutoSearchPtr as) {
			as->changeNumber(true);
			fire(AutoSearchManagerListener::UpdateItem(), as, false);
		});
		dirty = true;
	}

	if(!result) //if no enabled items, start checking from the beginning with newly enabled ones.
		curPos = 0;

	return result;
}

void AutoSearchManager::runSearches() {
	if(!checkItems())
		return;

	StringList allowedHubs;
	ClientManager::getInstance()->getOnlineClients(allowedHubs);
	//no hubs? no fun...
	if(allowedHubs.empty()) {
		return;
	}

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

			if (!(*i)->allowNewItems())
				continue;
			if ((*i)->nextAllowedSearch() > GET_TIME())
				continue;


			as = *i;
			lastSearch = 0;
			break;
		}
	}
	
	if(as) {
		performSearch(as, allowedHubs, TYPE_NORMAL);
	}
}


/* SearchManagerListener and matching */
void AutoSearchManager::on(SearchManagerListener::SearchTypeRenamed, const string& oldName, const string& newName) noexcept {
	RLock l(cs);
	for(auto i = searchItems.begin(); i != searchItems.end(); ++i) {
		if ((*i)->getFileType() == oldName) {
			(*i)->setFileType(newName);
			fire(AutoSearchManagerListener::UpdateItem(), *i, false);
		}
	}
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
			if (!as->allowNewItems() && !as->getManualSearch())
				continue;
			
			if (as->getManualSearch()) {
				as->setManualSearch(false);
				as->updateStatus();
			}

			//match
			if (as->getFileType() == SEARCH_TYPE_TTH) {
				if (!as->match(sr->getTTH().toBase32()))
					continue;
			} else {
				/* Check the type (folder) */
				if(as->getFileType() == SEARCH_TYPE_DIRECTORY && sr->getType() != SearchResult::TYPE_DIRECTORY) {
					continue;
				}

				if (as->getMatchFullPath()) {
					if (!as->match(sr->getFile()))
						continue;
				} else if (!as->match(sr->getType() == SearchResult::TYPE_DIRECTORY ? Util::getLastDir(sr->getFile()) : sr->getFileName()))
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

	//extra checks outside the lock
	for (auto i = matches.begin(); i != matches.end(); ++i) {
		auto as = *i;

		if (as->getFileType() == SEARCH_TYPE_DIRECTORY) {
			string dir = Util::getLastDir(sr->getFile());

			//check shared
			if(as->getCheckAlreadyShared() && ShareManager::getInstance()->isDirShared(dir)) {
				continue;
			}

			//check queued
			if(as->getCheckAlreadyQueued() && QueueManager::getInstance()->isDirQueued(dir)) {
				continue;
			}
		} else if (as->getFileType() != SEARCH_TYPE_ANY && as->getFileType() != SEARCH_TYPE_TTH) {
			if (sr->getType() == SearchResult::TYPE_DIRECTORY)
				continue;

			//check the extension
			try {
				int tmp = 0;
				StringList exts;
				SearchManager::getInstance()->getSearchType(as->getFileType(), tmp, exts, true);
				auto name = sr->getFileName();

				//match
				auto p = find_if(exts, [&name](const string& i) { return name.length() >= i.length() && stricmp(name.c_str() + name.length() - i.length(), i.c_str()) == 0; });
				if (p == exts.end()) continue;
			} catch(...) {
				//lets agree that it's match...
			}
		}

		{
			WLock l(cs);
			auto& rl = searchResults[as->getToken()];
			if (rl.empty()) {
				as->setStatus(AutoSearch::STATUS_COLLECTING);
				fire(AutoSearchManagerListener::UpdateItem(), as, false);
			}
			rl.push_back(sr);
		}

		resultCollector.addEvent(as->getToken(), [this, as] { pickMatch(as); }, 2000);
	}
}

void AutoSearchManager::pickMatch(AutoSearchPtr as) {
	SearchResultList results;

	//get the result list
	{
		WLock l(cs);
		auto p = searchResults.find(as->getToken());
		if (p != searchResults.end()) {
			results = move(p->second);
			searchResults.erase(p);
		}

		as->updateStatus();
	}

	fire(AutoSearchManagerListener::UpdateItem(), as, false);
	dcassert(!results.empty());

	//sort results by the name
	unordered_map<string, SearchResultList> dirList;
	for (auto i = results.begin(); i != results.end(); ++i) {
		dirList[Text::toLower((*i)->getFileName())].push_back(*i);
	}

	//we'll pick one name or all of them, depending on the auto search item
	if (as->getRemove() || as->usingIncrementation()) {
		auto p = find_if(dirList | map_keys, [](const string& s) { return s.find("proper") != string::npos; }).base();
		if (p == dirList.end()) {
			p = find_if(dirList | map_keys, [](const string& s) { return s.find("repack") != string::npos; }).base();
		}

		// no repack or proper, pick the one with most matches
		if (p == dirList.end()) {
			if (as->getStatus() == AutoSearch::STATUS_POSTSEARCH) //don't download anything
				return;
			p = max_element(dirList | map_values, [](const SearchResultList& p1, const SearchResultList& p2) { return p1.size() < p2.size(); }).base();
		}

		unordered_map<string, SearchResultList> dirList2;
		dirList2.insert(*p);
		dirList = move(dirList2);
	}

	//pick the item for each name that has most size matches
	for (auto i = dirList.begin(); i != dirList.end(); ++i) {
		unordered_map<int64_t, int> sizeMap;
		for_each(i->second, [&sizeMap](const SearchResultPtr sr) { sizeMap[sr->getSize()]++; });

		auto p = max_element(sizeMap, [] (pair<int64_t, int> p1, pair<int64_t, int> p2)-> bool { 
			if (p1.first == 0)
				return true;
			if (p2.first == 0)
				return false;
			return p1.second < p2.second; 
		});

		//download all matches with the given size
		for_each(i->second, [p, this, as](const SearchResultPtr sr) { 
			if (sr->getSize() == p->first)
				handleAction(sr, as);
		});
	}
}

void AutoSearchManager::handleAction(const SearchResultPtr sr, AutoSearchPtr as) {
	if (as->getAction() == AutoSearch::ACTION_QUEUE || as->getAction() == AutoSearch::ACTION_DOWNLOAD) {
		try {
			if(sr->getType() == SearchResult::TYPE_DIRECTORY) {
				DirectoryListingManager::getInstance()->addDirectoryDownload(sr->getFile(), HintedUser(sr->getUser(), sr->getHubURL()), as->getTarget(), as->getTargetType(), REPORT_SYSLOG, 
					(as->getAction() == AutoSearch::ACTION_QUEUE) ? QueueItem::PAUSED : QueueItem::DEFAULT, false, as->getToken(), as->getRemove() || as->usingIncrementation());
			} else {
				TargetUtil::TargetInfo ti;
				bool hasSpace = TargetUtil::getVirtualTarget(as->getTarget(), as->getTargetType(), ti, sr->getSize());
				if (!hasSpace)
					TargetUtil::reportInsufficientSize(ti, sr->getSize());

				QueueManager::getInstance()->addFile(ti.targetDir + sr->getFileName(), sr->getSize(), sr->getTTH(), HintedUser(sr->getUser(), sr->getHubURL()), sr->getFile(), 0, true, 
					((as->getAction() == AutoSearch::ACTION_QUEUE) ? QueueItem::PAUSED : QueueItem::DEFAULT), nullptr, as->getToken());
			}
		} catch(const Exception& /*e*/) {
			//LogManager::getInstance()->message("AutoSearch failed to queue " + sr->getFileName() + " (" + e.getError() + ")");
			return;
		}
	} else if (as->getAction() == AutoSearch::ACTION_REPORT) {
		ClientManager* c = ClientManager::getInstance();
		c->lockRead();
		ScopedFunctor([c] { c->unlockRead(); });
		OnlineUser* u = c->findOnlineUser(sr->getUser()->getCID(), sr->getHubURL());

		if(u) {
			Client* client = &u->getClient();
			if(client && client->isConnected()) {
				client->Message("AutoSearch found a file: " + sr->getFile() + " from an user " + u->getIdentity().getNick());
			}

			if(as->getRemove()) {
				removeAutoSearch(as);
			}
		} else {
			return;
		}
	}
}


/* Loading and saving */
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
				xml.addChildAttrib("MatcherString", as->getMatcherString()),
				xml.addChildAttrib("UserMatch", (*i)->getNickPattern());
				xml.addChildAttrib("ExpireTime", (*i)->getExpireTime());
				xml.addChildAttrib("CheckAlreadyQueued", as->getCheckAlreadyQueued());
				xml.addChildAttrib("CheckAlreadyShared", as->getCheckAlreadyShared());
				xml.addChildAttrib("SearchDays", (*i)->searchDays.to_string());
				xml.addChildAttrib("StartTime", (*i)->startTime.toString());
				xml.addChildAttrib("EndTime", (*i)->endTime.toString());
				xml.addChildAttrib("LastSearchTime", Util::toString(as->getLastSearch()));
				xml.addChildAttrib("MatchFullPath", as->getMatchFullPath());
				xml.addChildAttrib("Token", Util::toString(as->getToken()));

				xml.stepIn();

				xml.addTag("Params");
				xml.addChildAttrib("Enabled", as->getUseParams());
				xml.addChildAttrib("CurNumber", as->getCurNumber());
				xml.addChildAttrib("MaxNumber", as->getMaxNumber());
				xml.addChildAttrib("MinNumberLen", as->getNumberLen());
				xml.addChildAttrib("ForceNoDelay", as->getNoDelay());


				if (!as->getFinishedPaths().empty()) {
					xml.addTag("FinishedPaths");
					xml.stepIn();
					for(auto i = as->getFinishedPaths().begin(); i != as->getFinishedPaths().end(); ++i) {
						xml.addTag("Path", i->first);
						xml.addChildAttrib("FinishTime", i->second);
					}
					xml.stepOut();
				}
				xml.stepOut();
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
				aXml.getIntChildAttrib("ExpireTime"),
				aXml.getBoolChildAttrib("CheckAlreadyQueued"),
				aXml.getBoolChildAttrib("CheckAlreadyShared"),
				aXml.getBoolChildAttrib("MatchFullPath"),
				aXml.getIntChildAttrib("Token"));

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

			aXml.stepIn();

			if (aXml.findChild("Params")) {
				as->setUseParams(aXml.getBoolChildAttrib("Enabled"));
				as->setCurNumber(aXml.getIntChildAttrib("CurNumber"));
				as->setMaxNumber(aXml.getIntChildAttrib("MaxNumber"));
				as->setNumberLen(aXml.getIntChildAttrib("MinNumberLen"));
				as->setNoDelay(aXml.getBoolChildAttrib("ForceNoDelay"));
			}
			aXml.resetCurrentChild();

			if (aXml.findChild("FinishedPaths")) {
				aXml.stepIn();
				while(aXml.findChild("Path")) {
					auto time = aXml.getIntChildAttrib("FinishTime");
					aXml.stepIn();
					as->addPath(aXml.getData(), time);
					aXml.stepOut();
				}
				aXml.stepOut();
			}

			addAutoSearch(as, false);
			aXml.stepOut();
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