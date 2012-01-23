/*
 * Copyright (C) 2011 AirDC++ Project
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
#include "TimerManager.h"
#include "Download.h"
#include "UserConnection.h"
#include "HashManager.h"
#include "QueueItem.h"
#include "LogManager.h"
#include "AirUtil.h"
#include "SearchResult.h"
#include "SimpleXML.h"

#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/numeric.hpp>

namespace dcpp {
	
Bundle::Bundle(QueueItem* qi, const string& aToken) : target(qi->getTarget()), fileBundle(true), token(aToken), size(qi->getSize()), 
	finishedSegments(qi->getDownloadedSegments()), speed(0), lastSpeed(0), running(0), lastPercent(0), singleUser(true), 
	priority((Priority)qi->getPriority()), autoPriority(true), dirty(true), added(qi->getAdded()), dirDate(0), simpleMatching(true), recent(false), 
	currentDownloaded(qi->getDownloadedBytes()), hashed(0), moved(0) {
	qi->setBundle(this);
	queueItems.push_back(qi);
	setFlag(FLAG_NEW);
}

Bundle::Bundle(const string& target, time_t added, Priority aPriority, time_t aDirDate /*0*/) : target(target), fileBundle(false), token(Util::toString(Util::rand())), size(0), 
	finishedSegments(0), speed(0), lastSpeed(0), running(0), lastPercent(0), singleUser(true), priority(aPriority), dirty(true), added(added), simpleMatching(true), 
	recent(false), currentDownloaded(0), hashed(0), moved(0) {

	if (dirDate > 0) {
		recent = (dirDate + (SETTING(RECENT_BUNDLE_HOURS)*60*60)) > GET_TIME();
	} else {
		dirDate = GET_TIME();
	}

	if (aPriority != DEFAULT) {
		autoPriority = false;
	} else {
		priority = LOW;
		autoPriority = true;
	}
	setFlag(FLAG_NEW);
}

Bundle::~Bundle() { 
	//bla
}

void Bundle::setDownloadedBytes(int64_t aSize) {
	dcassert(aSize + finishedSegments <= size);
	dcassert(((aSize + finishedSegments)) >= currentDownloaded);
	dcassert(((aSize + finishedSegments)) >= 0);
	currentDownloaded = aSize;
	dcassert(currentDownloaded <= size);
}

void Bundle::addSegment(int64_t aSize, bool downloaded) {
#ifdef _DEBUG
	int64_t tmp1 = accumulate(queueItems.begin(), queueItems.end(), (int64_t)0, [&](int64_t old, QueueItem* qi) {
		return old + qi->getDownloadedSegments(); 
	});

	tmp1 = accumulate(finishedFiles.begin(), finishedFiles.end(), tmp1, [&](int64_t old, QueueItem* qi) {
		return old + qi->getDownloadedSegments(); 
	});
	dcassert(tmp1 == aSize + finishedSegments);
#endif

	dcassert(aSize + finishedSegments <= size);
	finishedSegments += aSize;
	/*if (downloaded) {
		currentDownloaded -= aSize;
	} */
	dcassert(currentDownloaded >= 0);
	dcassert(currentDownloaded <= size);
	dcassert(finishedSegments <= size);
}

void Bundle::removeDownloadedSegment(int64_t aSize) {
	dcassert(finishedSegments - aSize >= 0);
	finishedSegments -= aSize;
	dcassert(finishedSegments <= size);
	dcassert(currentDownloaded <= size);
}

void Bundle::finishBundle() noexcept {
	speed = 0;
	currentDownloaded = 0;
}

int64_t Bundle::getSecondsLeft() {
	return (speed > 0) ? static_cast<int64_t>((size - (currentDownloaded+finishedSegments)) / speed) : 0;
}

string Bundle::getName() {
	if (!fileBundle) {
		return Util::getDir(target, false, true);
	} else {
		return Util::getFileName(target);
	}
}

void Bundle::setDirty(bool enable) {
	if (enable) {
		if(!dirty) {
			dirty = true;
		}
	} else {
		dirty = false;
	}
}

QueueItem* Bundle::findQI(const string& aTarget) const {
	for(auto i = queueItems.begin(); i != queueItems.end(); ++i) {
		QueueItem* qi = *i;
		if (qi->getTarget() == aTarget) {
			return qi;
		}
	}
	return NULL;
}

string Bundle::getBundleFile() {
	return Util::getPath(Util::PATH_BUNDLES) + "Bundle" + token + ".xml";
}


void Bundle::getItems(const UserPtr& aUser, QueueItemList& ql) noexcept {
	for(int i = 0; i < QueueItem::LAST; ++i) {
		auto j = userQueue[i].find(aUser);
		if(j != userQueue[i].end()) {
			for(auto m = j->second.begin(); m != j->second.end(); ++m) {
				ql.push_back(*m);
			}
		}
	}
}

int64_t Bundle::getDiskUse(bool countAll) {
	int64_t size = 0; 
	for (auto p = queueItems.begin(); p != queueItems.end(); ++p) {
		if (countAll || (*p)->getDownloadedBytes() == 0) {
			size += (*p)->getSize();
		}
	}
	return size;
}

void Bundle::addFinishedItem(QueueItem* qi, bool finished) {
	finishedFiles.push_back(qi);
	if (!finished) {
		moved++;
		qi->setBundle(this);
		increaseSize(qi->getSize());
		addSegment(qi->getSize(), false);
	}
}

void Bundle::removeFinishedItem(QueueItem* qi) {
	int pos = 0;
	for (auto s = finishedFiles.begin(); s != finishedFiles.end(); ++s) {
		if ((*s) == qi) {
			dcassert(moved > 0);
			moved--;
			decreaseSize(qi->getSize());
			removeDownloadedSegment(qi->getSize());
			swap(finishedFiles[pos], finishedFiles[finishedFiles.size()-1]);
			finishedFiles.pop_back();
			return;
		}
		pos++;
	}
}

bool Bundle::addQueue(QueueItem* qi) {
	dcassert(find(queueItems.begin(), queueItems.end(), qi) == queueItems.end());
	qi->setBundle(this);
	queueItems.push_back(qi);
	increaseSize(qi->getSize());

	string dir = Util::getDir(qi->getTarget(), false, false);
	bundleDirs[dir]++;
	if (bundleDirs[dir] == 1) {
		return true;
	}
	return false;
}

bool Bundle::removeQueue(QueueItem* qi, bool finished) {
	int pos = 0;
	for (auto s = queueItems.begin(); s != queueItems.end(); ++s) {
		if ((*s) == qi) {
			swap(queueItems[pos], queueItems[queueItems.size()-1]);
			queueItems.pop_back();
			break;
		}
		pos++;
	}

	if (!finished) {
		if (qi->getDownloadedSegments() > 0) {
			removeDownloadedSegment(qi->getDownloadedSegments());
		}
		decreaseSize(qi->getSize());
		setFlag(Bundle::FLAG_UPDATE_SIZE);
	} else {
		addFinishedItem(qi, true);
	}

	bundleDirs[Util::getDir(qi->getTarget(), false, false)]--;
	if (bundleDirs[Util::getDir(qi->getTarget(), false, false)] == 0) {
		bundleDirs.erase(Util::getDir(qi->getTarget(), false, false));
		return true;
	}
	return false;
}

bool Bundle::isSource(const UserPtr& aUser) {
	return find_if(sources.begin(), sources.end(), [&](const SourceTuple& st) { return get<Bundle::SOURCE_USER>(st).user == aUser; }) != sources.end();
}

bool Bundle::isBadSource(const UserPtr& aUser) {
	return find_if(badSources.begin(), badSources.end(), [&](const SourceTuple& st) { return get<Bundle::SOURCE_USER>(st).user == aUser; }) != badSources.end();
}

void Bundle::addUserQueue(QueueItem* qi) {
	for(QueueItem::SourceConstIter i = qi->getSources().begin(); i != qi->getSources().end(); ++i) {
		addUserQueue(qi, i->getUser());
	}
}

bool Bundle::addUserQueue(QueueItem* qi, const HintedUser& aUser) {
	auto& l = userQueue[qi->getPriority()][aUser.user];
	dcassert(find(l.begin(), l.end(), qi) == l.end());
	l.push_back(qi);
	if (l.size() > 1) {
		auto i = l.begin();
		auto start = (size_t)Util::rand((uint32_t)(l.size() < 200 ? l.size() : 200)); //limit the max value to lower the required moving distance
		advance(i, start);
		swap(queueItems[start], queueItems[queueItems.size()-1]);
	}

	auto i = find_if(sources.begin(), sources.end(), [&](const SourceTuple& st) { return get<Bundle::SOURCE_USER>(st) == aUser; });
	if (i != sources.end()) {
		get<SOURCE_FILES>(*i)++;
		get<SOURCE_SIZE>(*i) += qi->getSize();
		//LogManager::getInstance()->message("ADD, SOURCE FOR " + Util::toString(i->second) + " ITEMS");
		return false;
	} else {
		sources.push_back(make_tuple(aUser, qi->getSize() - qi->getDownloadedSegments(), 1));
		return true;
	}
	//LogManager::getInstance()->message("ADD QI FOR BUNDLE USERQUEUE, total items for the user " + aUser->getCID().toBase32() + ": " + Util::toString(l.size()));
}

QueueItem* Bundle::getNextQI(const UserPtr& aUser, string aLastError, Priority minPrio, int64_t wantedSize, int64_t lastSpeed, bool smallSlot, bool allowOverlap) {
	int p = QueueItem::LAST - 1;
	do {
		auto i = userQueue[p].find(aUser);
		if(i != userQueue[p].end()) {
			dcassert(!i->second.empty());
			for(auto j = i->second.begin(); j != i->second.end(); ++j) {
				QueueItem* qi = *j;
				if (qi->hasSegment(aUser, aLastError, wantedSize, lastSpeed, smallSlot, allowOverlap)) {
					return qi;
				}
			}
		}
		p--;
	} while(p >= minPrio);

	return NULL;
}

bool Bundle::isFinishedNotified(const UserPtr& aUser) {
	return find_if(finishedNotifications.begin(), finishedNotifications.end(), [&](const UserBundlePair& ubp) { return ubp.first.user == aUser; }) != finishedNotifications.end();;
}

void Bundle::addFinishedNotify(HintedUser& aUser, const string& remoteBundle) {
	if (!isFinishedNotified(aUser.user) && !isBadSource(aUser)) {
		finishedNotifications.push_back(make_pair(aUser, remoteBundle));
	}
}

void Bundle::removeFinishedNotify(const UserPtr& aUser) {
	for (auto s = finishedNotifications.begin(); s != finishedNotifications.end(); ++s) {
		if (s->first.user == aUser) {
			//LogManager::getInstance()->message("QueueManager::removeBundleNotify: CID found");
			finishedNotifications.erase(s);
			return;
		}
	}
}

void Bundle::getDownloadsQI(DownloadList& l) {
	for (auto s = queueItems.begin(); s != queueItems.end(); ++s) {
		QueueItem* qi = *s;
		for (auto k = qi->getDownloads().begin(); k != qi->getDownloads().end(); ++k) {
			l.push_back(*k);
		}
	}
}

void Bundle::getSources(HintedUserList& l) {
	for_each(sources.begin(), sources.end(), [&](SourceTuple st) { l.push_back(get<Bundle::SOURCE_USER>(st)); });
}

void Bundle::getDirQIs(const string& aDir, QueueItemList& ql) {
	if (aDir == target) {
		ql = queueItems;
		return;
	}

	for (auto s = queueItems.begin(); s != queueItems.end(); ++s) {
		QueueItem* qi = *s;
		if (AirUtil::isSub(qi->getTarget(), aDir)) {
			ql.push_back(qi);
		}
	}
}

string Bundle::getMatchPath(const string& aRemoteFile, const string& aLocalFile, bool nmdc) {
	/* returns the local path for nmdc and the remote path for adc */
	string remoteDir = Util::getFilePath(aRemoteFile);
	string bundleDir = Util::getFilePath(aLocalFile);
	string path;
	if (simpleMatching) {
		if (nmdc) {
			if (Text::toLower(remoteDir).find(Text::toLower(getName())) != string::npos)
				path = target;
		} else {
			path = Util::getDir(remoteDir, true, false);
		}
	} else {
		/* try to find the bundle name from the path */
		size_t pos = Text::toLower(remoteDir).find(Text::toLower(getName()) + "\\");
		if (pos != string::npos) {
			path = nmdc ? target : remoteDir.substr(0, pos+getName().length()+1);
		}
	}
		
	if (path.empty() && remoteDir.length() > 3) {
		/* failed, look up the common dirs from the end */
		string::size_type i = remoteDir.length()-2;
		string::size_type j;
		for(;;) {
			j = remoteDir.find_last_of("\\", i);
			if(j == string::npos)
				break;
			if(stricmp(remoteDir.substr(j), bundleDir.substr(bundleDir.length() - (remoteDir.length()-j))) != 0)
				break;
			i = j - 1;
		}
		if ((remoteDir.length() - j)-1 > bundleDir.length() - target.length()) {
			/* The next dir to compare would be the bundle dir but it doesn't really exist in the path (which is why we are here) */
			/* There's a risk that the other user has different directory structure and all subdirs inside a big list directory */
			/* In those cases the recursive partial list can be huge, or in NMDC there's a bigger risk of adding the sources for files that they don't really have */
			/* TODO: do something with those */
		}
		path = nmdc ? bundleDir.substr(0, bundleDir.length() - (remoteDir.length()-i-2)) : remoteDir.substr(0, i+2);
	}
	return path;
}

string Bundle::getDirPath(const string& aDir) noexcept {
	string path;
	string releaseDir = AirUtil::getReleaseDir(Util::getDir(aDir, false, false));
	if (releaseDir.empty())
		return Util::emptyString;

	//size_t pos = Text::toLower(aDir).find(Text::toLower(getName()) + "\\");
	for (auto s = bundleDirs.begin(); s != bundleDirs.end(); ++s) {
		if (s->first.length() > releaseDir.length()) {
			//compare the end of the dir with the release dir
			if (stricmp(s->first.substr(s->first.length()-releaseDir.length()-1, releaseDir.length()), releaseDir) == 0)
				return s->first;
		}
	}
	return Util::emptyString;
}

QueueItemList Bundle::getRunningQIs(const UserPtr& aUser) noexcept {
	QueueItemList ret;
	auto i = runningItems.find(aUser);
	if (i != runningItems.end()) {
		return i->second;
	}
	return ret;
}

void Bundle::removeUserQueue(QueueItem* qi) noexcept {
	for(QueueItem::SourceConstIter i = qi->getSources().begin(); i != qi->getSources().end(); ++i) {
		removeUserQueue(qi, i->getUser(), false);
	}
}

bool Bundle::removeUserQueue(QueueItem* qi, const UserPtr& aUser, bool addBad) noexcept {

	dcassert(qi->isSource(aUser));
	auto& ulm = userQueue[qi->getPriority()];
	auto j = ulm.find(aUser);
	dcassert(j != ulm.end());
	if (j == ulm.end()) {
		return false;
	}
	auto& l = j->second;
	int pos = 0;
	for (auto s = l.begin(); s != l.end(); ++s) {
		if ((*s) == qi) {
			swap(l[pos], l[l.size()-1]);
			l.pop_back();
			break;
		}
		pos++;
	}

	if(l.empty()) {
		ulm.erase(j);
	}

	//check bundle sources
	auto m = find_if(sources.begin(), sources.end(), [&](const SourceTuple& st) { return get<Bundle::SOURCE_USER>(st).user == aUser; });
	dcassert(m != sources.end());

	if (addBad) {
		auto bsi = find_if(badSources.begin(), badSources.end(), [&](const SourceTuple& st) { return get<Bundle::SOURCE_USER>(st).user == aUser; });
		if (bsi == badSources.end()) {
			badSources.push_back(make_tuple(get<SOURCE_USER>(*m), qi->getSize(), 1));
		} else {
			get<SOURCE_FILES>(*bsi)++;
			get<SOURCE_SIZE>(*bsi) += qi->getSize();
		}
	}

	get<SOURCE_FILES>(*m)--;
	get<SOURCE_SIZE>(*m) -= qi->getSize();
	//LogManager::getInstance()->message("REMOVE, SOURCE FOR " + Util::toString(m->second) + " ITEMS");
	if (get<SOURCE_FILES>(*m) == 0) {
		sources.erase(m);   //crashed when nothing found to erase with only 1 source and removing multiple bundles.
		return true;
	}
	return false;
}

void Bundle::removeBadSource(const HintedUser& aUser) noexcept {
	auto m = find_if(badSources.begin(), badSources.end(), [&](const SourceTuple& st) { return get<Bundle::SOURCE_USER>(st) == aUser; });
	dcassert(m != badSources.end());
	if (m != badSources.end()) {
		badSources.erase(m);
	}
	dcassert(m == badSources.end());
}
	
Bundle::Priority Bundle::calculateProgressPriority() const noexcept {
	if(autoPriority) {
		Bundle::Priority p;
		int percent = static_cast<int>(getDownloadedBytes() * 10.0 / size);
		switch(percent){
				case 0:
				case 1:
				case 2:
					p = Bundle::LOW;
					break;
				case 3:
				case 4:
				case 5:						
				default:
					p = Bundle::NORMAL;
					break;
				case 6:
				case 7:
				case 8:
					p = Bundle::HIGH;
					break;
					case 9:
					case 10:
					p = Bundle::HIGHEST;			
					break;
		}
		return p;			
	}
	return priority;
}

pair<int64_t, double> Bundle::getPrioInfo() noexcept {
	vector<int64_t> speedList, sizeList;
	for (auto s = sources.begin(); s != sources.end(); ++s) {
		UserPtr& u = get<SOURCE_USER>(*s).user;
		int64_t filesSize = accumulate(queueItems.begin(), queueItems.end(), (int64_t)0, [&](int64_t old, QueueItem* qi) {
			return qi->isSource(u) ? old + (qi->getSize() - qi->getDownloadedSegments()) : old; 
		});
		int64_t timeLeft = static_cast<int64_t>(filesSize * u->getSpeed());

		//lower prio for offline users
		sizeList.push_back(u->isOnline() ? filesSize : filesSize*2);
		if (timeLeft > 0)
			speedList.push_back(timeLeft);
	}
	int64_t speedRatio = speedList.empty() ? 0 : dcpp::accumulate(speedList.begin(), speedList.end(), (int64_t)0) / speedList.size();
	double sizeRatio = dcpp::accumulate(sizeList.begin(), sizeList.end(), (double)0) / static_cast<double>(size);
	return make_pair(speedRatio, (sizeRatio > 0 ? sizeRatio : 1));
}

void Bundle::getQIBalanceMaps(SourceSpeedMapQI& speedMap, SourceSpeedMapQI& sourceMap) noexcept {
	for (auto j = queueItems.begin(); j != queueItems.end(); ++j) {
		QueueItem* q = *j;
		if(q->getAutoPriority()) {
			if(q->getPriority() != QueueItem::PAUSED) {
				int64_t qiSpeed = 0;
				double qiSources = 0;
				for (auto s = q->getSources().begin(); s != q->getSources().end(); ++s) {
					if ((*s).getUser().user->isOnline()) {
						qiSpeed += (*s).getUser().user->getSpeed();
						qiSources++;
					} else {
						qiSources += 2;
					}
				}
				//sources = sources / bundle->getQueueItems().size();
				///if (verbose) {
				//	LogManager::getInstance()->message("Sourcepoints for bundle " + bundle->getName() + " : " + Util::toString(sources));
				//}
				speedMap.insert(make_pair((double)qiSpeed, q));
				sourceMap.insert(make_pair(qiSources, q));
			}
		}
	}
}

void Bundle::calculateBalancedPriorities(PrioList& priorities, SourceSpeedMapQI& speedMap, SourceSpeedMapQI& sourceMap, bool verbose) noexcept {
	map<QueueItem*, double> autoPrioMap;
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
	multimap<int, QueueItem*> finalMap;
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
			LogManager::getInstance()->message("Not enough QueueItems for the bundle " + getName() + " with unique points to perform the priotization!");
		}
		return;
	} else if (uniqueValues > 2) {
		prioGroup = uniqueValues / 3;
	}

	if (verbose) {
		LogManager::getInstance()->message("BUNDLE QIs: Unique values: " + Util::toString(uniqueValues) + " prioGroup size: " + Util::toString(prioGroup));
	}


	//priority to set (4-2, high-low)
	int8_t prio = 4;

	//counters for analyzing identical points
	int lastPoints = 999;
	int prioSet=0;

	for (auto i = finalMap.begin(); i != finalMap.end(); ++i) {
		if (lastPoints==i->first) {
			if (verbose) {
				LogManager::getInstance()->message("QueueItem: " + i->second->getTarget() + " points: " + Util::toString(i->first) + " setting prio " + AirUtil::getPrioText(prio));
			}
			priorities.push_back(make_pair(i->second, prio));
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
				LogManager::getInstance()->message("QueueItem: " + i->second->getTarget() + " points: " + Util::toString(i->first) + " setting prio " + AirUtil::getPrioText(prio));
			}
			priorities.push_back(make_pair(i->second, (int8_t)prio));
			prioSet++;
			lastPoints=i->first;
		}
	}
}

size_t Bundle::countOnlineUsers() const noexcept {
	size_t users = 0;
	int files = 0;
	for(auto i = sources.begin(); i != sources.end(); ++i) {
		if(get<SOURCE_USER>(*i).user->isOnline()) {
			users++;
			files += get<SOURCE_FILES>(*i);
		}
	}
	return (queueItems.size() == 0 ? 0 : (files / queueItems.size()));
}

tstring Bundle::getBundleText() noexcept {
	double percent = (currentDownloaded+finishedSegments) > size ? 100.00 : (double)((currentDownloaded+finishedSegments)*100.0)/(double)size;
	if (fileBundle) {
		return Text::toT(getName());
	} else {
		return Text::toT(getName()) + _T(" (") + Util::toStringW(percent) + _T("%, ") + Text::toT(AirUtil::getPrioText(priority)) + _T(", ") + Util::toStringW(sources.size()) + _T(" sources)");
	}
}

void Bundle::sendRemovePBD(const UserPtr& aUser) noexcept {
	//LogManager::getInstance()->message("QueueManager::sendRemovePBD");
	for (auto s = finishedNotifications.begin(); s != finishedNotifications.end(); ++s) {
		if (s->first.user == aUser) {
			AdcCommand cmd(AdcCommand::CMD_PBD, AdcCommand::TYPE_UDP);

			cmd.addParam("HI", s->first.hint);
			cmd.addParam("BU", s->second);
			cmd.addParam("RM1");
			ClientManager::getInstance()->send(cmd, s->first.user->getCID());
			return;
			//LogManager::getInstance()->message("QueueManager::sendRemovePBD: user found");
		}
	}
}

void Bundle::getTTHList(OutputStream& tthList) noexcept {
	string tmp2;
	for(auto i = finishedFiles.begin(); i != finishedFiles.end(); ++i) {
		tmp2.clear();
		tthList.write((*i)->getTTH().toBase32(tmp2) + " ");
	}
}

void Bundle::getSearchItems(StringPairList& searches, bool manual) noexcept {
	if (fileBundle) {
		searches.push_back(make_pair(Util::emptyString, queueItems.front()->getTTH().toBase32()));
		return;
	}

	string searchString;
	for (auto i = bundleDirs.begin(); i != bundleDirs.end(); ++i) {
		string dir = Util::getDir(i->first, true, false);
		//don't add the same directory twice
		if (find_if(searches.begin(), searches.end(), [&](const StringPair& sp) { return sp.first == dir; }) != searches.end()) {
			continue;
		}

		QueueItemList ql;
		getDirQIs(dir, ql);

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
			if(q->getPriority() == QueueItem::PAUSED && !manual) {
				s++;
				continue;
			}
			if(q->isRunning() || (q->getPriority() == QueueItem::PAUSED)) {
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
		}
	}
}

void Bundle::updateSearchMode() {
	StringList searches;
	for (auto i = bundleDirs.begin(); i != bundleDirs.end(); ++i) {
		string dir = Util::getDir(i->first, true, false);
		if (find(searches.begin(), searches.end(), dir) == searches.end()) {
			searches.push_back(dir);
		}
	}
	simpleMatching = searches.size() <= 4 ? true : false;
}

/* ONLY CALLED FROM DOWNLOADMANAGER BEGIN */

void Bundle::addDownload(Download* d) noexcept {
	downloads.push_back(d);
}

void Bundle::removeDownload(Download* d) noexcept {
	auto m = find(downloads.begin(), downloads.end(), d);
	dcassert(m != downloads.end());
	if (m != downloads.end()) {
		countSpeed();
		downloads.erase(m);
	}
}

uint64_t Bundle::countSpeed() noexcept {
	int64_t bundleSpeed = 0, bundleRatio = 0, bundlePos = 0;
	int down = 0;
	for (auto s = downloads.begin(); s != downloads.end(); ++s) {
		Download* d = *s;
		if (d->getAverageSpeed() > 0 && d->getStart() > 0) {
			down++;
			int64_t pos = d->getPos();
			bundleSpeed += d->getAverageSpeed();
			bundleRatio += pos > 0 ? (double)d->getActual() / (double)pos : 1.00;
			bundlePos += pos;
		}
	}

	if (bundleSpeed > 0) {
		setDownloadedBytes(bundlePos);
		speed = bundleSpeed;
		running = down;

		bundleRatio = bundleRatio / down;
		actual = ((int64_t)((double)(finishedSegments+bundlePos) * (bundleRatio == 0 ? 1.00 : bundleRatio)));
	}
	return bundleSpeed;
}

void Bundle::addUploadReport(const HintedUser& aUser) noexcept {
	if (uploadReports.empty()) {
		lastSpeed = 0;
		lastPercent = 0;
	}
	uploadReports.push_back(aUser);
}

void Bundle::removeUploadReport(const UserPtr& aUser) noexcept {
	for(auto i = uploadReports.begin(); i != uploadReports.end(); ++i) {
		if (i->user == aUser) {
			uploadReports.erase(i);
			//LogManager::getInstance()->message("ERASE UPLOAD REPORT: " + Util::toString(bundle->getUploadReports().size()));
			break;
		}
	}
}

void Bundle::sendUBN(const string& speed, double percent) noexcept {
	for(auto i = uploadReports.begin(); i != uploadReports.end(); ++i) {
		AdcCommand cmd(AdcCommand::CMD_UBN, AdcCommand::TYPE_UDP);

		cmd.addParam("HI", i->hint);
		cmd.addParam("BU", token);
		if (!speed.empty())
			cmd.addParam("DS", speed);
		if (percent > 0)
			cmd.addParam("PE", Util::toString(percent));

		ClientManager::getInstance()->send(cmd, i->user->getCID(), true);
	}
}

bool Bundle::sendBundle(UserConnection* aSource, bool updateOnly) noexcept {
	AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

	cmd.addParam("HI", aSource->getHintedUser().hint);
	cmd.addParam("TO", aSource->getToken());
	cmd.addParam("BU", token);
	if (!updateOnly) {
		cmd.addParam("SI", Util::toString(size));
		cmd.addParam("NA", getName());
		cmd.addParam("DL", Util::toString(currentDownloaded+finishedSegments));
		if (singleUser) {
			cmd.addParam("SU1");
		} else {
			cmd.addParam("MU1");
		}
		cmd.addParam("AD1");
	} else {
		cmd.addParam("CH1");
	}
	return ClientManager::getInstance()->send(cmd, aSource->getUser()->getCID(), true, true);
}

void Bundle::sendBundleMode() noexcept {
	for(auto i = uploadReports.begin(); i != uploadReports.end(); ++i) {
		AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

		cmd.addParam("HI", (*i).hint);
		cmd.addParam("BU", token);
		cmd.addParam("UD1");
		if (singleUser) {
			cmd.addParam("SU1");
			cmd.addParam("DL", Util::toString(finishedSegments));
		} else {
			cmd.addParam("MU1");
		}

		ClientManager::getInstance()->send(cmd, (*i).user->getCID(), true);
	}
}

void Bundle::sendBundleFinished() noexcept {
	for(auto i = uploadReports.begin(); i != uploadReports.end(); ++i) {
		sendBundleFinished(*i);
	}
}

void Bundle::sendBundleFinished(const HintedUser& aUser) noexcept {
	AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

	cmd.addParam("HI", aUser.hint);
	cmd.addParam("BU", token);
	cmd.addParam("FI1");

	ClientManager::getInstance()->send(cmd, aUser.user->getCID(), true);
}

void Bundle::sendSizeNameUpdate() noexcept {
	//LogManager::getInstance()->message("QueueManager::sendBundleUpdate");
	for(auto i = uploadReports.begin(); i != uploadReports.end(); ++i) {
		AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);
		cmd.addParam("HI", (*i).hint);
		cmd.addParam("BU", token);

		if (isSet(FLAG_UPDATE_SIZE)) {
			unsetFlag(FLAG_UPDATE_SIZE);
			cmd.addParam("SI", Util::toString(size));
			//LogManager::getInstance()->message("UBD for bundle: " + aBundle->getName() + ", size: " + Util::toString(aBundle->getSize()));
		}

		if (isSet(FLAG_UPDATE_NAME)) {
			unsetFlag(FLAG_UPDATE_NAME);
			cmd.addParam("NA", getName());
			//LogManager::getInstance()->message("UBD for bundle: " + aBundle->getName() + ", name: " + aBundle->getName());
		}

		cmd.addParam("UD1");

		ClientManager::getInstance()->send(cmd, (*i).user->getCID(), true);
	}
}

/* ONLY CALLED FROM DOWNLOADMANAGER END */


void Bundle::save() {
	//LogManager::getInstance()->message("SAVING BUNDLE: " + bundle->getName());
	File ff(getBundleFile() + ".tmp", File::WRITE, File::CREATE | File::TRUNCATE);
	BufferedOutputStream<false> f(&ff);
	f.write(SimpleXML::utf8Header);
	string tmp;
	string b32tmp;

	if (getFileBundle()) {
		f.write(LIT("<File Version=\"1.0\" Token=\""));
		f.write(token);
		f.write(LIT("\">\r\n"));
		queueItems.front()->save(f, tmp, b32tmp);
		f.write(LIT("</File>\r\n"));
	} else {
		f.write(LIT("<Bundle Version=\"1\" Target=\""));
		f.write(SimpleXML::escape(target, tmp, true));
		f.write(LIT("\" Token=\""));
		f.write(token);
		f.write(LIT("\" Added=\""));
		f.write(Util::toString(added));
		f.write(LIT("\" Date=\""));
		f.write(Util::toString(dirDate));
		if (!autoPriority) {
			f.write(LIT("\" Priority=\""));
			f.write(Util::toString((int)priority));
		}
		f.write(LIT("\">\r\n"));

		for (auto k = finishedFiles.begin(); k != finishedFiles.end(); ++k) {
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

		for (auto j = queueItems.begin(); j != queueItems.end(); ++j) {
			(*j)->save(f, tmp, b32tmp);
		}

		f.write(LIT("</Bundle>\r\n"));
	}

	f.flush();
	ff.close();
	try {
		File::deleteFile(getBundleFile());
		File::renameFile(getBundleFile() + ".tmp", getBundleFile());
	}catch(...) {
		LogManager::getInstance()->message("ERROR WHEN MOVING BUNDLEXML: " + getName());
	}
	setDirty(false);
}

}
