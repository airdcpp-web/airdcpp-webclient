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

namespace dcpp {
AutoSearchManager::AutoSearchManager() {
	TimerManager::getInstance()->addListener(this);
	SearchManager::getInstance()->addListener(this);
	removeRegExpFromSearches();

	lastSave = GET_TICK();
	curPos = 0;
	endOfList = false;
	recheckTime = 0;
	curSearch = Util::emptyString;
	dirty = false;
	setTime((uint16_t)SETTING(AUTOSEARCH_EVERY) -1); //1 minute delay
}

AutoSearchManager::~AutoSearchManager() {
	SearchManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
	//for_each(as.begin(), as.end(), DeleteFunction());
}


bool AutoSearchManager::addAutoSearch(bool en, const string& ss, int ft, int act, bool remove, const string& targ, AutoSearch::targetType aTargetType, const string& aUserMatch) {
	Lock l(acs);
	for(AutoSearchList::iterator i = as.begin(); i != as.end(); ++i) {
		if(stricmp((*i)->getSearchString(), ss) == 0)
			return false; //already exists
	}
	AutoSearchPtr ipw = AutoSearchPtr(new AutoSearch(en, ss, ft, act, remove, targ, aTargetType, aUserMatch));
	as.push_back(ipw);
	dirty = true;
	fire(AutoSearchManagerListener::AddItem(), ipw);
	return true;
}

void AutoSearchManager::getAutoSearch(unsigned int index, AutoSearchPtr &ipw) {
	Lock l(acs);
	if(as.size() > index)
		ipw = as[index];
}

void AutoSearchManager::updateAutoSearch(unsigned int index, AutoSearchPtr &ipw) {
	Lock l(acs);
	as[index] = ipw;
	dirty = true;
}

void AutoSearchManager::removeAutoSearch(AutoSearchPtr a) {
	Lock l(acs);
	AutoSearchList::const_iterator i = find_if(as.begin(), as.end(), [&](AutoSearchPtr& c) { return c == a; });

	if(i != as.end()) {	
		fire(AutoSearchManagerListener::RemoveItem(), a->getSearchString());
		as.erase(i);
		dirty = true;
	}
}

void AutoSearchManager::removeRegExpFromSearches() {
	//clear old data
	vs.clear();
	for(AutoSearchList::const_iterator j = as.begin(); j != as.end(); j++) {
		if((*j)->getEnabled()) {
			if(((*j)->getFileType() != 9)) {
				vs.push_back((*j));			//valid searches - we can search for it
			}
		}
	}
}

string AutoSearchManager::matchDirectory(const string& aFile, const string& aStrToMatch) {
	string lastDir = Util::getLastDir(aFile);
	string dir = Text::toLower(lastDir);
	string strToMatch = Text::toLower(aStrToMatch);

	//path separator at string's end
	if(strToMatch.rfind(PATH_SEPARATOR) == string::npos)
		strToMatch = strToMatch + PATH_SEPARATOR;
	if(dir.rfind(PATH_SEPARATOR) == string::npos)
		dir = dir + PATH_SEPARATOR;

	if(dir == strToMatch) {
		return aFile;
	} else {
		return Util::emptyString;
	}
}

void AutoSearchManager::on(TimerManagerListener::Minute, uint64_t /*aTick*/) noexcept {
	
	//todo check the locking
		Lock l(cs);

		//empty list...
		if(!as.size())
			return;

		if(endOfList) {
			recheckTime++;
			if(recheckTime <= SETTING(AUTOSEARCH_RECHECK_TIME)) {
				return;
			} else {
				endOfList = false; //time's up, search for items again ;]
			}
		}

		time++;
		if(time < SETTING(AUTOSEARCH_EVERY))
			return;

		removeRegExpFromSearches();

		StringList allowedHubs;
		ClientManager::getInstance()->getOnlineClients(allowedHubs);
		//no hubs? no fun...
		if(allowedHubs.empty()) {
			return;
		}
		//empty valid autosearch list? too bad
		if(!vs.size()) {
			return;
		}
		AutoSearchList::const_iterator pos = vs.begin() + curPos;
		users.clear();

		if(pos < vs.end()) {
			// NIGHT LOOK HERE
				// TODO: Get ADC searchtype extensions if any is selected
			StringList extList;
			SearchManager::getInstance()->search(allowedHubs, (*pos)->getSearchString(), 0, (SearchManager::TypeModes)(*pos)->getFileType(), SearchManager::SIZE_DONTCARE, "auto", extList, Search::AUTO_SEARCH);
			curSearch = (*pos)->getSearchString();
			curPos++;
			setTime(0);
			LogManager::getInstance()->message("[A][S:" + Util::toString(curPos) + "]Searching for: " + (*pos)->getSearchString());
		} else {
			LogManager::getInstance()->message("[A]Recheck Items, Next search after " + Util::toString(SETTING(AUTOSEARCH_RECHECK_TIME))+ " minutes.");
			setTime(0);
			curPos = 0;
			endOfList = true;
			recheckTime = 0;
			curSearch = Util::emptyString;
		}
}

void AutoSearchManager::on(SearchManagerListener::SR, const SearchResultPtr& sr) noexcept {
	
	if(!as.empty()) {
		Lock l(cs);
		UserPtr user = static_cast<UserPtr>(sr->getUser());
		if(users.find(user) == users.end()) {
			users.insert(user);
			for(AutoSearchList::iterator i = as.begin(); i != as.end(); ++i) {
				
				if(!(*i)->getUserMatch().empty()) {
					//user nicks should be kinda simple to match so only use wildcards.
					if(!Wildcard::patternMatch( Text::utf8ToAcp(Util::toString(ClientManager::getInstance()->getNicks(user->getCID(), sr->getHubURL()))), Text::utf8ToAcp((*i)->getUserMatch()), '|' ))
						continue;
				}

				if((*i)->getFileType() == 9) { //regexp

					string str1 = (*i)->getSearchString();
					string str2 = sr->getFile();
					try {
						boost::regex reg(str1);
						if(boost::regex_search(str2.begin(), str2.end(), reg)){
							if((*i)->getAction() == 0 || (*i)->getAction() == 1) { 
								addToQueue(sr, *i);
								
							} else if((*i)->getAction() == 2) {
								ClientManager* c = ClientManager::getInstance();
								OnlineUser* u =c->findOnlineUser(user->getCID(), sr->getHubURL(), false);
								if(u) {
								Client* client = &u->getClient();
								if(!client || !client->isConnected())
									break;

								client->Message(Text::fromT(_T("AutoSearch Found File: ")) + sr->getFile() + Text::fromT(_T(" From User: ")) + Util::toString(ClientManager::getInstance()->getNicks(user->getCID(), sr->getHubURL())));
								
								 }
								}
							if((*i)->getRemove()) {
								 fire(AutoSearchManagerListener::RemoveItem(), (*i)->getSearchString());
								 i = as.erase(i);
								 i--;
								 curPos--;
								 dirty = true;
							}
							break;
						};
					} catch(...) {
					}
				} else if(curSearch.compare((*i)->getSearchString()) == 0) { //match only to current search
					if((*i)->getFileType() == 8) { //TTH
						if(sr->getTTH().toBase32() == (*i)->getSearchString()) {
							if((*i)->getAction() == 0 || (*i)->getAction() == 1) { 
								addToQueue(sr, *i);
							} else if((*i)->getAction() == 2) {
								ClientManager* c = ClientManager::getInstance();
								OnlineUser* u =c->findOnlineUser(user->getCID(), sr->getHubURL(), false);
								if(u) {
								Client* client = &u->getClient();
								if(!client || !client->isConnected())
									break;

								client->Message(Text::fromT(_T("AutoSearch Found File: ")) + sr->getFile() + Text::fromT(_T(" From User: ")) + Util::toString(ClientManager::getInstance()->getNicks(user->getCID(), sr->getHubURL())));
								
								 }
								}
							if((*i)->getRemove()) {
								 fire(AutoSearchManagerListener::RemoveItem(), (*i)->getSearchString());
								 i = as.erase(i);
								 i--;
								 curPos--;
								 dirty = true;
							}
							break;
						}
					} else if((*i)->getFileType() == 7 && sr->getType() == SearchResult::TYPE_DIRECTORY) { //directory
						string matchedDir = matchDirectory(sr->getFile(), (*i)->getSearchString());
						if(!matchedDir.empty()) {
							if((*i)->getAction() == 1 || (*i)->getAction() == 0) {
								addToQueue(sr, *i);
							} else if((*i)->getAction() == 2) {
								ClientManager* c = ClientManager::getInstance();
								OnlineUser* u =c->findOnlineUser(user->getCID(), sr->getHubURL(), false);
								if(u) {
								Client* client = &u->getClient();
								if(!client || !client->isConnected())
									break;

								client->Message(Text::fromT(_T("AutoSearch Found File: ")) + sr->getFile() + Text::fromT(_T(" From User: ")) + Util::toString(ClientManager::getInstance()->getNicks(user->getCID(), sr->getHubURL())));
								
								 }
								}
							if((*i)->getRemove()) {
								 fire(AutoSearchManagerListener::RemoveItem(), (*i)->getSearchString());
								 i = as.erase(i);
								 i--;
								 curPos--;
								 dirty = true;
							}
							break;
						}
					} else if(ShareManager::getInstance()->checkType(sr->getFile(), (*i)->getFileType())) {
						if(!sr->getFile().empty()) {
							string iFile = Text::toLower(sr->getFile());
							StringTokenizer<string> tss(Text::toLower((*i)->getSearchString()), " ");
							StringList& slSrch = tss.getTokens();
							bool matched = true;
							for(StringList::const_iterator j = slSrch.begin(); j != slSrch.end(); ++j) {
								if(j->empty()) continue;
								if(iFile.find(*j) == string::npos) {
									matched = false;
									break;
								}
							}
							if(matched) {
								if((*i)->getAction() == 0 || (*i)->getAction() == 1) { 
									addToQueue(sr, *i);
								} else if((*i)->getAction() == 2) {
								ClientManager* c = ClientManager::getInstance();
								OnlineUser* u =c->findOnlineUser(user->getCID(), sr->getHubURL(), false);
								if(u) {
								Client* client = &u->getClient();
								if(!client || !client->isConnected())
									break;

								client->Message(Text::fromT(_T("AutoSearch Found File: ")) + sr->getFile() + Text::fromT(_T(" From User: ")) + Util::toString(ClientManager::getInstance()->getNicks(user->getCID(), sr->getHubURL())));
								
								 }
								}
							}
							if((*i)->getRemove()) {
								 fire(AutoSearchManagerListener::RemoveItem(), (*i)->getSearchString());
								 i = as.erase(i);
								 i--;
								 curPos--;
								 dirty = true;
							}
							break;
						}
					}
				}
			}
		}
	}
}

void AutoSearchManager::addToQueue(const SearchResultPtr sr, const AutoSearchPtr as) {
	string path;
	if (!getTarget(sr, as, path)) {
		//not enough space, do something fun
	}

	try {
		if(sr->getType() == SearchResult::TYPE_DIRECTORY) {
			QueueManager::getInstance()->addDirectory(sr->getFile(), HintedUser(sr->getUser(), sr->getHubURL()), path, as->getAction() == 1 ? QueueItem::PAUSED : QueueItem::DEFAULT);
		} else {
			path = path + Util::getFileName(sr->getFile());
			QueueManager::getInstance()->add(path, sr->getSize(), sr->getTTH(), HintedUser(sr->getUser(), sr->getHubURL()), 0);
			if(as->getAction() == 1) {
				QueueManager::getInstance()->setQIPriority(path, QueueItem::PAUSED);
			}
		}
	} catch(...) {
		LogManager::getInstance()->message("AutoSearch Failed to Queue: " + sr->getFile());
	}
}

bool AutoSearchManager::getTarget(const SearchResultPtr sr, const AutoSearchPtr as, string& target) {
	string aTarget = as->getTarget();
	int64_t freeSpace = 0;
	if (as->getTargetType() == AutoSearch::TARGET_PATH) {
		target = aTarget;
	} else {
		auto dirList = (as->getTargetType() == AutoSearch::TARGET_FAVORITE) ? FavoriteManager::getInstance()->getFavoriteDirs() : ShareManager::getInstance()->getGroupedDirectories();
		auto s = find_if(dirList.begin(), dirList.end(), CompareFirst<string, StringList>(aTarget));
		if (s != dirList.end()) {
			StringList targets = s->second;
			AirUtil::getTarget(targets, target, freeSpace);
			if (!target.empty()) {
				return (freeSpace > sr->getSize());
			}
		}
	}

	if (target.empty()) {
		//failed to get the target, use the default one
		target = SETTING(DOWNLOAD_DIRECTORY);
	}
	AirUtil::getDiskInfo(target, freeSpace);
	return (freeSpace > sr->getSize());
}

void AutoSearchManager::AutoSearchSave() {
	Lock l(cs);
	try {
		dirty = false;
		SimpleXML xml;

		xml.addTag("Autosearch");
		xml.stepIn();
		xml.addTag("Autosearch");
		xml.stepIn();

		for(AutoSearchList::const_iterator i = as.begin(); i != as.end(); ++i) {
			xml.addTag("Autosearch");
			xml.addChildAttrib("Enabled", (*i)->getEnabled());
			xml.addChildAttrib("SearchString", (*i)->getSearchString());
			xml.addChildAttrib("FileType", (*i)->getFileType());
			xml.addChildAttrib("Action", (*i)->getAction());
			xml.addChildAttrib("Remove", (*i)->getRemove());
			xml.addChildAttrib("Target", (*i)->getTarget());
			xml.addChildAttrib("TargetType", (*i)->getTargetType());
			xml.addChildAttrib("UserMatch", (*i)->getUserMatch());
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
	as.clear();
	aXml.resetCurrentChild();
	if(aXml.findChild("Autosearch")) {
		aXml.stepIn();
		while(aXml.findChild("Autosearch")) {					
			addAutoSearch(aXml.getBoolChildAttrib("Enabled"),
				aXml.getChildAttrib("SearchString"), 
				aXml.getIntChildAttrib("FileType"), 
				aXml.getIntChildAttrib("Action"),
				aXml.getBoolChildAttrib("Remove"),
				aXml.getChildAttrib("Target"),
				(AutoSearch::targetType)aXml.getIntChildAttrib("TargetType"),
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
		dcdebug("FavoriteManager::recentload: %s\n", e.getError().c_str());
	}	
}
}