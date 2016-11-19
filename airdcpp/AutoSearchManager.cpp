/*
 * Copyright (C) 2011-2016 AirDC++ Project
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
#include "SearchManager.h"
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
#define XML_GROUPING_VERSION 1

AutoSearchManager::AutoSearchManager() noexcept
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

void AutoSearchManager::logMessage(const string& aMsg, LogMessage::Severity aSeverity) const noexcept {
	LogManager::getInstance()->message(STRING(AUTO_SEARCH) + ": " +  aMsg, aSeverity);
}

/* Adding new items for external use */
AutoSearchPtr AutoSearchManager::addAutoSearch(const string& ss, const string& aTarget, bool isDirectory, AutoSearch::ItemType asType, bool aRemove, int aInterval) noexcept {
	if (ss.length() <= 5) {
		logMessage(STRING_F(AUTOSEARCH_ADD_FAILED, ss % STRING(LINE_EMPTY_OR_TOO_SHORT)), LogMessage::SEV_ERROR);
		return nullptr;
	}

	auto lst = getSearchesByString(ss);
	if (!lst.empty()) {
		logMessage(STRING_F(AUTOSEARCH_ADD_FAILED, ss % STRING(ITEM_NAME_EXISTS)), LogMessage::SEV_ERROR);
		return nullptr;
	}

	AutoSearchPtr as = new AutoSearch(true, ss, isDirectory ? SEARCH_TYPE_DIRECTORY : SEARCH_TYPE_FILE, AutoSearch::ACTION_DOWNLOAD, aRemove, aTarget, 
		StringMatch::PARTIAL, Util::emptyString, Util::emptyString, SETTING(AUTOSEARCH_EXPIRE_DAYS) > 0 ? GET_TIME() + (SETTING(AUTOSEARCH_EXPIRE_DAYS)*24*60*60) : 0, false, false, false, Util::emptyString, aInterval, asType, false);

	addAutoSearch(as, true);
	return as;
}


/* List changes */
void AutoSearchManager::addAutoSearch(AutoSearchPtr aAutoSearch, bool search, bool loading) noexcept {
	aAutoSearch->prepareUserMatcher();
	aAutoSearch->updatePattern();
	aAutoSearch->updateSearchTime();
	aAutoSearch->updateStatus();
	aAutoSearch->updateExcluded();

	{
		WLock l(cs);
		searchItems.addItem(aAutoSearch);
	}

	dirty = true;
	fire(AutoSearchManagerListener::AddItem(), aAutoSearch);
	if (search) {
		if (!searchItem(aAutoSearch, TYPE_NEW)) {
			//no hubs
			logMessage(CSTRING_F(AUTOSEARCH_ADDED, aAutoSearch->getSearchString()), LogMessage::SEV_INFO);
		}
	} 
	if(!loading) {
		delayEvents.addEvent(RECALCULATE_SEARCH, [=] { resetSearchTimes(GET_TICK(), true); }, 1000);
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
	{
		RLock l(cs);
		as->setEnabled(toActive);
		updateStatus(as, true);
	}
	delayEvents.addEvent(RECALCULATE_SEARCH, [=] { resetSearchTimes(GET_TICK(), true); }, 1000);
	dirty = true;
	return true;
}

bool AutoSearchManager::updateAutoSearch(AutoSearchPtr& ipw) noexcept {
	{
		WLock l(cs);
		ipw->prepareUserMatcher();
		ipw->updatePattern();
		ipw->updateSearchTime();
		ipw->updateStatus();
		ipw->updateExcluded();
	}

	delayEvents.addEvent(RECALCULATE_SEARCH, [=] { resetSearchTimes(GET_TICK(), true); }, 1000);
	//if (find_if(searchItems, [ipw](const AutoSearchPtr as) { return as->getSearchString() == ipw->getSearchString() && compare(ipw->getToken(), as->getToken()) != 0; }) != searchItems.end())
	//	return false;
	fire(AutoSearchManagerListener::UpdateItem(), ipw, true);
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
	bool hasItem = false;
	{
		WLock l(cs);
		hasItem = searchItems.hasItem(aItem);
		if(hasItem) {
			fire(AutoSearchManagerListener::RemoveItem(), aItem);
			searchItems.removeItem(aItem);
			dirty = true;
		}
	}
	if(hasItem)
		delayEvents.addEvent(RECALCULATE_SEARCH, [=] { resetSearchTimes(GET_TICK(), true); }, 1000);
}

AutoSearchList AutoSearchManager::getSearchesByBundle(const BundlePtr& aBundle) const noexcept{
	AutoSearchList ret;

	RLock l(cs);
	copy_if(searchItems.getItems() | map_values, back_inserter(ret), [&](const AutoSearchPtr& as) { return as->hasBundle(aBundle); });
	return ret;
}

AutoSearchList AutoSearchManager::getSearchesByString(const string& aSearchString, const AutoSearchPtr& ignoredSearch) const noexcept{
	AutoSearchList ret;

	RLock l(cs);
	copy_if(searchItems.getItems() | map_values, back_inserter(ret), [&](const AutoSearchPtr& as) { return as->getSearchString() == aSearchString && (!ignoredSearch || as != ignoredSearch); });
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

void AutoSearchManager::onBundleCreated(BundlePtr& aBundle, void* aSearch) noexcept {
	bool found = false;
	{
		WLock l(cs);
		auto as = searchItems.getItem(aSearch);
		if (as) {
			aBundle->setAddedByAutoSearch(true); //yes, not the best place to modify bundle information.
			as->addBundle(aBundle);
			updateStatus(as, true);
			dirty = true;
			found = true;
		}
	}

	if(found)
		delayEvents.addEvent(RECALCULATE_SEARCH, [=] { resetSearchTimes(GET_TICK(), true); }, 1000);
}

void AutoSearchManager::onBundleError(void* aSearch, const string& aError, const string& aDir, const HintedUser& aUser) noexcept {
	RLock l(cs);
	auto as = searchItems.getItem(aSearch);
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
				RLock l(cs);
				updateStatus(as, true);
			}
			// if we already have a waiting time over 5 minutes in search queue don't pile up more...
			if (!searched && aBundle->getStatus() == Bundle::STATUS_FAILED_MISSING && checkSearchQueueLimit()) {
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
	bool itemsEnabled = false;
	{
		WLock l(cs);
		for (auto& as : items) {
			if (finished && as->removeOnCompleted()) {
				removed.push_back(as);
			} else if (as->onBundleRemoved(aBundle, finished)) {
				expired.push_back(as);
			} else {
				itemsEnabled = true;
				as->setLastError(Util::emptyString);
				dirty = true;
				fire(AutoSearchManagerListener::UpdateItem(), as, true);
			}
		}
	}

	handleExpiredItems(expired);
	for (auto& as : removed) {
		removeAutoSearch(as);
		logMessage(STRING_F(COMPLETE_ITEM_X_REMOVED, as->getSearchString()), LogMessage::SEV_INFO);
	}
	//One or more items got in searching state again
	if (itemsEnabled)
		delayEvents.addEvent(RECALCULATE_SEARCH, [=] { resetSearchTimes(GET_TICK(), true);  }, 2000);
	
}

void AutoSearchManager::handleExpiredItems(AutoSearchList& expired) noexcept{
	for (auto& as : expired) {
		if (SETTING(REMOVE_EXPIRED_AS)) {
			logMessage(STRING_F(EXPIRED_AS_REMOVED, as->getSearchString()), LogMessage::SEV_INFO);
			removeAutoSearch(as);
		} else if (as->getEnabled()) {
			logMessage(STRING_F(EXPIRED_AS_DISABLED, as->getSearchString()), LogMessage::SEV_INFO);
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

	//7 days expiry
	auto as = new AutoSearch(true, aBundle->getName(), SEARCH_TYPE_DIRECTORY, AutoSearch::ACTION_DOWNLOAD, true, Util::getParentDir(aBundle->getTarget()), 
		StringMatch::EXACT, Util::emptyString, Util::emptyString, GET_TIME() + 7*24*60*60, false, false, false, Util::emptyString, 60, AutoSearch::FAILED_BUNDLE, false);

	as->setGroup(SETTING(AS_FAILED_DEFAULT_GROUP));
	as->addBundle(aBundle);
	addAutoSearch(as, aBundle->isRecent() && checkSearchQueueLimit());
	return true;
}

string AutoSearch::getFormatedSearchString() const noexcept {
	return useParams ? formatParams(false) : searchString;
}

/* Item searching */
void AutoSearchManager::performSearch(AutoSearchPtr& as, StringList& aHubs, SearchType aType, uint64_t aTick) noexcept {

	//Get the search type
	StringList extList;
	auto ftype = Search::TYPE_ANY;
	try {
		SearchManager::getInstance()->getSearchType(as->getFileType(), ftype, extList, true);
	} catch(const SearchTypeException&) {
		//reset to default
		as->setFileType(SEARCH_TYPE_ANY);
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
	
	//Run the search
	if (aType != TYPE_MANUAL_FG) {
		auto s = make_shared<Search>(aType == TYPE_MANUAL_BG ? Search::MANUAL : Search::AUTO_SEARCH, "as");
		s->query = searchWord;
		s->fileType = ftype;
		s->exts = extList;
		s->excluded = SearchQuery::parseSearchString(as->getExcludedString());

		lastSearchQueueTime = SearchManager::getInstance()->search(aHubs, s).queueTime;

		//Report
		string msg;
		if (lastSearchQueueTime == 0) {
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
			auto time = lastSearchQueueTime / 1000;
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
		logMessage(msg, LogMessage::SEV_INFO);
	} else {
		fire(AutoSearchManagerListener::SearchForeground(), as, searchWord);
	}
	resetSearchTimes(aTick, false);
	nextSearch += lastSearchQueueTime / 1000; //add the waiting time from search queue to avoid pile up...
	fire(AutoSearchManagerListener::UpdateItem(), as, false);
}
void AutoSearchManager::resetSearchTimes(uint64_t aTick, bool aUpdate) noexcept {
	int itemCount = 0;
	int recentItems = 0;

	RLock l(cs);
	if (searchItems.getItems().empty()) {
		nextSearch = 0;
		return;
	}

	time_t tmp = 0;
	//calculate which of the items has the nearest possible search time.
	for (const auto& x : searchItems.getItems() | map_values) {
		if (!x->allowNewItems())
			continue;
		if (x->isRecent())
			recentItems++;

		if (!x->isRecent() && x->nextAllowedSearch() <= GET_TIME())
			itemCount++;

		auto next_tt = x->getNextSearchTime();
		tmp = tmp == 0 ? next_tt : min(next_tt, tmp);
	}

	//We have nothing to search for...
	if (tmp == 0) {
		nextSearch = 0;
		return;
	}
	uint64_t nextSearchTick = 0;
	
	if(itemCount > 0)
		nextSearchTick = searchItems.recalculateSearchTimes(false, aUpdate, aTick, itemCount, SETTING(AUTOSEARCH_EVERY));

	//Calculate interval for recent items, if any..
	uint64_t recentSearchTick = 0;
	if (recentItems > 0)
		recentSearchTick = searchItems.recalculateSearchTimes(true, aUpdate, aTick, recentItems, SETTING(AUTOSEARCH_EVERY));

	nextSearchTick = recentSearchTick > 0 ? nextSearchTick > 0 ? min(recentSearchTick, nextSearchTick) : recentSearchTick : nextSearchTick;

	//We already missed the search time, add 3 seconds and search then.
	if (aTick > nextSearchTick)
		nextSearchTick = aTick + 3000;

	time_t t = GET_TIME() + ((nextSearchTick - aTick) / 1000);
	nextSearch = max(tmp, t);
	
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

	if(nextSearch != 0 && nextSearch <= GET_TIME()) {
		StringList allowedHubs;
		ClientManager::getInstance()->getOnlineClients(allowedHubs);
		//no hubs? no fun...
		if (allowedHubs.empty()) {
			return;
		}
		AutoSearchPtr searchItem = nullptr;
		{
			RLock l(cs);
			searchItem = searchItems.findSearchItem(aTick);
		}
		if (searchItem)
			performSearch(searchItem, allowedHubs, TYPE_NORMAL, aTick);
	}

	if (dirty && (lastSave + (20 * 1000) < aTick)) { //20 second delay between saves.
		lastSave = aTick;
		dirty = false;
		save();
	}

}

void AutoSearchManager::on(TimerManagerListener::Minute, uint64_t /*aTick*/) noexcept {
	checkItems();
}

/* Scheduled searching */
void AutoSearchManager::checkItems() noexcept {
	AutoSearchList expired;
	bool hasStatusChange = false;
	AutoSearchList updateitems;

	{
		WLock l(cs);

		for(auto& as: searchItems.getItems() | map_values) {
			bool fireUpdate = false;
			auto aStatus = as->getStatus();

			//update possible priority change
			auto newPrio = as->calculatePriority();
			if(!as->isRecent() && newPrio != as->getPriority()) {
				searchItems.removeSearchPrio(as);
				as->setPriority(newPrio);
				searchItems.addSearchPrio(as);
			}

			
			//check expired, and remove them.
			if (aStatus != AutoSearch::STATUS_EXPIRED && as->expirationTimeReached() && as->getBundles().empty()) {
				expired.push_back(as);
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
					fireUpdate = true;
				}
			}

			if (fireUpdate || as->updateSearchTime() || as->getExpireTime() > 0)
				updateitems.push_back(as);

			if (aStatus != AutoSearch::STATUS_WAITING && as->getStatus() == AutoSearch::STATUS_WAITING)
				hasStatusChange = true;

		}
	}

	for_each(updateitems, [=](AutoSearchPtr as) { fire(AutoSearchManagerListener::UpdateItem(), as, false); });

	//One or more items were set to waiting state due to search times
	if(hasStatusChange)
		delayEvents.addEvent(RECALCULATE_SEARCH, [=] { resetSearchTimes(GET_TICK(), true); }, 1000);

	handleExpiredItems(expired);
}

/* SearchManagerListener and matching */
void AutoSearchManager::on(SearchManagerListener::SearchTypeRenamed, const string& oldName, const string& newName) noexcept {
	RLock l(cs);
	for(auto& as: searchItems.getItems() | map_values) {
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
		for(auto& as: searchItems.getItems() | map_values) {
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
				bool hasMatch = find_if(nicks, [&](const string& aNick) { return as->matchNick(aNick); }) != nicks.end();
				if((!as->getUserMatcherExclude() && !hasMatch) || (as->getUserMatcherExclude() && hasMatch))
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
				Search::TypeModes tmp;
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

		delayEvents.addEvent(as->getToken(), [this, as] { pickNameMatch(as); }, 2000);
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
			auto bundle = find_if(as->getBundles(), Bundle::HasStatus(Bundle::STATUS_FAILED_MISSING));
			dcassert(bundle != as->getBundles().end());
			minWantedSize = (*bundle)->getSize();
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
				auto paths = ShareManager::getInstance()->getNmdcDirPaths(dir);
				if (!paths.empty()) {
					as->setLastError(STRING_F(DIR_SHARED_ALREADY, paths.front()));
					fire(AutoSearchManagerListener::UpdateItem(), as, true);
					continue;
				}
			}

			//check queued
			if (as->getCheckAlreadyQueued() && as->getStatus() != AutoSearch::STATUS_FAILED_MISSING) {
				auto paths = QueueManager::getInstance()->getNmdcDirPaths(dir);
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

			// Do we have a bundle with the same name?
			{
				RLock l(cs);
				auto p = find_if(as->getBundles(), 
					[&](const BundlePtr& b) { return b->getName() == sr->getFileName(); });
				
				if (p != as->getBundles().end()) {
					// Use the same path
					target = Util::getParentDir((*p)->getTarget());
				}
			}

			DirectoryListingManager::getInstance()->addDirectoryDownload(sr->getPath(), sr->getFileName(), sr->getUser(), target,
				(as->getAction() == AutoSearch::ACTION_QUEUE) ? Priority::PAUSED : Priority::DEFAULT,
				false, as.get(), as->getRemove() || as->usingIncrementation(), false);
		} else {
			try {
				auto b = QueueManager::getInstance()->createFileBundle(as->getTarget() + sr->getFileName(), sr->getSize(), sr->getTTH(), 
					sr->getUser(), sr->getDate(), 0, 
					((as->getAction() == AutoSearch::ACTION_QUEUE) ? Priority::PAUSED : Priority::DEFAULT));

				if (b) {
					onBundleCreated(b, as.get());
				}
			} catch(const Exception& e) {
				onBundleError(as.get(), e.getError(), as->getTarget() + sr->getFileName(), sr->getUser());
				return;
			}
		}
	} else if (as->getAction() == AutoSearch::ACTION_REPORT) {
		ClientManager* cm = ClientManager::getInstance();
		{
			RLock l(cm->getCS());
			auto u = cm->findOnlineUser(sr->getUser());
			if (u) {
				auto client = u->getClient();
				if (client && client->isConnected()) {
					//TODO: use magnet link
					client->addLine(STRING(AUTO_SEARCH) + ": " + 
						STRING_F(AS_X_FOUND_FROM, Text::toLower(sr->getType() == SearchResult::TYPE_DIRECTORY ? STRING(FILE) : STRING(DIRECTORY)) % sr->getFileName() % u->getIdentity().getNick()));
				}

				if (as->getRemove()) {
					removeAutoSearch(as);
					logMessage(STRING_F(COMPLETE_ITEM_X_REMOVED, as->getSearchString()), LogMessage::SEV_INFO);
				}
			}
		}
	}
}

void AutoSearchManager::moveItemToGroup(AutoSearchPtr& as, const string& aGroup) {
	if ((aGroup.empty() && !as->getGroup().empty()) || hasGroup(aGroup)) {
		as->setGroup(aGroup);
		fire(AutoSearchManagerListener::UpdateItem(), as, false);
	}
}

int AutoSearchManager::getGroupIndex(const AutoSearchPtr& as) {
	RLock l(cs);
	int index = 0;
	if (!as->getGroup().empty()) {
		auto groupI = find(groups.begin(), groups.end(), as->getGroup());
		if (groupI != groups.end())
			index = static_cast<int>(groupI - groups.begin()) + 1;
	}
	return index;
}

/* Loading and saving */
void AutoSearchManager::save() noexcept {
	dirty = false;
	SimpleXML xml;

	xml.addTag("Autosearch");
	xml.stepIn();
	xml.addTag("Autosearch");
	xml.stepIn();
	{
		RLock l(cs);

		xml.addTag("Groups");
		xml.addChildAttrib("Version", XML_GROUPING_VERSION); //Reserve way to add preset groups when RSS is ready
		xml.stepIn();
		for (auto& i : groups) {
			xml.addTag("Group");
			xml.addChildAttrib("Name", i);
		}
		xml.stepOut();

		for (auto& as : searchItems.getItems() | map_values) {
			as->saveToXml(xml);
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
		
		aXml.resetCurrentChild();
		if (aXml.findChild("Groups")) {
			aXml.stepIn();
			while (aXml.findChild("Group")) {
				string name = aXml.getChildAttrib("Name");
				if (name.empty())
					continue;
				groups.push_back(name);
			}
			aXml.stepOut();
		} else {
			groups.push_back("Failed Bundles");
		}

		aXml.resetCurrentChild();
		while(aXml.findChild("Autosearch")) {
			auto as = loadItemFromXml(aXml);
			addAutoSearch(as, false, true);
			aXml.stepOut();
		}
		aXml.stepOut();
	}
}

AutoSearchPtr AutoSearchManager::loadItemFromXml(SimpleXML& aXml) {
	auto as = new AutoSearch(aXml.getBoolChildAttrib("Enabled"),
		aXml.getChildAttrib("SearchString"),
		aXml.getChildAttrib("FileType"),
		(AutoSearch::ActionType)aXml.getIntChildAttrib("Action"),
		aXml.getBoolChildAttrib("Remove"),
		aXml.getChildAttrib("Target"),
		(StringMatch::Method)aXml.getIntChildAttrib("MatcherType"),
		aXml.getChildAttrib("MatcherString"),
		aXml.getChildAttrib("UserMatch"),
		aXml.getIntChildAttrib("ExpireTime"),
		aXml.getBoolChildAttrib("CheckAlreadyQueued"),
		aXml.getBoolChildAttrib("CheckAlreadyShared"),
		aXml.getBoolChildAttrib("MatchFullPath"),
		aXml.getChildAttrib("ExcludedWords"),
		aXml.getIntChildAttrib("SearchInterval"),
		(AutoSearch::ItemType)aXml.getIntChildAttrib("ItemType"),
		aXml.getBoolChildAttrib("UserMatcherExclude"),
		aXml.getIntChildAttrib("Token"));

	as->setGroup(aXml.getChildAttrib("Group"));
	as->setExpireTime(aXml.getIntChildAttrib("ExpireTime"));
	as->setTimeAdded(aXml.getIntChildAttrib("TimeAdded"));

	auto searchDays = aXml.getChildAttrib("SearchDays");
	if (!searchDays.empty()) {
		as->searchDays = bitset<7>(searchDays);
	}
	else {
		as->searchDays = bitset<7>("1111111");
	}

	auto startTime = aXml.getChildAttrib("StartTime");
	if (!startTime.empty()) {
		as->startTime = SearchTime(startTime);
	} else {
		as->startTime = SearchTime();
	}

	auto endTime = aXml.getChildAttrib("EndTime");
	if (!endTime.empty()) {
		as->endTime = SearchTime(endTime);
	} else {
		as->endTime = SearchTime(true);
	}
	as->setLastSearch(aXml.getIntChildAttrib("LastSearchTime"));

	// LEGACY
	auto targetType = (TargetUtil::TargetType)aXml.getIntChildAttrib("TargetType");
	if (targetType > TargetUtil::TARGET_PATH) {
		TargetUtil::TargetInfo ti;
		TargetUtil::getVirtualTarget(as->getTarget(), targetType, ti);

		as->setTarget(ti.getTarget());
		logMessage("The target path of item " + as->getDisplayName() + " was changed to " + ti.getTarget() + " (auto selecting of paths isn't supported in this client version)", LogMessage::SEV_INFO);
	}

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
		while (aXml.findChild("Path")) {
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
		while (aXml.findChild("Bundle")) {
			aXml.stepIn();
			auto token = Util::toUInt32(aXml.getData());
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

	return as;
}

void AutoSearchManager::load() noexcept {
	try {
		SimpleXML xml;
		SettingsManager::loadSettingFile(xml, CONFIG_DIR, CONFIG_NAME);

		if(xml.findChild("Autosearch")) {
			xml.stepIn();
			loadAutoSearch(xml);
			xml.stepOut();
		}
		resetSearchTimes(GET_TICK(), true);
	} catch(const Exception& e) {
		LogManager::getInstance()->message(STRING_F(LOAD_FAILED_X, CONFIG_NAME % e.getError()), LogMessage::SEV_ERROR);
	}
}
}