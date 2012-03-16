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
	StringMatcher::Type aMatcherType, const string& aMatcherString, const string& aUserMatch, int aSearchInterval) noexcept : enabled(aEnabled), searchString(aSearchString), 
	fileType(aFileType), action(aAction), remove(aRemove), target(aTarget), tType(aTargetType), userMatch(aUserMatch), searchInterval(aSearchInterval) {

	string matchPattern = aMatcherString;
	if (aMatcherString.empty())
		matchPattern = aSearchString;

	if (aFileType == SearchManager::TYPE_TTH)
		matcher = new TTHMatcher(matchPattern);
	else if (aMatcherType == StringMatcher::MATCHER_STRING)
		matcher = new TokenMatcher(matchPattern);
	else if (aMatcherType == StringMatcher::MATCHER_REGEX)
		matcher = new RegExMatcher(matchPattern);
	else if (aMatcherType == StringMatcher::MATCHER_WILDCARD)
		matcher = new WildcardMatcher(matchPattern);
};

AutoSearch::~AutoSearch() { 
	delete matcher;
};

AutoSearchManager::AutoSearchManager() : lastSave(0), dirty(false) {
	TimerManager::getInstance()->addListener(this);
	SearchManager::getInstance()->addListener(this);
}

AutoSearchManager::~AutoSearchManager() {
	SearchManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
}


bool AutoSearchManager::addAutoSearch(bool en, const string& ss, SearchManager::TypeModes ft, AutoSearch::ActionType act, bool remove, const string& targ, AutoSearch::TargetType aTargetType, 
	StringMatcher::Type aMatcherType, const string& aMatcherString, int aSearchInterval, const string& aUserMatch /* = Util::emptyString*/) {

	AutoSearchPtr as = nullptr;
	{
		WLock l(cs);

		if (find_if(searchItems.begin(), searchItems.end(), [&ss](AutoSearchPtr as) { return as->getSearchString() == ss; }) != searchItems.end())
			return false;

		as = AutoSearchPtr(new AutoSearch(en, ss, (SearchManager::TypeModes)ft, act, remove, targ, aTargetType, aMatcherType, aMatcherString, aUserMatch, aSearchInterval));
		searchItems.push_back(as);
	}
	dirty = true;
	fire(AutoSearchManagerListener::AddItem(), as);
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

	AutoSearchList searches;
	{
		RLock l (cs);
		for_each(searchItems, [&searches, force, aTick](AutoSearchPtr as) {
			if (as->getEnabled() && ((as->getLastSearch() + (as->getSearchInterval() > 0 ? as->getSearchInterval() : SETTING(AUTOSEARCH_EVERY))*1000*60 < aTick) || force))
				searches.push_back(as);
		});
	}

	uint64_t searchTime = 0;
	StringList extList;

	for_each(searches, [&](AutoSearchPtr as) { 
		// TODO: Get ADC searchtype extensions if any is selected
		searchTime = SearchManager::getInstance()->search(allowedHubs, as->getSearchString(), 0, as->getFileType(), SearchManager::SIZE_DONTCARE, "auto", extList, Search::AUTO_SEARCH);
		as->setLastSearch(aTick);
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
	AutoSearchList matches;

	{
		RLock l (cs);
		for(auto i = searchItems.begin(); i != searchItems.end(); ++i) {
			AutoSearchPtr as = *i;

			//check the file type
			if(as->getFileType() == SearchManager::TYPE_DIRECTORY && sr->getType() != SearchResult::TYPE_DIRECTORY) {
				continue;
			} else if(!ShareManager::getInstance()->checkType(sr->getFile(), (*i)->getFileType())) {
				continue;
			}

			//match
			if (as->getMatcherType() == StringMatcher::MATCHER_TTH) {
				if (!as->match(sr->getTTH()))
					continue;
			} else {
				if (!as->match(sr->getType() == SearchResult::TYPE_DIRECTORY ? Util::getLastDir(sr->getFile()) : sr->getFileName()))
					continue;
			}

			//check the nick
			if(!as->getUserMatch().empty()) {
				StringList nicks = ClientManager::getInstance()->getNicks(sr->getUser()->getCID(), sr->getHubURL());
				for(auto s = nicks.begin(); s != nicks.end(); ++i) {
					if(!Wildcard::patternMatch(Text::utf8ToAcp(*s), Text::utf8ToAcp((*i)->getUserMatch()), '|' ))
						continue;
				}
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
		} catch(...) {
			LogManager::getInstance()->message("AutoSearch Failed to Queue: " + sr->getFile());
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
	string target = aTarget;
	int64_t freeSpace = 0;
	if (targetType == AutoSearch::TARGET_PATH) {
		target = aTarget;
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
			AirUtil::getTarget(targets, target, freeSpace);
			if (!target.empty()) {
				return freeSpace;
			}
		}
	}

	if (target.empty()) {
		//failed to get the target, use the default one
		target = SETTING(DOWNLOAD_DIRECTORY);
	}
	AirUtil::getDiskInfo(target, freeSpace);
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
				xml.addChildAttrib("UserMatch", (*i)->getUserMatch());
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
			addAutoSearch(aXml.getBoolChildAttrib("Enabled"),
				aXml.getChildAttrib("SearchString"), 
				(SearchManager::TypeModes)aXml.getIntChildAttrib("FileType"), 
				(AutoSearch::ActionType)aXml.getIntChildAttrib("Action"),
				aXml.getBoolChildAttrib("Remove"),
				aXml.getChildAttrib("Target"),
				(AutoSearch::TargetType)aXml.getIntChildAttrib("TargetType"),
				(StringMatcher::Type)aXml.getIntChildAttrib("MatcherType"),
				aXml.getChildAttrib("MatcherString"),
				aXml.getIntChildAttrib("SearchInterval"),
				aXml.getChildAttrib("UserMatch"));
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