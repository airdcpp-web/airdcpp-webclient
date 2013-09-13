/*
 * Copyright (C) 2011-2013 AirDC++ Project
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

#include "AirUtil.h"
#include "Bundle.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "Download.h"
#include "File.h"
#include "HashManager.h"
#include "LogManager.h"
#include "QueueItem.h"
#include "SearchResult.h"
#include "SimpleXML.h"
#include "TimerManager.h"
#include "UserConnection.h"

#include <boost/range/numeric.hpp>
#include <boost/range/algorithm/copy.hpp>

namespace dcpp {

using boost::range::find_if;
using boost::accumulate;
using boost::range::copy;
	
Bundle::Bundle(QueueItemPtr& qi, const string& aToken /*empty*/, bool aDirty /*true*/) noexcept : 
	Bundle(qi->getTarget(), qi->getAdded(), qi->getPriority(), 0, aToken, aDirty, true) {

	finishedSegments = qi->getDownloadedSegments();
	currentDownloaded = qi->getDownloadedBytes();
	setAutoPriority(qi->getAutoPriority());

	qi->setBundle(this);
	queueItems.push_back(qi);
}

Bundle::Bundle(const string& aTarget, time_t aAdded, Priority aPriority, time_t aDirDate /*0*/, const string& aToken /*Util::emptyString*/, bool aDirty /*true*/, bool isFileBundle /*false*/) noexcept :
	QueueItemBase(aTarget, 0, aPriority, aAdded), dirDate(aDirDate), fileBundle(isFileBundle), dirty(aDirty) {

	if (aToken.empty()) {
		token = ConnectionManager::getInstance()->tokens.getToken();
	} else {
		token = aToken;
	}

	setTarget(aTarget);
	auto time = GET_TIME();
	if (dirDate > 0) {
		checkRecent();
	} else {
		//make sure that it won't be set as recent but that it will use the random order
		dirDate = time - SETTING(RECENT_BUNDLE_HOURS)*60*60;
	}

	/* Randomize the downloading order for each user if the bundle dir date is newer than 7 days to boost partial bundle sharing */
	seqOrder = (dirDate + (7*24*60*60)) < time;

	if (aPriority != DEFAULT) {
		setAutoPriority(false);
	} else {
		setPriority(LOW);
		setAutoPriority(true);
	}

	if (SETTING(USE_SLOW_DISCONNECTING_DEFAULT))
		setFlag(FLAG_AUTODROP);
}

Bundle::~Bundle() noexcept {
	ConnectionManager::getInstance()->tokens.removeToken(token);
}

void Bundle::increaseSize(int64_t aSize) noexcept {
	size += aSize; 
}

void Bundle::decreaseSize(int64_t aSize) noexcept {
	size -= aSize; 
}

void Bundle::setTarget(const string& aTarget) noexcept {
	target = Util::validateFileName(aTarget);
	if (!fileBundle && target[target.length()-1] != PATH_SEPARATOR)
		target += PATH_SEPARATOR;
}

bool Bundle::checkRecent() noexcept {
	recent = (SETTING(RECENT_BUNDLE_HOURS) > 0 && (dirDate + (SETTING(RECENT_BUNDLE_HOURS)*60*60)) > GET_TIME());
	return recent;
}

bool Bundle::allowHash() const noexcept {
	return status != STATUS_HASHING && queueItems.empty() && find_if(finishedFiles, [](const QueueItemPtr& q) { 
		return !q->isSet(QueueItem::FLAG_MOVED); }) == finishedFiles.end();
}

void Bundle::setDownloadedBytes(int64_t aSize) noexcept {
	dcassert(aSize + finishedSegments <= size);
	//dcassert(((aSize + finishedSegments)) >= currentDownloaded);
	dcassert(((aSize + finishedSegments)) >= 0);
	currentDownloaded = aSize;
	dcassert(currentDownloaded <= size);
}

void Bundle::addFinishedSegment(int64_t aSize) noexcept {
#ifdef _DEBUG
	int64_t tmp1 = accumulate(queueItems, (int64_t)0, [&](int64_t old, const QueueItemPtr& qi) {
		return old + qi->getDownloadedSegments(); 
	});

	tmp1 = accumulate(finishedFiles, tmp1, [&](int64_t old, const QueueItemPtr& qi) {
		return old + qi->getDownloadedSegments(); 
	});
	dcassert(tmp1 == aSize + finishedSegments);
#endif

	dcassert(aSize + finishedSegments <= size);
	finishedSegments += aSize;
	dcassert(currentDownloaded >= 0);
	dcassert(currentDownloaded <= size);
	dcassert(finishedSegments <= size);
	setDirty();
}

void Bundle::removeDownloadedSegment(int64_t aSize) noexcept {
	dcassert(finishedSegments - aSize >= 0);
	finishedSegments -= aSize;
	dcassert(finishedSegments <= size);
	dcassert(currentDownloaded <= size);
}

void Bundle::finishBundle() noexcept {
	speed = 0;
	currentDownloaded = 0;
}

int64_t Bundle::getSecondsLeft() const noexcept {
	return (speed > 0) ? static_cast<int64_t>((size - (currentDownloaded+finishedSegments)) / speed) : 0;
}

string Bundle::getName() const noexcept  {
	if (!fileBundle) {
		return Util::getLastDir(target);
	} else {
		return Util::getFileName(target);
	}
}

void Bundle::setDirty() noexcept {
	if (status != STATUS_NEW)
		dirty = true;
}

bool Bundle::getDirty() const noexcept {
	if (status == STATUS_NEW)
		return false; //don't save bundles that are currently being added

	return dirty; 
}

QueueItemPtr Bundle::findQI(const string& aTarget) const noexcept {
	auto p = find_if(queueItems, [&aTarget](const QueueItemPtr& q) { return q->getTarget() == aTarget; });
	return p != queueItems.end() ? *p : nullptr;
}

string Bundle::getBundleFile() const noexcept {
	return Util::getPath(Util::PATH_BUNDLES) + "Bundle" + token + ".xml";
}

void Bundle::deleteBundleFile() noexcept {
	try {
		File::deleteFile(getBundleFile() + ".bak");
		File::deleteFile(getBundleFile());
	} catch(const FileException& /*e1*/) {
		//..
	}
}

void Bundle::getItems(const UserPtr& aUser, QueueItemList& ql) const noexcept {
	for(int i = QueueItemBase::PAUSED_FORCE; i < QueueItemBase::LAST; ++i) {
		auto j = userQueue[i].find(aUser);
		if(j != userQueue[i].end()) {
			copy(j->second, back_inserter(ql));
		}
	}
}

bool Bundle::addFinishedItem(QueueItemPtr& qi, bool finished) noexcept {
	finishedFiles.push_back(qi);
	if (!finished) {
		qi->setFlag(QueueItem::FLAG_MOVED);
		qi->setBundle(this);
		increaseSize(qi->getSize());
		addFinishedSegment(qi->getSize());
	}
	qi->setFlag(QueueItem::FLAG_FINISHED);

	auto& bd = bundleDirs[qi->getFilePath()];
	bd.second++;
	if (bd.first == 0 && bd.second == 1) {
		return true;
	}
	return false;
}

bool Bundle::removeFinishedItem(QueueItemPtr& qi) noexcept {
	int pos = 0;
	for (auto& fqi: finishedFiles) {
		if (fqi == qi) {
			decreaseSize(qi->getSize());
			removeDownloadedSegment(qi->getSize());
			swap(finishedFiles[pos], finishedFiles[finishedFiles.size()-1]);
			finishedFiles.pop_back();

			auto& bd = bundleDirs[qi->getFilePath()];
			bd.second--;
			if (bd.first == 0 && bd.second == 0) {
				bundleDirs.erase(Util::getFilePath(qi->getTarget()));
				return true;
			}
			return false;
		}
		pos++;
	}
	return false;
}

bool Bundle::addQueue(QueueItemPtr& qi) noexcept {
	dcassert(find(queueItems, qi) == queueItems.end());
	qi->setBundle(this);
	queueItems.push_back(qi);
	increaseSize(qi->getSize());

	auto& bd = bundleDirs[qi->getFilePath()];
	bd.first++;
	if (bd.first == 1 && bd.second == 0) {
		return true;
	}

	return false;
}

bool Bundle::removeQueue(QueueItemPtr& qi, bool finished) noexcept {
	int pos = 0;
	for (auto& cur: queueItems) {
		if (cur == qi) {
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

	auto& bd = bundleDirs[qi->getFilePath()];
	bd.first--;
	if (bd.first == 0 && bd.second == 0) {
		bundleDirs.erase(qi->getFilePath());
		return true;
	}
	return false;
}

bool Bundle::isSource(const UserPtr& aUser) const noexcept {
	return find(sources, aUser) != sources.end();
}

bool Bundle::isBadSource(const UserPtr& aUser) const noexcept  {
	return find(badSources, aUser) != badSources.end();
}

void Bundle::addUserQueue(QueueItemPtr& qi) noexcept {
	for(auto& s: qi->getSources())
		addUserQueue(qi, s.getUser());
}

bool Bundle::addUserQueue(QueueItemPtr& qi, const HintedUser& aUser, bool isBad /*false*/) noexcept {
	auto& l = userQueue[qi->getPriority()][aUser.user];
	dcassert(find(l, qi) == l.end());

	if (l.size() > 1) {
		if (!seqOrder) {
			/* Randomize the downloading order for each user if the bundle dir date is newer than 7 days to boost partial bundle sharing */
			l.push_back(qi);
			swap(l[Util::rand((uint32_t)l.size())], l[l.size()-1]);
		} else {
			/* Sequential order */
			l.insert(upper_bound(l.begin(), l.end(), qi, QueueItem::AlphaSortOrder()), qi);
		}
	} else {
		l.push_back(qi);
	}

	if (isBad) {
		auto i = find(badSources, aUser);
		dcassert(i != badSources.end());
		if (i != badSources.end()) {
			(*i).files--;
			(*i).size  -= qi->getSize();

			if ((*i).files == 0) {
				badSources.erase(i);
			}
		}
	}

	auto i = find(sources, aUser);
	if (i != sources.end()) {
		(*i).files++;
		(*i).size += qi->getSize();
		return false;
	} else {
		sources.emplace_back(aUser, qi->getSize() - qi->getDownloadedSegments());
		return true;
	}
}

QueueItemPtr Bundle::getNextQI(const UserPtr& aUser, const OrderedStringSet& onlineHubs, string& aLastError, Priority minPrio, int64_t wantedSize, int64_t lastSpeed, QueueItemBase::DownloadType aType, bool allowOverlap) noexcept {
	int p = QueueItem::LAST - 1;
	do {
		auto i = userQueue[p].find(aUser);
		if(i != userQueue[p].end()) {
			dcassert(!i->second.empty());
			for(auto& qi: i->second) {
				if (qi->hasSegment(aUser, onlineHubs, aLastError, wantedSize, lastSpeed, aType, allowOverlap)) {
					return qi;
				}
			}
		}
		p--;
	} while(p >= minPrio);

	return nullptr;
}

bool Bundle::isFinishedNotified(const UserPtr& aUser) const noexcept {
	return find_if(finishedNotifications, [&aUser](const UserBundlePair& ubp) { return ubp.first.user == aUser; }) != finishedNotifications.end();
}

void Bundle::addFinishedNotify(HintedUser& aUser, const string& remoteBundle) noexcept {
	if (!isFinishedNotified(aUser.user) && !isBadSource(aUser)) {
		finishedNotifications.emplace_back(aUser, remoteBundle);
	}
}

void Bundle::removeFinishedNotify(const UserPtr& aUser) noexcept {
	auto p = find_if(finishedNotifications, [&aUser](const UserBundlePair& ubp) { return ubp.first.user == aUser; });
	if (p != finishedNotifications.end()) {
		finishedNotifications.erase(p);
	}
}

void Bundle::getSources(HintedUserList& l) const noexcept {
	for(auto& st: sources) 
		l.push_back(st.user);
}

void Bundle::getDirQIs(const string& aDir, QueueItemList& ql) const noexcept {
	if (aDir == target) {
		ql = queueItems;
		return;
	}

	for(auto& q: queueItems) {
		if (AirUtil::isSub(q->getTarget(), aDir)) {
			ql.push_back(q);
		}
	}
}

bool Bundle::isFailed() const noexcept {
	return status == STATUS_SHARING_FAILED || status == STATUS_FAILED_MISSING || status == STATUS_HASH_FAILED;
}

string Bundle::getMatchPath(const string& aRemoteFile, const string& aLocalFile, bool nmdc) const noexcept {
	/* returns the local path for nmdc and the remote path for adc */
	string remoteDir = Util::getNmdcFilePath(aRemoteFile);
	string bundleDir = Util::getFilePath(aLocalFile);
#ifndef _WIN32
	bundleDir = Util::toNmdcFile(bundleDir);
#endif

	string path;
	if (simpleMatching) {
		if (nmdc) {
			if (Text::toLower(remoteDir).find(Text::toLower(getName())) != string::npos)
				path = target;
		} else {
			path = Util::getReleaseDir(remoteDir, false);
		}
	} else {
		/* try to find the bundle name from the path */
		size_t pos = Text::toLower(remoteDir).find(Text::toLower(getName()) + "\\");
		if (pos != string::npos) {
			path = nmdc ? target : remoteDir.substr(0, pos+getName().length()+1);
		}
	}
		
	if (path.empty() && remoteDir.length() > 3) {
		/* failed, cut the common dirs from the end of the remote path */
		string::size_type i = remoteDir.length()-2;
		string::size_type j;
		for(;;) {
			j = remoteDir.find_last_of("\\", i);
			if(j == string::npos || (int)(bundleDir.length() - (remoteDir.length() - j)) < 0) //also check if it goes out of scope for the local dir
				break;
			if(Util::stricmp(remoteDir.substr(j), bundleDir.substr(bundleDir.length() - (remoteDir.length()-j))) != 0)
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

pair<uint32_t, uint32_t> Bundle::getPathInfo(const string& aDir) const noexcept {
	auto p = bundleDirs.find(aDir);
	if (p != bundleDirs.end()) {
		return p->second;
	}
	return { 0, 0 };
}

void Bundle::rotateUserQueue(QueueItemPtr& qi, const UserPtr& aUser) noexcept {
	dcassert(qi->isSource(aUser));
	auto& ulm = userQueue[qi->getPriority()];
	auto j = ulm.find(aUser);
	dcassert(j != ulm.end());
	if (j == ulm.end()) {
		return;
	}
	auto& l = j->second;
	if (l.size() > 1) {
		auto s = find(l, qi);
		if (s != l.end()) {
			l.erase(s);
			l.push_back(qi);
		}
	}
}

void Bundle::removeUserQueue(QueueItemPtr& qi) noexcept {
	for(auto& s: qi->getSources())
		removeUserQueue(qi, s.getUser(), false);
}

bool Bundle::removeUserQueue(QueueItemPtr& qi, const UserPtr& aUser, bool addBad) noexcept {

	//remove from UserQueue
	dcassert(qi->isSource(aUser));
	auto& ulm = userQueue[qi->getPriority()];
	auto j = ulm.find(aUser);
	dcassert(j != ulm.end());
	if (j == ulm.end()) {
		return false;
	}
	auto& l = j->second;
	auto s = find(l, qi);
	if (s != l.end()) {
		l.erase(s);
	}

	if(l.empty()) {
		ulm.erase(j);
	}

	//remove from bundle sources
	auto m = find(sources, aUser);
	dcassert(m != sources.end());

	if (addBad) {
		auto bsi = find(badSources, aUser);
		if (bsi == badSources.end()) {
			badSources.emplace_back((*m).user, qi->getSize());
		} else {
			(*bsi).files++;
			(*bsi).size += qi->getSize();
		}
	}

	(*m).files--;
	(*m).size  -= qi->getSize();

	if ((*m).files == 0) {
		sources.erase(m);
		return true;
	}
	return false;
}
	
QueueItemBase::Priority Bundle::calculateProgressPriority() const noexcept {
	if(getAutoPriority()) {
		QueueItemBase::Priority p;
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
				p = Bundle::NORMAL;
				break;
			case 6:
			case 7:
			default:
				p = Bundle::HIGH;
				break;
		}
		return p;			
	}
	return getPriority();
}

pair<int64_t, double> Bundle::getPrioInfo() noexcept {
	int64_t bundleSpeed = 0;
	double bundleSources = 0;
	for (const auto& s: sources) {
		if (s.user.user->isOnline()) {
			bundleSpeed += s.user.user->getSpeed();
		}

		bundleSources += s.files;
	}

	bundleSources = bundleSources / queueItems.size();
	return { bundleSpeed, bundleSources };
}

multimap<QueueItemPtr, pair<int64_t, double>> Bundle::getQIBalanceMaps() noexcept {
	multimap<QueueItemPtr, pair<int64_t, double>> speedSourceMap;

	for (auto& q: queueItems) {
		if(q->getAutoPriority()) {
			int64_t qiSpeed = 0;
			double qiSources = 0;
			for (const auto& s: q->getSources()) {
				if (s.getUser().user->isOnline()) {
					qiSpeed += s.getUser().user->getSpeed();
					qiSources++;
				} else {
					qiSources += 2;
				}
			}
			speedSourceMap.insert(make_pair(q, make_pair(qiSpeed, qiSources)));
		}
	}
	return speedSourceMap;
}

int Bundle::countOnlineUsers() const noexcept {
	int files=0, users=0;
	for(const auto& s: sources) {
		if(s.user.user->isOnline()) {
			users++;
			files += s.files;
		}
	}
	return (queueItems.size() == 0 ? 0 : (files / queueItems.size()));
}

string Bundle::getBundleText() noexcept {
	double percent = size <= 0 ? 0.00 : (double)((currentDownloaded+finishedSegments)*100.0)/(double)size;
	if (fileBundle) {
		return getName();
	} else {
		return getName() + " (" + Util::toString(percent) + "%, " + AirUtil::getPrioText(getPriority()) + ", " + Util::toString(sources.size()) + " sources)";
	}
}

void Bundle::clearFinishedNotifications(FinishedNotifyList& fnl) noexcept {
	finishedNotifications.swap(fnl);
}

bool Bundle::allowAutoSearch() const noexcept {
	if (isSet(FLAG_SCHEDULE_SEARCH))
		return false; // handle this via bundle updates

	if (countOnlineUsers() >= SETTING(AUTO_SEARCH_LIMIT))
		return false; // can't exceed the user limit

	if (find_if(queueItems, [](const QueueItemPtr& q) { return !q->isPausedPrio(); } ) == queueItems.end())
		return false; // must have valid queue items

	if (getSecondsLeft() < 20 && getSecondsLeft() != 0)
		return false; // items running and it will be finished soon already

	return true;
}

void Bundle::getSearchItems(map<string, QueueItemPtr>& searches, bool manual) const noexcept {
	if (fileBundle || queueItems.size() == 1) {
		searches.insert(make_pair(Util::emptyString, queueItems.front()));
		return;
	}

	QueueItemPtr searchItem = nullptr;
	for (auto& i: bundleDirs | map_keys) {
		string dir = Util::getReleaseDir(i, false);
		//don't add the same directory twice
		if (searches.find(dir) != searches.end()) {
			continue;
		}

		QueueItemList ql;
		getDirQIs(dir, ql);

		if (ql.empty()) {
			continue;
		}

		size_t s = 0;
		searchItem = nullptr;

		//do a few guesses to get a random item
		while (s <= ql.size()) {
			QueueItemPtr q = ql[ql.size() == 1 ? 0 : Util::rand(ql.size()-1)];
			if(q->isPausedPrio() && !manual) {
				s++;
				continue;
			}
			if(q->isRunning() || (q->isPausedPrio())) {
				//it's ok but see if we can find better one
				searchItem = q;
			} else {
				searchItem = q;
				break;
			}
			s++;
		}

		if (searchItem) {
			searches.insert(make_pair(dir, searchItem));
		}
	}
}

void Bundle::updateSearchMode() noexcept {
	StringList searches;
	for (auto& i: bundleDirs | map_keys) {
		string dir = Util::getReleaseDir(i, false);
		if (find(searches, dir) == searches.end()) {
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
	auto m = find(downloads, d);
	dcassert(m != downloads.end());
	if (m != downloads.end()) {
		downloads.erase(m);
	}
}

bool Bundle::onDownloadTick(vector<pair<CID, AdcCommand>>& UBNList) noexcept {
	int64_t bundleSpeed = 0, bundleRatio = 0, bundlePos = 0;
	int down = 0;
	for (auto d: downloads) {
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

		if (!singleUser && !uploadReports.empty()) {

			string speedStr;
			double percent = 0;

			if (abs(speed-lastSpeed) > (lastSpeed / 10)) {
				//LogManager::getInstance()->message("SEND SPEED: " + Util::toString(abs(speed-lastSpeed)) + " is more than " + Util::toString(lastSpeed / 10));
				auto formatSpeed = [this] () -> string {
					char buf[64];
					if(speed < 1024) {
						snprintf(buf, sizeof(buf), "%d%s", (int)(speed&0xffffffff), "b");
					} else if(speed < 1048576) {
						snprintf(buf, sizeof(buf), "%.02f%s", (double)speed/(1024.0), "k");
					} else {
						snprintf(buf, sizeof(buf), "%.02f%s", (double)speed/(1048576.0), "m");
					}
					return buf;
				};

				speedStr = formatSpeed();
				lastSpeed = speed;
			} else {
				//LogManager::getInstance()->message("DON'T SEND SPEED: " + Util::toString(abs(speed-lastSpeed)) + " is less than " + Util::toString(lastSpeed / 10));
			}

			if (abs(lastDownloaded-getDownloadedBytes()) > (size / 200)) {
				//LogManager::getInstance()->message("SEND PERCENT: " + Util::toString(abs(lastDownloaded-getDownloadedBytes())) + " is more than " + Util::toString(size / 200));
				percent = (static_cast<float>(getDownloadedBytes()) / static_cast<float>(size)) * 100.;
				dcassert(percent <= 100.00);
				lastDownloaded = getDownloadedBytes();
			} else {
				//LogManager::getInstance()->message("DON'T SEND PERCENT: " + Util::toString(abs(lastDownloaded-getDownloadedBytes())) + " is less than " + Util::toString(size / 200));
			}

			if (!speedStr.empty() || percent > 0) {
				for(auto& i: uploadReports) {
					AdcCommand cmd(AdcCommand::CMD_UBN, AdcCommand::TYPE_UDP);

					cmd.addParam("HI", i.hint);
					cmd.addParam("BU", token);
					if (!speedStr.empty())
						cmd.addParam("DS", speedStr);
					if (percent > 0)
						cmd.addParam("PE", Util::toString(percent));

					UBNList.emplace_back(i.user->getCID(), cmd);
				}
			}
		}
		return true;
	}
	return false;
}

bool Bundle::addRunningUser(const UserConnection* aSource) noexcept {
	bool updateOnly = false;
	auto y = runningUsers.find(aSource->getUser());
	if (y == runningUsers.end()) {
		if (runningUsers.size() == 1) {
			setBundleMode(false);
		}
		runningUsers[aSource->getUser()]++;
	} else {
		y->second++;
		updateOnly = true;
	}

	if (aSource->isSet(UserConnection::FLAG_UBN1)) {
		//tell the uploader to connect this token to a correct bundle
		AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

		cmd.addParam("HI", aSource->getHintedUser().hint);
		cmd.addParam("TO", aSource->getToken());
		cmd.addParam("BU", token);
		if (!updateOnly) {
			cmd.addParam("SI", Util::toString(size));
			cmd.addParam("NA", getName());
			cmd.addParam("DL", Util::toString(currentDownloaded+finishedSegments));
			cmd.addParam(singleUser ? "SU1" : "MU1");
			cmd.addParam("AD1");
		} else {
			cmd.addParam("CH1");
		}

		if (ClientManager::getInstance()->sendUDP(cmd, aSource->getUser()->getCID(), true, true) && !updateOnly) {
			//add a new upload report
			if (!uploadReports.empty()) {
				lastSpeed = 0;
				lastDownloaded = 0;
			}
			uploadReports.push_back(aSource->getHintedUser());
		}
	}

	return runningUsers.size() == 1;
}

void Bundle::setBundleMode(bool setSingleUser) noexcept {
	if (setSingleUser) {
		lastSpeed = 0;
		lastDownloaded= 0;
		singleUser= true;
	} else {
		singleUser = false;
	}

	if (!uploadReports.empty()) {
		HintedUser& u = uploadReports.front();
		dcassert(u.user);

		AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

		cmd.addParam("HI", u.hint);
		cmd.addParam("BU", token);
		cmd.addParam("UD1");
		if (singleUser) {
			cmd.addParam("SU1");
			cmd.addParam("DL", Util::toString(finishedSegments));
		} else {
			cmd.addParam("MU1");
		}

		ClientManager::getInstance()->sendUDP(cmd, u.user->getCID(), true, true);
	}
}

bool Bundle::removeRunningUser(const UserConnection* aSource, bool sendRemove) noexcept {
	bool finished = false;
	auto y =  runningUsers.find(aSource->getUser());
	dcassert(y != runningUsers.end());
	if (y != runningUsers.end()) {
		y->second--;
		if (y->second == 0) {
			runningUsers.erase(aSource->getUser());
			if (runningUsers.size() == 1) {
				setBundleMode(true);
			}
			finished = true;
		}

		if (aSource->isSet(UserConnection::FLAG_UBN1) && (finished || sendRemove)) {
			AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);

			cmd.addParam("HI", aSource->getHintedUser().hint);
			if (finished) {
				cmd.addParam("BU", token);
				cmd.addParam("FI1");
			} else {
				cmd.addParam("TO", aSource->getToken());
				cmd.addParam("RM1");
			}

			ClientManager::getInstance()->sendUDP(cmd, aSource->getUser()->getCID(), true, true);

			if (finished)
				uploadReports.erase(remove(uploadReports.begin(), uploadReports.end(), aSource->getUser()), uploadReports.end());
		}
	}

	if (runningUsers.empty()) {
		speed = 0;
		currentDownloaded = 0;
		return true;
	}
	return false;
}

void Bundle::sendSizeNameUpdate() noexcept {
	for(auto& u: uploadReports) {
		AdcCommand cmd(AdcCommand::CMD_UBD, AdcCommand::TYPE_UDP);
		cmd.addParam("HI", u.hint);
		cmd.addParam("BU", token);

		if (isSet(FLAG_UPDATE_SIZE)) {
			unsetFlag(FLAG_UPDATE_SIZE);
			cmd.addParam("SI", Util::toString(size));
		}

		if (isSet(FLAG_UPDATE_NAME)) {
			unsetFlag(FLAG_UPDATE_NAME);
			cmd.addParam("NA", getName());
		}

		cmd.addParam("UD1");

		ClientManager::getInstance()->sendUDP(cmd, u.user->getCID(), true, true);
	}
}

/* ONLY CALLED FROM DOWNLOADMANAGER END */


void Bundle::save() noexcept {
	File ff(getBundleFile() + ".tmp", File::WRITE, File::CREATE | File::TRUNCATE);
	BufferedOutputStream<false> f(&ff);
	f.write(SimpleXML::utf8Header);
	string tmp;
	string b32tmp;

	if (isFileBundle()) {
		f.write(LIT("<File Version=\"" FILE_BUNDLE_VERSION));
		f.write(LIT("\" Token=\""));
		f.write(token);
		f.write(LIT("\">\r\n"));
		queueItems.front()->save(f, tmp, b32tmp);
		f.write(LIT("</File>\r\n"));
	} else {
		f.write(LIT("<Bundle Version=\"" DIR_BUNDLE_VERSION));
		f.write(LIT("\" Target=\""));
		f.write(SimpleXML::escape(target, tmp, true));
		f.write(LIT("\" Token=\""));
		f.write(token);
		f.write(LIT("\" Added=\""));
		f.write(Util::toString(getAdded()));
		f.write(LIT("\" Date=\""));
		f.write(Util::toString(dirDate));
		if (!getAutoPriority()) {
			f.write(LIT("\" Priority=\""));
			f.write(Util::toString((int)getPriority()));
		}
		f.write(LIT("\">\r\n"));

		for (const auto& qi: finishedFiles) {
			f.write(LIT("\t<Finished TTH=\""));
			f.write(qi->getTTH().toBase32());
			f.write(LIT("\" Target=\""));
			f.write(SimpleXML::escape(qi->getTarget(), tmp, true));
			f.write(LIT("\" Size=\""));
			f.write(Util::toString(qi->getSize()));
			f.write(LIT("\" Added=\""));
			f.write(Util::toString(qi->getAdded()));
			f.write(LIT("\"/>\r\n"));
		}

		for (const auto& q: queueItems) {
			q->save(f, tmp, b32tmp);
		}

		f.write(LIT("</Bundle>\r\n"));
	}

	f.flush();
	ff.close();
	try {
		File::deleteFile(getBundleFile());
		File::renameFile(getBundleFile() + ".tmp", getBundleFile());
	} catch(...) { }
	
	dirty = false;
}

}
