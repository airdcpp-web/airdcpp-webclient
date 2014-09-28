/*
 * Copyright (C) 2011-2014 AirDC++ Project
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

#include "AutoSearchManager.h"

#include "ClientManager.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "SearchResult.h"
#include "ShareManager.h"
#include "SimpleXML.h"
#include "User.h"
#include "DirectoryListingManager.h"

#include <boost/range/algorithm/max_element.hpp>

namespace dcpp {

using boost::range::find_if;
using boost::max_element;
using boost::algorithm::copy_if;

#define CONFIG_DIR Util::PATH_USER_CONFIG
#define CONFIG_NAME "AutoSearch.xml"

AutoSearchManager::AutoSearchManager() noexcept :
	lastSearch(SETTING(AUTOSEARCH_EVERY)-2), //start searching after 2 minutes.
	recheckTime(SETTING(AUTOSEARCH_RECHECK_TIME)) 
{
	TimerManager::getInstance()->addListener(this);
	SearchManager::getInstance()->addListener(this);
	QueueManager::getInstance()->addListener(this);
}

AutoSearchManager::~AutoSearchManager() noexcept {
	SearchManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);
}

void AutoSearchManager::logMessage(const string& aMsg, bool error) const noexcept {
	LogManager::getInstance()->message(STRING(AUTO_SEARCH) + ": " +  aMsg, error ? LogManager::LOG_ERROR : LogManager::LOG_INFO);
}

/* Adding new items for external use */
AutoSearchPtr AutoSearchManager::addAutoSearch(const string& ss, const string& aTarget, TargetUtil::TargetType aTargetType, bool isDirectory, bool aRemove/*true*/) noexcept {
	if (ss.length() <= 5) {
		logMessage(STRING_F(AUTOSEARCH_ADD_FAILED, ss % STRING(LINE_EMPTY_OR_TOO_SHORT)), true);
		return nullptr;
	}

	auto lst = getSearchesByString(ss);
	if (!lst.empty()) {
		logMessage(STRING_F(AUTOSEARCH_ADD_FAILED, ss % STRING(ITEM_NAME_EXISTS)), true);
		return nullptr;
	}

	AutoSearchPtr as = new AutoSearch(true, ss, isDirectory ? SEARCH_TYPE_DIRECTORY : SEARCH_TYPE_FILE, AutoSearch::ACTION_DOWNLOAD, aRemove, aTarget, aTargetType, 
		StringMatch::EXACT, Util::emptyString, Util::emptyString, SETTING(AUTOSEARCH_EXPIRE_DAYS) > 0 ? GET_TIME() + (SETTING(AUTOSEARCH_EXPIRE_DAYS)*24*60*60) : 0, false, false, false, Util::emptyString);

	addAutoSearch(as, true);
	return as;
}


/* List changes */
void AutoSearchManager::addAutoSearch(AutoSearchPtr aAutoSearch, bool search) noexcept {
	aAutoSearch->prepareUserMatcher();
	aAutoSearch->updatePattern();
	aAutoSearch->updateSearchTime();
	aAutoSearch->updateStatus();
	aAutoSearch->updateExcluded();

	{
		WLock l(cs);
		searchItems.push_back(aAutoSearch);
	}

	dirty = true;
	fire(AutoSearchManagerListener::AddItem(), aAutoSearch);
	if (search && !searchItem(aAutoSearch, TYPE_NEW)) {
		//no hubs
		logMessage(CSTRING_F(AUTOSEARCH_ADDED, aAutoSearch->getSearchString()), false);
	}
}

bool AutoSearchManager::setItemActive(AutoSearchPtr& as, bool toActive) noexcept {
	if (as->getEnabled() == toActive) {
		return false;
	}

	if (as->expirationTimeReached() && toActive) {
		// move the expiration date
		as->setExpireTime(GET_TIME() + SETTING(AUTOSEARCH_EXPIRE_DAYS)*24*60*60);
	} else if (as->maxNumberReached() && toActive) {
		// increase the maximum number by one
		as->setMaxNumber(as->getMaxNumber()+1);
	}

	RLock l(cs);
	as->setEnabled(toActive);

	updateStatus(as, true);
	dirty = true;
	return true;
}

bool AutoSearchManager::updateAutoSearch(AutoSearchPtr& ipw) noexcept {
	WLock l(cs);
	ipw->prepareUserMatcher();
	ipw->updatePattern();
	ipw->updateSearchTime();
	ipw->updateStatus();
	ipw->updateExcluded();

	//if (find_if(searchItems, [ipw](const AutoSearchPtr as) { return as->getSearchString() == ipw->getSearchString() && compare(ipw->getToken(), as->getToken()) != 0; }) != searchItems.end())
	//	return false;

	dirty = true;
	return true;
}

void AutoSearchManager::updateStatus(AutoSearchPtr& as, bool setTabDirty) noexcept {
	as->updateStatus();
	fire(AutoSearchManagerListener::UpdateItem(), as, setTabDirty);
}

void AutoSearchManager::changeNumber(AutoSearchPtr as, bool increase) noexcept {
	WLock l(cs);
	as->changeNumber(increase);
	as->setLastError(Util::emptyString);

	updateStatus(as, true);
}

void AutoSearchManager::removeAutoSearch(AutoSearchPtr& aItem) noexcept {
	WLock l(cs);
	auto i = find_if(searchItems, [aItem](const AutoSearchPtr& as) { return compare(as->getToken(), aItem->getToken()) == 0; });
	if(i != searchItems.end()) {

		if(static_cast<uint32_t>(distance(searchItems.begin(), i)) < curPos) //dont skip a search if we remove before the last search.
			curPos--;

		fire(AutoSearchManagerListener::RemoveItem(), aItem);
		searchItems.erase(i);
		dirty = true;
	}
}


/* Item lookup */
AutoSearchPtr AutoSearchManager::getSearchByIndex(unsigned int index) const noexcept {
	RLock l(cs);
	if(searchItems.size() > index)
		return searchItems[index];
	return nullptr;
}

AutoSearchPtr AutoSearchManager::getSearchByToken(ProfileToken aToken) const noexcept {
	auto p = find_if(searchItems, [&aToken](const AutoSearchPtr& as)  { return compare(as->getToken(), aToken) == 0; });
	return p != searchItems.end() ? *p : nullptr;
}

AutoSearchList AutoSearchManager::getSearchesByBundle(const BundlePtr& aBundle) const noexcept{
	AutoSearchList ret;

	RLock l(cs);
	copy_if(searchItems, back_inserter(ret), [&](const AutoSearchPtr& as) { return as->hasBundle(aBundle); });
	return ret;
}

AutoSearchList AutoSearchManager::getSearchesByString(const string& aSearchString, const AutoSearchPtr& ignoredSearch) const noexcept{
	AutoSearchList ret;

	RLock l(cs);
	copy_if(searchItems, back_inserter(ret), [&](const AutoSearchPtr& as) { return as->getSearchString() == aSearchString && (!ignoredSearch || as != ignoredSearch); });
	return ret;
}


/* GUI things */
void AutoSearchManager::getMenuInfo(const AutoSearchPtr& as, BundleList& bundleInfo, AutoSearch::FinishedPathMap& finishedPaths) const noexcept {
	{
		RLock l(cs);
		finishedPaths = as->getFinishedPaths();
		bundleInfo = as->getBundles();
	}
}

void AutoSearchManager::clearPaths(AutoSearchPtr as) noexcept {
	{
		WLock l (cs);
		as->clearPaths();
	}

	fire(AutoSearchManagerListener::UpdateItem(), as, true);
	dirty = true;
}

string AutoSearchManager::getBundleStatuses(const AutoSearchPtr& as) const noexcept {
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
				auto& b = *as->getBundles().begin();
				if (b->getStatus() == Bundle::STATUS_QUEUED) {
					statusString += STRING_F(BUNDLE_X_QUEUED, b->getName());
				} else if (b->getStatus() == Bundle::STATUS_FAILED_MISSING || b->getStatus() == Bundle::STATUS_SHARING_FAILED) {
					statusString += b->getName() + " (" + b->getLastError() + ")";
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

void AutoSearchManager::clearError(AutoSearchPtr& as) noexcept {
	as->setLastError(Util::emptyString);
	fire(AutoSearchManagerListener::UpdateItem(), as, true);
}

void AutoSearchManager::onBundleCreated(BundlePtr& aBundle, const ProfileToken aSearch) noexcept {
	WLock l(cs);
	auto as = getSearchByToken(aSearch);
	if (as) {
		aBundle->setAddedByAutoSearch(true); //yes, not the best place to modify bundle information.
		as->addBundle(aBundle);
		updateStatus(as, true);
	}
}

void AutoSearchManager::onBundleError(const ProfileToken aSearch, const string& aError, const string& aDir, const HintedUser& aUser) noexcept {
	RLock l(cs);
	auto as = getSearchByToken(aSearch);
	if (as) {
		as->setLastError(STRING_F(AS_ERROR, Util::getLastDir(aDir) % aError % Util::getTimeString() % ClientManager::getInstance()->getFormatedNicks(aUser)));
		fire(AutoSearchManagerListener::UpdateItem(), as, true);
	}

	//logMessage(aError, true);
}

void AutoSearchManager::on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept {
	if (aBundle->getStatus() == Bundle::STATUS_FINISHED) {
		onRemoveBundle(aBundle, true);
		return;
	}

	auto items = getSearchesByBundle(aBundle);
	bool found = false, searched = false;
	for (auto& as : items) {
		if (as->hasBundle(aBundle)) {
			found = true;
			{
				RLock l (cs);
				updateStatus(as, true);
			}

			if (!searched && aBundle->getStatus() == Bundle::STATUS_FAILED_MISSING) {
				searchItem(as, TYPE_NORMAL);
				searched = true;
			}
		}
	}

	if (aBundle->getStatus() == Bundle::STATUS_FAILED_MISSING && !found && SETTING(AUTO_COMPLETE_BUNDLES)) {
		AutoSearchManager::getInstance()->addFailedBundle(aBundle); 
	}
}

void AutoSearchManager::onRemoveBundle(const BundlePtr& aBundle, bool finished) noexcept {
	AutoSearchList expired, removed;
	auto items = getSearchesByBundle(aBundle);

	{
		WLock l(cs);
		for (auto& as : items) {
			if (finished && as->removeOnCompleted()) {
				removed.push_back(as);
			} else if (as->onBundleRemoved(aBundle, finished)) {
				expired.push_back(as);
			} else {
				as->setLastError(Util::emptyString);
				dirty = true;
				fire(AutoSearchManagerListener::UpdateItem(), as, true);
			}
		}
	}

	handleExpiredItems(expired);
	for (auto& as : removed) {
		removeAutoSearch(as);
		logMessage(STRING_F(COMPLETE_ITEM_X_REMOVED, as->getSearchString()), false);
	}
}

void AutoSearchManager::handleExpiredItems(AutoSearchList& expired) noexcept{
	for (auto& as : expired) {
		if (SETTING(REMOVE_EXPIRED_AS)) {
			logMessage(STRING_F(EXPIRED_AS_REMOVED, as->getSearchString()), false);
			removeAutoSearch(as);
		} else if (as->getEnabled()) {
			logMessage(STRING_F(EXPIRED_AS_DISABLED, as->getSearchString()), false);
			setItemActive(as, false);
		} else {
			// disabled already

			RLock l(cs);
			as->updateStatus();
			fire(AutoSearchManagerListener::UpdateItem(), as, false);
		}
	}
}

bool AutoSearchManager::addFailedBundle(const BundlePtr& aBundle) noexcept {
	auto lst = getSearchesByString(aBundle->getName());
	if (!lst.empty()) {
		return false;
	}

	auto as = new AutoSearch(true, aBundle->getName(), SEARCH_TYPE_DIRECTORY, AutoSearch::ACTION_DOWNLOAD, true, Util::getParentDir(aBundle->getTarget()), TargetUtil::TARGET_PATH, 
		StringMatch::EXACT, Util::emptyString, Util::emptyString, SETTING(AUTOSEARCH_EXPIRE_DAYS) > 0 ? GET_TIME() + (SETTING(AUTOSEARCH_EXPIRE_DAYS)*24*60*60) : 0, false, false, false, Util::emptyString);

	as->addBundle(aBundle);
	addAutoSearch(as, true);
	return true;
}

string AutoSearch::getFormatedSearchString() const noexcept {
	return useParams ? formatParams(false) : searchString;
}

/* Item searching */
void AutoSearchManager::performSearch(AutoSearchPtr& as, StringList& aHubs, SearchType aType) noexcept {

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

	string searchWord;
	bool failedBundle = false;
	
	//Update the item
	{
		WLock l(cs);
		as->updatePattern();
		if (as->getStatus() == AutoSearch::STATUS_FAILED_MISSING) {
			auto p = find_if(as->getBundles(), Bundle::HasStatus(Bundle::STATUS_FAILED_MISSING));
			if (p != as->getBundles().end()) {
				searchWord = (*p)->getName();
				failedBundle = true;
			}
		}
	}

	if (!failedBundle)
		searchWord = as->getFormatedSearchString();

	as->setLastSearch(GET_TIME());
	if ((aType == TYPE_MANUAL_BG || aType == TYPE_MANUAL_FG) && !as->getEnabled()) {
		as->setManualSearch(true);
		as->setStatus(AutoSearch::STATUS_MANUAL);
	}
	fire(AutoSearchManagerListener::UpdateItem(), as, false);


	//Run the search
	if (aType != TYPE_MANUAL_FG) {
		uint64_t searchTime = SearchManager::getInstance()->search(aHubs, searchWord, 0, (SearchManager::TypeModes)ftype, SearchManager::SIZE_DONTCARE,
			"as", extList, SearchQuery::parseSearchString(as->getExcludedString()), aType == TYPE_MANUAL_BG ? Search::MANUAL : Search::AUTO_SEARCH, 0, SearchManager::DATE_DONTCARE, false);

		//Report
		string msg;
		if (searchTime == 0) {
			if (failedBundle) {
				msg = STRING_F(FAILED_BUNDLE_SEARCHED, searchWord);
			}
			else if (aType == TYPE_NEW) {
				msg = CSTRING_F(AUTOSEARCH_ADDED_SEARCHED, searchWord);
			}
			else {
				msg = STRING_F(ITEM_SEARCHED, searchWord);
			}
		}
		else {
			auto time = searchTime / 1000;
			if (failedBundle) {
				msg = STRING_F(FAILED_BUNDLE_SEARCHED_IN, searchWord % time);
			}
			else if (aType == TYPE_NEW) {
				msg = CSTRING_F(AUTOSEARCH_ADDED_SEARCHED_IN, searchWord % time);
			}
			else {
				msg = STRING_F(ITEM_SEARCHED_IN, searchWord % time);
			}
		}

		logMessage(msg, false);
	} else {
		fire(AutoSearchManagerListener::SearchForeground(), as, searchWord);
	}
}


bool AutoSearchManager::searchItem(AutoSearchPtr& as, SearchType aType) noexcept {
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
		if(recheckTime >= static_cast<uint32_t>(SETTING(AUTOSEARCH_RECHECK_TIME))) {
			curPos = 0;
			endOfListReached = false;
		} else {
			checkItems();
			return;
		}
	}

	if(checkItems() && lastSearch >= static_cast<uint32_t>(SETTING(AUTOSEARCH_EVERY))) {
		runSearches();
	}
}

/* Scheduled searching */
bool AutoSearchManager::checkItems() noexcept {
	AutoSearchList expired;
	bool result = false;
	auto curTime = GET_TIME();

	{
		RLock l(cs);
		
		if(searchItems.empty()){
			curPos = 0; //list got empty, start from 0 with new items.
			return false;
		}

		for(auto& as: searchItems) {
			bool search = true;
			if (!as->allowNewItems())
				search = false;
			
			//check expired, and remove them.
			if (as->getStatus() != AutoSearch::STATUS_EXPIRED && as->expirationTimeReached() && as->getBundles().empty()) {
				expired.push_back(as);
				search = false;
			}

			// check post search items and whether we can change the number
			if (as->removePostSearch()) {
				if (as->maxNumberReached()) {
					expired.push_back(as);
					continue;
				} else {
					dirty = true;
					as->changeNumber(true);
					as->updateStatus();
					fire(AutoSearchManagerListener::UpdateItem(), as, false);
				}
			}

			if (as->updateSearchTime() || as->getExpireTime() > 0)
				fire(AutoSearchManagerListener::UpdateItem(), as, false);


			if (search && as->nextAllowedSearch() <= curTime)
				result = true;
		}
	}

	handleExpiredItems(expired);

	if(!result) //if no enabled items, start checking from the beginning with newly enabled ones.
		curPos = 0;

	return result;
}

void AutoSearchManager::runSearches() noexcept {
	StringList allowedHubs;
	ClientManager::getInstance()->getOnlineClients(allowedHubs);
	//no hubs? no fun...
	if(allowedHubs.empty()) {
		return;
	}

	AutoSearchPtr searchItem = nullptr;
	{
		RLock l (cs);
		
		//we have waited for search time, and we are at the end of list. wait for recheck time. so time between searches "autosearch every" + "recheck time" 
		if(curPos >= searchItems.size()) { 
			LogManager::getInstance()->message(STRING_F(AS_END_OF_LIST, SETTING(AUTOSEARCH_RECHECK_TIME)), LogManager::LOG_INFO);
			curPos = 0;
			endOfListReached = true;
			recheckTime = 0;
			return;
		}

		 for(auto i = searchItems.begin() + curPos; i != searchItems.end(); ++i) {
			AutoSearchPtr as = *i;

			curPos++; //move to next one, even if we skip something, dont check the same ones again until list has gone thru.

			if (!as->allowNewItems())
				continue;
			if (as->nextAllowedSearch() > GET_TIME())
				continue;


			searchItem = as;
			lastSearch = 0;
			break;
		}
	}
	
	if(searchItem) {
		performSearch(searchItem, allowedHubs, TYPE_NORMAL);
	}
}


/* SearchManagerListener and matching */
void AutoSearchManager::on(SearchManagerListener::SearchTypeRenamed, const string& oldName, const string& newName) noexcept {
	RLock l(cs);
	for(auto& as: searchItems) {
		if (as->getFileType() == oldName) {
			as->setFileType(newName);
			fire(AutoSearchManagerListener::UpdateItem(), as, false);
		}
	}
}

void AutoSearchManager::on(SearchManagerListener::SR, const SearchResultPtr& sr) noexcept {
	//don't match bundle searches
	if (Util::stricmp(sr->getToken(), "qa") == 0)
		return;

	AutoSearchList matches;

	{
		RLock l (cs);
		for(auto& as: searchItems) {
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
				} else if (as->getFileType() == SEARCH_TYPE_FILE && sr->getType() != SearchResult::TYPE_FILE) {
					continue;
				}

				if (as->getMatchFullPath()) {
					if (!as->match(sr->getPath()))
						continue;
					if (as->isExcluded(sr->getPath()))
						continue;
				} else {
					const string matchPath = sr->getFileName();
					if (!as->match(matchPath))
						continue;
					if (as->isExcluded(matchPath))
						continue;
				}
			}

			//check the nick
			if(!as->getNickPattern().empty()) {
				StringList nicks = ClientManager::getInstance()->getNicks(sr->getUser());
				if (find_if(nicks, [&](const string& aNick) { return as->matchNick(aNick); }) == nicks.end())
					continue;
			}

			//we have a valid result
			matches.push_back(as);
		}
	}

	//extra checks outside the lock
	for (auto& as: matches) {
		if (!SearchManager::isDefaultTypeStr(as->getFileType())) {
			if (sr->getType() == SearchResult::TYPE_DIRECTORY)
				continue;

			//check the extension
			try {
				int tmp = 0;
				StringList exts;
				SearchManager::getInstance()->getSearchType(as->getFileType(), tmp, exts, true);
				auto name = sr->getFileName();

				//match
				auto p = find_if(exts, [&name](const string& i) { return name.length() >= i.length() && Util::stricmp(name.c_str() + name.length() - i.length(), i.c_str()) == 0; });
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
			} else if (find_if(rl, [&sr](const SearchResultPtr& aSR) { return aSR->getUser() == sr->getUser() && aSR->getPath() == sr->getPath(); }) != rl.end()) {
				//don't add the same result multiple times, makes the counting more reliable
				return;
			}
			rl.push_back(sr);
		}

		resultCollector.addEvent(as->getToken(), [this, as] { pickNameMatch(as); }, 2000);
	}
}

void AutoSearchManager::pickNameMatch(AutoSearchPtr as) noexcept{
	SearchResultList results;
	int64_t minWantedSize = -1;

	//get the result list
	{
		WLock l(cs);
		auto p = searchResults.find(as->getToken());
		if (p != searchResults.end()) {
			results.swap(p->second);
			searchResults.erase(p);
		}

		updateStatus(as, false);
		if (as->getStatus() == AutoSearch::STATUS_FAILED_MISSING) {
			auto p = find_if(as->getBundles(), Bundle::HasStatus(Bundle::STATUS_FAILED_MISSING));
			dcassert(p != as->getBundles().end());
			minWantedSize = (*p)->getSize();
		}
	}

	dcassert(!results.empty());

	//sort results by the name
	unordered_map<string, SearchResultList> dirList;
	for (auto& r: results) {
		dirList[Text::toLower(r->getFileName())].push_back(r);
	}

	//we'll pick one name (or all of them, if the item isn't going to be removed after completion)
	if (as->getRemove() || as->usingIncrementation()) {
		auto p = find_if(dirList | map_keys, [](const string& s) { return s.find("proper") != string::npos; }).base();
		if (p == dirList.end()) {
			p = find_if(dirList | map_keys, [](const string& s) { return s.find("repack") != string::npos; }).base();
		}

		if (p == dirList.end()) {
			if (as->getStatus() == AutoSearch::STATUS_POSTSEARCH) //don't download anything
				return;

			// no repack or proper, pick the one with most matches
			p = max_element(dirList | map_values, [](const SearchResultList& p1, const SearchResultList& p2) { return p1.size() < p2.size(); }).base();
		}

		// only download this directory
		unordered_map<string, SearchResultList> dirList2;
		dirList2.insert(*p);
		dirList.swap(dirList2);
	}

	for (auto& p: dirList) {
		// dupe check
		if (as->getFileType() == SEARCH_TYPE_DIRECTORY) {
			auto& dir = p.first;

			//check shared
			if (as->getCheckAlreadyShared()) {
				auto paths = ShareManager::getInstance()->getDirPaths(dir);
				if (!paths.empty()) {
					as->setLastError(STRING_F(DIR_SHARED_ALREADY, paths.front()));
					fire(AutoSearchManagerListener::UpdateItem(), as, true);
					continue;
				}
			}

			//check queued
			if (as->getCheckAlreadyQueued() && as->getStatus() != AutoSearch::STATUS_FAILED_MISSING) {
				auto paths = QueueManager::getInstance()->getDirPaths(dir);
				if (!paths.empty()) {
					as->setLastError(STRING_F(DIR_QUEUED_ALREADY, dir));
					fire(AutoSearchManagerListener::UpdateItem(), as, true);
					continue;
				}
			}
		}

		downloadList(p.second, as, minWantedSize);
	}
}

void AutoSearchManager::downloadList(SearchResultList& srl, AutoSearchPtr& as, int64_t minWantedSize) noexcept{
	auto getDownloadSize = [](const SearchResultList& srl, int64_t minSize) -> int64_t {
		//pick the item that has most size matches
		unordered_map<int64_t, int> sizeMap;
		for (const auto& sr : srl) {
			if (sr->getSize() > minSize)
				sizeMap[sr->getSize()]++;
		}

		auto p = max_element(sizeMap, [](const pair<int64_t, int>& p1, const pair<int64_t, int>& p2)-> bool {
			//NMDC results always come last
			if (p1.first == 0)
				return true;
			if (p2.first == 0)
				return false;

			return p1.second < p2.second;
		});

		return p != sizeMap.end() ? p->first : -1;
	};

	// if we have a bundle with missing files, try to find one that is bigger than it
	auto dlSize = getDownloadSize(srl, minWantedSize);
	if (dlSize == -1) {
		//no bigger items found
		dlSize = getDownloadSize(srl, -1);
		if (minWantedSize == dlSize) {
			//no need to match an identical bundle again
			return;
		}
	}

	dcassert(dlSize > -1);

	//download matches with the given size
	srl.erase(remove_if(srl.begin(), srl.end(), [dlSize](const SearchResultPtr& aSR) { return aSR->getSize() != dlSize; }), srl.end());
	SearchResult::pickResults(srl, SETTING(MAX_AUTO_MATCH_SOURCES));

	for (const auto& sr : srl) {
		handleAction(sr, as);
	}
}

void AutoSearchManager::handleAction(const SearchResultPtr& sr, AutoSearchPtr& as) noexcept {
	if (as->getAction() == AutoSearch::ACTION_QUEUE || as->getAction() == AutoSearch::ACTION_DOWNLOAD) {
		if(sr->getType() == SearchResult::TYPE_DIRECTORY) {
			auto target = as->getTarget();
			auto targetType = as->getTargetType();

			// Do we have a bundle with the same name?
			{
				RLock l(cs);
				auto p = find_if(as->getBundles(), 
					[&](const BundlePtr& b) { return b->getName() == sr->getFileName(); });
				
				if (p != as->getBundles().end()) {
					// Use the same path
					target = Util::getParentDir((*p)->getTarget());
					targetType = TargetUtil::TARGET_PATH;
				}
			}

			DirectoryListingManager::getInstance()->addDirectoryDownload(sr->getPath(), sr->getFileName(), sr->getUser(), target,
				targetType, REPORT_SYSLOG, (as->getAction() == AutoSearch::ACTION_QUEUE) ? QueueItem::PAUSED : QueueItem::DEFAULT,
				false, as->getToken(), as->getRemove() || as->usingIncrementation(), false);
		} else {
			TargetUtil::TargetInfo ti;
			bool hasSpace = TargetUtil::getVirtualTarget(as->getTarget(), as->getTargetType(), ti, sr->getSize());
			if (!hasSpace)
				TargetUtil::reportInsufficientSize(ti, sr->getSize());

			try {
				auto b = QueueManager::getInstance()->createFileBundle(ti.targetDir + sr->getFileName(), sr->getSize(), sr->getTTH(), 
					sr->getUser(), sr->getDate(), 0, 
					((as->getAction() == AutoSearch::ACTION_QUEUE) ? QueueItem::PAUSED : QueueItem::DEFAULT));

				if (b) {
					onBundleCreated(b, as->getToken());
				}
			} catch(const Exception& e) {
				onBundleError(as->getToken(), e.getError(), ti.targetDir + sr->getFileName(), sr->getUser());
				return;
			}
		}
	} else if (as->getAction() == AutoSearch::ACTION_REPORT) {
		ClientManager* cm = ClientManager::getInstance();
		{
			RLock l(cm->getCS());
			OnlineUser* u = cm->findOnlineUser(sr->getUser());

			if (u) {
				Client* client = &u->getClient();
				if (client && client->isConnected()) {
					//TODO: use magnet link
					client->Message(STRING(AUTO_SEARCH) + ": " + 
						STRING_F(AS_X_FOUND_FROM, Text::toLower(sr->getType() == SearchResult::TYPE_DIRECTORY ? STRING(FILE) : STRING(DIRECTORY)) % sr->getFileName() % u->getIdentity().getNick()));
				}

				if (as->getRemove()) {
					removeAutoSearch(as);
					logMessage(STRING_F(COMPLETE_ITEM_X_REMOVED, as->getSearchString()), false);
				}
			}
		}
	}
}


/* Loading and saving */
void AutoSearchManager::AutoSearchSave() noexcept {
	dirty = false;
	SimpleXML xml;

	xml.addTag("Autosearch");
	xml.addChildAttrib("LastPosition", curPos);
	xml.stepIn();
	xml.addTag("Autosearch");
	xml.stepIn();

	{
		RLock l(cs);
		for(auto& as: searchItems) {
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
			xml.addChildAttrib("UserMatch", as->getNickPattern());
			xml.addChildAttrib("ExpireTime", as->getExpireTime());
			xml.addChildAttrib("CheckAlreadyQueued", as->getCheckAlreadyQueued());
			xml.addChildAttrib("CheckAlreadyShared", as->getCheckAlreadyShared());
			xml.addChildAttrib("SearchDays", as->searchDays.to_string());
			xml.addChildAttrib("StartTime", as->startTime.toString());
			xml.addChildAttrib("EndTime", as->endTime.toString());
			xml.addChildAttrib("LastSearchTime", Util::toString(as->getLastSearch()));
			xml.addChildAttrib("MatchFullPath", as->getMatchFullPath());
			xml.addChildAttrib("ExcludedWords", as->getExcludedString());
			xml.addChildAttrib("Token", Util::toString(as->getToken()));

			xml.stepIn();

			xml.addTag("Params");
			xml.addChildAttrib("Enabled", as->getUseParams());
			xml.addChildAttrib("CurNumber", as->getCurNumber());
			xml.addChildAttrib("MaxNumber", as->getMaxNumber());
			xml.addChildAttrib("MinNumberLen", as->getNumberLen());
			xml.addChildAttrib("LastIncFinish", as->getLastIncFinish());


			if (!as->getFinishedPaths().empty()) {
				xml.addTag("FinishedPaths");
				xml.stepIn();
				for(auto& p: as->getFinishedPaths()) {
					xml.addTag("Path", p.first);
					xml.addChildAttrib("FinishTime", p.second);
				}
				xml.stepOut();
			}

			if (!as->getBundles().empty()) {
				xml.addTag("Bundles");
				xml.stepIn();
				for (const auto& b: as->getBundles()) {
					xml.addTag("Bundle", b->getToken());
				}
				xml.stepOut();
			}
			xml.stepOut();
		}
	}

	xml.stepOut();
	xml.stepOut();
		
	SettingsManager::saveSettingFile(xml, CONFIG_DIR, CONFIG_NAME);
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
				aXml.getChildAttrib("ExcludedWords"),
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
				as->setLastIncFinish(aXml.getBoolChildAttrib("LastIncFinish"));
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
			aXml.resetCurrentChild();

			if (aXml.findChild("Bundles")) {
				aXml.stepIn();
				while(aXml.findChild("Bundle")) {
					aXml.stepIn();
					auto token = aXml.getData();
					auto b = QueueManager::getInstance()->findBundle(token);
					if (b)
						as->addBundle(b);

					aXml.stepOut();
				}
				aXml.stepOut();
			}
			aXml.resetCurrentChild();

			if (as->getExpireTime() > 0 && as->getBundles().empty() && as->getExpireTime() < GET_TIME()) {
				as->setEnabled(false);
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
		SettingsManager::loadSettingFile(xml, CONFIG_DIR, CONFIG_NAME);

		if(xml.findChild("Autosearch")) {
			curPos = xml.getIntChildAttrib("LastPosition");
			xml.stepIn();
			loadAutoSearch(xml);
			xml.stepOut();
		}
		if(curPos >= searchItems.size())
			curPos = 0;
	} catch(const Exception& e) {
		LogManager::getInstance()->message(STRING_F(LOAD_FAILED_X, CONFIG_NAME % e.getError()), LogManager::LOG_ERROR);
	}
}
}