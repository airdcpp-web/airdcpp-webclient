/* 
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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
#include "QueueManager.h"

#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm/for_each.hpp>

#include "AirUtil.h"
#include "Bundle.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "DirectoryListing.h"
#include "Download.h"
#include "DownloadManager.h"
#include "HashManager.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "SearchManager.h"
#include "ShareScannerManager.h"
#include "ShareManager.h"
#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "Transfer.h"
#include "UploadManager.h"
#include "UserConnection.h"
#include "version.h"
#include "Wildcards.h"
#include "SearchResult.h"
#include "SharedFileStream.h"
#include "MerkleCheckOutputStream.h"

#include <limits>

#if !defined(_WIN32) && !defined(PATH_MAX) // Extra PATH_MAX check for Mac OS X
#include <sys/syslimits.h>
#endif

#ifdef ff
#undef ff
#endif

namespace dcpp {

using boost::adaptors::map_values;
using boost::range::for_each;

class DirectoryItem {
public:
	DirectoryItem() : priority(QueueItem::DEFAULT) { }
	DirectoryItem(const UserPtr& aUser, const string& aName, const string& aTarget, 
		QueueItem::Priority p) : name(aName), target(aTarget), priority(p), user(aUser) { }
	~DirectoryItem() { }
	
	UserPtr& getUser() { return user; }
	void setUser(const UserPtr& aUser) { user = aUser; }
	
	GETSET(string, name, Name);
	GETSET(string, target, Target);
	GETSET(QueueItem::Priority, priority, Priority);
private:
	UserPtr user;
};

QueueManager::FileQueue::~FileQueue() {
	for(auto i = queue.begin(); i != queue.end(); ++i)
		i->second->dec();
}

QueueItem* QueueManager::FileQueue::add(const string& aTarget, int64_t aSize, 
						  Flags::MaskType aFlags, QueueItem::Priority p, const string& aTempTarget, 
						  time_t aAdded, const TTHValue& root) throw(QueueException, FileException)
{
//remember the default state so high_prio files are matched
//even if priority is set with priority by size setting
	bool Default = false;

	if(p == QueueItem::DEFAULT)
		Default = true;

	if(p == QueueItem::DEFAULT) {
		if(aSize <= SETTING(PRIO_HIGHEST_SIZE)*1024) {
			p = QueueItem::HIGHEST;
		} else if(aSize <= SETTING(PRIO_HIGH_SIZE)*1024) {
			p = QueueItem::HIGH;
		} else if(aSize <= SETTING(PRIO_NORMAL_SIZE)*1024) {
			p = QueueItem::NORMAL;
		} else if(aSize <= SETTING(PRIO_LOW_SIZE)*1024) {
			p = QueueItem::LOW;
		} else if(SETTING(PRIO_LOWEST)) {
			p = QueueItem::LOWEST;
		}
	}

	if((p != QueueItem::HIGHEST) && ( Default )){
		if(!SETTING(HIGH_PRIO_FILES).empty()){
			int pos = aTarget.rfind("\\")+1;
		
			if(BOOLSETTING(HIGHEST_PRIORITY_USE_REGEXP)){
				/*PME regexp;
				regexp.Init(Text::utf8ToAcp(SETTING(HIGH_PRIO_FILES)));
				if((regexp.IsValid()) && (regexp.match(Text::utf8ToAcp(aTarget.substr(pos))))) {
					p = QueueItem::HIGH;
				}*/
				string str1 = SETTING(HIGH_PRIO_FILES);
				string str2 = aTarget.substr(pos);
				try {
					boost::regex reg(str1);
					if(boost::regex_search(str2.begin(), str2.end(), reg)){
						p = QueueItem::HIGH;
						Default = false;
					};
				} catch(...) {
				}
			}else{
				if(Wildcard::patternMatch(Text::utf8ToAcp(aTarget.substr(pos)), Text::utf8ToAcp(SETTING(HIGH_PRIO_FILES)), '|')) {
					p = QueueItem::HIGH;
					Default = false;
				}
			}
		}
	}



	QueueItem* qi = new QueueItem(aTarget, aSize, p, aFlags, aAdded, root);

	if(qi->isSet(QueueItem::FLAG_USER_LIST)) {
		qi->setPriority(QueueItem::HIGHEST);
	} else {
		qi->setMaxSegments(getMaxSegments(qi->getSize()));
		
		if(p == QueueItem::DEFAULT) {
			if(BOOLSETTING(AUTO_PRIORITY_DEFAULT)) {
				qi->setAutoPriority(true);
				qi->setPriority(QueueItem::LOW);
			} else {
				qi->setPriority(QueueItem::NORMAL);
			}
		}
	}

	qi->setTempTarget(aTempTarget);
	if(!Util::fileExists(aTempTarget) && Util::fileExists(aTempTarget + ".antifrag")) {
		// load old antifrag file
		File::renameFile(aTempTarget + ".antifrag", qi->getTempTarget());
	}
			
	dcassert(find(aTarget) == NULL);
	add(qi, false);
	return qi;
}

void QueueManager::FileQueue::add(QueueItem* qi, bool addFinished, bool addTTH) {
	if (!addFinished) {
		if (!qi->isSet(QueueItem::FLAG_USER_LIST)) {
			queueSize += qi->getSize();
		}
		targetMapInsert = queue.insert(targetMapInsert, make_pair(const_cast<string*>(&qi->getTarget()), qi));
		//queue.insert(make_pair(const_cast<string*>(&qi->getTarget()), qi)).first;
	}

	if (addTTH) {
		tthIndex[qi->getTTH()].push_back(qi);
	}
}

void QueueManager::FileQueue::remove(QueueItem* qi, bool isFinished) {
	prev(targetMapInsert);
	queue.erase(const_cast<string*>(&qi->getTarget()));
	if  (!qi->isSet(QueueItem::FLAG_USER_LIST)) {
		queueSize -= qi->getSize();
	}

	queueSize -= qi->getSize();

	if (!isFinished) {
		removeTTH(qi);
	}
}

void QueueManager::FileQueue::removeTTH(QueueItem* qi) {
	auto i = tthIndex.find(qi->getTTH());
	if (i != tthIndex.end()) {
		if (i->second.size() == 1) {
			tthIndex.erase(i);
		} else {
			i->second.erase(std::remove(i->second.begin(), i->second.end(), qi), i->second.end());
		}
	}
	qi->dec();
}


QueueItem* QueueManager::FileQueue::find(const string& target) {
	auto i = queue.find(const_cast<string*>(&target));
	return (i == queue.end()) ? NULL : i->second;
}

void QueueManager::FileQueue::find(QueueItemList& sl, int64_t aSize, const string& suffix) {
	for(auto i = queue.begin(); i != queue.end(); ++i) {
		if(i->second->getSize() == aSize) {
			const string& t = i->second->getTarget();
			if(suffix.empty() || (suffix.length() < t.length() &&
				stricmp(suffix.c_str(), t.c_str() + (t.length() - suffix.length())) == 0) )
				sl.push_back(i->second);
		}
	}
}

QueueItemList QueueManager::FileQueue::find(const TTHValue& tth) {
	QueueItemList ql;
	auto i = tthIndex.find(tth);
	if (i != tthIndex.end()) {
		ql = i->second;
	}
	return ql;
}

int QueueManager::FileQueue::isTTHQueued(const TTHValue& tth) {
	auto i = tthIndex.find(tth);
	if (i != tthIndex.end()) {
		return ((*i).second.front()->isFinished() ? 2 : 1);
	} else {
		return 0;
	}
}

void QueueManager::FileQueue::addSearchPrio(BundlePtr aBundle, Bundle::Priority p) {
	if (aBundle->getRecent()) {
		recentSearchQueue.push_back(aBundle);
		return;
	}
	prioSearchQueue[p].push_back(aBundle);
}

void QueueManager::FileQueue::removeSearchPrio(BundlePtr aBundle, Bundle::Priority p) {
	auto& q = prioSearchQueue[p];
	if (aBundle->getRecent()) {
		q = recentSearchQueue;
	}
	auto i = std::find(q.begin(), q.end(), aBundle);
	dcassert(i != q.end());
	q.erase(i);
}

void QueueManager::FileQueue::setSearchPriority(BundlePtr aBundle, Bundle::Priority oldPrio, Bundle::Priority newPrio) {
	if (aBundle->getRecent()) {
		return;
	}

	if (oldPrio >= Bundle::LOW) {
		removeSearchPrio(aBundle, oldPrio);
	}

	if (newPrio >= Bundle::LOW) {
		addSearchPrio(aBundle, newPrio);
	}
}

BundlePtr QueueManager::FileQueue::findRecent(int& recentBundles) {
	//if (bundlePrioQueue[p].empty()) {
	if ((int)recentSearchQueue.size() == 0) {
		return NULL;
	}
	BundlePtr tmp = recentSearchQueue.front();
	recentSearchQueue.pop_front();
	//check if the bundle still belongs to here
	if ((tmp->getDirDate() + (SETTING(RECENT_BUNDLE_HOURS)*60*60)) < (GET_TIME())) {
	//if (tmp->getDirDate() < (GET_TIME() - SETTING(RECENT_BUNDLE_HOURS)*60*60*1000)) {
		recentSearchQueue.push_back(tmp);
	} else {
		tmp->setRecent(false);
	}
	recentBundles = recentSearchQueue.size();
	return tmp;
}

BundlePtr QueueManager::FileQueue::findAutoSearch(int& prioBundles) {

	int p = Bundle::LAST - 1;
	int prioSum = 0;
	//int loopBundles = 0;
	do {
		for (auto k = prioSearchQueue[p].begin(); k != prioSearchQueue[p].end(); ++k) {
			if ((*k)->countOnlineUsers() > (size_t)SETTING(AUTO_SEARCH_LIMIT)) {
				continue;
			}
			prioBundles++;
			//loopBundles++;
		}
		//prioBundles += loopBundles;
		prioSum += (int)(p-1)*prioSearchQueue[p].size();
		p--;
	} while(p >= Bundle::LOW);

	//do we have anything where to search from?
	if (prioBundles == 0) {
		return NULL;
	}

	//choose the bundle
	BundlePtr cand = NULL;
	int rand = Util::rand(prioSum); //no results for paused or lowest
	//LogManager::getInstance()->message("prioSum: " + Util::toString(prioSum) + " random: " + Util::toString(rand));

	p = Bundle::LOW;
	do {
		size_t size = prioSearchQueue[p].size();
		if (rand < (int)(p-1)*size) {
			//we got the prio
			int s = 0;
			while (s <= size) {
				if (prioSearchQueue[p].empty()) {
					break;
				}
				BundlePtr tmp = prioSearchQueue[p].front();
				prioSearchQueue[p].pop_front();
				prioSearchQueue[p].push_back(tmp);
				if (tmp->countOnlineUsers() > (size_t)SETTING(AUTO_SEARCH_LIMIT)) {
					s++;
					continue;
				}
				for (auto k = tmp->getQueueItems().begin(); k != tmp->getQueueItems().end(); ++k) {
					QueueItem* q = *k;
					if(q->getPriority() == QueueItem::PAUSED || q->countOnlineUsers() >= (size_t)SETTING(MAX_AUTO_MATCH_SOURCES))
						continue;
					if(q->isRunning()) {
						//it's ok but see if we can find better
						cand = tmp;
					} else {
						//LogManager::getInstance()->message("BUNDLE1 " + tmp->getName() + " CHOSEN FROM PRIO " + AirUtil::getPrioText(p) + ", rand " + Util::toString(rand));
						return tmp;
					}
				}
				s++;
			}
		} else {
			//LogManager::getInstance()->message("NO MATCH FOR PRIO " + AirUtil::getPrioText(p) + ", rand " + Util::toString(rand));
		}
		rand -= (int)(p-1)*size;
		p++;
	} while(p < Bundle::LAST);

	//LogManager::getInstance()->message("BUNDLE2");
	return cand;
}

void QueueManager::FileQueue::move(QueueItem* qi, const string& aTarget) {
	queue.erase(const_cast<string*>(&qi->getTarget()));
	qi->setTarget(aTarget);
	add(qi, false, false);
}

void QueueManager::UserQueue::add(QueueItem* qi) {
	for(QueueItem::SourceConstIter i = qi->getSources().begin(); i != qi->getSources().end(); ++i) {
		add(qi, i->getUser());
	}
}

void QueueManager::UserQueue::add(QueueItem* qi, const HintedUser& aUser) {

	if (qi->getPriority() == QueueItem::HIGHEST) {
		auto& l = userPrioQueue[aUser.user];

		if(qi->getDownloadedBytes() > 0 ) {
			l.push_front(qi);
		} else {
			l.push_back(qi);
		}
	}

	BundlePtr bundle = qi->getBundle();
	if (bundle) {
		if (bundle->addQueue(qi, aUser)) {
			//bundles can't be added here with the priority DEFAULT
			//dcassert(userBundleQueue.find(aUser.user) == userBundleQueue.end());
			auto& s = userBundleQueue[aUser.user];
			if (SETTING(DOWNLOAD_ORDER) != SettingsManager::ORDER_RANDOM) {
				s.insert(upper_bound(s.begin(), s.end(), bundle, Bundle::SortOrder()), bundle);
				/*for (auto k = s.begin(); k != s.end(); ++k) {
					LogManager::getInstance()->message("USERQUEUE: " + (*k)->getName() + " prio " + AirUtil::getPrioText((*k)->getPriority()) + " CID " + aUser.user->getCID().toBase32());
				} */
			} else {
				auto pp = equal_range(s.begin(), s.end(), bundle, [](const BundlePtr leftBundle, const BundlePtr rightBundle) { return leftBundle->getPriority() > rightBundle->getPriority(); });
				int dist = distance(pp.first, pp.second);
				if (dist > 0) {
					std::advance(pp.first, Util::rand(dist));
				}
				s.insert(pp.first, bundle);
			}

			//LogManager::getInstance()->message("Add new bundle " + bundle->getName() + " for an user: " + aUser->getCID().toBase32() + ", total bundles" + Util::toString(s.size()));
		} else {
			dcassert(userBundleQueue.find(aUser.user) != userBundleQueue.end());
			//LogManager::getInstance()->message("Don't add new bundle " + bundle->getName() + " for an user: " + aUser->getCID().toBase32());
		}
	}
}

QueueItem* QueueManager::UserQueue::getNext(const UserPtr& aUser, QueueItem::Priority minPrio, int64_t wantedSize, int64_t lastSpeed, bool allowRemove, bool smallSlot) {
	QueueItem* qi = getNextPrioQI(aUser, 0, 0, smallSlot);
	if(!qi) {
		qi = getNextBundleQI(aUser, (Bundle::Priority)minPrio, 0, 0, smallSlot);
	}

	//Check partial sources here
	if (qi && allowRemove) {
		QueueItem::SourceConstIter source = qi->getSource(aUser);
		if(source->isSet(QueueItem::Source::FLAG_PARTIAL)) {
			int64_t blockSize = HashManager::getInstance()->getBlockSize(qi->getTTH());
			if(blockSize == 0)
				blockSize = qi->getSize();
					
			Segment segment = qi->getNextSegment(blockSize, wantedSize, lastSpeed, source->getPartialSource());
			if(segment.getStart() != -1 && segment.getSize() == 0) {
				// no other partial chunk from this user, remove him from queue
				removeQI(qi, aUser);
				qi->removeSource(aUser, QueueItem::Source::FLAG_NO_NEED_PARTS);
				lastError = STRING(NO_NEEDED_PART);
				qi = NULL;
			}
		}
	}

	return qi;
}

QueueItem* QueueManager::UserQueue::getNextPrioQI(const UserPtr& aUser, int64_t wantedSize, int64_t lastSpeed, bool smallSlot, bool listAll) {
	lastError = Util::emptyString;
	auto i = userPrioQueue.find(aUser);
	if(i != userPrioQueue.end()) {
		dcassert(!i->second.empty());
		for(auto j = i->second.begin(); j != i->second.end(); ++j) {
			QueueItem* qi = *j;
			if (qi->hasSegment(aUser, lastError, wantedSize, lastSpeed, smallSlot) || listAll) {
				return qi;
			}
		}
	}
	return NULL;
}

QueueItem* QueueManager::UserQueue::getNextBundleQI(const UserPtr& aUser, Bundle::Priority minPrio, int64_t wantedSize, int64_t lastSpeed, bool smallSlot) {
	lastError = Util::emptyString;

	auto i = userBundleQueue.find(aUser);
	if(i != userBundleQueue.end()) {
		dcassert(!i->second.empty());
		//auto j = prev(i->second.end());
		for (auto j = i->second.begin(); j != i->second.end(); ++j) {
			if ((*j)->getPriority() < minPrio) {
				break;
			}
			QueueItem* qi = (*j)->getNextQI(aUser, lastError, minPrio, wantedSize, lastSpeed, smallSlot);
			if (qi) {
				return qi;
			}
		}
	}
	return NULL;
}

bool QueueManager::UserQueue::addDownload(QueueItem* qi, Download* d) {
	qi->getDownloads().push_back(d);
	auto& j = running[d->getUser()];
	j.push_back(qi);
	if (qi->getBundle()) {
		if (qi->getBundle()->addDownload(d)) {
			return true;
		}
	}
	return false;
	//LogManager::getInstance()->message("addDownload, Running size for the user: " + Util::toString(j.size()));
}

void QueueManager::UserQueue::removeDownload(QueueItem* qi, const UserPtr& user, const string& token) {
	auto i = running.find(user);
	if (i != running.end()) {
		for (auto s = i->second.begin(); s != i->second.end(); ++s) {
			if (qi == *s) {
				i->second.erase(s);
				//LogManager::getInstance()->message("Running size for the user: " + Util::toString(i->second.size()));
				if (i->second.empty()) {
					//LogManager::getInstance()->message("ERASE RUNNING USER");
					running.erase(i);
				}
				break;
			}
		}
	}

	//if (!qi->getBundleToken().empty()) {
	//	QueueManager::getInstance()->removeRunningUser(qi->getBundleToken(), user->getCID(), tree);
	//}

	BundlePtr bundle;
	if (!token.empty()) {
		for(DownloadList::iterator i = qi->getDownloads().begin(); i != qi->getDownloads().end(); ++i) {
			if ((*i)->getUserConnection().getToken() == token) {
				qi->getDownloads().erase(i);
				bundle = qi->getBundle();
				if (bundle) {
					if (bundle->removeDownload(token) == 1) {
						QueueManager::getInstance()->fire(QueueManagerListener::BundleUser(), bundle->getToken(), bundle->getDownloads().front()->getHintedUser());
					}
				}
				return;
			}
		}
		dcassert(token.empty());
	} else {
		//erase all downloads from this user
		for(DownloadList::iterator i = qi->getDownloads().begin(); i != qi->getDownloads().end();) {
			if((*i)->getUser() == user) {
				bundle = qi->getBundle();
				if (bundle) {
					if (bundle->removeDownload((*i)->getUserConnection().getToken()) == 1) {
						QueueManager::getInstance()->fire(QueueManagerListener::BundleUser(), bundle->getToken(), bundle->getDownloads().front()->getHintedUser());
					}
				}
				qi->getDownloads().erase(i);
				i = qi->getDownloads().begin();
			} else {
				i++;
			}
		}
	}
	return;
}

void QueueManager::UserQueue::setQIPriority(QueueItem* qi, QueueItem::Priority p) {
	removeQI(qi, false);
	qi->setPriority(p);
	add(qi);
}

QueueItemList QueueManager::UserQueue::getRunning(const UserPtr& aUser) {
	QueueItemList ret;
	auto i = running.find(aUser);
	if (i != running.end()) {
		return i->second;
	}
	return ret;
}

void QueueManager::UserQueue::removeQI(QueueItem* qi, bool removeRunning) {
	for(QueueItem::SourceConstIter i = qi->getSources().begin(); i != qi->getSources().end(); ++i) {
		removeQI(qi, i->getUser(), removeRunning);
	}
}

void QueueManager::UserQueue::removeQI(QueueItem* qi, const UserPtr& aUser, bool removeRunning) {

	if(removeRunning) {
		QueueItemList runningItems = getRunning(aUser);
		for (auto s = runningItems.begin(); s != runningItems.end(); ++s) {
			if (qi == *s) {
				removeDownload(qi, aUser);
				break;
			}
		}
	}

	dcassert(qi->isSource(aUser));

	BundlePtr bundle = qi->getBundle();
	if (bundle) {
		if (qi->getBundle()->removeQueue(qi, aUser)) {
			//no bundle should come here with the default prio... fix those by not starting to download incomplete bundles
			auto j = userBundleQueue.find(aUser);
			dcassert(j != userBundleQueue.end());
			auto& l = j->second;
			auto s = find(l.begin(), l.end(), bundle);
			dcassert(s != l.end());
			l.erase(s);

			if(l.empty()) {
				//LogManager::getInstance()->message("Remove bundle " + bundle->getName() + " and the whole user " + aUser->getCID().toBase32());
				userBundleQueue.erase(j);
			} else {
				//LogManager::getInstance()->message("Remove bundle " + bundle->getName() + " from user: " + aUser->getCID().toBase32() + ", total bundles" + Util::toString(l.size()));
			}
			//LogManager::getInstance()->message("Remove bundle " + bundle->getName() + " from an user: " + aUser->getCID().toBase32() + ", total bundles" + Util::toString(l.size()));
		} else {
			dcassert(userBundleQueue.find(aUser) != userBundleQueue.end());
			//LogManager::getInstance()->message("Don't remove bundle " + bundle->getName() + " from an user: " + aUser->getCID().toBase32());
		}
	}

	if (qi->getPriority() == QueueItem::HIGHEST) {
		auto j = userPrioQueue.find(aUser);
		dcassert(j != userPrioQueue.end());
		auto& l = j->second;
		auto i = find(l.begin(), l.end(), qi);
		dcassert(i != l.end());
		l.erase(i);

		if(l.empty()) {
			userPrioQueue.erase(j);
		}
	}
}


void QueueManager::UserQueue::setBundlePriority(BundlePtr aBundle, Bundle::Priority p) {
	//Bundle::Priority oldPrio = aBundle->getPriority();
	aBundle->setPriority(p);
	HintedUserList sources;
	aBundle->getQISources(sources);
	//LogManager::getInstance()->message("CHANGING THE PRIO FOR " + aBundle->getName() +  " SOURCES SIZE: " + Util::toString(sources.size()));

	for(auto i = sources.begin(); i != sources.end(); ++i) {
		//UserPtr aUser = i->first.user;
		UserPtr aUser = *i;
		//LogManager::getInstance()->message("BUNDLESOURCE FOUND, CID: " + aUser->getCID().toBase32());
		//dcassert(aBundle->isSource(aUser));

		//erase old
		//for(auto y = ulm.begin(); y != ulm.end(); ++y) {
		//	LogManager::getInstance()->message("OLD PRIO ULM CID: " + (*y).first->getCID().toBase32());
		//}
		auto j = userBundleQueue.find(aUser);
		dcassert(j != userBundleQueue.end());
		//LogManager::getInstance()->message("CID TAKEN FROM ULM: " + j->first->getCID().toBase32());
		auto& l = j->second;
		auto s = find(l.begin(), l.end(), aBundle);
		dcassert(s != l.end());
		l.erase(s);

		if(l.empty()) {
			userBundleQueue.erase(j);
		}

		//insert new
		auto& ulm2 = userBundleQueue[aUser];
		ulm2.insert(upper_bound(ulm2.begin(), ulm2.end(), aBundle, Bundle::SortOrder()), aBundle);
		/*for (auto k = ulm2.begin(); k != ulm2.end(); ++k) {
			LogManager::getInstance()->message("USERQUEUE: " + (*k)->getName() + " prio " + AirUtil::getPrioText((*k)->getPriority()));
		} */
		//ulm2.push_back(aBundle);
		//for(auto y = ulm2.begin(); y != ulm2.end(); ++y) {
		//	LogManager::getInstance()->message("NEW PRIO ULM CID: " + (*y).first->getCID().toBase32());
		//}
	}
}


void QueueManager::FileMover::moveFile(const string& source, const string& target, BundlePtr aBundle) {
	Lock l(cs);
	files.push_back(make_pair(aBundle, make_pair(source, target)));
	if(!active) {
		active = true;
		start();
	}
}

int QueueManager::FileMover::run() {
	for(;;) {
		FileBundlePair next;
		{
			Lock l(cs);
			if(files.empty()) {
				active = false;
				return 0;
			}
			next = files.back();
			files.pop_back();
		}
		moveFile_(next.second.first, next.second.second, next.first);
	}
}

void QueueManager::Rechecker::add(const string& file) {
	Lock l(cs);
	files.push_back(file);
	if(!active) {
		active = true;
		start();
	}
}

int QueueManager::Rechecker::run() {
	while(true) {
		string file;
		{
			Lock l(cs);
			StringIter i = files.begin();
			if(i == files.end()) {
				active = false;
				return 0;
			}
			file = *i;
			files.erase(i);
		}

		QueueItem* q;
		int64_t tempSize;
		TTHValue tth;

		{
			Lock l(qm->cs);

			q = qm->fileQueue.find(file);
			if(!q || q->isSet(QueueItem::FLAG_USER_LIST))
				continue;

			qm->fire(QueueManagerListener::RecheckStarted(), q->getTarget());
			dcdebug("Rechecking %s\n", file.c_str());

			tempSize = File::getSize(q->getTempTarget());

			if(tempSize == -1) {
				qm->fire(QueueManagerListener::RecheckNoFile(), q->getTarget());
				continue;
			}

			if(tempSize < 64*1024) {
				qm->fire(QueueManagerListener::RecheckFileTooSmall(), q->getTarget());
				continue;
			}

			if(tempSize != q->getSize()) {
				File(q->getTempTarget(), File::WRITE, File::OPEN).setSize(q->getSize());
			}

			if(q->isRunning()) {
				qm->fire(QueueManagerListener::RecheckDownloadsRunning(), q->getTarget());
				continue;
			}

			tth = q->getTTH();
		}

		TigerTree tt;
		bool gotTree = HashManager::getInstance()->getTree(tth, tt);

		string tempTarget;

		{
			Lock l(qm->cs);

			// get q again in case it has been (re)moved
			q = qm->fileQueue.find(file);
			if(!q)
				continue;

			if(!gotTree) {
				qm->fire(QueueManagerListener::RecheckNoTree(), q->getTarget());
				continue;
			}

			//Clear segments
			q->resetDownloaded();

			tempTarget = q->getTempTarget();
		}

		//Merklecheck
		int64_t startPos=0;
		DummyOutputStream dummy;
		int64_t blockSize = tt.getBlockSize();
		bool hasBadBlocks = false;

		vector<uint8_t> buf((size_t)min((int64_t)1024*1024, blockSize));

		typedef pair<int64_t, int64_t> SizePair;
		typedef vector<SizePair> Sizes;
		Sizes sizes;

		{
			File inFile(tempTarget, File::READ, File::OPEN);

			while(startPos < tempSize) {
				try {
					MerkleCheckOutputStream<TigerTree, false> check(tt, &dummy, startPos);

					inFile.setPos(startPos);
					int64_t bytesLeft = min((tempSize - startPos),blockSize); //Take care of the last incomplete block
					int64_t segmentSize = bytesLeft;
					while(bytesLeft > 0) {
						size_t n = (size_t)min((int64_t)buf.size(), bytesLeft);
						size_t nr = inFile.read(&buf[0], n);
						check.write(&buf[0], nr);
						bytesLeft -= nr;
						if(bytesLeft > 0 && nr == 0) {
							// Huh??
							throw Exception();
						}
					}
					check.flush();

					sizes.push_back(make_pair(startPos, segmentSize));
				} catch(const Exception&) {
					hasBadBlocks = true;
					dcdebug("Found bad block at " I64_FMT "\n", startPos);
				}
				startPos += blockSize;
			}
		}

		Lock l(qm->cs);

		// get q again in case it has been (re)moved
		q = qm->fileQueue.find(file);
		if(!q)
			continue;

		//If no bad blocks then the file probably got stuck in the temp folder for some reason
		if(!hasBadBlocks) {
			qm->moveStuckFile(q);
			continue;
		}

		for(Sizes::const_iterator i = sizes.begin(); i != sizes.end(); ++i)
			q->addSegment(Segment(i->first, i->second));

		qm->rechecked(q);
	}
	return 0;
}

QueueManager::QueueManager() : 
	lastSave(0),
	lastAutoPrio(0), 
	queueFile(Util::getPath(Util::PATH_USER_CONFIG) + "Queue.xml"),
	rechecker(this),
	nextSearch(0),
	nextRecentSearch(0)
{ 
	TimerManager::getInstance()->addListener(this); 
	SearchManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);

	regexp.Init("[Rr0-9][Aa0-9][Rr0-9]");

	highestSel=0, highSel=0, normalSel=0, lowSel=0, calculations=0;
	File::ensureDirectory(Util::getListPath());
	File::ensureDirectory(Util::getBundlePath());
}

QueueManager::~QueueManager() noexcept { 
	SearchManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this); 
	ClientManager::getInstance()->removeListener(this);

	saveQueue(true);

	if(!BOOLSETTING(KEEP_LISTS)) {
		string path = Util::getListPath();

		std::sort(protectedFileLists.begin(), protectedFileLists.end());

		StringList filelists = File::findFiles(path, "*.xml.bz2");
		std::sort(filelists.begin(), filelists.end());
		std::for_each(filelists.begin(), std::set_difference(filelists.begin(), filelists.end(),
			protectedFileLists.begin(), protectedFileLists.end(), filelists.begin()), &File::deleteFile);

		filelists = File::findFiles(path, "*.DcLst");
		std::sort(filelists.begin(), filelists.end());
		std::for_each(filelists.begin(), std::set_difference(filelists.begin(), filelists.end(),
			protectedFileLists.begin(), protectedFileLists.end(), filelists.begin()), &File::deleteFile);
	}
}

bool QueueManager::getTTH(const string& name, TTHValue& tth) noexcept {
	Lock l(cs);
	QueueItem* qi = fileQueue.find(name);
	if(qi) {
		tth = qi->getTTH();
		return true;
	}
	return false;
}

struct PartsInfoReqParam{
	PartsInfo	parts;
	string		tth;
	string		myNick;
	string		hubIpPort;
	string		ip;
	uint16_t	udpPort;
};

void QueueManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	BundlePtr bundle;
	vector<const PartsInfoReqParam*> params;

	{
		Lock l(cs);

		//find max 10 pfs sources to exchange parts
		//the source basis interval is 5 minutes
		PFSSourceList sl;
		fileQueue.findPFSSources(sl);

		for(PFSSourceList::const_iterator i = sl.begin(); i != sl.end(); i++){
			QueueItem::PartialSource::Ptr source = (*i->first).getPartialSource();
			const QueueItem* qi = i->second;

			PartsInfoReqParam* param = new PartsInfoReqParam;
			
			int64_t blockSize = HashManager::getInstance()->getBlockSize(qi->getTTH());
			if(blockSize == 0)
				blockSize = qi->getSize();			
			qi->getPartialInfo(param->parts, blockSize);
			
			param->tth = qi->getTTH().toBase32();
			param->ip  = source->getIp();
			param->udpPort = source->getUdpPort();
			param->myNick = source->getMyNick();
			param->hubIpPort = source->getHubIpPort();

			params.push_back(param);

			source->setPendingQueryCount(source->getPendingQueryCount() + 1);
			source->setNextQueryTime(aTick + 300000);		// 5 minutes

		}

		bundle = findSearchBundle(aTick);
	}

	if(bundle != NULL) {
		searchBundle(bundle, false);
	}

	// Request parts info from partial file sharing sources
	for(vector<const PartsInfoReqParam*>::const_iterator i = params.begin(); i != params.end(); i++){
		const PartsInfoReqParam* param = *i;
		dcassert(param->udpPort > 0);
		
		try {
			AdcCommand cmd = SearchManager::getInstance()->toPSR(true, param->myNick, param->hubIpPort, param->tth, param->parts);
			Socket s;
			s.writeTo(param->ip, param->udpPort, cmd.toString(ClientManager::getInstance()->getMyCID()));
		} catch(...) {
			dcdebug("Partial search caught error\n");		
		}
		
		delete param;
	}
}

BundlePtr QueueManager::findSearchBundle(uint64_t aTick, bool force /* =false */) {
	BundlePtr bundle = NULL;
	if((BOOLSETTING(AUTO_SEARCH) && (aTick >= nextSearch) && (bundles.size() > 0)) || force) {
		int prioBundles = 0;
		bundle = fileQueue.findAutoSearch(prioBundles);
		int next = SETTING(SEARCH_TIME);
		if (bundle && prioBundles > 0) {
			next = max(60 / prioBundles, next);
		}
		nextSearch = aTick + (next * 60 * 1000); //this is also the time for next check, set it here so we dont need to start checking every minute
		//LogManager::getInstance()->message("Next search in " + Util::toString(next) + " minutes");
	} 
	
	if(!bundle && (BOOLSETTING(AUTO_SEARCH) && (aTick >= nextRecentSearch) || force)) {
		int recentBundles = 0;
		bundle = fileQueue.findRecent(recentBundles);
		//int next = SETTING(SEARCH_TIME);
		//if (bundle && recentBundles > 0) {
		//	next = max(60 / recentBundles, next);
		//}
		nextRecentSearch = aTick + ((recentBundles > 1 ? 5 : 10) * 60 * 1000);
		//LogManager::getInstance()->message("Next recent search in " + Util::toString(recentBundles > 1 ? 5 : 10) + " minutes");
	}

	if(bundle != NULL) {
		if (!bundle->getRecent()) {
			calculations++;
			switch((int)bundle->getPriority()) {
				case 2:
					lowSel++;
					break;
				case 3:
					normalSel++;
					break;
				case 4:
					highSel++;
					break;
				case 5:
					highestSel++;
					break;
			}
		} else {
			LogManager::getInstance()->message("Performing search for a RECENT bundle: " + bundle->getName());
		}
		//LogManager::getInstance()->message("Calculations performed: " + Util::toString(calculations) + ", highest: " + Util::toString(((double)highestSel/calculations)*100) + "%, high: " + Util::toString(((double)highSel/calculations)*100) + "%, normal: " + Util::toString(((double)normalSel/calculations)*100) + "%, low: " + Util::toString(((double)lowSel/calculations)*100) + "%");
	}
	return bundle;
}

void QueueManager::searchBundle(BundlePtr aBundle, bool newBundle) {
	string searchString;
	//boost::unordered_map<string, string> searches;
	StringPairList searches;
	for (auto i = aBundle->getBundleDirs().begin(); i != aBundle->getBundleDirs().end(); ++i) {
		string dir = Util::getDir(i->first, true, false);
		//if (searches.find(dir) != searches.end()) {
		if (find_if(searches.begin(), searches.end(), [&](const StringPair& sp) { return sp.first == dir; }) != searches.end()) {
			continue;
		}

		QueueItemList ql = i->second;
		if (ql.empty()) {
			continue;
		}

		size_t s = 0;
		searchString = Util::emptyString;

		//do a few guesses to get a random item
		while (s <= ql.size()) {
			auto pos = ql.begin();
			auto rand = Util::rand(ql.size());
			advance(pos, rand);
			QueueItem* q = *pos;
			if(q->getPriority() == QueueItem::PAUSED) {
				s++;
				continue;
			}
			if(q->isRunning()) {
				//it's ok but see if we can find better one
				searchString = q->getTTH().toBase32();
			} else {
				searchString = q->getTTH().toBase32();
				break;
			}
			s++;
		}

		if (!searchString.empty()) {
			searches.push_back(make_pair(dir, searchString));
			//searches[dir] = searchString;
			//SearchManager::getInstance()->search(searchString, 0, SearchManager::TYPE_TTH, SearchManager::SIZE_DONTCARE, "auto", Search::ALT_AUTO);
		}
	}

	if (searches.size() <= 5) {
		aBundle->setSimpleMatching(true);
		for (auto i = searches.begin(); i != searches.end(); ++i) {
			//LogManager::getInstance()->message("QueueManager::searchBundle, searchString: " + i->second);
			SearchManager::getInstance()->search(i->second, 0, SearchManager::TYPE_TTH, SearchManager::SIZE_DONTCARE, "auto", Search::ALT_AUTO);
		}
	} else {
		//use an alternative matching, choose random items to search for
		aBundle->setSimpleMatching(false);
		int k = 0;
		while (k < 5) {
			auto pos = searches.begin();
			auto rand = Util::rand(searches.size());
			advance(pos, rand);
			//LogManager::getInstance()->message("QueueManager::searchBundle, ALT searchString: " + pos->second);
			SearchManager::getInstance()->search(pos->second, 0, SearchManager::TYPE_TTH, SearchManager::SIZE_DONTCARE, "auto", Search::ALT_AUTO);
			searches.erase(pos);
			k++;
		}
	}

	if(BOOLSETTING(REPORT_ALTERNATES)) {
		//LogManager::getInstance()->message(STRING(ALTERNATES_SEND) + " " + Util::getFileName(qi->getTargetFileName()));
		if (aBundle->getSimpleMatching()) {
			if (!aBundle->getRecent()) {
				LogManager::getInstance()->message(STRING(ALTERNATES_SEND) + " " + aBundle->getName() + ", queued " + Util::toString(searches.size()) + " search(es), next search in " + Util::toString((nextSearch - GET_TICK()) / (60*1000)) + " minutes");
			} else {
				LogManager::getInstance()->message(STRING(ALTERNATES_SEND) + " " + aBundle->getName() + ", queued " + Util::toString(searches.size()) + " recent search(es), next recent search in " + Util::toString((nextRecentSearch - GET_TICK()) / (60*1000)) + " minutes and normal search in " + Util::toString((nextSearch - GET_TICK()) / (60*1000)) + " minutes");
			}
		} else {
			LogManager::getInstance()->message(STRING(ALTERNATES_SEND) + " " + aBundle->getName() + ", not using partial lists, next search in " + Util::formatTime(nextSearch - GET_TICK()));
		}
	}
}
 

void QueueManager::addList(const HintedUser& aUser, Flags::MaskType aFlags, const string& aInitialDir /* = Util::emptyString */) throw(QueueException, FileException) {
	add(aInitialDir, -1, TTHValue(), aUser, (Flags::MaskType)(QueueItem::FLAG_USER_LIST | aFlags));
}

string QueueManager::getListPath(const HintedUser& user) {
	StringList nicks = ClientManager::getInstance()->getNicks(user);
	string nick = nicks.empty() ? Util::emptyString : Util::cleanPathChars(nicks[0]) + ".";
	return checkTarget(Util::getListPath() + nick + user.user->getCID().toBase32(), /*checkExistence*/ false);
}
	
void QueueManager::add(const string& aTarget, int64_t aSize, const TTHValue& root, const HintedUser& aUser,
					   Flags::MaskType aFlags /* = 0 */, BundlePtr aBundle, bool addBad /* = true */) throw(QueueException, FileException)
{
	bool wantConnection = true;

	// Check that we're not downloading from ourselves...
	if(aUser == ClientManager::getInstance()->getMe()) {
		throw QueueException(STRING(NO_DOWNLOADS_FROM_SELF));
	}

	// Check if we're not downloading something already in our share
	if (BOOLSETTING(DONT_DL_ALREADY_SHARED)){
		if (!(aFlags & QueueItem::FLAG_CLIENT_VIEW) && !(aFlags & QueueItem::FLAG_USER_LIST) && !(aFlags & QueueItem::FLAG_PARTIAL_LIST)) {
			if (ShareManager::getInstance()->isTTHShared(root)){
				LogManager::getInstance()->message(STRING(FILE_ALREADY_SHARED) + " " + aTarget );
				throw QueueException(STRING(TTH_ALREADY_SHARED));
			}
		}
	}
	
	string target;
	string tempTarget;
	if((aFlags & QueueItem::FLAG_USER_LIST) == QueueItem::FLAG_USER_LIST) {
		if((aFlags & QueueItem::FLAG_PARTIAL_LIST) && !aTarget.empty()) {
			StringList nicks = ClientManager::getInstance()->getNicks(aUser);
			string nick = nicks.empty() ? Util::emptyString : Util::cleanPathChars(nicks[0]) + ".";
			target = Util::getListPath() + nick + Util::toString(Util::rand());
		} else {
			target = getListPath(aUser);
		}
		tempTarget = aTarget;
	} else {
		target = checkTarget(aTarget, /*checkExistence*/ true, aBundle);
	}

	// Check if it's a zero-byte file, if so, create and return...
	if(aSize == 0) {
		if(!BOOLSETTING(SKIP_ZERO_BYTE)) {
			File::ensureDirectory(target);
			File f(target, File::WRITE, File::CREATE);
		}
		return;
	}
	

	
	if( !SETTING(SKIPLIST_DOWNLOAD).empty() ){
		int pos = aTarget.rfind("\\")+1;
		
		if(BOOLSETTING(DOWNLOAD_SKIPLIST_USE_REGEXP)){
			string str1 = SETTING(SKIPLIST_DOWNLOAD);
			string str2 = aTarget.substr(pos);
			try {
				boost::regex reg(str1);
				if(boost::regex_search(str2.begin(), str2.end(), reg)){
					return;
				};
			} catch(...) {
			}
			/*PME regexp;
			regexp.Init(Text::utf8ToAcp(SETTING(SKIPLIST_DOWNLOAD)));
			if((regexp.IsValid()) && (regexp.match(Text::utf8ToAcp(aTarget.substr(pos))))) {
				return;
			}*/
		}else{
			if(Wildcard::patternMatch(Text::utf8ToAcp(aTarget.substr(pos)), Text::utf8ToAcp(SETTING(SKIPLIST_DOWNLOAD)), '|') ){
				return;
			}
		}
	}

	{
		Lock l(cs);
		if(BOOLSETTING(DONT_DL_ALREADY_QUEUED) && !(aFlags & QueueItem::FLAG_USER_LIST)) {
			QueueItemList ql = fileQueue.find(root);
			if (ql.size() > 0) {
				// Found one or more existing queue items, lets see if we can add the source to them
				bool sourceAdded = false;
				for(auto i = ql.begin(); i != ql.end(); ++i) {
					if(!(*i)->isSource(aUser)) {
						try {
							wantConnection = addSource(*i, aUser, addBad ? QueueItem::Source::FLAG_MASK : 0);
							sourceAdded = true;
						} catch(const Exception&) {
						//...
						}
					}
				}

				if(!sourceAdded) {
					LogManager::getInstance()->message(STRING(FILE_WITH_SAME_TTH) + " " + aTarget );
					throw QueueException(STRING(FILE_WITH_SAME_TTH));
				}
				goto connect;
			}
		}

		QueueItem* q = fileQueue.find(target);	
		if(q == NULL) {
			q = fileQueue.add(target, aSize, aFlags, QueueItem::DEFAULT, tempTarget, GET_TIME(), root);
			if (aBundle) {
				addBundleItem(q, aBundle, true, false);
				//LogManager::getInstance()->message("ADD BUNDLEITEM, items: " + Util::toString(aBundle->items.size()) + " totalsize: " + Util::formatBytes(aBundle->getSize()));
			} else if ((aFlags & QueueItem::FLAG_USER_LIST) != QueueItem::FLAG_USER_LIST && (aFlags & QueueItem::FLAG_CLIENT_VIEW) != QueueItem::FLAG_CLIENT_VIEW) {
				findBundle(q, true);
				//LogManager::getInstance()->message("ADD QI: NO BUNDLE");
			}
			fire(QueueManagerListener::Added(), q);
		} else {
			if(q->getSize() != aSize) {
				throw QueueException(STRING(FILE_WITH_DIFFERENT_SIZE));
			}
			if(!(root == q->getTTH())) {
				throw QueueException(STRING(FILE_WITH_DIFFERENT_TTH));
			}

			if(q->isFinished()) {
				throw QueueException(STRING(FILE_ALREADY_FINISHED));
			}

			q->setFlag(aFlags);
		}
		try {
			wantConnection = aUser.user && addSource(q, aUser, (Flags::MaskType)(addBad ? QueueItem::Source::FLAG_MASK : 0));
		} catch(const Exception&) {
			//...
		}

		if (aBundle) {
			//don't connect to directory bundle sources here
			return;
		}
	}
connect:
	bool smallSlot=false;
	if ((aFlags & QueueItem::FLAG_PARTIAL_LIST) || (aSize <= 65792 && !(aFlags & QueueItem::FLAG_USER_LIST) && (aFlags & QueueItem::FLAG_CLIENT_VIEW))) {
		smallSlot=true;
	}

	if(!aUser.user->isOnline())
		return;

	if(wantConnection || smallSlot) {
		ConnectionManager::getInstance()->getDownloadConnection(aUser, smallSlot);
	}

}

void QueueManager::readd(const string& target, const HintedUser& aUser) throw(QueueException) {
	bool wantConnection = false;
	{
		Lock l(cs);
		QueueItem* q = fileQueue.find(target);
		if(q && q->isBadSource(aUser)) {
			wantConnection = addSource(q, aUser, QueueItem::Source::FLAG_MASK);
		}
	}
	if(wantConnection && aUser.user->isOnline())
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
}

string QueueManager::checkTarget(const string& aTarget, bool checkExistence, BundlePtr aBundle) throw(QueueException, FileException) {
#ifdef _WIN32
	if(aTarget.length() > UNC_MAX_PATH) {
		throw QueueException(STRING(TARGET_FILENAME_TOO_LONG));
	}
	// Check that target starts with a drive or is an UNC path
	if( (aTarget[1] != ':' || aTarget[2] != '\\') &&
		(aTarget[0] != '\\' && aTarget[1] != '\\') ) {
		throw QueueException(STRING(INVALID_TARGET_FILE));
	}
#else
	if(aTarget.length() > PATH_MAX) {
		throw QueueException(STRING(TARGET_FILENAME_TOO_LONG));
	}
	// Check that target contains at least one directory...we don't want headless files...
	if(aTarget[0] != '/') {
		throw QueueException(STRING(INVALID_TARGET_FILE));
	}
#endif

	string target = Util::validateFileName(aTarget);

	// Check that the file doesn't already exist...
	if(checkExistence && File::getSize(target) != -1) {
		if (aBundle) {
			int64_t size = File::getSize(target);
			aBundle->increaseDownloaded(size);
			aBundle->increaseSize(size);
		}
		throw FileException(target + ": " + STRING(TARGET_FILE_EXISTS));
	}
	return target;	
}

/** Add a source to an existing queue item */
bool QueueManager::addSource(QueueItem* qi, const HintedUser& aUser, Flags::MaskType addBad) throw(QueueException, FileException) {
	
	if (!aUser.user) //atleast magnet links can cause this to happen.
	throw QueueException("Can't find Source user to add For Target: " + Util::getFileName(qi->getTarget()));

	if(qi->isFinished()) //no need to add source to finished item.
		throw QueueException("Already Finished: " + Util::getFileName(qi->getTarget()));
	
	bool wantConnection = (qi->getPriority() != QueueItem::PAUSED) && userQueue.getRunning(aUser).empty();
	if (qi->getBundle()) {
		if (qi->getPriority() != QueueItem::HIGHEST && qi->getBundle()->getPriority() == Bundle::PAUSED) {
			wantConnection = false;
		}
	} else {
		dcassert(qi->getPriority() == QueueItem::HIGHEST);
	}

	if(qi->isSource(aUser)) {
		if(qi->isSet(QueueItem::FLAG_USER_LIST)) {
			return wantConnection;
		}
		throw QueueException(STRING(DUPLICATE_SOURCE) + ": " + Util::getFileName(qi->getTarget()));
	}

	if(qi->isBadSourceExcept(aUser, addBad)) {
		throw QueueException(STRING(DUPLICATE_SOURCE) + ": " + Util::getFileName(qi->getTarget()));
	}

		qi->addSource(aUser);
		userQueue.add(qi, aUser);

		if ((!SETTING(SOURCEFILE).empty()) && (!BOOLSETTING(SOUNDS_DISABLED)))
			PlaySound(Text::toT(SETTING(SOURCEFILE)).c_str(), NULL, SND_FILENAME | SND_ASYNC);
	

	fire(QueueManagerListener::SourcesUpdated(), qi);
	changeBundleSource(qi, aUser, true);
	//setDirty();

	return wantConnection;
	
}

void QueueManager::addDirectory(const string& aDir, const HintedUser& aUser, const string& aTarget, QueueItem::Priority p /* = QueueItem::DEFAULT */, bool useFullList) noexcept {
	bool adc=true;
	if (aUser.user->isSet(User::NMDC) || useFullList)
		adc=false;
	bool needList;
	{
		Lock l(cs);
		
		auto dp = directories.equal_range(aUser);
		
		for(auto i = dp.first; i != dp.second; ++i) {
			if(stricmp(aTarget.c_str(), i->second->getName().c_str()) == 0)
				return;
		}
		
		// Unique directory, fine...
		directories.insert(make_pair(aUser, new DirectoryItem(aUser, aDir, aTarget, p)));
		needList = (dp.first == dp.second);
		//setDirty();  why do we need to save??
	}
	if(needList || adc) {
		try {
			if (!adc) {
				addList(aUser, QueueItem::FLAG_DIRECTORY_DOWNLOAD, aDir);
			} else {
				addList(aUser, QueueItem::FLAG_DIRECTORY_DOWNLOAD | QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_RECURSIVE_LIST, aDir);
			}
		} catch(const Exception&) {
			// Ignore, we don't really care...
		}
	}
}

QueueItem::Priority QueueManager::hasDownload(const UserPtr& aUser, bool smallSlot) noexcept {
	Lock l(cs);
	QueueItem* qi = userQueue.getNextPrioQI(aUser, 0, 0, smallSlot);
	if(qi) {
		dcassert(qi->getPriority() == QueueItem::HIGHEST);
		return QueueItem::HIGHEST;
	} else {
		qi = userQueue.getNextBundleQI(aUser, Bundle::LOWEST, 0, 0, smallSlot);
		if (qi) {
			return (QueueItem::Priority)qi->getBundle()->getPriority();
		}
	}
	return QueueItem::PAUSED;
}
namespace {
	//using vector for testing, atleast ram is cleared.
typedef vector<pair<TTHValue, const DirectoryListing::File*>> TTHMap;

// *** WARNING *** 
// Lock(cs) makes sure that there's only one thread accessing this
static TTHMap tthMap;

void buildMap(const DirectoryListing::Directory* dir) noexcept {
	for(DirectoryListing::Directory::List::const_iterator j = dir->directories.begin(); j != dir->directories.end(); ++j) {
		if(!(*j)->getAdls())
			buildMap(*j);
	}

	for(DirectoryListing::File::List::const_iterator i = dir->files.begin(); i != dir->files.end(); ++i) {
		const DirectoryListing::File* df = *i;
		tthMap.push_back(make_pair(df->getTTH(), df));
	}
}
}

int QueueManager::matchListing(const DirectoryListing& dl, bool partialList) noexcept {
	int matches = 0;
	bool wantConnection = false;
	{
		Lock l(cs);
		//uint64_t start = GET_TICK();
		tthMap.clear(); //incase we throw, map was not cleared at the end.
		buildMap(dl.getRoot());

		//our queue is most likely bigger than the partial list, so do it in this order
		if (partialList) {
			//LogManager::getInstance()->message("MATCHING PARTIAL LIST");
 			for (auto s = tthMap.begin(); s != tthMap.end(); ++s) {
				QueueItemList ql = fileQueue.find(s->first);
				if (!ql.empty()) {
					for (auto i = ql.begin(); i != ql.end(); ++i) {
						QueueItem* qi = (*i);
						if(qi->isFinished())
							continue;
						if(qi->isSet(QueueItem::FLAG_USER_LIST))
							continue;

						try {	 
							wantConnection = addSource(qi, dl.getHintedUser(), QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);
						} catch(...) {
							// Ignore...
						}
						matches++;
					}
				}
			}
		} else {
			//LogManager::getInstance()->message("MATCHING FULL LIST");
			for(auto i = fileQueue.getQueue().begin(); i != fileQueue.getQueue().end(); ++i) {
				QueueItem* qi = i->second;
				if(qi->isFinished())
					continue;
				if(qi->isSet(QueueItem::FLAG_USER_LIST))
					continue;
					TTHMap::iterator j = find_if(tthMap.begin(), tthMap.end(), CompareFirst<TTHValue, const DirectoryListing::File*>(qi->getTTH()));
					if(j != tthMap.end()) {
					if(j->second->getSize() == qi->getSize()) {
					try {
						wantConnection = addSource(qi, dl.getHintedUser(), QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);	
					} catch(const Exception&) {
						//...
					}
					matches++;
				}
			}
		}
		}
		tthMap.clear();
		//uint64_t end = GET_TICK();
		//LogManager::getInstance()->message("List matched in " + Util::toString(end-start) + " ms");
	}
	if((matches > 0) && wantConnection)
		ConnectionManager::getInstance()->getDownloadConnection(dl.getHintedUser());

		return matches;
}

bool QueueManager::getQueueInfo(const UserPtr& aUser, string& aTarget, int64_t& aSize, int& aFlags, string& bundleToken) noexcept {
	Lock l(cs);
	QueueItem* qi = userQueue.getNext(aUser);
	if(qi == NULL)
		return false;

	aTarget = qi->getTarget();
	aSize = qi->getSize();
	aFlags = qi->getFlags();
	if (qi->getBundle()) {
		bundleToken = qi->getBundle()->getToken();
	}

	return true;
}

uint8_t QueueManager::FileQueue::getMaxSegments(int64_t filesize) const {
	uint8_t MaxSegments = 1;

	if(BOOLSETTING(SEGMENTS_MANUAL)) {
		MaxSegments = min((uint8_t)SETTING(NUMBER_OF_SEGMENTS), (uint8_t)10);
	} else {
		if((filesize >= 2*1048576) && (filesize < 15*1048576)) {
			MaxSegments = 2;
		} else if((filesize >= (int64_t)15*1048576) && (filesize < (int64_t)30*1048576)) {
			MaxSegments = 3;
		} else if((filesize >= (int64_t)30*1048576) && (filesize < (int64_t)60*1048576)) {
			MaxSegments = 4;
		} else if((filesize >= (int64_t)60*1048576) && (filesize < (int64_t)120*1048576)) {
			MaxSegments = 5;
		} else if((filesize >= (int64_t)120*1048576) && (filesize < (int64_t)240*1048576)) {
			MaxSegments = 6;
		} else if((filesize >= (int64_t)240*1048576) && (filesize < (int64_t)480*1048576)) {
			MaxSegments = 7;
		} else if((filesize >= (int64_t)480*1048576) && (filesize < (int64_t)960*1048576)) {
			MaxSegments = 8;
		} else if((filesize >= (int64_t)960*1048576) && (filesize < (int64_t)1920*1048576)) {
			MaxSegments = 9;
		} else if(filesize >= (int64_t)1920*1048576) {
			MaxSegments = 10;
		}
	}

#ifdef _DEBUG
	return 88;
#else
	return MaxSegments;
#endif
}

StringList QueueManager::getTargets(const TTHValue& tth) {
	Lock l(cs);
	QueueItemList ql = fileQueue.find(tth);
	StringList sl;
	for(auto i = ql.begin(); i != ql.end(); ++i) {
		sl.push_back((*i)->getTarget());
	}
	return sl;
}

Download* QueueManager::getDownload(UserConnection& aSource, string& aMessage, bool smallSlot) noexcept {
	Lock l(cs);

	const UserPtr& u = aSource.getUser();
	dcdebug("Getting download for %s...", u->getCID().toBase32().c_str());

	QueueItem* q = userQueue.getNext(u, QueueItem::LOWEST, aSource.getChunkSize(), aSource.getSpeed(), true, smallSlot);

	if(!q) {
		aMessage = userQueue.getLastError();
		dcdebug("none\n");
		return 0;
	}

	// Check that the file we will be downloading to exists
	if(q->getDownloadedBytes() > 0) {
		if(!Util::fileExists(q->getTempTarget())) {
			// Temp target gone?
			q->resetDownloaded();
		}
	}

	bool partial = q->isSet(QueueItem::FLAG_PARTIAL_LIST);
	
	Download* d = new Download(aSource, *q, partial ? q->getTempTarget() : q->getTarget());
	if (q->getBundle()) {
		d->setBundle(q->getBundle());
	}
	if (partial) {
		d->setTempTarget(q->getTarget());
	}
	if (userQueue.addDownload(q, d)) {
		fire(QueueManagerListener::BundleUser(), q->getBundle()->getToken(), d->getHintedUser());
	}
	fire(QueueManagerListener::SourcesUpdated(), q);
	dcdebug("found %s\n", q->getTarget().c_str());
	return d;
}

namespace {
class TreeOutputStream : public OutputStream {
public:
	TreeOutputStream(TigerTree& aTree) : tree(aTree), bufPos(0) {
	}

	virtual size_t write(const void* xbuf, size_t len) throw(Exception) {
		size_t pos = 0;
		uint8_t* b = (uint8_t*)xbuf;
		while(pos < len) {
			size_t left = len - pos;
			if(bufPos == 0 && left >= TigerTree::BYTES) {
				tree.getLeaves().push_back(TTHValue(b + pos));
				pos += TigerTree::BYTES;
			} else {
				size_t bytes = min(TigerTree::BYTES - bufPos, left);
				memcpy(buf + bufPos, b + pos, bytes);
				bufPos += bytes;
				pos += bytes;
				if(bufPos == TigerTree::BYTES) {
					tree.getLeaves().push_back(TTHValue(buf));
					bufPos = 0;
				}
			}
		}
		return len;
	}

	virtual size_t flush() throw(Exception) {
		return 0;
	}
private:
	TigerTree& tree;
	uint8_t buf[TigerTree::BYTES];
	size_t bufPos;
};

}

void QueueManager::setFile(Download* d) {
	if(d->getType() == Transfer::TYPE_FILE) {
		Lock l(cs);

		QueueItem* qi = fileQueue.find(d->getPath());
		if(!qi) {
			throw QueueException(STRING(TARGET_REMOVED));
		}
		
		if(d->getOverlapped()) {
			d->setOverlapped(false);

			bool found = false;
			// ok, we got a fast slot, so it's possible to disconnect original user now
			for(DownloadList::const_iterator i = qi->getDownloads().begin(); i != qi->getDownloads().end(); ++i) {
				if((*i) != d && (*i)->getSegment().contains(d->getSegment())) {

					// overlapping has no sense if segment is going to finish
					if((*i)->getSecondsLeft() < 10)
						break;					

					found = true;

					// disconnect slow chunk
					(*i)->getUserConnection().disconnect();
					break;
				}
			}

			if(!found) {
				// slow chunk already finished ???
				throw QueueException(STRING(DOWNLOAD_FINISHED_IDLE));
			}
		}
		
		string target = d->getDownloadTarget();
		
		if(qi->getDownloadedBytes() > 0) {
			if(!Util::fileExists(qi->getTempTarget())) {
				// When trying the download the next time, the resume pos will be reset
				throw QueueException(STRING(TARGET_REMOVED));
			}
		} else {
			File::ensureDirectory(target);
		}

		// open stream for both writing and reading, because UploadManager can request reading from it
		SharedFileStream* f = new SharedFileStream(target, File::RW, File::OPEN | File::CREATE | File::NO_CACHE_HINT);

		// Only use antifrag if we don't have a previous non-antifrag part
		if(BOOLSETTING(ANTI_FRAG) && f->getSize() != qi->getSize()) {
			f->setSize(d->getTigerTree().getFileSize());
		}
		
		f->setPos(d->getSegment().getStart());
		d->setFile(f);
	} else if(d->getType() == Transfer::TYPE_FULL_LIST) {
		{
			Lock l(cs);

			QueueItem* qi = fileQueue.find(d->getPath());
			if(!qi) {
				throw QueueException(STRING(TARGET_REMOVED));
			}
		
			// set filelist's size
			qi->setSize(d->getSize());
		}

		string target = d->getPath();
		File::ensureDirectory(target);

		if(d->isSet(Download::FLAG_XML_BZ_LIST)) {
			target += ".xml.bz2";
		} else {
			target += ".xml";
		}
		d->setFile(new File(target, File::WRITE, File::OPEN | File::TRUNCATE | File::CREATE));
	} else if(d->getType() == Transfer::TYPE_PARTIAL_LIST) {
		d->setFile(new StringOutputStream(d->getPFS()));
	} else if(d->getType() == Transfer::TYPE_TREE) {
		d->setFile(new TreeOutputStream(d->getTigerTree()));		
	}
}

void QueueManager::moveFile(const string& source, const string& target, BundlePtr aBundle) {
	File::ensureDirectory(target);
	if(File::getSize(source) > MOVER_LIMIT) {
		mover.moveFile(source, target, aBundle);
	} else {
		moveFile_(source, target, aBundle);
	}
}

void QueueManager::moveFile_(const string& source, const string& target, BundlePtr aBundle) {
	try {
		File::renameFile(source, target);
		//getInstance()->fire(QueueManagerListener::FileMoved(), target);
	} catch(const FileException& /*e1*/) {
		// Try to just rename it to the correct name at least
		string newTarget = Util::getFilePath(source) + Util::getFileName(target);
		try {
			File::renameFile(source, newTarget);
			LogManager::getInstance()->message(source + " " + STRING(RENAMED_TO) + " " + newTarget);
		} catch(const FileException& e2) {
			LogManager::getInstance()->message(STRING(UNABLE_TO_RENAME) + " " + source + ": " + e2.getError());
		}
	}

	//dcassert(aBundle);
	if (!aBundle) {
		return;
	}

	if (aBundle->getQueueItems().empty()) {
		
		if (SETTING(SCAN_DL_BUNDLES) && !aBundle->getFileBundle()) {
			ShareScannerManager::getInstance()->scanBundle(aBundle);
		} else {
			string tmp;
			tmp.resize(STRING(DL_BUNDLE_FINISHED).size() + 512);	 
			tmp.resize(snprintf(&tmp[0], tmp.size(), CSTRING(DL_BUNDLE_FINISHED), aBundle->getName().c_str()));	 
			LogManager::getInstance()->message(tmp);
		}
		getInstance()->fire(QueueManagerListener::BundleFilesMoved(), aBundle);
	}
}


void QueueManager::moveStuckFile(QueueItem* qi) {

	moveFile(qi->getTempTarget(), qi->getTarget(), qi->getBundle());

	if(qi->isFinished()) {
		//Lock l (cs);
		userQueue.removeQI(qi);
	}

	string target = qi->getTarget();

	if(!BOOLSETTING(KEEP_FINISHED_FILES)) {
		fire(QueueManagerListener::Removed(), qi);
		removeBundleItem(qi, true, true);
	 } else {
		qi->addSegment(Segment(0, qi->getSize()));
		fire(QueueManagerListener::StatusUpdated(), qi);
	}

	fire(QueueManagerListener::RecheckAlreadyFinished(), target);
}

void QueueManager::rechecked(QueueItem* qi) {
	fire(QueueManagerListener::RecheckDone(), qi->getTarget());
	fire(QueueManagerListener::StatusUpdated(), qi);

	setBundleDirty(qi->getBundle());
}

void QueueManager::putDownload(Download* aDownload, bool finished, bool reportFinish) noexcept {
	HintedUserList getConn;
 	string fl_fname;
	string fl_path = Util::emptyString;
	HintedUser fl_user = aDownload->getHintedUser();
	Flags::MaskType fl_flag = 0;
	bool downloadList = false;
	bool tthList = aDownload->isSet(Download::FLAG_TTHLIST);

	{
		Lock l(cs);

		delete aDownload->getFile();
		aDownload->setFile(0);

		if(aDownload->getType() == Transfer::TYPE_PARTIAL_LIST) {
			QueueItem* q;
			if (!aDownload->getPath().empty()) {
				q = fileQueue.find(aDownload->getTempTarget());
			} else {
				//root directory in the partial list
				q = fileQueue.find(getListPath(aDownload->getHintedUser()));
			}
			if(q) {
				if(!aDownload->getPFS().empty()) {
					if( (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) && directories.find(aDownload->getUser()) != directories.end()) ||
						(q->isSet(QueueItem::FLAG_MATCH_QUEUE)) ||
						(q->isSet(QueueItem::FLAG_VIEW_NFO)))
					{
						dcassert(finished);
											
						fl_fname = aDownload->getPFS();
						fl_user = aDownload->getHintedUser();
						fl_path = aDownload->getPath();
						fl_flag = (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) ? (QueueItem::FLAG_DIRECTORY_DOWNLOAD) : 0)
							| (q->isSet(QueueItem::FLAG_PARTIAL_LIST) ? (QueueItem::FLAG_PARTIAL_LIST) : 0)
							| (q->isSet(QueueItem::FLAG_MATCH_QUEUE) ? QueueItem::FLAG_MATCH_QUEUE : 0) | QueueItem::FLAG_TEXT
							| (q->isSet(QueueItem::FLAG_VIEW_NFO) ? QueueItem::FLAG_VIEW_NFO : 0);
					} else {
						fire(QueueManagerListener::PartialList(), aDownload->getHintedUser(), aDownload->getPFS());
					}
				} else {
					// partial filelist probably failed, redownload full list
					dcassert(!finished);
					if (!q->isSet(QueueItem::FLAG_VIEW_NFO) || !aDownload->getUserConnection().isSet(UserConnection::FLAG_SMALL_SLOT))
						downloadList = true;
					fl_flag = q->getFlags() & ~QueueItem::FLAG_PARTIAL_LIST;	
				}
					
				fire(QueueManagerListener::Removed(), q);

				userQueue.removeQI(q);
				fileQueue.remove(q);
			}
		} else {
			QueueItem* q = fileQueue.find(aDownload->getPath());

			if(q) {
				if(aDownload->getType() == Transfer::TYPE_FULL_LIST) {
					if(aDownload->isSet(Download::FLAG_XML_BZ_LIST)) {
						q->setFlag(QueueItem::FLAG_XML_BZLIST);
					} else {
						q->unsetFlag(QueueItem::FLAG_XML_BZLIST);
					}
				}

				if(finished) {
					if(aDownload->getType() == Transfer::TYPE_TREE) {
						// Got a full tree, now add it to the HashManager
						dcassert(aDownload->getTreeValid());
						HashManager::getInstance()->addTree(aDownload->getTigerTree());

						userQueue.removeDownload(q, aDownload->getUser(), aDownload->getUserConnection().getToken());
						fire(QueueManagerListener::StatusUpdated(), q);
					} else {
						// Now, let's see if this was a directory download filelist...
						if( (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) && directories.find(aDownload->getHintedUser()) != directories.end()) ||
							(q->isSet(QueueItem::FLAG_MATCH_QUEUE)) ||
							(q->isSet(QueueItem::FLAG_VIEW_NFO))) 
						{
							fl_fname = q->getListName();
							fl_user = aDownload->getHintedUser();
							fl_flag = (q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) ? QueueItem::FLAG_DIRECTORY_DOWNLOAD : 0)
								| (q->isSet(QueueItem::FLAG_MATCH_QUEUE) ? QueueItem::FLAG_MATCH_QUEUE : 0)
								| (q->isSet(QueueItem::FLAG_VIEW_NFO) ? QueueItem::FLAG_VIEW_NFO : 0);
						} 

						string dir;

						if(aDownload->getType() == Transfer::TYPE_FULL_LIST) {
							dir = q->getTempTarget();
							q->addSegment(Segment(0, q->getSize()));
						} else if(aDownload->getType() == Transfer::TYPE_FILE) {
							aDownload->setOverlapped(false);
							q->addSegment(aDownload->getSegment());							
						}
						
						if(aDownload->getType() != Transfer::TYPE_FILE || q->isFinished()) {

							if(aDownload->getType() == Transfer::TYPE_FILE) {
								// For partial-share, abort upload first to move file correctly
								UploadManager::getInstance()->abortUpload(q->getTempTarget());

								// Disconnect all possible overlapped downloads
								for(DownloadList::const_iterator i = q->getDownloads().begin(); i != q->getDownloads().end(); ++i) {
									if(*i != aDownload)
										(*i)->getUserConnection().disconnect();
								}
							}

							if(BOOLSETTING(LOG_DOWNLOADS) && (BOOLSETTING(LOG_FILELIST_TRANSFERS) || aDownload->getType() == Transfer::TYPE_FILE)) {
								StringMap params;
								aDownload->getParams(aDownload->getUserConnection(), params);
								LOG(LogManager::DOWNLOAD, params);
							}
							
							fire(QueueManagerListener::Finished(), q, dir, aDownload);

							userQueue.removeQI(q);
							fire(QueueManagerListener::Removed(), q);

							BundlePtr bundle = q->getBundle();
							if (bundle) {
								removeBundleItem(q, true, true);
							} else {
								fileQueue.remove(q);
							}

							// Check if we need to move the file
							if( aDownload->getType() == Transfer::TYPE_FILE && !aDownload->getTempTarget().empty() && (stricmp(aDownload->getPath().c_str(), aDownload->getTempTarget().c_str()) != 0) ) {
								moveFile(aDownload->getTempTarget(), aDownload->getPath(), bundle);
							}

							//if(!BOOLSETTING(KEEP_FINISHED_FILES) || aDownload->getType() == Transfer::TYPE_FULL_LIST) {
							//	fire(QueueManagerListener::Removed(), q);
							//	removeBundleItem(q, true, true);
							//} else {
							//	fire(QueueManagerListener::StatusUpdated(), q, false);
							//}
						} else {
							userQueue.removeDownload(q, aDownload->getUser(), aDownload->getUserConnection().getToken());
							if(aDownload->getType() != Transfer::TYPE_FILE || (reportFinish && q->isWaiting())) {
								fire(QueueManagerListener::StatusUpdated(), q);
							}
							if (q->getBundle()) {
								setBundleDirty(q->getBundle());
							}
						}
					}
				} else {
					if(aDownload->getType() != Transfer::TYPE_TREE) {
						if(q->getDownloadedBytes() == 0) {
							q->setTempTarget(Util::emptyString);
						}
						if(q->isSet(QueueItem::FLAG_USER_LIST)) {
							// Blah...no use keeping an unfinished file list...
							File::deleteFile(q->getListName());
						}
						if(aDownload->getType() == Transfer::TYPE_FILE) {
							// mark partially downloaded chunk, but align it to block size
							int64_t downloaded = aDownload->getPos();
							downloaded -= downloaded % aDownload->getTigerTree().getBlockSize();

							if(downloaded > 0) {
								// since download is not finished, it should never happen that downloaded size is same as segment size
								dcassert(downloaded < aDownload->getSize());
								
								q->addSegment(Segment(aDownload->getStartPos(), downloaded));
								setBundleDirty(q->getBundle());
							}
						}
					}

					if(q->getPriority() != QueueItem::PAUSED) {
						q->getOnlineUsers(getConn);
					}
	
					userQueue.removeDownload(q, aDownload->getUser(), aDownload->getUserConnection().getToken());
					fire(QueueManagerListener::StatusUpdated(), q);

					if(aDownload->isSet(Download::FLAG_OVERLAP)) {
						// overlapping segment disconnected, unoverlap original segment
						for(DownloadList::const_iterator i = q->getDownloads().begin(); i != q->getDownloads().end(); ++i) {
							if((*i)->getSegment().contains(aDownload->getSegment())) {
								(*i)->setOverlapped(false);
								break;
							}
						}
					}
 				}
 			} else if(aDownload->getType() != Transfer::TYPE_TREE) {
				string path = aDownload->getPath();
				if(aDownload->getType() == Transfer::TYPE_FULL_LIST) {
					// delete unfinished lists manually removed from queue
					if(aDownload->isSet(Download::FLAG_XML_BZ_LIST)) {
						path += ".xml.bz2";
					} else {
						path += ".xml";
					}
					File::deleteFile(path);
				} else if(!aDownload->getTempTarget().empty() && aDownload->getTempTarget() != path) {
 					File::deleteFile(aDownload->getTempTarget());
 				}
 			}
		}
		delete aDownload;
	}

	for(HintedUserList::const_iterator i = getConn.begin(); i != getConn.end(); ++i) {
		ConnectionManager::getInstance()->getDownloadConnection(*i);
	}

	if(!fl_fname.empty()) {
		if (tthList) {	 
			matchTTHList(fl_fname, fl_user, fl_flag);	 
		} else {	 
			processList(fl_fname, fl_user, fl_path, fl_flag);	 
		}
	}

	// partial file list failed, redownload full list
	if(fl_user.user->isOnline() && downloadList) {
		try {
			addList(fl_user, fl_flag);
		} catch(const Exception&) {}
	}
}

void QueueManager::matchTTHList(const string& name, const HintedUser& user, int flags) {	 
	dcdebug("matchTTHList");
	if(flags & QueueItem::FLAG_MATCH_QUEUE) {
		bool wantConnection = false;
		int matches = 0;
		 
			typedef unordered_set<TTHValue> TTHSet;
			typedef TTHSet::const_iterator TTHSetIter;
			TTHSet tthList;
 	 
			size_t start = 0;
			while (start+39 < name.length()) {
				tthList.insert(name.substr(start, 39));
				start = start+40;
			}
 	 
			if(tthList.empty())
				return;
 			{	 
				Lock l(cs);

			for (auto s = tthList.begin(); s != tthList.end(); ++s) {
				QueueItemList ql = fileQueue.find(*s);
				if (!ql.empty()) {
					for (auto i = ql.begin(); i != ql.end(); ++i) {
						QueueItem* qi = (*i);
						if(qi->isFinished())
							continue;
						if(qi->isSet(QueueItem::FLAG_USER_LIST))
							continue;

						try {	 
							wantConnection = addSource(qi, user, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE);
						} catch(...) {
							// Ignore...
						}
						matches++;
					}
				}
			}
		}

		if((matches > 0) && wantConnection)
			ConnectionManager::getInstance()->getDownloadConnection(user); 
	}
 }

void QueueManager::processList(const string& name, const HintedUser& user, const string path, int flags) {
	DirectoryListing dirList(user);
	try {
		if(flags & QueueItem::FLAG_TEXT) {
			MemoryInputStream mis(name);
			dirList.loadXML(mis, true, false, false);
		} else {
			dirList.loadFile(name, false, false);
		}
	} catch(const Exception&) {
		LogManager::getInstance()->message(STRING(UNABLE_TO_OPEN_FILELIST) + " " + name);
		return;
	}

	if(flags & QueueItem::FLAG_DIRECTORY_DOWNLOAD) {
		if (!path.empty()) {
			DirectoryItem* d = NULL;
			{
				Lock l(cs);
				auto dp = directories.equal_range(user);
				for(auto i = dp.first; i != dp.second; ++i) {
					if(stricmp(path.c_str(), i->second->getName().c_str()) == 0) {
						d = i->second;
						directories.erase(i);
						break;
					}
				}
			}
			if(d != NULL){
				/* fix deadlock!!  cant call to download inside a lock, the lock time becomes too long and goes too deep,
				threads in Queuemanager and Connectionmanager will lock each other. */
				dirList.download(d->getName(), d->getTarget(), false, d->getPriority(), true);
				delete d;
			}

		} else {
			vector<DirectoryItemPtr> dl;
			{
				Lock l(cs);
				auto dpf = directories.equal_range(user) | map_values;
				dl.assign(boost::begin(dpf), boost::end(dpf));
				directories.erase(user);
			}
			for(auto i = dl.begin(); i != dl.end(); ++i) {
				DirectoryItem* di = *i;
				dirList.download(di->getName(), di->getTarget(), false, di->getPriority());
				delete di;
			}
		}
	}
	if(flags & QueueItem::FLAG_MATCH_QUEUE) {
		const size_t BUF_SIZE = STRING(MATCHED_FILES).size() + 16;
		string tmp;
		tmp.resize(BUF_SIZE);
		snprintf(&tmp[0], tmp.size(), CSTRING(MATCHED_FILES), matchListing(dirList, (flags & QueueItem::FLAG_PARTIAL_LIST ? true : false)));
		if(flags & QueueItem::FLAG_PARTIAL_LIST) {
			//no report
		} else {
			LogManager::getInstance()->message(Util::toString(ClientManager::getInstance()->getNicks(user)) + ": " + tmp);
		}
	}
	if((flags & QueueItem::FLAG_VIEW_NFO) && (flags & QueueItem::FLAG_PARTIAL_LIST)) {
		findNfo(dirList.getRoot(), dirList);
	}
}

bool QueueManager::findNfo(const DirectoryListing::Directory* dl, const DirectoryListing& dir) noexcept {

	for(DirectoryListing::Directory::List::const_iterator j = dl->directories.begin(); j != dl->directories.end(); ++j) {
		if(!(*j)->getAdls())
			findNfo(*j, dir);
	}


	if (!dl->files.empty()) {
		boost::wregex reg;
		reg.assign(_T("(.+\\.nfo)"), boost::regex_constants::icase);
		for(DirectoryListing::File::List::const_iterator i = dl->files.begin(); i != dl->files.end(); ++i) {
			const DirectoryListing::File* df = *i;
			if (regex_match(Text::toT(df->getName()), reg)) {
				QueueManager::getInstance()->add(Util::getTempPath() + df->getName(), df->getSize(), df->getTTH(), dir.getHintedUser(), QueueItem::FLAG_CLIENT_VIEW | QueueItem::FLAG_TEXT);
				return true;
			}
		}
		//can be reported because this is the only folder containing files in partial list
		LogManager::getInstance()->message(Util::toString(ClientManager::getInstance()->getNicks(dir.getHintedUser())) + ": " + STRING(NO_NFO_FOUND));
	}
	
	return false;
}

void QueueManager::recheck(const string& aTarget) {
	rechecker.add(aTarget);
}

void QueueManager::remove(const string& aTarget) noexcept {
	UserConnectionList x;

	{
		Lock l(cs);

		QueueItem* q = fileQueue.find(aTarget);
		if(!q)
			return;

		if(q->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD)) {
			dcassert(q->getSources().size() == 1);
			auto dp = directories.equal_range(q->getSources()[0].getUser());
			for(auto i = dp.first; i != dp.second; ++i) {
				delete i->second;
			}
			directories.erase(q->getSources()[0].getUser());
		}

		// For partial-share
		UploadManager::getInstance()->abortUpload(q->getTempTarget());

		if(q->isRunning()) {
			for(DownloadList::iterator i = q->getDownloads().begin(); i != q->getDownloads().end(); ++i) {
				UserConnection* uc = &(*i)->getUserConnection();
				x.push_back(uc);
			}
		} else if(!q->getTempTarget().empty() && q->getTempTarget() != q->getTarget()) {
			File::deleteFile(q->getTempTarget());
		}

		fire(QueueManagerListener::Removed(), q);

		if(!q->isFinished()) {
			userQueue.removeQI(q);
		}
		
		removeBundleItem(q, false, true);

	}

	for(UserConnectionList::const_iterator i = x.begin(); i != x.end(); ++i) {
		(*i)->disconnect(true);
	}
}

void QueueManager::removeSource(const string& aTarget, const UserPtr& aUser, Flags::MaskType reason, bool removeConn /* = true */) noexcept {
	bool isRunning = false;
	bool removeCompletely = false;
	{
		Lock l(cs);
		QueueItem* q = fileQueue.find(aTarget);
		if(!q)
			return;

		if(!q->isSource(aUser))
			return;
	
		if(q->isSet(QueueItem::FLAG_USER_LIST)) {
			removeCompletely = true;
			goto endCheck;
		}

		if(reason == QueueItem::Source::FLAG_NO_TREE) {
			q->getSource(aUser)->setFlag(reason);
			return;
		}

		if(q->isRunning()) {
			isRunning = true;
		}
		if(!q->isFinished()) {
			userQueue.removeQI(q, aUser, false);
		}
		q->removeSource(aUser, reason);
		
		fire(QueueManagerListener::SourcesUpdated(), q);
		setBundleDirty(q->getBundle());
	}
endCheck:
	if(isRunning && removeConn) {
		DownloadManager::getInstance()->abortDownload(aTarget, aUser);
	}
	if(removeCompletely) {
		remove(aTarget);
	}	
}

void QueueManager::removeSource(const UserPtr& aUser, Flags::MaskType reason) noexcept {
	// @todo remove from finished items
	bool isRunning = false;
	string removeRunning;
	{
		Lock l(cs);
		QueueItem* qi = NULL;
		while( (qi = userQueue.getNext(aUser, QueueItem::PAUSED)) != NULL) {
			if(qi->isSet(QueueItem::FLAG_USER_LIST)) {
				remove(qi->getTarget());
			} else {
				userQueue.removeQI(qi, aUser, false);
				qi->removeSource(aUser, reason);
				fire(QueueManagerListener::SourcesUpdated(), qi);
				setBundleDirty(qi->getBundle());
			}
		}

		isRunning = !userQueue.getRunning(aUser).empty();
	}

	if(isRunning) {
		ConnectionManager::getInstance()->disconnect(aUser, true);
	}
	if(!removeRunning.empty()) {
		remove(removeRunning);
	}	
}

void QueueManager::setBundlePriority(const string& bundleToken, Bundle::Priority p) noexcept {
	BundlePtr bundle = findBundle(bundleToken);
	if (bundle) {
		setBundlePriority(bundle, p, false);
	}
}

void QueueManager::setBundlePriority(BundlePtr aBundle, Bundle::Priority p, bool isAuto) noexcept {

	//LogManager::getInstance()->message("Changing priority to: " + Util::toString(p));
	Bundle::Priority oldPrio = aBundle->getPriority();
	if (oldPrio == p) {
		//LogManager::getInstance()->message("Prio not changed: " + Util::toString(oldPrio));
		return;
	}
	{
		Lock l (cs);
		userQueue.setBundlePriority(aBundle, p);
		fileQueue.setSearchPriority(aBundle, oldPrio, p);
	}
	if (!isAuto) {
		aBundle->setAutoPriority(false);
	}

	if (p == Bundle::PAUSED) {
		Lock l (cs);
		//LogManager::getInstance()->message("Pausing bundle...");
		DownloadList disconnect = aBundle->getDownloads();
		//bundle->getDownloadsQI(disconnect);
		for(auto i = disconnect.begin(); i != disconnect.end(); ++i) {
			//LogManager::getInstance()->message("Disconnecting download!");
			Download* d = *i;
			d->getUserConnection().disconnect(true);
		}
	} else if (oldPrio == Bundle::PAUSED) {
		//LogManager::getInstance()->message("Starting paused bundle");
		HintedUserList sources;
		aBundle->getQISources(sources);
		for (auto i = sources.begin(); i != sources.end(); ++i) {
			HintedUser aUser = *i;
			if(aUser.user->isOnline()) {
				ConnectionManager::getInstance()->getDownloadConnection(aUser);
			}
		}
	}
	fire(QueueManagerListener::BundlePriority(), aBundle);
	aBundle->setDirty(true);
	//LogManager::getInstance()->message("Prio changed to: " + Util::toString(bundle->getPriority()));
}

void QueueManager::setBundleAutoPriority(const string& bundleToken) noexcept {
	BundlePtr bundle = findBundle(bundleToken);
	if (bundle) {
		bundle->setAutoPriority(!bundle->getAutoPriority());
		bundle->setDirty(true);
	}
}

void QueueManager::removeBundleSource(const string& bundleToken, const UserPtr& aUser) noexcept {
	BundlePtr bundle = findBundle(bundleToken);
	if (bundle) {
		removeBundleSource(bundle, aUser);
	}
}

void QueueManager::removeBundleSource(BundlePtr aBundle, const UserPtr& aUser) noexcept {
	if (aBundle) {
		for (auto i = aBundle->getQueueItems().begin(); i != aBundle->getQueueItems().end(); ++i) {
			//(*i)->removeSource(aUser, QueueItem::Source::FLAG_REMOVED);
			removeSource((*i)->getTarget(), aUser, QueueItem::Source::FLAG_REMOVED);
		}
		dcassert(!aBundle->isSource(aUser));
		//TODO: handle in correct place
		SearchManager::getInstance()->removeUserPBD(aUser, aBundle);
	}
}

void QueueManager::setBundleDirty(BundlePtr aBundle) {
	if (!aBundle) {
		return;
	}
	aBundle->setDirty(true);
}

void QueueManager::changeBundleSource(QueueItem* qi, const HintedUser& aUser, bool add) noexcept {
	BundlePtr bundle = qi->getBundle();
	if (!bundle) {
		return;
	}

	bundle->setDirty(true);
	/*if (add) {
		bundle->addSource(aUser);
	} else {
		bundle->removeSource(aUser.user);
	} */
}

void QueueManager::setPriority(const string& aTarget, QueueItem::Priority p) noexcept {
	HintedUserList getConn;
	bool running = false;

	{
		Lock l(cs);
	
		QueueItem* q = fileQueue.find(aTarget);
		if( (q != NULL) && (q->getPriority() != p) && !q->isFinished() ) {
			if (!q->getBundle()) {
				//those should always use the highest prio
				return;
			}
			running = q->isRunning();
			if(q->getPriority() == QueueItem::PAUSED || p == QueueItem::HIGHEST) {
				// Problem, we have to request connections to all these users...
				q->getOnlineUsers(getConn);
			}
			userQueue.setQIPriority(q, p);
			setBundleDirty(q->getBundle());
			fire(QueueManagerListener::StatusUpdated(), q);
		}
	}

	if(p == QueueItem::PAUSED) {
		if(running)
			DownloadManager::getInstance()->abortDownload(aTarget);
	} else {
		for(HintedUserList::const_iterator i = getConn.begin(); i != getConn.end(); ++i) {
			ConnectionManager::getInstance()->getDownloadConnection(*i);
		}
	}
}

void QueueManager::setAutoPriority(const string& aTarget, bool ap) noexcept {
	vector<pair<string, QueueItem::Priority>> priorities;

	{
		Lock l(cs);
	
		QueueItem* q = fileQueue.find(aTarget);
		if( (q != NULL) && (q->getAutoPriority() != ap) ) {
			q->setAutoPriority(ap);
			if(ap) {
				priorities.push_back(make_pair(q->getTarget(), q->calculateAutoPriority()));
			}
			setBundleDirty(q->getBundle());
			fire(QueueManagerListener::StatusUpdated(), q);
		}
	}

	for(vector<pair<string, QueueItem::Priority>>::const_iterator p = priorities.begin(); p != priorities.end(); p++) {
		setPriority((*p).first, (*p).second);
	}
}

void QueueManager::saveQueue(bool force, uint64_t aTick) noexcept {
	try {
		Lock l(cs);	
		
		BundleList fileBundles;
		for (auto i = bundles.begin(); i != bundles.end(); ++i) {
			BundlePtr bundle = i->second;
			if (!bundle->getQueueItems().empty() && (bundle->getDirty() || force)) {
				saveBundle(bundle);
			}
		}
	} catch(...) {
		// ...
	}
	// Put this here to avoid very many saves tries when disk is full...
	lastSave = GET_TICK();
}

void QueueManager::saveBundle(BundlePtr bundle) {
	//LogManager::getInstance()->message("SAVING BUNDLE: " + bundle->getName());
	File ff(bundle->getBundleFile() + ".tmp", File::WRITE, File::CREATE | File::TRUNCATE);
	BufferedOutputStream<false> f(&ff);
	f.write(SimpleXML::utf8Header);
	string tmp;
	string b32tmp;

	if (bundle->getFileBundle()) {
		f.write(LIT("<File Version=\"1.0\" Token=\""));
		f.write(bundle->getToken());
		f.write(LIT("\">\r\n"));
		saveQI(f, bundle->getQueueItems().front(), tmp, b32tmp);
		f.write(LIT("</File>\r\n"));
	} else {
		//f.write(LIT("<Downloads Version=\"" VERSIONSTRING "\">\r\n"));
		f.write(LIT("<Bundle Version=\"1.0\" Target=\""));
		f.write(SimpleXML::escape(bundle->getTarget(), tmp, true));
		f.write(LIT("\" Token=\""));
		f.write(bundle->getToken());
		f.write(LIT("\" Size=\""));
		f.write(Util::toString(bundle->getSize()));
		f.write(LIT("\" Added=\""));
		f.write(Util::toString(bundle->getAdded()));
		f.write(LIT("\" Date=\""));
		f.write(Util::toString(bundle->getDirDate()));
		if (!bundle->getAutoPriority()) {
			f.write(LIT("\" Priority=\""));
			f.write(Util::toString((int)bundle->getPriority()));
		}
		f.write(LIT("\">\r\n"));

		for (auto k = bundle->getFinishedFiles().begin(); k != bundle->getFinishedFiles().end(); ++k) {
			QueueItem* qi = *k;
			f.write(LIT("\t<Finished TTH=\""));
			f.write(qi->getTTH().toBase32());
			f.write(LIT("\" Target=\""));
			f.write(qi->getTarget());
			f.write(LIT("\" Size=\""));
			f.write(Util::toString(qi->getSize()));
			f.write(LIT("\" Added=\""));
			f.write(Util::toString(qi->getAdded()));
			f.write(LIT("\"/>\r\n"));
		}

		for (auto j = bundle->getQueueItems().begin(); j != bundle->getQueueItems().end(); ++j) {
			saveQI(f, *j, tmp, b32tmp);
		}

		f.write(LIT("</Bundle>\r\n"));
		//f.write("</Downloads>\r\n");
	}

	f.flush();
	ff.close();
	//File::deleteFile(bundle->getBundleFile() + ".bak");
	//File::copyFile(bundle->getBundleFile(), bundle->getBundleFile() + ".bak");
	try {
		File::deleteFile(bundle->getBundleFile());
		File::renameFile(bundle->getBundleFile() + ".tmp", bundle->getBundleFile());
	}catch(...) {
		LogManager::getInstance()->message("ERROR WHEN MOVING BUNDLEXML: " + bundle->getName());
	}
	bundle->setDirty(false);
}

void QueueManager::saveQI(OutputStream &f, QueueItem* qi, string tmp, string b32tmp) {
	string indent = "\t";
	//if (bundle)
	//	indent = "\t\t";

	f.write(indent);
	f.write(LIT("<Download Target=\""));
	f.write(SimpleXML::escape(qi->getTarget(), tmp, true));
	f.write(LIT("\" Size=\""));
	f.write(Util::toString(qi->getSize()));
	f.write(LIT("\" Priority=\""));
	f.write(Util::toString((int)qi->getPriority()));
	f.write(LIT("\" Added=\""));
	f.write(Util::toString(qi->getAdded()));
	b32tmp.clear();
	f.write(LIT("\" TTH=\""));
	f.write(qi->getTTH().toBase32(b32tmp));
	if(!qi->getDone().empty()) {
		f.write(LIT("\" TempTarget=\""));
		f.write(SimpleXML::escape(qi->getTempTarget(), tmp, true));
	}
	f.write(LIT("\" AutoPriority=\""));
	f.write(Util::toString(qi->getAutoPriority()));
	f.write(LIT("\" MaxSegments=\""));
	f.write(Util::toString(qi->getMaxSegments()));
	//if (!qi->getBundleToken().empty()) {
	//	f.write(LIT("\" BundleToken=\""));
	//	f.write(qi->getBundleToken());
	//}

	f.write(LIT("\">\r\n"));

	for(QueueItem::SegmentSet::const_iterator i = qi->getDone().begin(); i != qi->getDone().end(); ++i) {
		f.write(indent);
		f.write(LIT("\t<Segment Start=\""));
		f.write(Util::toString(i->getStart()));
		f.write(LIT("\" Size=\""));
		f.write(Util::toString(i->getSize()));
		f.write(LIT("\"/>\r\n"));
	}

	for(QueueItem::SourceConstIter j = qi->sources.begin(); j != qi->sources.end(); ++j) {
		if(j->isSet(QueueItem::Source::FLAG_PARTIAL)) continue;
					
		const CID& cid = j->getUser().user->getCID();
		const string& hint = j->getUser().hint;

		f.write(indent);
		f.write(LIT("\t<Source CID=\""));
		f.write(cid.toBase32());
		f.write(LIT("\" Nick=\""));
		f.write(SimpleXML::escape(ClientManager::getInstance()->getNicks(cid, hint)[0], tmp, true));
		if(!hint.empty()) {
			f.write(LIT("\" HubHint=\""));
			f.write(hint);
		}
		f.write(LIT("\"/>\r\n"));
	}

	f.write(indent);
	f.write(LIT("</Download>\r\n"));
}

class QueueLoader : public SimpleXMLReader::CallBack {
public:
	QueueLoader() : cur(NULL), inDownloads(false), inBundle(false), inFile(false) { }
	~QueueLoader() { }
	void startTag(const string& name, StringPairList& attribs, bool simple);
	void endTag(const string& name, const string& data);
private:
	string target;

	QueueItem* cur;
	BundlePtr curBundle;
	bool inDownloads;
	bool inBundle;
	bool inFile;
};

void QueueManager::loadQueue() noexcept {
	try {
		QueueLoader l;
		Util::migrate(getQueueFile());

		StringList fileList = File::findFiles(Util::getPath(Util::PATH_BUNDLES), "Bundle*");
		for (StringIter i = fileList.begin(); i != fileList.end(); ++i) {
			//LogManager::getInstance()->message("LOADING BUNDLE1: " + *i);
			if ((*i).substr((*i).length()-4, 4) == ".xml") {
				//LogManager::getInstance()->message("LOADING BUNDLE2: " + *i);
				File f(*i, File::READ, File::OPEN);
				SimpleXMLReader(&l).parse(f);
			}
		}

		//load the old queue file
		File f(getQueueFile(), File::READ, File::OPEN);
		SimpleXMLReader(&l).parse(f);
	} catch(const Exception&) {
		//LogManager::getInstance()->message("QUEUEUEUEUE EXCEPTION");
		// ...
	}
}

static const string sFile = "File";
static const string sBundle = "Bundle";
static const string sName = "Name";
static const string sToken = "Token";
static const string sDownload = "Download";
static const string sTempTarget = "TempTarget";
static const string sTarget = "Target";
static const string sSize = "Size";
static const string sDownloaded = "Downloaded";
static const string sPriority = "Priority";
static const string sSource = "Source";
static const string sNick = "Nick";
static const string sDirectory = "Directory";
static const string sAdded = "Added";
static const string sDate = "Date";
static const string sTTH = "TTH";
static const string sCID = "CID";
static const string sHubHint = "HubHint";
static const string sSegment = "Segment";
static const string sStart = "Start";
static const string sAutoPriority = "AutoPriority";
static const string sMaxSegments = "MaxSegments";
static const string sBundleToken = "BundleToken";
static const string sFinished = "Finished";



void QueueLoader::startTag(const string& name, StringPairList& attribs, bool simple) {
	QueueManager* qm = QueueManager::getInstance();
	if(!inDownloads && name == "Downloads") {
		inDownloads = true;
	} else if (!inFile && name == sFile) {
		const string& token = getAttrib(attribs, sToken, 1);
		if(token.empty())
			return;

		curBundle = BundlePtr(new Bundle(Util::emptyString, true, GET_TIME()));
		curBundle->setToken(token);
		inFile = true;		
	} else if (!inBundle && name == sBundle) {
		const string& bundleTarget = getAttrib(attribs, sTarget, 0);
		//LogManager::getInstance()->message("BUNDLEFOUND!!!!!!!!!!!!!: " + bundleTarget);
		const string& token = getAttrib(attribs, sToken, 1);
		if(token.empty())
			return;

		const string& prio = getAttrib(attribs, sPriority, 3);
		time_t added = static_cast<time_t>(Util::toInt(getAttrib(attribs, sAdded, 4)));
		time_t dirDate = static_cast<time_t>(Util::toInt(getAttrib(attribs, sDate, 5)));
		if(added == 0) {
			added = GET_TIME();
		}

		BundlePtr bundle = BundlePtr(new Bundle(bundleTarget, false, added));
		if (!prio.empty()) {
			bundle->setAutoPriority(false);
			bundle->setPriority((Bundle::Priority)Util::toInt(prio));
		} else {
			bundle->setAutoPriority(true);
			bundle->setPriority(Bundle::LOW);
		}
		bundle->setDirDate(dirDate);
		bundle->setToken(token);
		curBundle = bundle;
		inBundle = true;		
	} else if(inDownloads || inBundle || inFile) {
		if(cur == NULL && name == sDownload) {
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 1));
			if(size == 0)
				return;
			try {
				const string& tgt = getAttrib(attribs, sTarget, 0);
				// @todo do something better about existing files
				target = QueueManager::checkTarget(tgt, false, (curBundle ? curBundle : NULL));
				if(target.empty())
					return;
			} catch(const Exception&) {
				return;
			}
			QueueItem::Priority p = (QueueItem::Priority)Util::toInt(getAttrib(attribs, sPriority, 3));
			time_t added = static_cast<time_t>(Util::toInt(getAttrib(attribs, sAdded, 4)));
			const string& tthRoot = getAttrib(attribs, sTTH, 5);
			if(tthRoot.empty())
				return;

			string tempTarget = getAttrib(attribs, sTempTarget, 5);
			uint8_t maxSegments = (uint8_t)Util::toInt(getAttrib(attribs, sMaxSegments, 5));
			int64_t downloaded = Util::toInt64(getAttrib(attribs, sDownloaded, 5));
			if (downloaded > size || downloaded < 0)
				downloaded = 0;

			if(added == 0)
				added = GET_TIME();

			QueueItem* qi = qm->fileQueue.find(target);

			if(qi == NULL) {
				qi = qm->fileQueue.add(target, size, 0, p, tempTarget, added, TTHValue(tthRoot));
				if(downloaded > 0) {
					qi->addSegment(Segment(0, downloaded));
					qi->setPriority(qi->calculateAutoPriority());
				}

				bool ap = Util::toInt(getAttrib(attribs, sAutoPriority, 6)) == 1;
				qi->setAutoPriority(ap);
				qi->setMaxSegments(max((uint8_t)1, maxSegments));

				//bundles
				if (curBundle && (inBundle || inFile)) {
					//LogManager::getInstance()->message("itemtoken exists: " + bundleToken);
					qm->addBundleItem(qi, curBundle, true, true);
				} else if (inDownloads) {
					//assign bundles for old queue items / single file bundle
					//LogManager::getInstance()->message("BUNDLETOKEN EMPTY");
					curBundle = qm->createFileBundle(qi);
				}
				
				qm->fire(QueueManagerListener::Added(), qi);
			}
			if(!simple)
				cur = qi;
		} else if(cur && name == sSegment) {
			int64_t start = Util::toInt64(getAttrib(attribs, sStart, 0));
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 1));
			
			if(size > 0 && start >= 0 && (start + size) <= cur->getSize()) {
				cur->addSegment(Segment(start, size));
				cur->setPriority(cur->calculateAutoPriority());
				if (curBundle) {
					curBundle->increaseDownloaded(size);
				}
			}
		} else if(cur && name == sSource) {
			const string& cid = getAttrib(attribs, sCID, 0);
			if(cid.length() != 39) {
				// Skip loading this source - sorry old users
				return;
			}
			UserPtr user = ClientManager::getInstance()->getUser(CID(cid));
			ClientManager::getInstance()->updateNick(user, getAttrib(attribs, sNick, 1));

			try {
				const string& hubHint = getAttrib(attribs, sHubHint, 1);
				HintedUser hintedUser(user, hubHint);
				if(qm->addSource(cur, hintedUser, 0) && user->isOnline())
					ConnectionManager::getInstance()->getDownloadConnection(hintedUser);
			} catch(const Exception&) {
				return;
			}
		} else if(inBundle && curBundle && name == sFinished) {
			//LogManager::getInstance()->message("FOUND FINISHED TTH");
			const string& tth = getAttrib(attribs, sTTH, 0);
			const string& target = getAttrib(attribs, sTarget, 0);
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 1));
			time_t added = static_cast<time_t>(Util::toInt(getAttrib(attribs, sAdded, 4)));
			if(size == 0 || tth.empty() || target.empty() || added == 0)
				return;
			if(!Util::fileExists(target))
				return;
			qm->addFinishedTTH(TTHValue(tth), curBundle, target, size, added);
			curBundle->increaseSize(size);
			curBundle->increaseDownloaded(size);
		} else {
			LogManager::getInstance()->message("QUEUE LOADING ERROR");
		}
	}
}

void QueueLoader::endTag(const string& name, const string&) {
	
	if(inDownloads || inBundle || inFile) {
		if(name == sDownload) {
			cur = NULL;
		} else if(name == "Downloads") {
			inDownloads = false;
		} else if(name == sFile) {
			if (!curBundle->getQueueItems().empty()) {
				curBundle->setTarget(curBundle->getQueueItems().front()->getTarget());
				curBundle->setAdded(curBundle->getQueueItems().front()->getAdded());
				QueueManager::getInstance()->addBundle(curBundle, true);
			}
			curBundle = NULL;
			inFile = false;
		} else if(name == sBundle) {
			QueueManager::getInstance()->addBundle(curBundle, true);
			curBundle = NULL;
			inBundle = false;
		}
	}
}

void QueueManager::addFinishedTTH(const TTHValue& tth, BundlePtr aBundle, const string& aTarget, int64_t aSize, time_t aFinished) {
	//LogManager::getInstance()->message("ADD FINISHED TTH: " + tth.toBase32());
	QueueItem* qi = new QueueItem(aTarget, aSize, QueueItem::DEFAULT, QueueItem::FLAG_NORMAL, aFinished, tth);
	qi->addSegment(Segment(0, aSize)); //make it complete

	Lock l (cs);
	fileQueue.add(qi, true);
	aBundle->getFinishedFiles().push_back(qi);
	//LogManager::getInstance()->message("added finished tth, totalsize: " + Util::toString(aBundle->getFinishedFiles().size()));
}

void QueueManager::noDeleteFileList(const string& path) {
	if(!BOOLSETTING(KEEP_LISTS)) {
		protectedFileLists.push_back(path);
	}
}

// SearchManagerListener
void QueueManager::on(SearchManagerListener::SR, const SearchResultPtr& sr) noexcept {
	bool added = false;
	bool wantConnection = false;
	bool matchPartialADC = false;
	bool matchPartialNMDC = false;
	size_t users = 0;
	BundlePtr bundle = NULL;

	if (!BOOLSETTING(AUTO_ADD_SOURCE)) {
		return;
	}

	{
		Lock l(cs);
		QueueItemList matches = fileQueue.find(sr->getTTH());
		for(auto i = matches.begin(); i != matches.end(); ++i) {
			if (!(*i)->getBundle()) {
				continue;
			}
			QueueItem* qi = *i;
			bundle = qi->getBundle();
			// Size compare to avoid popular spoof
			if(qi->getSize() == sr->getSize() && !qi->isSource(sr->getUser())) {
				try {
					if(qi->isFinished()) {
						if (bundle->isSource(sr->getUser())) {
							bundle = NULL;
							continue;
						}
					}

					users = qi->getBundle()->countOnlineUsers(); 
					
					bool nmdcUser = sr->getUser()->isSet(User::NMDC);
					/* match with partial list allways but decide if we are in nmdc hub here. 
					( dont want to change the settings, would just break what user has set already, 
					alltho would make a bit more sense to have just 1 match queue option, but since many have that disabled totally and we prefer to match partials in adchubs... ) */
					if(BOOLSETTING(AUTO_SEARCH_AUTO_MATCH) && (users < (size_t)SETTING(MAX_AUTO_MATCH_SOURCES))) {
						if(nmdcUser) {
							matchPartialNMDC = true;
						} else {
							matchPartialADC = true;
						}
					} else if (!nmdcUser) {
						matchPartialADC = true;
					}else if (regexp.match(sr->getFile(), sr->getFile().length()-4) > 0) {
						wantConnection = addAlternates(qi, HintedUser(sr->getUser(), sr->getHubURL()));
						// this is how sdc has it, dont add sources and receive wantconnection if we are about to match queue.
					} else if (!qi->isFinished()) {
						wantConnection = addSource(qi, HintedUser(sr->getUser(), sr->getHubURL()), 0);
					}
					added = true;
					break;
				} catch(const Exception&) {
					//...
				}
			}
		}
	}

	if (!added) {
		return;
	}

	//moved outside lock range.
	if(matchPartialADC && bundle) {
		string path;
		if (bundle->getSimpleMatching()) {
			path = Util::toAdcFile(Util::getDir(Util::getFilePath(sr->getFile()), true, false));
		} else {
			//try to find the corrent location from the path manually
			size_t pos = sr->getFile().find(bundle->getName() + "\\");
			if (pos != string::npos) {
				path = Util::toAdcFile(Util::getFilePath(sr->getFile().substr(0, pos+bundle->getName().length()+1)));
				//LogManager::getInstance()->message("ALT RELEASE MATCH, PATH: " + path);
			} else {
				//failed, should we use full filelist now?
				//LogManager::getInstance()->message("ALT NO RELEASE MATCH, PATH: " + sr->getFile());
			}
		}
		if (!path.empty()) {
			try {
				addList(HintedUser(sr->getUser(), sr->getHubURL()), QueueItem::FLAG_MATCH_QUEUE | QueueItem::FLAG_RECURSIVE_LIST |(path.empty() ? 0 : QueueItem::FLAG_PARTIAL_LIST) | 
					QueueItem::FLAG_TTHLIST, path);
			} catch(...) { }
			return;
		}
	}

	if(matchPartialNMDC) {
		try {
			string path = Util::getFilePath(sr->getFile());
			addList(HintedUser(sr->getUser(), sr->getHubURL()), QueueItem::FLAG_MATCH_QUEUE |(path.empty() ? 0 : QueueItem::FLAG_PARTIAL_LIST), path);
		} catch(const Exception&) {
			// ...
		}
		return;
	}
	if(sr->getUser()->isOnline() && wantConnection) {
		ConnectionManager::getInstance()->getDownloadConnection(HintedUser(sr->getUser(), sr->getHubURL()));
	}

}

bool QueueManager::addAlternates(QueueItem* qi, const dcpp::HintedUser& aUser) {
	string path, file;
	string::size_type pos, pos2;
	bool wantConnection = false;
	string aFile = qi->getTarget();

	pos = aFile.find(".part");
	if (pos != string::npos) {
		pos += 4;
	} else {
		pos = aFile.find_last_of(".");
		if(pos == string::npos) {
			return false;
		}
	}
	pos2 = aFile.find_last_of("\\");
	if(pos2 == string::npos) {
		return false;
	}

	file = aFile.substr(pos2+1, pos - pos2);
	path = aFile.substr(0, pos2);

	if(file.empty() || path.empty() || !qi->getBundle()) {
		return false;
	}

	BundlePtr bundle = qi->getBundle();
	if (bundle) {
		for (auto i = bundle->getQueueItems().begin(); i != bundle->getQueueItems().end(); ++i) {
			QueueItem* bundleItem = *i;
			if(bundleItem->getTarget().find(file) != string::npos) {
				try {	 
					if (addSource(bundleItem, aUser, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE)) {
						wantConnection = true;
					}
				} catch(...) {
					// Ignore...
				}
			}
		}
	}

	return wantConnection;
}
// ClientManagerListener
void QueueManager::on(ClientManagerListener::UserConnected, const UserPtr& aUser) noexcept {
	bool hasDown = false;
	{
		Lock l(cs);
		auto j = userQueue.getBundleList().find(aUser);
		if(j != userQueue.getBundleList().end()) {
			for(auto m = j->second.begin(); m != j->second.end(); ++m) {
				BundlePtr bundle = *m;
				QueueItemList items = bundle->getItems(aUser);
				for(auto s = items.begin(); s != items.end(); ++s) {
					QueueItem* qi = *s;
					fire(QueueManagerListener::StatusUpdated(), qi);
					if((bundle->getPriority() != QueueItem::PAUSED && qi->getPriority() != QueueItem::PAUSED) || qi->getPriority() == QueueItem::HIGHEST) {
						hasDown = true;
					}
				}
			}
		}
	}

	if(hasDown && aUser->isOnline()) { //even tho user just connected, check if he is still online and that we really have a user.
		// the user just came on, so there's only 1 possible hub, no need for a hint
		ConnectionManager::getInstance()->getDownloadConnection(HintedUser(aUser, Util::emptyString));
	}
}

void QueueManager::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser) noexcept {
	Lock l(cs);
	auto j = userQueue.getBundleList().find(aUser);
	if(j != userQueue.getBundleList().end()) {
		for(auto m = j->second.begin(); m != j->second.end(); ++m) {
			BundlePtr bundle = *m;
			QueueItemList items = bundle->getItems(aUser);
			for(auto s = items.begin(); s != items.end(); ++s) {
				fire(QueueManagerListener::StatusUpdated(), *s);
			}
		}
	}
}

void QueueManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	if((lastSave + 10000) < aTick) {
		saveQueue(false, aTick);
	}
	/*BundlePtr bundle = findSearchBundle(aTick, true);
	if(bundle != NULL) {
		LogManager::getInstance()->message("next search in " + Util::toString((nextSearch - aTick) / (60*1000)) + " minutes");
		LogManager::getInstance()->message("Calculations performed: " + Util::toString(calculations) + ", highest: " + Util::toString(((double)highestSel/calculations)*100) + "%, high: " + Util::toString(((double)highSel/calculations)*100) + "%, normal: " + Util::toString(((double)normalSel/calculations)*100) + "%, low: " + Util::toString(((double)lowSel/calculations)*100) + "%");
	} else {
		LogManager::getInstance()->message("NO BUNDLE");
	} */
	/*vector<pair<string, QueueItem::Priority>> priorities;

	for (auto s = userQueue.getRunning().begin(); s != userQueue.getRunning().end(); ++s) {
		QueueItemList ql = s->second;
		for (auto j = ql.begin(); j != ql.end(); ++j) {
			QueueItem* q = *j;
			if(q->getAutoPriority()) {
				QueueItem::Priority p1 = q->getPriority();
				if(p1 != QueueItem::PAUSED) {
					QueueItem::Priority p2 = q->calculateAutoPriority();
					if(p1 != p2)
						priorities.push_back(make_pair(q->getTarget(), p2));
				}
			}
		}
	}

	for(vector<pair<string, QueueItem::Priority>>::const_iterator p = priorities.begin(); p != priorities.end(); p++) {
		setPriority((*p).first, (*p).second);
	}
	 */
	/*
	vector<pair<string, QueueItem::Priority>> priorities;

	{
		Lock l(cs);

		QueueItemList um = getRunningFiles();
		for(auto j = um.begin(); j != um.end(); ++j) {
			QueueItem* q = *j;

			if(q->getAutoPriority()) {
				QueueItem::Priority p1 = q->getPriority();
				if(p1 != QueueItem::PAUSED) {
					QueueItem::Priority p2 = q->calculateAutoPriority();
					if(p1 != p2)
						priorities.push_back(make_pair(q->getTarget(), p2));
				}
			}
			fire(QueueManagerListener::StatusUpdated(), q, true);
		}
	}

	for(vector<pair<string, QueueItem::Priority>>::const_iterator p = priorities.begin(); p != priorities.end(); p++) {
		setPriority((*p).first, (*p).second);
	}
	*/

	BundleList runningBundles;
	vector<pair<BundlePtr, Bundle::Priority>> priorities;
	boost::unordered_map<UserPtr, int64_t> userSpeedMap;

	{
		Lock l (cs);
		for (auto i = bundles.begin(); i != bundles.end(); ++i) {
			BundlePtr bundle = i->second;
			int64_t bundleSpeed = 0, bundleRatio = 0;
			int downloads = 0;
			for (auto s = bundle->getDownloads().begin(); s != bundle->getDownloads().end(); ++s) {
				Download* d = *s;
				if (d->getAverageSpeed() > 0 && d->getStart() > 0) {
					downloads++;
					bundleSpeed += d->getAverageSpeed();
					bundleRatio += d->getPos() > 0 ? (double)d->getActual() / (double)d->getPos() : 1.00;
					userSpeedMap[d->getUser()] += d->getAverageSpeed();
				}
			}
			if (bundleSpeed > 0) {
				if (SETTING(DOWNLOAD_ORDER) == SettingsManager::ORDER_PROGRESS && bundle->getAutoPriority()) {
					Bundle::Priority p1 = bundle->getPriority();
					if(p1 != Bundle::PAUSED) {
						Bundle::Priority p2 = bundle->calculateAutoPriority();
						if(p1 != p2) {
							priorities.push_back(make_pair(bundle, p2));
						}
					}
				}
				bundleRatio = bundleRatio / downloads;
				bundle->setActual((int64_t)((double)bundle->getDownloadedBytes() * (bundleRatio == 0 ? 1.00 : bundleRatio)));
				bundle->setSpeed(bundleSpeed);
				bundle->setRunning(downloads);
				runningBundles.push_back(bundle);
				fire(QueueManagerListener::BundleTick(), bundle);
			}
		}

		if (!bundleUpdates.empty()) {
			for (bundleTickMap::const_iterator i = bundleUpdates.begin(); i != bundleUpdates.end(); ++i) {
				if (aTick > i->second + 1000) {
					handleBundleUpdate(i->first);
					bundleUpdates.erase(i);
					break; // one update per second
				}
			}
		}
	}

	for (auto i = userSpeedMap.begin(); i != userSpeedMap.end(); ++i) {
		i->first->setSpeed(i->second);
	}

	if (!runningBundles.empty()) {
		DownloadManager::getInstance()->updateBundles(runningBundles);
	}

	if (SETTING(DOWNLOAD_ORDER) == SettingsManager::ORDER_BALANCED && aTick > lastAutoPrio + 5000) {
		//LogManager::getInstance()->message("Calculate autoprio");
		calculateBundlePriorities(false);
		setLastAutoPrio(aTick);
	}

	for(vector<pair<BundlePtr, Bundle::Priority>>::const_iterator p = priorities.begin(); p != priorities.end(); p++) {
		setBundlePriority((*p).first, (*p).second, true);
	}

}

void QueueManager::calculateBundlePriorities(bool verbose) {
	//get bundles with auto priority and update user speeds
	boost::unordered_map<BundlePtr, double, Bundle::Hash> autoPrioMap;
	//boost::unordered_map<UserPtr, int64_t> userSpeedMap;
	{
		Lock l (cs);
		for (auto i = bundles.begin(); i != bundles.end(); ++i) {
			BundlePtr bundle = i->second;
			if (bundle->getAutoPriority()) {
				autoPrioMap[bundle] = 0;
			}
		}
	}
	/*for (auto i = userSpeedMap.begin(); i != userSpeedMap.end(); ++i) {
		i->first->setSpeed(i->second);
	} */

	if (autoPrioMap.size() <= 1) {
		if (verbose) {
			LogManager::getInstance()->message("Not enough bundles with autoprio to calculate anything!");
		}
		return;
	}


	//create priorization maps
	multimap<double, BundlePtr> speedMap;
	multimap<double, BundlePtr> sourceMap;

	{
		Lock l (cs);
		for (auto i = autoPrioMap.begin(); i != autoPrioMap.end(); ++i) {
			BundlePtr bundle = i->first;
			int64_t bundleSpeed = 0;
			double sources = 0;
			for (auto s = bundle->getBundleSources().begin(); s != bundle->getBundleSources().end(); ++s) {
				UserPtr& user = (*s).first.user;
				if (user->isOnline()) {
					bundleSpeed += user->getSpeed();
					sources += s->second;
				} else {
					sources += s->second / 2.0;
				}
			}
			sources = sources / bundle->getQueueItems().size();
			///if (verbose) {
			//	LogManager::getInstance()->message("Sourcepoints for bundle " + bundle->getName() + " : " + Util::toString(sources));
			//}
			speedMap.insert(make_pair((double)bundleSpeed, bundle));
			sourceMap.insert(make_pair(sources, bundle));
		}
	}

	//scale the priorization maps
	double factor;
	double max = max_element(speedMap.begin(), speedMap.end())->first;
	if (max) {
		double factor = 100 / max;
		for (auto i = speedMap.begin(); i != speedMap.end(); ++i) {
			autoPrioMap[i->second] = i->first * factor;
		}
	}

	max = max_element(sourceMap.begin(), sourceMap.end())->first;
	if (max > 0) {
		factor = 100 / max;
		for (auto i = sourceMap.begin(); i != sourceMap.end(); ++i) {
			autoPrioMap[i->second] += i->first * factor;
		}
	}

	//prepare to set the prios
	multimap<int, BundlePtr> finalMap;
	int uniqueValues = 0;
	for (auto i = autoPrioMap.begin(); i != autoPrioMap.end(); ++i) {
		if (finalMap.find(i->second) == finalMap.end()) {
			uniqueValues++;
		}
		finalMap.insert(make_pair(i->second, i->first));
	}


	int prioGroup = 1;
	if (uniqueValues <= 1) {
		if (verbose) {
			LogManager::getInstance()->message("Not enough bundles with unique points to perform the priotization!");
		}
		return;
	} else if (uniqueValues > 2) {
		prioGroup = uniqueValues / 3;
	}

	if (verbose) {
		LogManager::getInstance()->message("Unique values: " + Util::toString(uniqueValues) + " prioGroup size: " + Util::toString(prioGroup));
	}

	//priority to set (4-2, high-low)
	int prio = 4;

	//counters for analyzing identical points
	int lastPoints = 999;
	int prioSet=0;

	for (auto i = finalMap.begin(); i != finalMap.end(); ++i) {
		if (lastPoints==i->first) {
			if (verbose) {
				LogManager::getInstance()->message("Bundle: " + i->second->getName() + " points: " + Util::toString(i->first) + " setting prio " + AirUtil::getPrioText(prio));
			}
			setBundlePriority(i->second, (Bundle::Priority)prio, true);
			//don't increase the prio if two bundles have identical points
			if (prioSet < prioGroup) {
				prioSet++;
			}
		} else {
			if (prioSet == prioGroup && prio != 2) {
				prio--;
				prioSet=0;
			} 
			if (verbose) {
				LogManager::getInstance()->message("Bundle: " + i->second->getName() + " points: " + Util::toString(i->first) + " setting prio " + AirUtil::getPrioText(prio));
			}
			setBundlePriority(i->second, (Bundle::Priority)prio, true);
			prioSet++;
			lastPoints=i->first;
		}
	}
	if (verbose) {
		LogManager::getInstance()->message("Autosearch calculations performed: " + Util::toString(calculations) + ", highest: " + Util::toString(((double)highestSel/calculations)*100) + "%, high: " + Util::toString(((double)highSel/calculations)*100) + "%, normal: " + Util::toString(((double)normalSel/calculations)*100) + "%, low: " + Util::toString(((double)lowSel/calculations)*100) + "%");
	}
}

bool QueueManager::dropSource(Download* d) {
	size_t activeSegments = 0, onlineUsers = 0;
	uint64_t overallSpeed = 0;
	bool found=false;

	{
		Lock l(cs);
		QueueItemList runningItems = userQueue.getRunning(d->getUser());
		for (auto s = runningItems.begin(); s != runningItems.end(); ++s) {
			QueueItem* q = *s;
			if (q->getTarget() == d->getDownloadTarget()) {
				found=true;
   				dcassert(q->isSource(d->getUser()));

				if(!q->isSet(QueueItem::FLAG_AUTODROP))
					return false;

				for(DownloadList::const_iterator i = q->getDownloads().begin(); i != q->getDownloads().end(); i++) {
					if((*i)->getStart() > 0) {
						activeSegments++;
					}

					// more segments won't change anything
					if(activeSegments > 2)
						break;
				}

				onlineUsers = q->countOnlineUsers();
				overallSpeed = q->getAverageSpeed();
				break;
			}
		}
	}

	if (!found)
		return false;

	if(!SETTING(DROP_MULTISOURCE_ONLY) || (activeSegments >= 2)) {
		size_t iHighSpeed = SETTING(DISCONNECT_FILE_SPEED);

		if((iHighSpeed == 0 || overallSpeed > iHighSpeed * 1024) && onlineUsers > 2) {
			d->setFlag(Download::FLAG_SLOWUSER);

			if(d->getAverageSpeed() < SETTING(REMOVE_SPEED)*1024) {
				return true;
			} else {
				d->getUserConnection().disconnect();
			}
		}
	}

	return false;
}

bool QueueManager::handlePartialResult(const HintedUser& aUser, const TTHValue& tth, const QueueItem::PartialSource& partialSource, PartsInfo& outPartialInfo) {
	bool wantConnection = false;
	dcassert(outPartialInfo.empty());

	{
		Lock l(cs);

		// Locate target QueueItem in download queue
		QueueItemList ql = fileQueue.find(tth);
		
		if(ql.empty()){
			dcdebug("Not found in download queue\n");
			return false;
		}
		
		QueueItemPtr qi = ql.front();

		// don't add sources to finished files
		// this could happen when "Keep finished files in queue" is enabled
		if(qi->isFinished())
			return false;

		// Check min size
		if(qi->getSize() < PARTIAL_SHARE_MIN_SIZE){
			dcassert(0);
			return false;
		}

		// Get my parts info
		int64_t blockSize = HashManager::getInstance()->getBlockSize(qi->getTTH());
		if(blockSize == 0)
			blockSize = qi->getSize();
		qi->getPartialInfo(outPartialInfo, blockSize);
		
		// Any parts for me?
		wantConnection = qi->isNeededPart(partialSource.getPartialInfo(), blockSize);

		// If this user isn't a source and has no parts needed, ignore it
		QueueItem::SourceIter si = qi->getSource(aUser);
		if(si == qi->getSources().end()){
			si = qi->getBadSource(aUser);

			if(si != qi->getBadSources().end() && si->isSet(QueueItem::Source::FLAG_TTH_INCONSISTENCY))
				return false;

			if(!wantConnection){
				if(si == qi->getBadSources().end())
					return false;
			}else{
				// add this user as partial file sharing source
				qi->addSource(aUser);
				si = qi->getSource(aUser);
				si->setFlag(QueueItem::Source::FLAG_PARTIAL);

				QueueItem::PartialSource* ps = new QueueItem::PartialSource(partialSource.getMyNick(),
					partialSource.getHubIpPort(), partialSource.getIp(), partialSource.getUdpPort());
				si->setPartialSource(ps);

				userQueue.add(qi, aUser);
				dcassert(si != qi->getSources().end());
				fire(QueueManagerListener::SourcesUpdated(), qi);
			}
		}

		// Update source's parts info
		if(si->getPartialSource()) {
			si->getPartialSource()->setPartialInfo(partialSource.getPartialInfo());
		}
	}
	
	// Connect to this user
	if(wantConnection && aUser.user->isOnline())
		ConnectionManager::getInstance()->getDownloadConnection(aUser);

	return true;
}

BundlePtr QueueManager::findBundle(const TTHValue& tth) {
	Lock l (cs);
	QueueItemList ql = fileQueue.find(tth);
	if (!ql.empty()) {
		return ql.front()->getBundle();
	}
	return NULL;
}

bool QueueManager::handlePartialSearch(const TTHValue& tth, PartsInfo& _outPartsInfo, string& _bundle, bool& _reply, bool& _add) {
	{
		//LogManager::getInstance()->message("QueueManager::handlePartialSearch");
		Lock l(cs);

		// Locate target QueueItem in download queue
		QueueItemList ql = fileQueue.find(tth);
		if (ql.empty()) {
			//LogManager::getInstance()->message("QL EMPTY, QUIIIIIIIT");
			return false;
		}

		QueueItemPtr qi = ql.front();
		if (qi->getBundle()) {
			//LogManager::getInstance()->message("handlePartialSearch: QI FOUND");
			_bundle = qi->getBundle()->getToken();
			if (!qi->getBundle()->getQueueItems().empty()) {
				_reply = true;
			}
			if (!qi->getBundle()->getFinishedFiles().empty()) {
				_add = true;
			}
		} else {
			//LogManager::getInstance()->message("QI: NO BUNDLE OR FINISHEDTOKEN EXISTS");
		}

		if(qi->getSize() < PARTIAL_SHARE_MIN_SIZE){
			return false;  
		}

		// don't share when file does not exist
		if(!Util::fileExists(qi->isFinished() ? qi->getTarget() : qi->getTempTarget()))
			return false;

		int64_t blockSize = HashManager::getInstance()->getBlockSize(qi->getTTH());
		if(blockSize == 0)
			blockSize = qi->getSize();
		qi->getPartialInfo(_outPartsInfo, blockSize);
	}

	return !_outPartsInfo.empty();
}

bool QueueManager::isDirQueued(const string& aDir) {
	string dir = AirUtil::getReleaseDir(aDir);
	if (dir.empty()) {
		return false;
	}

	Lock l (cs);
	auto i = bundleDirs.find(dir);
	if (i != bundleDirs.end()) {
		return true;
	}
	return false;
}

tstring QueueManager::getDirPath(const string& aDir) {
	string dir = AirUtil::getReleaseDir(aDir);
	if (dir.empty()) {
		return false;
	}

	Lock l (cs);
	auto i = bundleDirs.find(dir);
	if (i != bundleDirs.end()) {
		return Text::toT(i->second);
	}
	return Util::emptyStringT;
}

void QueueManager::getBundlePaths(StringList& retBundles) {
	Lock l (cs);
	for (auto i = bundles.begin(); i != bundles.end(); ++i) {
		if (!i->second->getFileBundle()) {
			retBundles.push_back(i->second->getTarget());
		}
	}
}

bool QueueManager::isChunkDownloaded(const TTHValue& tth, int64_t startPos, int64_t& bytes, string& target) {
	Lock l(cs);
	QueueItemList ql = fileQueue.find(tth);

	if(ql.empty()) return false;

	QueueItem* qi = ql.front();

	target = qi->isFinished() ? qi->getTarget() : qi->getTempTarget();

	return qi->isChunkDownloaded(startPos, bytes);
}

bool QueueManager::addBundle(BundlePtr aBundle, bool loading) {

	if (aBundle->getQueueItems().size() == 0) {
		//LogManager::getInstance()->message("EMPTY BUNDLE, QUIT");
		return false;
	}

	if (!aBundle->getFileBundle()) {
		BundlePtr oldBundle = getMergeBundle(aBundle->getTarget());
		if (oldBundle) {
			mergeBundle(oldBundle, aBundle);
			return false;
		}
	}

	//check that there are no file bundles inside the bundle that will be created and merge them in that case
	mergeFileBundles(aBundle);

	if (!loading) {
		LogManager::getInstance()->message("The bundle " + aBundle->getName() + " has been created with " + Util::toString(aBundle->getQueueItems().size()) + " items (total size: " + Util::formatBytes(aBundle->getSize()) + ")");
	}

	/*if (aBundle->getPriority() == Bundle::DEFAULT) {
		aBundle->setPriority(Bundle::LOW);
		aBundle->setAutoPriority(true);
	} */

	if ((aBundle->getDirDate() + (SETTING(RECENT_BUNDLE_HOURS)*60*60)) > (GET_TIME())) {
		/*LogManager::getInstance()->message("Time: " + Util::toString(GET_TIME()));
		LogManager::getInstance()->message("dirDate: " + Util::toString(aBundle->getDirDate()));
		LogManager::getInstance()->message("settingHours: " + Util::toString(SETTING(RECENT_BUNDLE_HOURS)));
		LogManager::getInstance()->message("recentMS: " + Util::toString(SETTING(RECENT_BUNDLE_HOURS)*60*60*1000));
		LogManager::getInstance()->message("Bundle set as recent: " + aBundle->getName() + ", age: " + Text::fromT(Util::formatSeconds(GET_TIME()-aBundle->getDirDate()))); */
		aBundle->setRecent(true);
	}

	{
		Lock l(cs);
		bundles.insert(make_pair(aBundle->getToken(), aBundle));
		fileQueue.addSearchPrio(aBundle, aBundle->getPriority());
	}

	//check if we need to insert the root bundle dir
	if (!aBundle->getFileBundle()) {
		string target = aBundle->getTarget();
		if (aBundle->getBundleDirs().find(target) == aBundle->getBundleDirs().end()) {
			QueueItemList ql;
			aBundle->getBundleDirs()[target] = ql;
			string releaseDir = AirUtil::getReleaseDir(target);
			if (!releaseDir.empty()) {
				Lock l (cs);
				bundleDirs[releaseDir] = target;
			}
		}
	}


	if (!loading && !aBundle->getSources().empty()) {
		bool wantConnection = aBundle->getSources().front().first.user && (aBundle->getPriority() != Bundle::PAUSED) && userQueue.getRunning(aBundle->getSources().front().first.user).empty();

		if(wantConnection) {
			ConnectionManager::getInstance()->getDownloadConnection(aBundle->getSources().front().first, false);
		}

	}

	if (!loading && BOOLSETTING(AUTO_SEARCH)) {
		searchBundle(aBundle, true);
	}
	return true;
}

void QueueManager::mergeFileBundles(BundlePtr aBundle) {
	BundleList mergeBundles;
	{
		Lock l (cs);
		for (auto j = bundles.begin(); j != bundles.end(); ++j) {
			BundlePtr compareBundle = (*j).second;
			if (!compareBundle->getFileBundle()) {
				continue;
			}
			size_t pos = compareBundle->getTarget().find(aBundle->getTarget());
			if (pos != string::npos) {
				//dcassert(compareBundle->getFileBundle());
				//LogManager::getInstance()->message("FILEBUNDLE FOUND");
				mergeBundles.push_back(compareBundle);
			}
		}
	}

	for (auto j = mergeBundles.begin(); j != mergeBundles.end(); ++j) {
		QueueItem* qi = (*j)->getQueueItems().front();
		LogManager::getInstance()->message("The file bundle " + (*j)->getName() + " has been merged with the bundle " + aBundle->getName());
		removeBundleItem(qi, false, false);
		addBundleItem(qi, aBundle, true);
	}
}

BundlePtr QueueManager::getMergeBundle(const string& aTarget) {
	BundlePtr compareBundle;
	Lock l (cs);
	for (auto j = bundles.begin(); j != bundles.end(); ++j) {
		BundlePtr compareBundle = (*j).second;
		if (compareBundle->getFileBundle()) {
			//should we support this too later?
			continue;
		}

		//LogManager::getInstance()->message("Comparebundletarget: " + compareBundle->getTarget() + " aTarget: " + aTarget);
		//don't try to merge with this bundle
		if (compareBundle->getTarget() == aTarget) {
			continue;
		}

		size_t pos = compareBundle->getTarget().find(aTarget);
		if (pos == string::npos) {
			pos = aTarget.find(compareBundle->getTarget());
			if (pos != string::npos) {
				return compareBundle;
			}
		} else {
			return compareBundle;
		}
	}
	return NULL;
}

int QueueManager::mergeBundle(BundlePtr targetBundle, BundlePtr sourceBundle) {
	int added = 0;

	string sourceBundleTarget = sourceBundle->getTarget();
	bool changeName = (targetBundle->getTarget().find(sourceBundleTarget) != string::npos) && (targetBundle->getTarget().length() > sourceBundleTarget.length());

	{
		//Lock l (cs);
		QueueItemList ql = sourceBundle->getQueueItems();
		for (auto i = ql.begin(); i != ql.end(); ++i) {
			QueueItem* qi = *i;
			dcassert(qi);
			userQueue.removeQI(qi, false); //it has no running items anyway
			removeBundleItem(qi, false, false);
			addBundleItem(qi, targetBundle, false);
			userQueue.add(qi);
			added++;
		}
	}

	if (added > 0) {
		//do we need to change the bundle target?
		if (changeName) {
			//handle release dirs
			if (targetBundle->getBundleDirs().find(sourceBundleTarget) == targetBundle->getBundleDirs().end()) {
				Lock l (cs);
				QueueItemList ql;
				targetBundle->getBundleDirs()[sourceBundleTarget] = ql;
				string releaseDir = AirUtil::getReleaseDir(sourceBundleTarget);
				if (!releaseDir.empty()) {
					Lock l (cs);
					bundleDirs[releaseDir] = sourceBundleTarget;
				}
			}

			targetBundle->setTarget(sourceBundleTarget);
			//check that there are no file bundles inside the new target dir and merge them in that case
			mergeFileBundles(targetBundle);
			targetBundle->setFlag(Bundle::UPDATE_NAME);
			//LogManager::getInstance()->message("MERGE CHANGE TARGET");
		}
		targetBundle->setFlag(Bundle::UPDATE_SIZE);
		targetBundle->setDirty(true);
		addBundleUpdate(targetBundle->getToken());
		LogManager::getInstance()->message("Added " + Util::toString(added) + " items to an existing bundle " + targetBundle->getName());
	} else {
		LogManager::getInstance()->message("No items to an existing bundle " + targetBundle->getName() + " has been added");
	}

	/*if (!sourceBundle->getNotifiedUsers().empty()) {
		//we need to handle them too........
		//DownloadManager::getInstance()->sendBundle();
	} */
	return added;
}

BundleList QueueManager::getBundleInfo(const string& aSource, int& finishedFiles, int& dirBundles, int& fileBundles) {
	BundleList retBundles;
	BundlePtr tmpBundle;
	bool subFolder = false;
	{
		Lock l (cs);
		for (auto j = bundles.begin(); j != bundles.end(); ++j) {
			tmpBundle = (*j).second;
			if (tmpBundle->getTarget().find(aSource) != string::npos) {
				//LogManager::getInstance()->message("findBundleFinished target found (root bundle): " + j->second->getTarget());
				retBundles.push_back(tmpBundle);
				if (tmpBundle->getFileBundle()) {
					fileBundles++;
				} else {
					dirBundles++;
				}
			} else if (!tmpBundle->getFileBundle()) {
				//check subfolders in case we are splitting the bundle
				for (auto s = tmpBundle->getBundleDirs().begin(); s != tmpBundle->getBundleDirs().end(); ++s) {
					if (s->first.find(aSource) != string::npos) {
						//LogManager::getInstance()->message("findBundleFinished target found (subdir): " + *s);
						retBundles.push_back(tmpBundle);
						dirBundles++;
						subFolder = true;
						break; //there can be only one bundle containing the subdir
					} else {
						//LogManager::getInstance()->message("findBundleFinished target not found (subdir): " + *s);
					}
				}
				//LogManager::getInstance()->message("findBundleFinished target not found: " + j->second->getName());
			}
			if (subFolder) {
				break;
			}
		}
	}

	for (auto j = retBundles.begin(); j != retBundles.end(); ++j) {
		tmpBundle = *j;
		if (tmpBundle->getFileBundle()) {
			//filebundles can't have finished files...
			continue;
		}

		if (subFolder) {
			Lock l (cs);
			for (auto i = tmpBundle->getFinishedFiles().begin(); i != tmpBundle->getFinishedFiles().end(); ++i) {
				if((*i)->getTarget().find(aSource) != string::npos) {
					finishedFiles++;
				}
			}
			return retBundles;
		}

		finishedFiles += tmpBundle->getFinishedFiles().size();
	}
	//for (auto r = retBundles.begin(); r != retBundles.end(); ++r) {
	//	LogManager::getInstance()->message("retBundle: " + (*r)->getTarget());
	//}
	return retBundles;
}

void QueueManager::setBundlePriorities(const string& aSource, BundleList sourceBundles, Bundle::Priority p, bool autoPrio) {
	//LogManager::getInstance()->message("Setting priorities for " + Util::toString(sourceBundles.size()) + " bundles");
	if (sourceBundles.empty()) {
		//LogManager::getInstance()->message("moveDir, sourceBundles empty");
		return;
	}

	BundlePtr bundle;

	if (sourceBundles.size() == 1 && sourceBundles.front()->getTarget().find(aSource) == string::npos) {
		//we aren't removing the whole bundle
		bundle = sourceBundles.front();
		QueueItemList ql;
		{
			Lock l (cs);
			for (auto i = bundle->getQueueItems().begin(); i != bundle->getQueueItems().end(); ++i) {
				if ((*i)->getTarget().find(aSource) != string::npos) {
					//LogManager::getInstance()->message("removeDir subdirlist, source: " + aSource + " found from : " + (*i)->getTarget());
					ql.push_back(*i);
				} else {
					//LogManager::getInstance()->message("DON'T REMOVE: removeDir subdirlist, source: " + aSource + " found from : " + (*i)->getTarget());
				}
			}
		}
		for (auto i = ql.begin(); i != ql.end(); ++i) {
			if (autoPrio) {
				setAutoPriority((*i)->getTarget(), (*i)->getAutoPriority());
			} else {
				setPriority((*i)->getTarget(), (QueueItem::Priority)p);
			}
		}
	} else {
		for (auto r = sourceBundles.begin(); r != sourceBundles.end(); ++r) {
			if (autoPrio) {
				setBundleAutoPriority((*r)->getToken());
			} else {
				setBundlePriority(*r, (Bundle::Priority)p);
			}
		}
	}
}

void QueueManager::removeDir(const string& aSource, BundleList sourceBundles, bool removeFinished) {

	if (sourceBundles.empty()) {
		//LogManager::getInstance()->message("moveDir, sourceBundles empty");
		return;
	}

	BundlePtr bundle;

	if (sourceBundles.size() == 1 && sourceBundles.front()->getTarget().find(aSource) == string::npos) {
		//we aren't removing the whole bundle
		bundle = sourceBundles.front();
		QueueItemList ql;
		{
			Lock l (cs);
			for (auto i = bundle->getQueueItems().begin(); i != bundle->getQueueItems().end(); ++i) {
				if ((*i)->getTarget().find(aSource) != string::npos) {
					//LogManager::getInstance()->message("removeDir subdirlist, source: " + aSource + " found from : " + (*i)->getTarget());
					ql.push_back(*i);
				} else {
					//LogManager::getInstance()->message("DON'T REMOVE: removeDir subdirlist, source: " + aSource + " found from : " + (*i)->getTarget());
				}
			}
			if (removeFinished) {
				QueueItemList remove;
				for (auto i = bundle->getFinishedFiles().begin(); i != bundle->getFinishedFiles().end(); ++i) {
					if ((*i)->getTarget().find(aSource) != string::npos) {
						UploadManager::getInstance()->abortUpload((*i)->getTarget());
						File::deleteFile((*i)->getTarget());
						remove.push_back(*i);
					}
				}
				for (auto i = remove.begin(); i != remove.end(); ++i) {
					bundle->getFinishedFiles().erase(std::remove(bundle->getFinishedFiles().begin(), bundle->getFinishedFiles().end(), *i), bundle->getFinishedFiles().end());
				}
			}
		}
		for (auto i = ql.begin(); i != ql.end(); ++i) {
			remove((*i)->getTarget());
		}
	} else {
		for (auto r = sourceBundles.begin(); r != sourceBundles.end(); ++r) {
			bundle = *r;
			removeBundleFiles(bundle, removeFinished);
		}
	}
}

void QueueManager::moveDir(const string& aSource, const string& aTarget, BundleList sourceBundles, bool moveFinished) {
	//LogManager::getInstance()->message("moveDir, source: " + aSource + " target: " + aTarget);
	if (sourceBundles.empty()) {
		//LogManager::getInstance()->message("moveDir, sourceBundles empty");
		return;
	}

	for (auto r = sourceBundles.begin(); r != sourceBundles.end(); ++r) {
		BundlePtr sourceBundle = *r;
		if (!sourceBundle->getFileBundle()) {
			/*//debug:
			for (auto i = sourceBundle->getQueueItems().begin(); i != sourceBundle->getQueueItems().end(); ++i) {
				LogManager::getInstance()->message("OLD TARGET: " + (*i)->getTarget());
			}
			for (auto i = sourceBundle->getBundleDirs().begin(); i != sourceBundle->getBundleDirs().end(); ++i) {
				LogManager::getInstance()->message("OLD BUNDLEDIR: " + (*i));
			} */

			//LogManager::getInstance()->message("moveDir, not filebundle");
			if (sourceBundle->getTarget().find(aSource) != string::npos) {
				//we are moving the root bundle dir or some of it's parents
				moveBundle(aSource, aTarget, sourceBundle, moveFinished);
			} else {
				//LogManager::getInstance()->message("moveDir, bundle subdir");
				//we are moving a subfolder, get the list of queueitems we need to move
				splitBundle(aSource, aTarget, sourceBundle, moveFinished);
			}

			/*//debug:
			for (auto i = sourceBundle->getQueueItems().begin(); i != sourceBundle->getQueueItems().end(); ++i) {
				LogManager::getInstance()->message("NEW TARGET: " + (*i)->getTarget());
				}
			for (auto i = sourceBundle->getBundleDirs().begin(); i != sourceBundle->getBundleDirs().end(); ++i) {
				LogManager::getInstance()->message("NEW BUNDLEDIR: " + (*i));
			} */
		} else {
			//LogManager::getInstance()->message("moveDir, is filebundle");
			//move queue items
			if (move(sourceBundle->getQueueItems().front(), aTarget)) {
				moveFileBundle(sourceBundle, aTarget);
			}
		}
	}
}

void QueueManager::moveBundle(const string& aSource, const string& aTarget, BundlePtr sourceBundle, bool moveFinished) {

	string sourceBundleTarget = sourceBundle->getTarget();

	//remove old releasedir
	/*string releaseDir = ShareManager::getInstance()->getReleaseDir(sourceBundleTarget);
	if (!releaseDir.empty()) {
		auto s = bundleDirs.find(releaseDir);
		if (s != bundleDirs.end()) {
			bundleDirs.erase(s);
		}
	} */

	//can we merge this with an existing bundle?
	bool hasMergeBundle = false;
	BundlePtr newBundle = getMergeBundle(convertMovePath(sourceBundleTarget,aSource, aTarget));
	if (newBundle) {
		hasMergeBundle = true;
	} else {
		newBundle = sourceBundle;
	}

	//handle finished items
	if (moveFinished) {
		Lock l (cs);
		for (auto i = sourceBundle->getFinishedFiles().begin(); i != sourceBundle->getFinishedFiles().end(); ++i) {
			QueueItem* qi = *i;
			UploadManager::getInstance()->abortUpload((*i)->getTarget());
			string targetPath = convertMovePath(qi->getTarget(), aSource, aTarget);
			string qiTarget = qi->getTarget();
			if (hasMergeBundle) {
				newBundle->getFinishedFiles().push_back(qi);
			}
			moveFile(qiTarget, targetPath, newBundle);
			qi->setTarget(targetPath);
		}
	}

	//convert the QIs
	QueueItemList ql = sourceBundle->getQueueItems();
	for (auto i = ql.begin(); i != ql.end(); ++i) {
		QueueItem* qi = *i;
		move(qi, convertMovePath(qi->getTarget(), aSource, aTarget));
	}

	if (!sourceBundle) {
		//may happen if all queueitems are being merged with existing ones and the bundle is being removed
		return;
	}

	if (hasMergeBundle) {
		//LogManager::getInstance()->message("moveDir, merge bundle found: " + newBundle->getTarget());
		mergeBundle(newBundle, sourceBundle);
		//sourceBundle->dec();
	} else {
		//LogManager::getInstance()->message("moveDir, no merge bundle");
		//nothing to merge to, move the old bundle
		bool changeName = !(sourceBundle->getName() == Util::getDir(aTarget, false, true));
		sourceBundle->setTarget(convertMovePath(sourceBundleTarget, aSource, aTarget));
		mergeFileBundles(sourceBundle);
		if (changeName) {
			sourceBundle->setFlag(Bundle::UPDATE_NAME);
			addBundleUpdate(sourceBundle->getToken(), false);
		}

		//add new release dir
		/*releaseDir = ShareManager::getInstance()->getReleaseDir(newBundle->getTarget());
		if (!releaseDir.empty()) {
			bundleDirs[releaseDir] = newBundle->getTarget();
		} */

		sourceBundle->setDirty(true);
		LogManager::getInstance()->message("The bundle " + sourceBundle->getName() + " has been moved to " + sourceBundle->getTarget());
	}
}

void QueueManager::splitBundle(const string& aSource, const string& aTarget, BundlePtr sourceBundle, bool moveFinished) {
	//first pick the items that we need to move
	QueueItemList ql;
	size_t pos;
	for (auto i = sourceBundle->getQueueItems().begin(); i != sourceBundle->getQueueItems().end(); ++i) {
		pos = (*i)->getTarget().find(aSource);
		if (pos != string::npos) {
			//LogManager::getInstance()->message("moveDir subdirlist, source: " + aSource + " found from : " + (*i)->getTarget());
			ql.push_back(*i);
		}
	}

	//create a temp bundle for split items
	BundlePtr tempBundle = BundlePtr(new Bundle(aTarget, false, GET_TIME()));

	//can we merge the split folder?
	bool hasMergeBundle = false;
	BundlePtr newBundle = getMergeBundle(aTarget);
	if (newBundle) {
		hasMergeBundle = true;
	} else {
		newBundle = tempBundle;
	}

	//handle finished items
	if (moveFinished) {
		Lock l (cs);
		for (auto i = sourceBundle->getFinishedFiles().begin(); i != sourceBundle->getFinishedFiles().end();) {
			QueueItem* qi = *i;
			if ((qi->getTarget()).find(aSource) != string::npos) {
				UploadManager::getInstance()->abortUpload((*i)->getTarget());
				string targetPath = convertMovePath(qi->getTarget(), aSource, aTarget);
				sourceBundle->getFinishedFiles().erase(i);
				newBundle->getFinishedFiles().push_back(qi);
				moveFile(qi->getTarget(), targetPath, newBundle);
				qi->setTarget(targetPath);
				i = sourceBundle->getFinishedFiles().begin();
			} else {
				i++;
			}
		}
	}

	//convert the QIs
	for (auto i = ql.begin(); i != ql.end(); ++i) {
		QueueItem* qi = *i;
		//LogManager::getInstance()->message("QueueManager::splitBundle, remove from old bundle!" + qi->getTarget());
		if (move(qi, convertMovePath(qi->getTarget(), aSource, aTarget))) {
			userQueue.removeQI(qi, false); //it has no running items anyway
			removeBundleItem(qi, false, false);
			addBundleItem(qi, tempBundle, true);
			userQueue.add(qi);
		}
	}

	//merge or add the temp bundle
	if (hasMergeBundle) {
		//merge the temp bundle
		//LogManager::getInstance()->message("splitBundle, mergebundle found");
		mergeBundle(newBundle, tempBundle);
		//tempBundle->dec();
	} else {
		//LogManager::getInstance()->message("splitBundle, no mergebundle found, create new");
		addBundle(tempBundle);
	}

	/*//next: handle downloads, notifications etc...............
	if (sourceBundle) {
		//.........
	} */
}

void QueueManager::moveFileBundle(BundlePtr aBundle, const string& aTarget) noexcept {
	QueueItem* qi = aBundle->getQueueItems().front();

	BundlePtr mergeBundle = findBundle(qi, false);
	if (mergeBundle) {
		LogManager::getInstance()->message("The file bundle " + aBundle->getName() + " has been merged into bundle " + mergeBundle->getName());
		removeBundleItem(qi, false, false);
	} else {
		LogManager::getInstance()->message("The file bundle " + aBundle->getName() + " has been moved to " + aBundle->getTarget());
		aBundle->setTarget(qi->getTarget());
		aBundle->setDirty(true);
	}
}

string QueueManager::convertMovePath(const string& aSourceCur, const string& aSourceRoot, const string& aTarget) noexcept {
	//cut the filename
	string oldDir = Util::getDir(aSourceCur, false, false);
	string target = aTarget;

	if (oldDir.length() > aSourceRoot.length()) {
		target += oldDir.substr(aSourceRoot.length(), oldDir.length() - aSourceRoot.length());
	}

	if(aSourceCur[aSourceCur.size() -1] != '\\') {
		target += Util::getFileName(aSourceCur);
		//LogManager::getInstance()->message("NEW TARGET (FILE): " + target + " OLD FILE: " + aSource);
	} else {
		//LogManager::getInstance()->message("NEW TARGET (DIR): " + target + " OLD DIR: " + aSource);
	}
	return target;
}

bool QueueManager::move(QueueItem* qs, const string& aTarget) noexcept {
	string target = Util::validateFileName(aTarget);

	if(qs->getTarget() == target) {
		//LogManager::getInstance()->message("MOVE FILE, TARGET SAME");
		return false;
	}

	// Don't move running downloads
	if(qs->isRunning()) {
		//LogManager::getInstance()->message("MOVE FILE, RUNNING");
		return false;
	}

	// Let's see if the target exists...then things get complicated...
	QueueItem* qt = NULL;
	{
		Lock l (cs);
		qt = fileQueue.find(target);
	}

	if(qt == NULL) {
		//LogManager::getInstance()->message("QT NOT FOUND");
		//Does the file exist already on the disk?
		if(Util::fileExists(target)) {
			//LogManager::getInstance()->message("FILE EXISTS");
			remove(qs->getTarget());
			return false;
		}
		// Good, update the target and move in the queue...
		/*if(qs->isRunning()) {
			for(DownloadList::iterator i = qs->getDownloads().begin(); i != qs->getDownloads().end(); ++i) {
				Download* d = *i;
				d->setT
			}
		} */
		fire(QueueManagerListener::Moved(), qs, qs->getTarget());
		fileQueue.move(qs, target);
		fire(QueueManagerListener::Added(), qs);
		return true;
	} else {
		//LogManager::getInstance()->message("QT FOUND: " + qt->getTarget());
		// Don't move to target of different size
		if(qs->getSize() != qt->getSize() || qs->getTTH() != qt->getTTH())
			return false;

		{
			Lock l (cs);
			for(QueueItem::SourceConstIter i = qs->getSources().begin(); i != qs->getSources().end(); ++i) {
				try {
					addSource(qt, i->getUser(), QueueItem::Source::FLAG_MASK);
				} catch(const Exception&) {
					//..
				}
			}
		}
		removeBundleItem(qs, false, false);
		remove(qs->getTarget());
	}
	return false;
}


void QueueManager::move(const StringPairList& sourceTargetList) noexcept {
	QueueItemList ql;
	BundlePtr sourceBundle;
	string aTarget = Util::getDir(sourceTargetList[0].second, false, false);;
	for (auto k = sourceTargetList.begin(); k != sourceTargetList.end(); ++k) {
		string source = k->first;
		string target = k->second;
		QueueItem* qs = fileQueue.find(source);
		if(qs) {
			dcassert(!qs->getBundle());
			if (qs->getBundle()) {
				if (move(qs, target)) {
					moveFileBundle(qs->getBundle(), Util::emptyString);
				} else {
					LogManager::getInstance()->message("::move, QI MOVE FAILED");
				}
			}
		}
	}
}

BundlePtr QueueManager::findBundle(QueueItem* qi, bool allowAdd) {
	BundlePtr bundle;
	{
		Lock l (cs);
		for (auto j = bundles.begin(); j != bundles.end(); ++j) {
			size_t pos = qi->getFolder().find((*j).second->getTarget());
			if (pos != string::npos) {
				bundle = j->second;
			
				//debug:
				//for (auto i = (*j).second->getQueueItems().begin(); i != (*j).second->getQueueItems().end(); ++i) {
				//	LogManager::getInstance()->message("NEW TARGET: " + (*i)->getTarget());
				//}
				//for (auto i = sourceBundle->getBundleDirs().begin(); i != sourceBundle->getBundleDirs().end(); ++i) {
				//	LogManager::getInstance()->message("NEW BUNDLEDIR: " + (*i));
				//}
				//LogManager::getInstance()->message("ADD BUNDLEITEM2, size: " + Util::formatBytes(bundle->getSize()) + " items " + Util::formatBytes(bundle->items.size()));
				break;
			}
		}
	}

	if (bundle) {
		addBundleItem(qi, bundle, false);
		return bundle;
	}

	if (allowAdd) {
		//create a new single file bundle
		return createFileBundle(qi);
	}
	return bundle;
	//LogManager::getInstance()->message("FINDBUNDLE, NO BUNDLES FOUND");
}

BundlePtr QueueManager::createFileBundle(QueueItem* qi) {
	//LogManager::getInstance()->message("CREATE NEW FILEBUNDLE");
	string bundleToken = Util::toString(Util::rand());
	BundlePtr bundle = BundlePtr(new Bundle(qi->getTarget(), true, qi->getAdded()));
	bundle->setToken(bundleToken);
	qi->setBundle(bundle);
	bundle->getQueueItems().push_back(qi);
	bundle->increaseSize(qi->getSize());
	bundle->setDownloaded(qi->getDownloadedBytes());
	bundle->setPriority((Bundle::Priority)qi->getPriority());
	bundle->setAutoPriority(qi->getAutoPriority());
	if (addBundle(bundle, true)) {
		return bundle;
	} else {
		return NULL;
	}
}

bool QueueManager::addBundleItem(QueueItem* qi, BundlePtr aBundle, bool newBundle, bool loading) {
	//check if the item exists already
	/*for (auto i = aBundle->getQueueItems().begin(); i != aBundle->getQueueItems().end(); ++i) {
		QueueItem* compareQI = *i;
		if (qi->getTarget() == compareQI->getTarget()) {
			//try to add sources for the existing bundle item
			for(QueueItem::SourceConstIter i = qi->getSources().begin(); i != qi->getSources().end(); ++i) {
				try {
					addSource(compareQI, i->getUser(), QueueItem::Source::FLAG_MASK);
				} catch(const Exception&) {
					//..
				}
			}
			remove(qi->getTarget());
			return false;
		}
	} */

	string dir = Util::getDir(qi->getTarget(), false, false);
	auto& s = aBundle->getBundleDirs()[dir];
	s.push_back(qi);
	if (s.size() == 1) {
		string releaseDir = AirUtil::getReleaseDir(dir);
		if (!releaseDir.empty()) {
			Lock l (cs);
			bundleDirs[releaseDir] = dir;
		}
	}

	qi->setBundle(aBundle);
	aBundle->getQueueItems().push_back(qi);
	aBundle->increaseSize(qi->getSize());
	if (!newBundle) {
		aBundle->setFlag(Bundle::UPDATE_SIZE);
		addBundleUpdate(aBundle->getToken());
		aBundle->setDirty(true);
	}
	return true;
}

BundlePtr QueueManager::findBundle(const string bundleToken) {
	Lock l (cs);
	auto i = bundles.find(bundleToken);
	if (i != bundles.end()) {
		return i->second;
	}
	return NULL;
}

void QueueManager::removeRunningUser(const string& bundleToken, const UserPtr& aUser, bool finished) {
	BundlePtr bundle = findBundle(bundleToken);
	bool addUpdate = false;
	if (bundle) {
		Lock l (cs);
		auto y =  bundle->getRunningUsers().find(aUser);
		if (y != bundle->getRunningUsers().end()) {
			y->second--;
			if (y->second == 0) {
				bundle->getRunningUsers().erase(y);
				for(auto i = bundle->getUploadReports().begin(); i != bundle->getUploadReports().end(); ++i) {
					if (i->user == aUser) {
						bundle->getUploadReports().erase(i);
						//LogManager::getInstance()->message("ERASE UPLOAD REPORT: " + Util::toString(bundle->getUploadReports().size()));
						break;
					}
				}
				//LogManager::getInstance()->message("NO RUNNING, ERASE: uploadReports size " + Util::toString(bundle->getUploadReports().size()));
				if (bundle->getRunningUsers().size() == 1) {
					bundle->setFlag(Bundle::UPDATE_SINGLEUSER);
					addUpdate = true;
				} else if (bundle->getRunningUsers().empty()) {
					if (finished) {
						bundle->setFlag(Bundle::SET_WAITING);
						addUpdate = true;
					} else {
						fire(QueueManagerListener::BundleWaiting(), bundle);
					}
				}
			} else {
				//LogManager::getInstance()->message("STILL RUNNING: " + Util::toString(y->second));
			}
		}
	} else {
		return;
	}
	addBundleUpdate(bundle->getToken(), finished);
}

void QueueManager::addBundleUpdate(const string bundleToken, bool finished) {
	//LogManager::getInstance()->message("QueueManager::addBundleUpdate");
	Lock l (cs);
	for (auto i = bundleUpdates.begin(); i != bundleUpdates.end(); ++i) {
		if (i->first == bundleToken) {
			i->second = GET_TICK();
			return;
		}
	}

	int64_t tick = GET_TICK();
	if (finished) {
		//LogManager::getInstance()->message("QueueManager::addBundleUpdate FINISHED");
		tick = tick+4000;
	}
	bundleUpdates.push_back(make_pair(bundleToken, tick));
}

void QueueManager::handleBundleUpdate(const string& bundleToken) {
	//LogManager::getInstance()->message("QueueManager::sendBundleUpdate");
	BundlePtr bundle = findBundle(bundleToken);
	if (bundle) {
		if (bundle->isSet(Bundle::SET_WAITING)) {
			//LogManager::getInstance()->message("QueueManager::sendBundleUpdate waiting");
			fire(QueueManagerListener::BundleWaiting(), bundle);
			bundle->unsetFlag(Bundle::SET_WAITING);
		}

		if (bundle->isSet(Bundle::UPDATE_SINGLEUSER)) {
			bundle->unsetFlag(Bundle::UPDATE_SINGLEUSER);
			DownloadManager::getInstance()->sendBundleMode(bundle, true);
		}

		if (bundle->isSet(Bundle::UPDATE_SIZE) || bundle->isSet(Bundle::UPDATE_NAME)) {
			sendBundleUpdate(bundle);
		}
	}
}

void QueueManager::sendBundleUpdate(BundlePtr aBundle) {
	//LogManager::getInstance()->message("QueueManager::sendBundleUpdate");

	for(auto i = aBundle->getUploadReports().begin(); i != aBundle->getUploadReports().end(); ++i) {
		AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);
		cmd.addParam("HI", (*i).hint);
		cmd.addParam("BU", aBundle->getToken());

		if (aBundle->isSet(Bundle::UPDATE_SIZE)) {
			aBundle->unsetFlag(Bundle::UPDATE_SIZE);
			cmd.addParam("SI", Util::toString(aBundle->getSize()));
		}

		if (aBundle->isSet(Bundle::UPDATE_NAME)) {
			aBundle->unsetFlag(Bundle::UPDATE_NAME);
			cmd.addParam("NA", aBundle->getName());
			//LogManager::getInstance()->message("Name: " + bundle->getName());
		}

		cmd.addParam("UD1");

		ClientManager::getInstance()->send(cmd, (*i).user->getCID(), true);
	}
}

void QueueManager::sendBundleFinished(BundlePtr aBundle) {
	for(auto i = aBundle->getUploadReports().begin(); i != aBundle->getUploadReports().end(); ++i) {
		AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

		cmd.addParam("HI", (*i).hint);
		cmd.addParam("BU", aBundle->getToken());
		cmd.addParam("FI1");

		ClientManager::getInstance()->send(cmd, (*i).user->getCID(), true);
	}
}

void QueueManager::removeBundleFiles(BundlePtr aBundle, bool removeFinished) {
	if (aBundle) {
		if (removeFinished) {
			Lock l (cs);
			for (auto i = aBundle->getFinishedFiles().begin(); i != aBundle->getFinishedFiles().end(); ++i) {
				File::deleteFile((*i)->getTarget());
			}
		}
		QueueItemList removeList = aBundle->getQueueItems();
		for (auto i = removeList.begin(); i != removeList.end(); ++i) {
			remove((*i)->getTarget());
		}
	}
}

void QueueManager::removeBundleItem(QueueItem* qi, bool finished, bool deleteQI) {
	BundlePtr bundle = qi->getBundle();
	if (!bundle) {
		//LogManager::getInstance()->message("QueueManager::removeBundleItem, token empty!");
		Lock l (cs);
		fileQueue.remove(qi, false);
		return;
	}
	bool emptyBundle = false;
	Bundle::CIDStringList notified;

	//LogManager::getInstance()->message("QueueManager::removeBundleItem, token: " + qi->getBundleToken());
	{
		Lock l (cs);
		if (bundle) {
			bundle->getQueueItems().erase(std::remove(bundle->getQueueItems().begin(), bundle->getQueueItems().end(), qi), bundle->getQueueItems().end());

			if (finished) {
				//LogManager::getInstance()->message("REMOVE FINISHED BUNDLEITEM, items: " + Util::toString(bundle->items.size()) + " totalsize: " + Util::formatBytes(bundle->getSize()));
				notified = bundle->getNotifiedUsers();
				fileQueue.remove(qi, true);
				bundle->getFinishedFiles().push_back(qi);
			} else {
				if (qi->getDownloadedBytes() > 0) {
					bundle->decreaseDownloaded(qi->getDownloadedBytes());
				}
				bundle->decreaseSize(qi->getSize());
				bundle->setFlag(Bundle::UPDATE_SIZE);
				if (deleteQI) {
					fileQueue.remove(qi, false);
				}
				//LogManager::getInstance()->message("REMOVE FAILED BUNDLEITEM, items: " + Util::toString(bundle->getQueueItems().size()) + " totalsize: " + Util::formatBytes(bundle->getSize()));
			}

			auto& s = bundle->getBundleDirs()[Util::getDir(qi->getTarget(), false, false)];
			s.erase(std::remove(s.begin(), s.end(), qi), s.end());

			if (bundle->getQueueItems().empty()) {
				emptyBundle = true;
			} else {
				//LogManager::getInstance()->message("QueueManager::removeBundleItem, set bundle dirty!: " + bundle->getName() + " items remaining: " + Util::toString(bundle->getQueueItems().size()));
				bundle->setDirty(true);
			}
		} else {
			//LogManager::getInstance()->message("QueueManager::removeBundleItem BUNDLE NOT FOUND");
		}
	}

	//notify users if finished
	if (finished) {
		for (auto s = notified.begin(); s != notified.end(); ++s) {
			sendPBD((*s).first, (*s).second, qi->getTTH(), bundle->getToken());
		}
	} else {
		addBundleUpdate(bundle->getToken());
	}

	if (emptyBundle) {
		removeBundle(bundle, finished);
	}
}

void QueueManager::removeBundle(BundlePtr aBundle, bool finished) {
	if (finished) {
		fire(QueueManagerListener::BundleFinished(), aBundle);
		sendBundleFinished(aBundle);
		string tmp;
		if (!SETTING(SCAN_DL_BUNDLES) && !aBundle->getFileBundle()) {
			tmp.resize(STRING(DL_BUNDLE_FINISHED).size() + 64);	 
			tmp.resize(snprintf(&tmp[0], tmp.size(), CSTRING(DL_BUNDLE_FINISHED), aBundle->getName().c_str()));	 
			LogManager::getInstance()->message(tmp);
			//LogManager::getInstance()->message("The Bundle " + bundle->getName() + " has finished downloading!");
		}
	} else {
		//LogManager::getInstance()->message("The Bundle " + aBundle->getName() + " has been removed");

		//clean finished files
		{
			Lock l (cs);
			for (auto i = aBundle->getFinishedFiles().begin(); i != aBundle->getFinishedFiles().end(); ++i) {
				fileQueue.removeTTH(*i);
			}
		}

		fire(QueueManagerListener::BundleRemoved(), aBundle);
	}

	{
		Lock l (cs);
		//handle dirs
		if (!aBundle->getFileBundle()) {
			for (auto i = aBundle->getBundleDirs().begin(); i != aBundle->getBundleDirs().end(); ++i) {
				string releaseDir = AirUtil::getReleaseDir(i->first);
				if (!releaseDir.empty()) {
					bundleDirs.erase(releaseDir);
				}
			}
		}

		bundles.erase(aBundle->getToken());
		fileQueue.removeSearchPrio(aBundle, aBundle->getPriority());
	}

	SearchManager::getInstance()->removeBundlePBD(aBundle->getToken());

	try {
		File::deleteFile(aBundle->getBundleFile() + ".bak");
		File::deleteFile(aBundle->getBundleFile());
	} catch(const FileException& /*e1*/) {
		LogManager::getInstance()->message("ERROR WHEN DELETING BUNDLE XML: " + aBundle->getName() + " file: " + aBundle->getBundleFile());
	}

	if (!finished) {
		//TODO: delete from memory
		//aBundle->dec();
	}
}

MemoryInputStream* QueueManager::generateTTHList(const HintedUser aUser, const string& bundleToken, bool isInSharingHub) {
	string tths, tmp2;
	StringOutputStream tthList(tths);

	//write finished items
	BundlePtr bundle = findBundle(bundleToken);
	if (bundle) {
		{
			Lock l (cs);
			for(auto i = bundle->getFinishedFiles().begin(); i != bundle->getFinishedFiles().end(); ++i) {
				tmp2.clear();
				tthList.write((*i)->getTTH().toBase32(tmp2) + " ");
			}
		}
		checkFinishedNotify(aUser.user->getCID(), bundleToken, true, aUser.hint);
	}

	//LogManager::getInstance()->message("TTHLIST: " + tths);

	if (tths.empty()) {
		dcdebug("QUEUEMANAGER TTHLIST NULL");
		return NULL;
	} else {
		return new MemoryInputStream(tths);
	}
}

void QueueManager::addTTHList(const HintedUser& aUser, const string& bundle) {
	//LogManager::getInstance()->message("ADD TTHLIST");
	addList(aUser, QueueItem::FLAG_TTHLIST | QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_MATCH_QUEUE, bundle);
}

bool QueueManager::checkFinishedNotify(const CID cid, const string bundleToken, bool addNotify, const string hubIpPort) {
	BundlePtr bundle = findBundle(bundleToken);
	if (bundle) {
		//check if the user is being notified already
		Lock l (cs);
		for (auto s = bundle->getNotifiedUsers().begin(); s != bundle->getNotifiedUsers().end(); ++s) {
			if ((*s).first == cid) {
				//LogManager::getInstance()->message("checkFinishedNotify: ALREADY NOTIFIED");
				return false;
			}
		}
		if (addNotify) {
			//LogManager::getInstance()->message("checkFinishedNotify: ADD NOTIFYUSER");
			bundle->getNotifiedUsers().insert(make_pair(cid, hubIpPort));
		}
		return true;
	} else {
		//LogManager::getInstance()->message("checkFinishedNotify: NO BUNDLE");
	}
	return false;
}

bool QueueManager::checkPBDReply(const HintedUser aUser, const TTHValue aTTH, string& _bundleToken, bool& _notify, bool& _add) {
	BundlePtr bundle = findBundle(aTTH);
	if (bundle) {
		//LogManager::getInstance()->message("checkPBDReply: FINISHED FOUND");
		_bundleToken = bundle->getToken();
		if (!bundle->getQueueItems().empty()) {
			_notify = true;
		}

		if (!bundle->getFinishedFiles().empty()) {
			_add=true;
		}

		if (checkFinishedNotify(aUser.user->getCID(), _bundleToken, true, aUser.hint)) {
			return true;
		}
	}
	//LogManager::getInstance()->message("checkPBDReply: CHECKNOTIFY FAIL");
	return false;
}

void QueueManager::removeBundleNotify(const CID cid, const string bundleToken) {
	BundlePtr bundle = findBundle(bundleToken);
	if (bundle) {
		//LogManager::getInstance()->message("QueueManager::removeBundleNotify: bundle found");
		Lock l (cs);
		for (auto s = bundle->getNotifiedUsers().begin(); s != bundle->getNotifiedUsers().end(); ++s) {
			if ((*s).first == cid) {
				//LogManager::getInstance()->message("QueueManager::removeBundleNotify: CID found");
				bundle->getNotifiedUsers().erase(s);
				return;
			}
		}
	} else {
		//LogManager::getInstance()->message("QueueManager::removeBundleNotify: bundle NOT found");
	}
}

void QueueManager::sendPBD(const CID cid, const string hubIpPort, const TTHValue& tth, const string bundleToken) {
	
	AdcCommand cmd(AdcCommand::CMD_PBD, AdcCommand::TYPE_UDP);

	cmd.addParam("UP1");
	cmd.addParam("HI", hubIpPort);
	cmd.addParam("TH", tth.toBase32());
	cmd.addParam("BU", bundleToken);
	//LogManager::getInstance()->message("SENDPBD UPDATE: " + cmd.toString());
	ClientManager::getInstance()->send(cmd, cid);
}

void QueueManager::updatePBD(const HintedUser aUser, const string bundleToken, const TTHValue aTTH) {
	//LogManager::getInstance()->message("UPDATEPBD");
	bool wantConnection = false;
	{
		Lock l(cs);
		QueueItemList qiList = fileQueue.find(aTTH);
		if (!qiList.empty()) {
			for (auto i = qiList.begin(); i != qiList.end(); ++i) {
				QueueItem* qi = *i;
				if(qi->isFinished())
					continue;
				if(qi->isSet(QueueItem::FLAG_USER_LIST))
					continue;

				try {
					//LogManager::getInstance()->message("ADDSOURCE");
					wantConnection = addSource(qi, aUser, QueueItem::Source::FLAG_FILE_NOT_AVAILABLE); 
				} catch(...) {
					// Ignore...
				}
			}
		}
	}
	if(wantConnection && aUser.user->isOnline())
		ConnectionManager::getInstance()->getDownloadConnection(aUser);
}

// compare nextQueryTime, get the oldest ones
void QueueManager::FileQueue::findPFSSources(PFSSourceList& sl) 
{
	typedef multimap<time_t, pair<QueueItem::SourceConstIter, const QueueItem*> > Buffer;
	Buffer buffer;
	uint64_t now = GET_TICK();

	for(auto i = queue.begin(); i != queue.end(); ++i) {
		QueueItem* q = i->second;

		if(q->getSize() < PARTIAL_SHARE_MIN_SIZE) continue;

		// don't share when file does not exist
		if(!Util::fileExists(q->isFinished() ? q->getTarget() : q->getTempTarget()))
			continue;

		const QueueItem::SourceList& sources = q->getSources();
		const QueueItem::SourceList& badSources = q->getBadSources();

		for(QueueItem::SourceConstIter j = sources.begin(); j != sources.end(); ++j) {
			if(	(*j).isSet(QueueItem::Source::FLAG_PARTIAL) && (*j).getPartialSource()->getNextQueryTime() <= now &&
				(*j).getPartialSource()->getPendingQueryCount() < 10 && (*j).getPartialSource()->getUdpPort() > 0)
			{
				buffer.insert(make_pair((*j).getPartialSource()->getNextQueryTime(), make_pair(j, q)));
			}
		}

		for(QueueItem::SourceConstIter j = badSources.begin(); j != badSources.end(); ++j) {
			if(	(*j).isSet(QueueItem::Source::FLAG_TTH_INCONSISTENCY) == false && (*j).isSet(QueueItem::Source::FLAG_PARTIAL) &&
				(*j).getPartialSource()->getNextQueryTime() <= now && (*j).getPartialSource()->getPendingQueryCount() < 10 &&
				(*j).getPartialSource()->getUdpPort() > 0)
			{
				buffer.insert(make_pair((*j).getPartialSource()->getNextQueryTime(), make_pair(j, q)));
			}
		}
	}

	// copy to results
	dcassert(sl.empty());
	const int32_t maxElements = 10;
	sl.reserve(maxElements);
	for(Buffer::iterator i = buffer.begin(); i != buffer.end() && sl.size() < maxElements; i++){
		sl.push_back(i->second);
	}
}

} // namespace dcpp

/**
 * @file
 * $Id: QueueManager.cpp 568 2011-07-24 18:28:43Z bigmuscle $
 */
