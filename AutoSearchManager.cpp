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

namespace dcpp {
AutoSearchManager::AutoSearchManager() {
	TimerManager::getInstance()->addListener(this);
	SearchManager::getInstance()->addListener(this);
	removeRegExpFromSearches();

	curPos = 0;
	endOfList = false;
	recheckTime = 0;
	curSearch = Util::emptyString;

	setTime((uint16_t)SETTING(AUTOSEARCH_EVERY) -1); //1 minute delay
}

AutoSearchManager::~AutoSearchManager() {
	SearchManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
	for_each(as.begin(), as.end(), DeleteFunction());
}

void AutoSearchManager::removeRegExpFromSearches() {
	//clear old data
	vs.clear();
	for(Autosearch::List::const_iterator j = as.begin(); j != as.end(); j++) {
		if((*j)->getEnabled()) {
			if(((*j)->getFileType() != 9)) {
				vs.push_back((*j));			//valid searches - we can search for it
			}
		}
	}
}

void AutoSearchManager::getAllowedHubs() {
	allowedHubs.clear();
	ClientManager* cm = ClientManager::getInstance();

	const Client::List& clients = cm->getClients();
	cm->lock();

	Client::Iter it;
	Client::Iter endIt = clients.end();
	for(it = clients.begin(); it != endIt; ++it) {
		Client* client = it->second;
		if (!client->isConnected())
			continue;
	
	//if(client->getUseAutosearch())
			allowedHubs.push_back(client->getHubUrl());
	}
	cm->unlock();
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
	if(BOOLSETTING(AUTOSEARCH_ENABLED_TIME) && BOOLSETTING(AUTOSEARCH_ENABLED)) {
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

		getAllowedHubs();
		removeRegExpFromSearches();

		//no hubs? no fun...
		if(!allowedHubs.size()) {
			return;
		}
		//empty valid autosearch list? too bad
		if(!vs.size()) {
			return;
		}
		Autosearch::List::const_iterator pos = vs.begin() + curPos;
		users.clear();

		if(pos < vs.end()) {
			// NIGHT LOOK HERE
				// TODO: Get ADC searchtype extensions if any is selected
			StringList extList;
			SearchManager::getInstance()->search(allowedHubs, (*pos)->getSearchString(), 0, (SearchManager::TypeModes)(*pos)->getFileType(), SearchManager::SIZE_DONTCARE, "auto", extList);
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
}

void AutoSearchManager::on(SearchManagerListener::SR, const SearchResultPtr& sr) noexcept {
	Lock l(cs);
	if(!as.empty() && BOOLSETTING(AUTOSEARCH_ENABLED) && !allowedHubs.empty()) {
		UserPtr user = static_cast<UserPtr>(sr->getUser());
		if(users.find(user) == users.end()) {
			users.insert(user);
			for(Autosearch::List::iterator i = as.begin(); i != as.end(); ++i) {
				if((*i)->getFileType() == 9) { //regexp

					string str1 = (*i)->getSearchString();
					string str2 = sr->getFile();
					try {
						boost::regex reg(str1);
						if(boost::regex_search(str2.begin(), str2.end(), reg)){
							if((*i)->getAction() == 0) { 
								addToQueue(sr, false , (*i)->getTarget());
								
							} else if((*i)->getAction() == 1) {
								addToQueue(sr, true, (*i)->getTarget());
								
							
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
								 i = as.erase(i);
								 i--;
								 curPos--;
							}
							break;
						};
					} catch(...) {
					}
					/*PME reg((*i)->getSearchString(), "gims");
					if(reg.match(sr->getFile())) {
						if((*i)->getAction() == 0) { 
							addToQueue(sr);
							break;
						} else if((*i)->getAction() == 1) {
							addToQueue(sr, true);
							break;
						}
					}*/
				} else if(curSearch.compare((*i)->getSearchString()) == 0) { //match only to current search
					if((*i)->getFileType() == 8) { //TTH
						if(sr->getTTH().toBase32() == (*i)->getSearchString()) {
							if((*i)->getAction() == 0) { 
								addToQueue(sr, false, (*i)->getTarget());
								
							} else if((*i)->getAction() == 1) {
								addToQueue(sr, true, (*i)->getTarget());
								
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
								 i = as.erase(i);
								 i--;
								 curPos--;
							}
							break;
						}
					} else if((*i)->getFileType() == 7 && sr->getType() == SearchResult::TYPE_DIRECTORY) { //directory
						string matchedDir = matchDirectory(sr->getFile(), (*i)->getSearchString());
						if(!matchedDir.empty()) {
							if((*i)->getAction() == 0) { 
								addToQueue(sr, false, (*i)->getTarget() );
								
							} else if((*i)->getAction() == 1) {
								addToQueue(sr, true, (*i)->getTarget());
								
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
								 i = as.erase(i);
								 i--;
								 curPos--;
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
								if((*i)->getAction() == 0) { 
									addToQueue(sr, false, (*i)->getTarget());
									
								} else if((*i)->getAction() == 1) {
									addToQueue(sr, true, (*i)->getTarget());
									
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
								 i = as.erase(i);
								 i--;
								 curPos--;
							}
							break;
						}
					}
				}
			}
		}
	}
}

void AutoSearchManager::addToQueue(SearchResultPtr sr, bool pausePrio/* = false*/, const string& dTarget ) {
	Lock l(cs);
	string fullpath;
	
	if((dTarget != Util::emptyString) && Util::fileExists(dTarget))
		fullpath = dTarget + Util::getFileName(sr->getFile());
	else
		fullpath = SETTING(DOWNLOAD_DIRECTORY) + Util::getFileName(sr->getFile());
	
	if(!BOOLSETTING(DONT_DL_ALREADY_SHARED) || !ShareManager::getInstance()->isTTHShared(sr->getTTH())) {
		try {
			if(sr->getType() == SearchResult::TYPE_DIRECTORY) {
				if(pausePrio)
					QueueManager::getInstance()->addDirectory(sr->getFile(), HintedUser(sr->getUser(), sr->getHubURL()), fullpath, QueueItem::PAUSED);
			} else {
			QueueManager::getInstance()->add(fullpath, sr->getSize(), sr->getTTH(), HintedUser(sr->getUser(), sr->getHubURL()));
			
			if(pausePrio)
				QueueManager::getInstance()->setPriority(fullpath, QueueItem::PAUSED);
			}
		} catch(...) {
			LogManager::getInstance()->message("AutoSearch Failed to Queue: " + sr->getFile());
		}
	}
}

void AutoSearchManager::AutosearchSave() {
	try {
		SimpleXML xml;

		xml.addTag("Autosearch");
		xml.stepIn();
		xml.addTag("Autosearch");
		xml.stepIn();

		for(Autosearch::List::const_iterator i = as.begin(); i != as.end(); ++i) {
			xml.addTag("Autosearch");
			xml.addChildAttrib("Enabled", (*i)->getEnabled());
			xml.addChildAttrib("SearchString", (*i)->getSearchString());
			xml.addChildAttrib("FileType", (*i)->getFileType());
			xml.addChildAttrib("Action", (*i)->getAction());
			xml.addChildAttrib("Remove", (*i)->getRemove());
			xml.addChildAttrib("Target", (*i)->getTarget());
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

void AutoSearchManager::loadAutosearch(SimpleXML& aXml) {
	as.clear();
	aXml.resetCurrentChild();
	if(aXml.findChild("Autosearch")) {
		aXml.stepIn();
		while(aXml.findChild("Autosearch")) {					
			addAutosearch(aXml.getBoolChildAttrib("Enabled"),
				aXml.getChildAttrib("SearchString"), 
				aXml.getIntChildAttrib("FileType"), 
				aXml.getIntChildAttrib("Action"),
				aXml.getBoolChildAttrib("Remove"),
				aXml.getChildAttrib("Target"));
		}
		aXml.stepOut();
	}
}

void AutoSearchManager::AutosearchLoad() {
	try {
		SimpleXML xml;
		xml.fromXML(File(Util::getPath(Util::PATH_USER_CONFIG) + AUTOSEARCH_FILE, File::READ, File::OPEN).read());
		if(xml.findChild("Autosearch")) {
			xml.stepIn();
			loadAutosearch(xml);
			xml.stepOut();
		}
	} catch(const Exception& e) {
		dcdebug("FavoriteManager::recentload: %s\n", e.getError().c_str());
	}	
}
}