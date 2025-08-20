/*
 * Copyright (C) 2011-2024 AirDC++ Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/util/DupeUtil.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/queue/QueueManager.h>
#include <airdcpp/search/SearchManager.h>
#include <airdcpp/search/SearchQuery.h>
#include <airdcpp/search/SearchResult.h>
#include <airdcpp/search/SearchTypes.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/core/io/xml/SimpleXML.h>
#include <airdcpp/user/User.h>

#include <airdcpp/filelist/DirectoryListingManager.h>

namespace dcpp {

using ranges::find_if;
using ranges::max_element;
using ranges::copy_if;

#define CONFIG_DIR AppUtil::PATH_USER_CONFIG
#define CONFIG_NAME "AutoSearch.xml"
#define XML_GROUPING_VERSION 1

AutoSearchManager::AutoSearchManager() noexcept
{
	TimerManager::getInstance()->addListener(this);
	SearchManager::getInstance()->addListener(this);
	DirectoryListingManager::getInstance()->addListener(this);
}

AutoSearchManager::~AutoSearchManager() noexcept {
	SearchManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);
	DirectoryListingManager::getInstance()->removeListener(this);
}

void AutoSearchManager::logMessage(const string& aMsg, LogMessage::Severity aSeverity) const noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(AUTO_SEARCH));
}

/* Adding new items for external use */
AutoSearchPtr AutoSearchManager::addAutoSearch(const string& ss, const string& aTarget, bool isDirectory, AutoSearch::ItemType asType, bool aRemove, bool aSearch, int aExpriredays) noexcept {
	if (!validateAutoSearchStr(ss)) {
		return nullptr;
	}

	if (aExpriredays == 0)
		aExpriredays = SETTING(AUTOSEARCH_EXPIRE_DAYS);

	time_t expireTime = aExpriredays > 0 ? GET_TIME() + aExpriredays * 24 * 60 * 60 : 0;
	
	AutoSearchPtr as = std::make_shared<AutoSearch>(true, ss, isDirectory ? SEARCH_TYPE_DIRECTORY : SEARCH_TYPE_FILE, AutoSearch::ACTION_DOWNLOAD, aRemove, aTarget, 
		StringMatch::PARTIAL, Util::emptyString, Util::emptyString, expireTime, false, false, false, Util::emptyString, asType, false);

	addAutoSearch(as, aSearch);
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
	fire(AutoSearchManagerListener::ItemAdded(), aAutoSearch);
	if (search) {
		if (!searchItem(aAutoSearch, TYPE_NEW)) {
			//no hubs
			logMessage(CSTRING_F(AUTOSEARCH_ADDED, aAutoSearch->getSearchString()), LogMessage::SEV_INFO);
		}
	} 
	if(!loading) {
		delayEvents.addEvent(RECALCULATE_SEARCH, [this] { resetSearchTimes(GET_TICK()); }, 1000);
	}
}

bool AutoSearchManager::validateAutoSearchStr(const string& aStr) const noexcept {
	if (aStr.length() <= 5) {
		logMessage(STRING_F(AUTOSEARCH_ADD_FAILED, aStr % STRING(LINE_EMPTY_OR_TOO_SHORT)), LogMessage::SEV_ERROR);
		return false;
	}

	auto lst = getSearchesByString(aStr);
	if (!lst.empty()) {
		logMessage(STRING_F(AUTOSEARCH_ADD_FAILED, aStr % STRING(ITEM_NAME_EXISTS)), LogMessage::SEV_ERROR);
		return false;
	}
	return true;
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

	//No items enabled at this time? Schedule search for it...
	if(toActive && nextSearch == 0 && (as->getLastSearch() + SETTING(AUTOSEARCH_EVERY) * 60 < GET_TIME()))
		delayEvents.addEvent(SEARCH_ITEM, [this] { maybePopSearchItem(GET_TICK(), true); }, 1000);

	delayEvents.addEvent(RECALCULATE_SEARCH, [this] { resetSearchTimes(GET_TICK()); }, 1000);
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

	delayEvents.addEvent(RECALCULATE_SEARCH, [this] { resetSearchTimes(GET_TICK()); }, 1000);
	//if (find_if(searchItems, [ipw](const AutoSearchPtr as) { return as->getSearchString() == ipw->getSearchString() && compare(ipw->getToken(), as->getToken()) != 0; }) != searchItems.end())
	//	return false;
	fire(AutoSearchManagerListener::ItemUpdated(), ipw, true);
	dirty = true;
	return true;
}

void AutoSearchManager::updateStatus(AutoSearchPtr& as, bool setTabDirty) noexcept {
	as->updateStatus();
	fire(AutoSearchManagerListener::ItemUpdated(), as, setTabDirty);
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
			fire(AutoSearchManagerListener::ItemRemoved(), aItem);
			searchItems.removeItem(aItem);
			dirty = true;
		}
	}
	if(hasItem)
		delayEvents.addEvent(RECALCULATE_SEARCH, [=] { resetSearchTimes(GET_TICK()); }, 1000);
}

AutoSearchList AutoSearchManager::getSearchesByBundle(const BundlePtr& aBundle) const noexcept{
	AutoSearchList ret;

	RLock l(cs);
	copy_if(searchItems.getItems() | views::values, back_inserter(ret), [&](const AutoSearchPtr& as) { return as->hasBundle(aBundle); });
	return ret;
}

AutoSearchList AutoSearchManager::getSearchesByString(const string& aSearchString, const AutoSearchPtr& ignoredSearch) const noexcept{
	AutoSearchList ret;

	RLock l(cs);
	copy_if(searchItems.getItems() | views::values, back_inserter(ret), [&](const AutoSearchPtr& as) { return as->getSearchString() == aSearchString && (!ignoredSearch || as != ignoredSearch); });
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

	fire(AutoSearchManagerListener::ItemUpdated(), as, true);
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
				} else if (b->getStatus() == Bundle::STATUS_VALIDATION_ERROR) {
					statusString += b->getName() + " (" + b->getError() + ")";
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
	fire(AutoSearchManagerListener::ItemUpdated(), as, true);
}

void AutoSearchManager::on(DirectoryListingManagerListener::DirectoryDownloadProcessed, const DirectoryDownloadPtr& aDirectoryInfo, const DirectoryBundleAddResult& aQueueInfo, const string& /*aError*/) noexcept {
	onBundleCreated(aQueueInfo.bundleInfo.bundle, aDirectoryInfo->getOwner());
}

void AutoSearchManager::on(DirectoryDownloadFailed, const DirectoryDownloadPtr& aDirectoryInfo, const string& aError) noexcept {
	onBundleError(aDirectoryInfo->getOwner(), aError, aDirectoryInfo->getBundleName(), aDirectoryInfo->getUser());
}

void AutoSearchManager::onBundleCreated(const BundlePtr& aBundle, const void* aSearch) noexcept {
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

	if (found) {
		delayEvents.addEvent(RECALCULATE_SEARCH, [=] { resetSearchTimes(GET_TICK()); }, 1000);
	}
}

void AutoSearchManager::onBundleError(const void* aSearch, const string& aError, const string& aBundleName, const HintedUser& aUser) noexcept {
	RLock l(cs);
	auto as = searchItems.getItem(aSearch);
	if (as) {
		as->setLastError(STRING_F(AS_ERROR, aBundleName % aError % Util::formatCurrentTime() % ClientManager::getInstance()->getFormattedNicks(aUser)));
		fire(AutoSearchManagerListener::ItemUpdated(), as, true);
	}

	//logMessage(aError, true);
}

void AutoSearchManager::on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept {
	if (aBundle->isCompleted()) {
		onRemoveBundle(aBundle, true);
		return;
	}

	auto filesMissing = AutoSearch::hasHookFilesMissing(aBundle->getHookError());
	auto items = getSearchesByBundle(aBundle);
	bool found = false, searched = false;
	for (auto& as : items) {
		if (!as->hasBundle(aBundle)) {
			continue;
		}

		found = true;

		{
			RLock l(cs);
			updateStatus(as, true);
		}

		if (!searched && filesMissing) {
			searchItem(as, TYPE_NORMAL);
			searched = true;
		}
	}

	if (filesMissing && !found && SETTING(AUTO_COMPLETE_BUNDLES)) {
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
				fire(AutoSearchManagerListener::ItemUpdated(), as, true);
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
		delayEvents.addEvent(RECALCULATE_SEARCH, [=] { resetSearchTimes(GET_TICK());  }, 2000);
	
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
			fire(AutoSearchManagerListener::ItemUpdated(), as, false);
		}
	}
}

bool AutoSearchManager::addFailedBundle(const BundlePtr& aBundle) noexcept {
	auto lst = getSearchesByString(aBundle->getName());
	if (!lst.empty()) {
		return false;
	}

	//allow adding only release dirs, avoid adding too common bundle names to auto search ( will result in bundle growing by pretty much anything that matches... )
	if (!DupeUtil::isRelease(aBundle->getName()))
		return false;


	//7 days expiry
	auto as = std::make_shared<AutoSearch>(true, aBundle->getName(), SEARCH_TYPE_DIRECTORY, AutoSearch::ACTION_DOWNLOAD, true, PathUtil::getParentDir(aBundle->getTarget()), 
		StringMatch::EXACT, Util::emptyString, Util::emptyString, GET_TIME() + 7*24*60*60, false, false, false, Util::emptyString, AutoSearch::FAILED_BUNDLE, false);

	as->setGroup(SETTING(AS_FAILED_DEFAULT_GROUP));
	as->addBundle(aBundle);
	addAutoSearch(as, aBundle->isRecent());
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
		string name;
		auto& typeManager = SearchManager::getInstance()->getSearchTypes();
		typeManager.getSearchType(as->getFileType(), ftype, extList, name);
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
			auto p = find_if(as->getBundles(), Bundle::HasStatus(Bundle::STATUS_VALIDATION_ERROR));
			if (p != as->getBundles().end()) {
				searchWord = (*p)->getName();
				failedBundle = true;
			}
		}
	}

	if (!failedBundle)
		searchWord = as->getFormatedSearchString();

	if ((aType == TYPE_MANUAL_BG || aType == TYPE_MANUAL_FG) && !as->getEnabled()) {
		as->setManualSearch(true);
		as->setStatus(AutoSearch::STATUS_MANUAL);
	}
	
	//Run the search
	if (aType == TYPE_MANUAL_FG) {
		as->setLastSearch(GET_TIME());
		fire(AutoSearchManagerListener::SearchForeground(), as, searchWord);
	} else if (!ClientManager::getInstance()->hasSearchQueueOverflow()) {
		auto s = make_shared<Search>(aType == TYPE_MANUAL_BG ? Priority::NORMAL : Priority::LOWEST, "as");
		s->query = searchWord;
		s->fileType = ftype;
		s->exts = extList;
		s->excluded = SearchQuery::parseSearchString(as->getExcludedString());

		auto searchInfo = SearchManager::getInstance()->search(aHubs, s);
		if (!searchInfo.queuedHubUrls.empty()) {
			as->setLastSearch(GET_TIME()); //set the item as searched only when we actually were able to queue a search for it.
			string msg;
			//Report
			if (searchInfo.queueTime == 0) {
				if (failedBundle) {
					msg = STRING_F(FAILED_BUNDLE_SEARCHED, searchWord);
				}
				else if (aType == TYPE_NEW) {
					msg = CSTRING_F(AUTOSEARCH_ADDED_SEARCHED, searchWord);
				}
				else {
					msg = as->isRecent() ? STRING_F(ITEM_SEARCHED_RECENT, searchWord) : STRING_F(ITEM_SEARCHED, searchWord);
				}
			} else {
				auto time = searchInfo.queueTime / 1000;
				if (failedBundle) {
					msg = STRING_F(FAILED_BUNDLE_SEARCHED_IN, searchWord % time);
				}
				else if (aType == TYPE_NEW) {
					msg = CSTRING_F(AUTOSEARCH_ADDED_SEARCHED_IN, searchWord % time);
				}
				else {
					msg = as->isRecent() ? STRING_F(ITEM_SEARCHED_IN_RECENT, searchWord % time) : STRING_F(ITEM_SEARCHED_IN, searchWord % time);
				}
			}
			fire(AutoSearchManagerListener::ItemSearched(), as, msg);
			logMessage(msg, LogMessage::SEV_INFO);
		}
	}

	resetSearchTimes(aTick, false);
	fire(AutoSearchManagerListener::ItemUpdated(), as, false);
}
void AutoSearchManager::resetSearchTimes(uint64_t aTick, bool aRecalculate/* = true*/) noexcept {
	RLock l(cs);
	if (searchItems.getItems().empty()) {
		nextSearch = 0;
		return;
	}
	auto recentSearchTick = searchItems.getNextSearchRecent();
	auto nextSearchTick = searchItems.getNextSearchNormal();

	if (aRecalculate) {
		nextSearchTick = searchItems.recalculateSearchTimes(false, false, aTick);
		recentSearchTick = searchItems.recalculateSearchTimes(true, false, aTick);
	}
	nextSearchTick = recentSearchTick > 0 ? nextSearchTick > 0 ? min(recentSearchTick, nextSearchTick) : recentSearchTick : nextSearchTick;

	nextSearch = toTimeT(nextSearchTick, aTick);
	
}

void AutoSearchManager::maybePopSearchItem(uint64_t aTick, bool aIgnoreSearchTimes) {
	if (!aIgnoreSearchTimes && (nextSearch == 0 || nextSearch > GET_TIME())) {
		return;
	}

	if (ClientManager::getInstance()->hasSearchQueueOverflow())
		return;

	StringList allowedHubs;
	ClientManager::getInstance()->getOnlineClients(allowedHubs);
	//no hubs? no fun...
	if (allowedHubs.empty())
		return;

	AutoSearchPtr searchItem = nullptr;
	{
		WLock l(cs);
		searchItem = searchItems.maybePopSearchItem(aTick, aIgnoreSearchTimes);
	}

	if (searchItem) {
		dcdebug("Auto search for %s (priority %d)\n", searchItem->getSearchString().c_str(), static_cast<int>(searchItem->getPriority()));
		performSearch(searchItem, allowedHubs, TYPE_NORMAL, aTick);
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
	
	maybePopSearchItem(aTick, false);

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
	int itemsEnabled = 0;
	int itemsDisabled = 0;
	bool hasPrioChange = false;

	AutoSearchList updateitems;

	{
		WLock l(cs);

		for (const auto& as: searchItems.getItems() | views::values) {
			bool fireUpdate = false;

			//update possible priority change
			auto newPrio = as->calculatePriority();
			if(!as->isRecent() && newPrio != as->getPriority()) {
				dcdebug("Auto search %s, priority change from %d to %d \n", as->getSearchString().c_str(), as->getPriority(), newPrio);
				searchItems.removeSearchPrio(as);
				as->setPriority(newPrio);
				searchItems.addSearchPrio(as);
				hasPrioChange = true;
			}

			// check post search items and whether we can change the number
			if (as->removePostSearch()) {
				if (as->maxNumberReached()) {
					expired.push_back(as);
					continue;
				}
				dirty = true;
				as->changeNumber(true);
				as->updateStatus();
				fireUpdate = true;
			}
			
			//check expired, and remove them.
			if (as->getStatus() != AutoSearch::STATUS_EXPIRED && as->getBundles().empty() && 
				(as->expirationTimeReached() || (as->usingIncrementation() && as->getMaxNumber() > 0 && as->getCurNumber() > as->getMaxNumber()))) {
				expired.push_back(as);
				continue;
			}

			//Check if the item gets enabled from search time limits...
			bool aOldSearchTimeEnabled = as->allowNewItems() && as->nextAllowedSearch() == 0;

			if (as->updateSearchTime() || as->getExpireTime() > 0 || fireUpdate)
				updateitems.push_back(as);

			bool aNewSearchTimeEnabled = as->allowNewItems() && as->nextAllowedSearch() == 0;

			if (!aOldSearchTimeEnabled && aNewSearchTimeEnabled)
				itemsEnabled++;

			if (aOldSearchTimeEnabled && !aNewSearchTimeEnabled)
				itemsDisabled++;
		}
	}

	ranges::for_each(updateitems, [=](const AutoSearchPtr& as) { fire(AutoSearchManagerListener::ItemUpdated(), as, false); });

	//No other enabled searches, start searching for the newly enabled immediately...
	if (itemsEnabled > 0 && nextSearch == 0) {
		maybePopSearchItem(GET_TICK(), true);
	}

	if(hasPrioChange || itemsEnabled > 0 || itemsDisabled > 0)
		delayEvents.addEvent(RECALCULATE_SEARCH, [=] { resetSearchTimes(GET_TICK()); }, 2000);

	handleExpiredItems(expired);
}

AutoSearchList AutoSearchManager::matchResult(const SearchResultPtr& sr) noexcept {
	AutoSearchList matches;

	RLock l (cs);
	for(auto& as: searchItems.getItems() | views::values) {
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
			if(as->getFileType() == SEARCH_TYPE_DIRECTORY && sr->getType() != SearchResult::Type::DIRECTORY) {
				continue;
			} else if (as->getFileType() == SEARCH_TYPE_FILE && sr->getType() != SearchResult::Type::FILE) {
				continue;
			}

			if (as->getMatchFullPath()) {
				if (!as->match(sr->getAdcPath()))
					continue;
				if (as->isExcluded(sr->getAdcPath()))
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

	return matches;
}

/* SearchManagerListener and matching */
void AutoSearchManager::on(SearchManagerListener::SR, const SearchResultPtr& sr) noexcept {
	//don't match bundle searches
	if (Util::stricmp(sr->getSearchToken(), "qa") == 0)
		return;

	auto matches = matchResult(sr);

	//extra checks outside the lock
	for (auto& as: matches) {
		if (!SearchTypes::isDefaultTypeStr(as->getFileType())) {
			if (sr->getType() == SearchResult::Type::DIRECTORY)
				continue;

			//check the extension
			try {
				StringList exts;

				{
					string typeName;
					Search::TypeModes tmp;
					auto& typeManager = SearchManager::getInstance()->getSearchTypes();
					typeManager.getSearchType(as->getFileType(), tmp, exts, typeName);
				}

				auto fileName = sr->getFileName();

				//match
				auto p = find_if(exts, [&fileName](const string& i) { 
					return fileName.length() >= i.length() && Util::stricmp(fileName.c_str() + fileName.length() - i.length(), i.c_str()) == 0;
				});
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
				fire(AutoSearchManagerListener::ItemUpdated(), as, false);
			} else if (find_if(rl, [&sr](const SearchResultPtr& aSR) { return aSR->getUser() == sr->getUser() && aSR->getAdcPath() == sr->getAdcPath(); }) != rl.end()) {
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
			auto bundle = find_if(as->getBundles(), Bundle::HasStatus(Bundle::STATUS_VALIDATION_ERROR));
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
		auto p = find_if(dirList | views::keys, [](const string& s) { return s.find("proper") != string::npos; }).base();
		if (p == dirList.end()) {
			p = find_if(dirList | views::keys, [](const string& s) { return s.find("repack") != string::npos; }).base();
		}

		if (p == dirList.end()) {
			if (as->getStatus() == AutoSearch::STATUS_POSTSEARCH) //don't download anything
				return;

			// no repack or proper, pick the one with most matches
			p = ranges::max_element(dirList | views::values, [](const SearchResultList& p1, const SearchResultList& p2) { return p1.size() < p2.size(); }).base();
		}

		// only download this directory
		decltype(dirList) dirListTmp;
		dirListTmp.insert(*p);
		dirList.swap(dirListTmp);
	}

	for (auto& [dir, resultList] : dirList) {
		// dupe check
		if (as->getFileType() == SEARCH_TYPE_DIRECTORY) {
			//check shared
			if (as->getCheckAlreadyShared()) {
				auto paths = ShareManager::getInstance()->getAdcDirectoryDupePaths(dir);
				if (!paths.empty()) {
					as->setLastError(STRING_F(DIR_SHARED_ALREADY, paths.front()));
					fire(AutoSearchManagerListener::ItemUpdated(), as, true);
					continue;
				}
			}

			//check queued
			if (as->getCheckAlreadyQueued() && as->getStatus() != AutoSearch::STATUS_FAILED_MISSING) {
				auto paths = QueueManager::getInstance()->getAdcDirectoryDupePaths(dir);
				if (!paths.empty()) {
					as->setLastError(STRING_F(DIR_QUEUED_ALREADY, dir));
					fire(AutoSearchManagerListener::ItemUpdated(), as, true);
					continue;
				}
			}
		}

		downloadList(resultList, as, minWantedSize);
	}
}

void AutoSearchManager::downloadList(SearchResultList& srl, AutoSearchPtr& as, int64_t minWantedSize) noexcept{
	auto getDownloadSize = [&srl](int64_t minSize) {
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
	auto dlSize = getDownloadSize(minWantedSize);
	if (dlSize == -1) {
		//no bigger items found
		dlSize = getDownloadSize(-1);
		if (minWantedSize == dlSize) {
			//no need to match an identical bundle again
			return;
		}
	}

	dcassert(dlSize > -1);

	//download matches with the given size
	std::erase_if(srl, [dlSize](const SearchResultPtr& aSR) { return aSR->getSize() != dlSize; });
	SearchResult::pickResults(srl, SETTING(MAX_AUTO_MATCH_SOURCES));

	for (const auto& sr : srl) {
		handleAction(sr, as);
	}
}

void AutoSearchManager::handleAction(const SearchResultPtr& sr, AutoSearchPtr& as) noexcept {
	if (as->getAction() == AutoSearch::ACTION_QUEUE || as->getAction() == AutoSearch::ACTION_DOWNLOAD) {
		try {
			if (sr->getType() == SearchResult::Type::DIRECTORY) {
				if ((as->getRemove() || as->usingIncrementation()) && DirectoryListingManager::getInstance()->hasDirectoryDownload(sr->getFileName(), as.get())) {
					return;
				}

				auto listData = FilelistAddData(sr->getUser(), as.get(), sr->getAdcFilePath());
				auto priority = as->getAction() == AutoSearch::ACTION_QUEUE ? Priority::PAUSED : Priority::DEFAULT;
				DirectoryListingManager::getInstance()->addDirectoryDownloadHookedThrow(listData, sr->getFileName(), as->getTarget(), priority, DirectoryDownload::ErrorMethod::NONE);
			} else {
				auto prio = as->getAction() == AutoSearch::ACTION_QUEUE ? Priority::PAUSED : Priority::DEFAULT;
				auto fileInfo = BundleFileAddData(sr->getFileName(), sr->getTTH(), sr->getSize(), prio, sr->getDate());
				auto options = BundleAddOptions(as->getTarget(), sr->getUser(), as.get());
				auto result = QueueManager::getInstance()->createFileBundleHooked(options, fileInfo);

				onBundleCreated(result.bundle, as.get());
			}
		} catch (const Exception& e) {
			onBundleError(as.get(), e.getError(), sr->getFileName(), sr->getUser());
			return;
		}
	} else if (as->getAction() == AutoSearch::ACTION_REPORT) {
		auto u = ClientManager::getInstance()->findOnlineUser(sr->getUser());
		if (u) {
			auto client = u->getClient();
			if (client && client->isConnected()) {
				//TODO: use magnet link
				client->statusMessage(STRING(AUTO_SEARCH) + ": " +
					STRING_F(AS_X_FOUND_FROM, Text::toLower(sr->getType() == SearchResult::Type::DIRECTORY ? STRING(FILE) : STRING(DIRECTORY)) % sr->getFileName() % u->getIdentity().getNick()), LogMessage::SEV_INFO);
			}

			if (as->getRemove()) {
				removeAutoSearch(as);
				logMessage(STRING_F(COMPLETE_ITEM_X_REMOVED, as->getSearchString()), LogMessage::SEV_INFO);
			}
		}
	}
}

void AutoSearchManager::moveItemToGroup(AutoSearchPtr& as, const string& aGroup) {
	if ((aGroup.empty() && !as->getGroup().empty()) || hasGroup(aGroup)) {
		as->setGroup(aGroup);
		fire(AutoSearchManagerListener::ItemUpdated(), as, false);
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

		for (auto& as : searchItems.getItems() | views::values) {
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
			groups.emplace_back("Failed Bundles");
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
	auto as = std::make_shared<AutoSearch>(aXml.getBoolChildAttrib("Enabled"),
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
		(AutoSearch::ItemType)aXml.getIntChildAttrib("ItemType"),
		aXml.getBoolChildAttrib("UserMatcherExclude"),
		aXml.getIntChildAttrib("Token"));

	as->setGroup(aXml.getChildAttrib("Group"));
	as->setExpireTime(aXml.getTimeChildAttrib("ExpireTime"));
	as->setTimeAdded(aXml.getTimeChildAttrib("TimeAdded"));

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
	as->setLastSearch(aXml.getTimeChildAttrib("LastSearchTime"));

	if (as->getTarget().empty()) {
		as->setTarget(SETTING(DOWNLOAD_DIRECTORY));
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
			auto time = aXml.getTimeChildAttrib("FinishTime");
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

	as->checkRecent();
	as->setPriority(as->calculatePriority());

	return as;
}

void AutoSearchManager::load() noexcept {
	SettingsManager::loadSettingFile(CONFIG_DIR, CONFIG_NAME, [this](SimpleXML& xml) {
		if(xml.findChild("Autosearch")) {
			xml.stepIn();
			loadAutoSearch(xml);
			xml.stepOut();
		}
	});

	//Start listening after queue has loaded, avoids adding duplicate failed items.
	QueueManager::getInstance()->addListener(this);
	resetSearchTimes(GET_TICK());
}

time_t AutoSearchManager::toTimeT(uint64_t aValue, uint64_t currentTick/* = GET_TICK()*/) {
	if (aValue == 0)
		return 0;

	if (currentTick >= aValue)
		return GET_TIME();


	return GET_TIME() + ((aValue - currentTick) / 1000);
}


}