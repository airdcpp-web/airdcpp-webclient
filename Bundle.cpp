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

namespace dcpp {
	
Bundle::Bundle(QueueItem* qi, const string& aToken) : target(qi->getTarget()), fileBundle(true), token(aToken), size(qi->getSize()), 
	downloadedSegments(qi->getDownloadedSegments()), speed(0), lastSpeed(0), running(0), lastPercent(0), singleUser(true), 
	priority((Priority)qi->getPriority()), autoPriority(true), dirty(true), added(qi->getAdded()), dirDate(0), simpleMatching(true), recent(false), 
	bytesDownloaded(qi->getDownloadedBytes()), hashed(0) {
	qi->setBundle(this);
	queueItems.push_back(qi);
	setFlag(FLAG_NEW);
}

Bundle::Bundle(const string& target, time_t added) : target(target), fileBundle(false), token(Util::toString(Util::rand())), size(0), downloadedSegments(0), speed(0), lastSpeed(0), 
		running(0), lastPercent(0), singleUser(true), priority(DEFAULT), autoPriority(true), dirty(true), added(added), dirDate(0), simpleMatching(true), recent(false), bytesDownloaded(0),
		hashed(0) { 
	setFlag(FLAG_NEW);
}

Bundle::~Bundle() { 
	//bla
}

void Bundle::setDownloadedBytes(int64_t aSize) {
	dcassert(aSize + downloadedSegments <= size);
	dcassert(((uint64_t)(aSize + downloadedSegments)) >= bytesDownloaded);
	dcassert(((aSize + downloadedSegments)) >= 0);
	bytesDownloaded = downloadedSegments + aSize;
	dcassert(bytesDownloaded <= (uint64_t)size);
}

void Bundle::addDownloadedSegment(int64_t aSize) {
	dcassert(aSize + downloadedSegments <= size);
	downloadedSegments += aSize;
	dcassert(downloadedSegments <= size);
}

void Bundle::removeDownloadedSegment(int64_t aSize) {
	dcassert(downloadedSegments - aSize >= 0);
	downloadedSegments -= aSize;
	bytesDownloaded -= aSize;
	dcassert(downloadedSegments <= size);
	dcassert(bytesDownloaded <= (uint64_t)size);
}

uint64_t Bundle::getSecondsLeft() {
	double avg = getSpeed();
	return (avg > 0) ? static_cast<int64_t>((size - bytesDownloaded) / avg) : 0;
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

void Bundle::addFinishedItem(QueueItem* qi, bool finished) {
	if (find(finishedFiles.begin(), finishedFiles.end(), qi) == finishedFiles.end()) {
		if (!finished) {
			increaseSize(qi->getSize());
			addDownloadedSegment(qi->getSize());
			qi->setBundle(this);
		}
		finishedFiles.push_back(qi);
	}
}

void Bundle::removeFinishedItem(QueueItem* qi) {
	int pos = 0;
	for (auto s = finishedFiles.begin(); s != finishedFiles.end(); ++s) {
		if ((*s) == qi) {
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
	if (qi->getDownloadedSegments() > 0) {
		addDownloadedSegment(qi->getDownloadedSegments());
		bytesDownloaded += qi->getDownloadedSegments();
	}

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
	return find_if(sources.begin(), sources.end(), [&](const UserRunningPair& urp) { return urp.first.user == aUser; }) != sources.end();
}

bool Bundle::isSource(const CID& cid) {
	return find_if(sources.begin(), sources.end(), [&](const UserRunningPair& urp) { return urp.first.user->getCID() == cid; }) != sources.end();
}

bool Bundle::isBadSource(const UserPtr& aUser) {
	return find_if(badSources.begin(), badSources.end(), [&](const UserRunningPair& urp) { return urp.first.user == aUser; }) != badSources.end();
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

	auto i = find_if(sources.begin(), sources.end(), [&](const UserRunningPair& urp) { return urp.first == aUser; });
	if (i != sources.end()) {
		i->second++;
		//LogManager::getInstance()->message("ADD, SOURCE FOR " + Util::toString(i->second) + " ITEMS");
		return false;
	} else {
		sources.push_back(make_pair(aUser, 1));
		return true;
	}
	//LogManager::getInstance()->message("ADD QI FOR BUNDLE USERQUEUE, total items for the user " + aUser->getCID().toBase32() + ": " + Util::toString(l.size()));
}

QueueItem* Bundle::getNextQI(const UserPtr& aUser, string aLastError, Priority minPrio, int64_t wantedSize, int64_t lastSpeed, bool smallSlot) {
	int p = QueueItem::LAST - 1;
	//lastError = Util::emptyString;

	do {
		auto i = userQueue[p].find(aUser);
		if(i != userQueue[p].end()) {
			dcassert(!i->second.empty());
			for(auto j = i->second.begin(); j != i->second.end(); ++j) {
				QueueItem* qi = *j;
				if (qi->hasSegment(aUser, aLastError, wantedSize, lastSpeed, smallSlot) || minPrio == PAUSED) {
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

void Bundle::getQISources(HintedUserList& l) {
	for (auto s = queueItems.begin(); s != queueItems.end(); ++s) {
		QueueItem* qi = *s;
		for (auto k = qi->getSources().begin(); k != qi->getSources().end(); ++k) {
			bool add = true;
			for (auto j = l.begin(); j != l.end(); ++j) {
				if ((*j) == (*k).getUser()) {
					add=false;
					break;
				}
			}
			if (add) {
				l.push_back((*k).getUser());
			}
		}
	}
	//LogManager::getInstance()->message("getQISources, size: " + Util::toString(l.size()));
}

void Bundle::getDirQIs(const string& aDir, QueueItemList& ql) {
	if (aDir == target) {
		ql = queueItems;
		return;
	}

	for (auto s = queueItems.begin(); s != queueItems.end(); ++s) {
		QueueItem* qi = *s;
		if (qi->getTarget().length() < aDir.length()) {
			continue;
		}

		if (qi->getTarget().substr(0, aDir.length()) == aDir) {
			ql.push_back(qi);
		}
	}
}

string Bundle::getMatchPath(const SearchResultPtr& sr) {
	string path;
	if (simpleMatching) {
		path = Util::getDir(sr->getFile(), true, false);
	} else {
		//try to find the corrent location from the path manually
		size_t pos = sr->getFile().find(getName() + "\\");
		if (pos != string::npos) {
			path = Util::getFilePath(sr->getFile().substr(0, pos+getName().length()+1));
			//LogManager::getInstance()->message("ALT RELEASE MATCH, PATH: " + path);
		}
	}
	return path;
}

QueueItemList Bundle::getRunningQIs(const UserPtr& aUser) {
	QueueItemList ret;
	auto i = runningItems.find(aUser);
	if (i != runningItems.end()) {
		return i->second;
	}
	return ret;
}

void Bundle::removeUserQueue(QueueItem* qi) {
	for(QueueItem::SourceConstIter i = qi->getSources().begin(); i != qi->getSources().end(); ++i) {
		removeUserQueue(qi, i->getUser(), false);
	}
}

bool Bundle::removeUserQueue(QueueItem* qi, const UserPtr& aUser, bool addBad) {

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
	auto m = find_if(sources.begin(), sources.end(), [&](const UserRunningPair& urp) { return urp.first.user == aUser; });
	dcassert(m != sources.end());

	if (addBad) {
		auto bsi = find_if(badSources.begin(), badSources.end(), [&](const UserRunningPair& urp) { return urp.first.user == aUser; });
		if (bsi == badSources.end()) {
			badSources.push_back(make_pair(m->first, 1));
		} else {
			bsi->second++;
		}
	}

	m->second--;
	//LogManager::getInstance()->message("REMOVE, SOURCE FOR " + Util::toString(m->second) + " ITEMS");
	if (m->second == 0) {
		sources.erase(m);   //crashed when nothing found to erase with only 1 source and removing multiple bundles.
		return true;
	}
	return false;
}

void Bundle::removeBadSource(const HintedUser& aUser) {
	auto m = find_if(badSources.begin(), badSources.end(), [&](const UserRunningPair& urp) { return urp.first == aUser; });
	dcassert(m != badSources.end());
	if (m != badSources.end()) {
		badSources.erase(m);
	}
	dcassert(m == badSources.end());
}
	
Bundle::Priority Bundle::calculateProgressPriority() const {
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

void Bundle::getBundleBalanceMaps(SourceSpeedMapB& speedMap, SourceSpeedMapB& sourceMap) {
	int64_t bundleSpeed = 0;
	double bundleSources = 0;
	for (auto s = sources.begin(); s != sources.end(); ++s) {
		UserPtr& user = (*s).first.user;
		if (user->isOnline()) {
			bundleSpeed += user->getSpeed();
			bundleSources += s->second;
		} else {
			bundleSources += s->second / 2.0;
		}
	}
	bundleSources = bundleSources / queueItems.size();
	///if (verbose) {
	//	LogManager::getInstance()->message("Sourcepoints for bundle " + bundle->getName() + " : " + Util::toString(sources));
	//}
	speedMap.insert(make_pair((double)bundleSpeed, this));
	sourceMap.insert(make_pair(bundleSources, this));
}

void Bundle::getQIBalanceMaps(SourceSpeedMapQI& speedMap, SourceSpeedMapQI& sourceMap) {
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

void Bundle::calculateBalancedPriorities(PrioList& priorities, SourceSpeedMapQI& speedMap, SourceSpeedMapQI& sourceMap, bool verbose) {
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

size_t Bundle::countOnlineUsers() const {
	size_t users = 0;
	int files = 0;
	for(auto i = sources.begin(); i != sources.end(); ++i) {
		if(i->first.user->isOnline()) {
			users++;
			files += i->second;
		}
	}
	return (queueItems.size() == 0 ? 0 : (files / queueItems.size()));
}

tstring Bundle::getBundleText() {
	double percent = (double)bytesDownloaded*100.0/(double)size;
	dcassert(percent <= 100.00);
	if (fileBundle) {
		return Text::toT(getName());
	} else {
		return Text::toT(getName()) + _T(" (") + Util::toStringW(percent) + _T("%, ") + Text::toT(AirUtil::getPrioText(priority)) + _T(", ") + Util::toStringW(sources.size()) + _T(" sources)");
	}
}

void Bundle::sendRemovePBD(const UserPtr& aUser) {
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

void Bundle::getTTHList(OutputStream& tthList) {
	string tmp2;
	for(auto i = finishedFiles.begin(); i != finishedFiles.end(); ++i) {
		tmp2.clear();
		tthList.write((*i)->getTTH().toBase32(tmp2) + " ");
	}
}

void Bundle::getSearchItems(StringPairList& searches, bool manual) {
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

/* ONLY CALLED FROM DOWNLOADMANAGER BEGIN */

void Bundle::addDownload(Download* d) {
	downloads.push_back(d);
}

void Bundle::removeDownload(Download* d) {
	auto m = find(downloads.begin(), downloads.end(), d);
	dcassert(m != downloads.end());
	if (m != downloads.end()) {
		countSpeed();
		bytesDownloaded -= (*m)->getPos();
		addDownloadedSegment((*m)->getPos());
		downloads.erase(m);
	}
}

uint64_t Bundle::countSpeed() {
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
		actual = ((int64_t)((double)bytesDownloaded * (bundleRatio == 0 ? 1.00 : bundleRatio)));
	}
	return bundleSpeed;
}

void Bundle::addUploadReport(const HintedUser& aUser) {
	if (uploadReports.empty()) {
		lastSpeed = 0;
		lastPercent = 0;
	}
	uploadReports.push_back(aUser);
}

void Bundle::removeUploadReport(const UserPtr& aUser) {
	for(auto i = uploadReports.begin(); i != uploadReports.end(); ++i) {
		if (i->user == aUser) {
			uploadReports.erase(i);
			//LogManager::getInstance()->message("ERASE UPLOAD REPORT: " + Util::toString(bundle->getUploadReports().size()));
			break;
		}
	}
}

void Bundle::sendUBN(const string& speed, double percent) {
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

bool Bundle::sendBundle(UserConnection* aSource, bool updateOnly) {
	AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

	cmd.addParam("HI", aSource->getHintedUser().hint);
	cmd.addParam("TO", aSource->getToken());
	cmd.addParam("BU", token);
	if (!updateOnly) {
		cmd.addParam("SI", Util::toString(size));
		cmd.addParam("NA", getName());
		cmd.addParam("DL", Util::toString(bytesDownloaded));
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

void Bundle::sendBundleMode() {
	for(auto i = uploadReports.begin(); i != uploadReports.end(); ++i) {
		AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

		cmd.addParam("HI", (*i).hint);
		cmd.addParam("BU", token);
		cmd.addParam("UD1");
		if (singleUser) {
			cmd.addParam("SU1");
			cmd.addParam("DL", Util::toString(downloadedSegments));
		} else {
			cmd.addParam("MU1");
		}

		ClientManager::getInstance()->send(cmd, (*i).user->getCID(), true);
	}
}

void Bundle::sendBundleFinished() {
	for(auto i = uploadReports.begin(); i != uploadReports.end(); ++i) {
		sendBundleFinished(*i);
	}
}

void Bundle::sendBundleFinished(const HintedUser& aUser) {
	AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

	cmd.addParam("HI", aUser.hint);
	cmd.addParam("BU", token);
	cmd.addParam("FI1");

	ClientManager::getInstance()->send(cmd, aUser.user->getCID(), true);
}

void Bundle::sendSizeNameUpdate() {
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

}
