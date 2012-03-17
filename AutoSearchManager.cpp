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

#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm_ext/for_each.hpp>

namespace dcpp {

using boost::range::for_each;


AutoSearch::AutoSearch(bool aEnabled, const string& aSearchString, SearchManager::TypeModes aFileType, ActionType aAction, bool aRemove, const string& aTarget, TargetType aTargetType, 
	StringMatcher::Type aMatcherType, const string& aMatcherString, const string& aUserMatch, int aSearchInterval, time_t aExpireTime) noexcept : 
	enabled(aEnabled), searchString(aSearchString), fileType(aFileType), action(aAction), remove(aRemove), target(aTarget), tType(aTargetType), 
		searchInterval(aSearchInterval), expireTime(aExpireTime) {

	string matchPattern = aMatcherString;
	if (aMatcherString.empty())
		matchPattern = aSearchString;

	if (aFileType == SearchManager::TYPE_TTH)
		resultMatcher = new TTHMatcher(matchPattern);
	else if (aMatcherType == StringMatcher::MATCHER_STRING)
		resultMatcher = new TokenMatcher(matchPattern);
	else if (aMatcherType == StringMatcher::MATCHER_REGEX)
		resultMatcher = new RegExMatcher(matchPattern);
	else if (aMatcherType == StringMatcher::MATCHER_WILDCARD)
		resultMatcher = new WildcardMatcher(matchPattern);

	userMatcher = new WildcardMatcher(aUserMatch);
};

AutoSearch::~AutoSearch() { 
	delete resultMatcher;
	delete userMatcher;
};

AutoSearchManager::AutoSearchManager() : lastSave(0), dirty(false) {
	TimerManager::getInstance()->addListener(this);
	SearchManager::getInstance()->addListener(this);
}

AutoSearchManager::~AutoSearchManager() {
	SearchManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
}

AutoSearchPtr AutoSearchManager::addAutoSearch(const string& ss, const string& aTarget, AutoSearch::TargetType aTargetType) {
	auto as = new AutoSearch(true, aTarget, SearchManager::TYPE_DIRECTORY, AutoSearch::ACTION_DOWNLOAD, true, Util::emptyString, AutoSearch::TARGET_PATH, 
		StringMatcher::MATCHER_STRING, Util::emptyString, Util::emptyString, 0, SETTING(AUTOSEARCH_EXPIRE_DAYS) > 0 ? GET_TIME() + (SETTING(AUTOSEARCH_EXPIRE_DAYS)*24*60*60) : 0);

	if (addAutoSearch(as))
		return as;
	else 
		return nullptr;
}

bool AutoSearchManager::addAutoSearch(AutoSearchPtr aAutoSearch) {
	{
		WLock l(cs);
		if (find(searchItems.begin(), searchItems.end(), aAutoSearch) != searchItems.end())
			return false;
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
		fire(AutoSearchManagerListener::RemoveItem(), aItem->getSearchString());
		searchItems.erase(i);
		dirty = true;
	}
}

void AutoSearchManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	if(dirty && (lastSave + (20*1000) > aTick)) { //20 second delay between saves.
		lastSave = aTick;
		dirty = false;
		AutoSearchSave();
	}
}

void AutoSearchManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	checkSearches(false, aTick);
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

	AutoSearchList searches;
	AutoSearchList expired;
	{
		RLock l (cs);
		for(auto i = searchItems.begin(); i != searchItems.end(); ++i) {
			AutoSearchPtr as = *i;
			//check expired
			if (as->getExpireTime() > 0 && as->getExpireTime() < curTime) {
				expired.push_back(as);
				continue;
			}

			if (!as->getEnabled())
				continue;
			//check the weekday
			if (!as->searchDays[_tm.tm_wday])
				continue;
			//check the hours
			if (!(as->startTime.hour < _tm.tm_hour && as->endTime.hour > _tm.tm_hour))
				continue;
			//check the minutes
			if ((as->endTime.hour || as->startTime.hour) == _tm.tm_hour) {
				if (!(as->startTime.minute < _tm.tm_min) || (as->endTime.minute > _tm.tm_min))
					continue;
			}
			//check the interval
			if (((as->getLastSearch() + (as->getSearchInterval() > 0 ? as->getSearchInterval() : SETTING(AUTOSEARCH_EVERY))*1000*60 > curTime) && !force))
				continue;

			as->setLastSearch(curTime);
			fire(AutoSearchManagerListener::UpdateItem(), as, distance(searchItems.begin(), i));
			searches.push_back(as);
		}
	}

	uint64_t searchTime = 0;
	StringList extList;

	for_each(searches, [&](AutoSearchPtr as) { 
		// TODO: Get ADC searchtype extensions if any is selected
		searchTime = SearchManager::getInstance()->search(allowedHubs, as->getSearchString(), 0, as->getFileType(), SearchManager::SIZE_DONTCARE, "as", extList, Search::AUTO_SEARCH);
	});

	for_each(expired, [&](AutoSearchPtr as) {
		LogManager::getInstance()->message("An expired autosearch has been removed: " + as->getPattern());
		removeAutoSearch(as);
	});

	if (searches.size() == 1) {
		if (searchTime == 0) {
			LogManager::getInstance()->message(str(boost::format("Autosearch: %s has been searched for") %
				searches.front()->getSearchString()));
		} else {
			LogManager::getInstance()->message(str(boost::format("Autosearch: %s will be searched in %d seconds") %
				searches.front()->getSearchString() %
				(searchTime / 1000)));
		}
	} else if (searches.size() > 1) {
		LogManager::getInstance()->message(str(boost::format("Autosearch: %s searches have been queued and will be completed in %d seconds") %
			Util::toString(searches.size()) %
			(searchTime / 1000)));
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
			if (!as->getEnabled())
				continue;

			//match
			if (as->getMatcherType() == StringMatcher::MATCHER_TTH) {
				if (!as->match(sr->getTTH()))
					continue;
			} else {
				/* Check the type */
				if(as->getFileType() == SearchManager::TYPE_DIRECTORY ) {
					if (sr->getType() != SearchResult::TYPE_DIRECTORY)
						continue;
				} else if(!ShareManager::getInstance()->checkType(sr->getFile(), (*i)->getFileType())) {
					continue;
				}

				if (!as->match(sr->getType() == SearchResult::TYPE_DIRECTORY ? Util::getLastDir(sr->getFile()) : sr->getFileName()))
					continue;
			}

			//check the nick
			if(!as->getNickPattern().empty()) {
				StringList nicks = ClientManager::getInstance()->getNicks(sr->getUser()->getCID(), sr->getHubURL());
				if (find_if(nicks.begin(), nicks.end(), [&](const string& aNick) { return as->matchNick(aNick); }) != nicks.end())
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
		string path;
		auto freeSpace = getTarget(as->getTarget(), as->getTargetType(), path);
		if (freeSpace < sr->getSize()) {
			//not enough space, do something fun
			LogManager::getInstance()->message("AutoSearch: Not enough free space left on the target path " + as->getTarget() + ", free space: " + Util::formatBytes(freeSpace) + 
				" while " + Util::formatBytes(sr->getSize()) + "is needed. Adding to queue with paused Priority.");
			as->setAction(AutoSearch::ACTION_QUEUE);
		}

		try {
			if(sr->getType() == SearchResult::TYPE_DIRECTORY) {
				QueueManager::getInstance()->addDirectory(sr->getFile(), HintedUser(sr->getUser(), sr->getHubURL()), path, 
					as->getAction() == AutoSearch::ACTION_QUEUE ? QueueItem::PAUSED : QueueItem::DEFAULT);
			} else {
				path = path + Util::getFileName(sr->getFile());
				QueueManager::getInstance()->add(path, sr->getSize(), sr->getTTH(), HintedUser(sr->getUser(), sr->getHubURL()), 0, true, 
					(as->getAction() == AutoSearch::ACTION_QUEUE ? QueueItem::PAUSED : QueueItem::DEFAULT));
			}
		} catch(const QueueException& e) {
			LogManager::getInstance()->message("AutoSearch failed to queue " + sr->getFileName() + " (" + e.getError() + ")");
			return;
		}
	} else if (as->getAction() == AutoSearch::ACTION_REPORT) {
		ClientManager* c = ClientManager::getInstance();
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

int64_t AutoSearchManager::getTarget(const string& aTarget, AutoSearch::TargetType targetType, string& newTarget) {
	int64_t freeSpace = 0;
	if (targetType == AutoSearch::TARGET_PATH) {
		newTarget = aTarget;
	} else {
		vector<pair<string, StringList>> dirList;
		if (targetType == AutoSearch::TARGET_FAVORITE) {
			dirList = FavoriteManager::getInstance()->getFavoriteDirs();
		} else {
			ShareManager::getInstance()->LockRead();
			dirList = ShareManager::getInstance()->getGroupedDirectories();
			ShareManager::getInstance()->unLockRead();
		}

		auto s = find_if(dirList.begin(), dirList.end(), CompareFirst<string, StringList>(aTarget));
		if (s != dirList.end()) {
			StringList& targets = s->second;
			AirUtil::getTarget(targets, newTarget, freeSpace);
			if (!newTarget.empty()) {
				return freeSpace;
			}
		}
	}

	if (newTarget.empty()) {
		//failed to get the target, use the default one
		newTarget = SETTING(DOWNLOAD_DIRECTORY);
	}
	AirUtil::getDiskInfo(newTarget, freeSpace);
	return freeSpace;
}

void AutoSearchManager::AutoSearchSave() {
	try {
		dirty = false;
		SimpleXML xml;

		xml.addTag("Autosearch");
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
				xml.addChildAttrib("MatcherType", as->getMatcherType()),
				xml.addChildAttrib("MatcherString", as->getPattern()),
				xml.addChildAttrib("SearchInterval", as->getSearchInterval()),
				xml.addChildAttrib("UserMatch", (*i)->getNickPattern());
				xml.addChildAttrib("ExpireTime", (*i)->getExpireTime());
				xml.addChildAttrib("SearchDays", (*i)->searchDays.to_string());
				xml.addChildAttrib("StartTime", (*i)->startTime.toString());
				xml.addChildAttrib("EndTime", (*i)->endTime.toString());
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
				(SearchManager::TypeModes)aXml.getIntChildAttrib("FileType"), 
				(AutoSearch::ActionType)aXml.getIntChildAttrib("Action"),
				aXml.getBoolChildAttrib("Remove"),
				aXml.getChildAttrib("Target"),
				(AutoSearch::TargetType)aXml.getIntChildAttrib("TargetType"),
				(StringMatcher::Type)aXml.getIntChildAttrib("MatcherType"),
				aXml.getChildAttrib("MatcherString"),
				aXml.getChildAttrib("UserMatch"),
				aXml.getIntChildAttrib("SearchInterval"),
				aXml.getIntChildAttrib("ExpireTime"));

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
			xml.stepIn();
			loadAutoSearch(xml);
			xml.stepOut();
		}
	} catch(const Exception& e) {
		dcdebug("AutoSearchManager::load: %s\n", e.getError().c_str());
	}	
}
}