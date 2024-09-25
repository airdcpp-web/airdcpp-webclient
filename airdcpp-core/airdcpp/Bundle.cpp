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
#include <airdcpp/Bundle.h>

#include <airdcpp/ActionHook.h>
#include <airdcpp/ClientManager.h>
#include <airdcpp/ConnectionManager.h>
#include <airdcpp/Download.h>
#include <airdcpp/File.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/PathUtil.h>
#include <airdcpp/QueueItem.h>
#include <airdcpp/SimpleXML.h>
#include <airdcpp/Streams.h>
#include <airdcpp/TimerManager.h>
#include <airdcpp/UserConnection.h>
#include <airdcpp/ValueGenerator.h>

#include <boost/range/numeric.hpp>

namespace dcpp {

using boost::accumulate;
	
Bundle::Bundle(const QueueItemPtr& qi, time_t aFileDate, QueueToken aToken /*0*/, bool aDirty /*true*/) noexcept :
	Bundle(qi->getTarget(), qi->getTimeAdded(), qi->getPriority(), aFileDate, aToken, aDirty, true) {

	if (!qi->isDownloaded()) {
		addFinishedSegment(qi->getDownloadedSegments());
		setDownloadedBytes(qi->getDownloadedBytes());
		setAutoPriority(qi->getAutoPriority());
	}
}

Bundle::Bundle(const string& aTarget, time_t aAdded, Priority aPriority, time_t aBundleDate /*0*/, QueueToken aToken /*0*/, bool aDirty /*true*/, bool aIsFileBundle /*false*/) noexcept :
	QueueItemBase(PathUtil::validatePath(aTarget, !aIsFileBundle), 0, aPriority, aAdded, aToken, 0), bundleDate(aBundleDate), fileBundle(aIsFileBundle), dirty(aDirty) {

	if (aToken == 0) {
		token = Util::toUInt32(ConnectionManager::getInstance()->tokens.createToken(CONNECTION_TYPE_DOWNLOAD));
	}

	auto time = GET_TIME();
	if (bundleDate > 0) {
		checkRecent();
	} else {
		//make sure that it won't be set as recent but that it will use the random order
		bundleDate = time - SETTING(RECENT_BUNDLE_HOURS) * 60 * 60;
	}

	/* Randomize the downloading order for each user if the bundle dir date is newer than 5 days to boost partial bundle sharing */
	seqOrder = (bundleDate + (5 * 24 * 60 * 60)) < time;

	if (aPriority != Priority::DEFAULT) {
		setAutoPriority(false);
	} else {
		setPriority(Priority::LOW);
		setAutoPriority(true);
	}

	if (SETTING(USE_SLOW_DISCONNECTING_DEFAULT))
		setFlag(FLAG_AUTODROP);
}

Bundle::~Bundle() noexcept {
	ConnectionManager::getInstance()->tokens.removeToken(getStringToken());
	dcdebug("Bundle %s was removed\n", getName().c_str());
}

string Bundle::getStatusString() const noexcept {
	switch (getStatus()) {
		case Bundle::STATUS_NEW:
		case Bundle::STATUS_QUEUED: {
			auto percentage = getPercentage(getDownloadedBytes());
			if (isPausedPrio()) {
				return STRING_F(PAUSED_PCT, percentage);
			}

			if (getSpeed() > 0) {
				return STRING_F(RUNNING_PCT, percentage);
			} else {
				return STRING_F(WAITING_PCT, percentage);
			}
		}
		case Bundle::STATUS_RECHECK: return STRING(RECHECKING);
		case Bundle::STATUS_DOWNLOADED: return STRING(DOWNLOADED);
		case Bundle::STATUS_VALIDATION_RUNNING: return STRING(VALIDATING_CONTENT);
		case Bundle::STATUS_DOWNLOAD_ERROR:
		case Bundle::STATUS_VALIDATION_ERROR: return getError();
		case Bundle::STATUS_COMPLETED: return STRING(FINISHED);
		case Bundle::STATUS_SHARED: return STRING(SHARED);
		default: return Util::emptyString;
	}
}

void Bundle::increaseSize(int64_t aSize) noexcept {
	size += aSize; 
}

void Bundle::decreaseSize(int64_t aSize) noexcept {
	size -= aSize; 
}

bool Bundle::checkRecent() noexcept {
	recent = (SETTING(RECENT_BUNDLE_HOURS) > 0 && (bundleDate + (SETTING(RECENT_BUNDLE_HOURS) * 60 * 60)) > GET_TIME());
	return recent;
}

bool Bundle::filesCompleted() const noexcept {
	return queueItems.empty() && ranges::find_if(finishedFiles, [](const QueueItemPtr& q) { 
		return !q->isCompleted(); }) == finishedFiles.end();
}

bool Bundle::isDownloaded() const noexcept { 
	return status >= STATUS_DOWNLOADED; 
}

bool Bundle::isCompleted() const noexcept {
	return status >= STATUS_COMPLETED;
}

void Bundle::setHookError(const ActionHookRejectionPtr& aError) noexcept {
	error = ActionHookRejection::formatError(aError);
	hookError = aError;
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

void Bundle::removeFinishedSegment(int64_t aSize) noexcept{
	dcassert(finishedSegments - aSize >= 0);
	finishedSegments -= aSize;
	dcassert(finishedSegments <= size);
	dcassert(currentDownloaded <= size);
}

void Bundle::finishBundle() noexcept {
	speed = 0;
	currentDownloaded = 0;
	timeFinished = GET_TIME();
}

int64_t Bundle::getDownloadedBytes() const noexcept {
	return currentDownloaded + finishedSegments; 
}

int64_t Bundle::getSecondsLeft() const noexcept {
	return (speed > 0) ? (size - (currentDownloaded+finishedSegments)) / speed : 0;
}

string Bundle::getName() const noexcept  {
	if (!fileBundle) {
		return PathUtil::getLastDir(target);
	} else {
		return PathUtil::getFileName(target);
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
	auto p = ranges::find_if(queueItems, [&aTarget](const QueueItemPtr& q) { return q->getTarget() == aTarget; });
	return p != queueItems.end() ? *p : nullptr;
}

string Bundle::getXmlFilePath() const noexcept {
	return AppUtil::getPath(AppUtil::PATH_BUNDLES) + "Bundle" + getStringToken() + ".xml";
}

void Bundle::deleteXmlFile() noexcept {
	try {
		File::deleteFile(getXmlFilePath() + ".bak");
		File::deleteFile(getXmlFilePath());
	} catch(const FileException& /*e1*/) {
		//..
	}
}

void Bundle::getItems(const UserPtr& aUser, QueueItemList& ql) const noexcept {
	for (auto i = static_cast<int>(Priority::PAUSED_FORCE); i < static_cast<int>(Priority::LAST); ++i) {
		auto j = userQueue[i].find(aUser);
		if(j != userQueue[i].end()) {
			ranges::copy(j->second, back_inserter(ql));
		}
	}
}

QueueItemList Bundle::getFailedItems() const noexcept {
	QueueItemList ret;
	copy_if(finishedFiles.begin(), finishedFiles.end(), back_inserter(ret), [this](const QueueItemPtr& q) { return q->getStatus() == QueueItem::STATUS_VALIDATION_ERROR; });
	return ret;
}

void Bundle::addFinishedItem(const QueueItemPtr& qi, bool aFinished) noexcept {
	dcassert(qi->isDownloaded() && qi->getTimeFinished() > 0);

	finishedFiles.push_back(qi);
	if (!aFinished) {
		increaseSize(qi->getSize());
		addFinishedSegment(qi->getSize());
	}
}

void Bundle::removeFinishedItem(const QueueItemPtr& aQI) noexcept {
	auto f = ranges::find(finishedFiles, aQI);
	if (f != finishedFiles.end()) {
		decreaseSize(aQI->getSize());
		removeFinishedSegment(aQI->getDownloadedSegments());
		iter_swap(f, finishedFiles.end()-1);
		finishedFiles.pop_back();
		return;
	}

	dcassert(0);
}

void Bundle::addQueue(const QueueItemPtr& qi) noexcept {
	if (qi->isDownloaded()) {
		addFinishedItem(qi, false);
		return;
	}

	dcassert(qi->getTimeFinished() == 0);
	dcassert(!qi->isCompleted() && !qi->segmentsDone());
	dcassert(ranges::find(queueItems, qi) == queueItems.end());

	queueItems.push_back(qi);
	increaseSize(qi->getSize());
	addFinishedSegment(qi->getDownloadedSegments());
}

void Bundle::removeQueue(const QueueItemPtr& aQI, bool aFileCompleted) noexcept {
	if (!aFileCompleted && aQI->isDownloaded()) {
		removeFinishedItem(aQI);
		return;
	}

	if (auto f = ranges::find(queueItems, aQI); f != queueItems.end()) {
		iter_swap(f, queueItems.end() - 1);
		queueItems.pop_back();
	} else {
		dcassert(0);
	}

	if (!aFileCompleted) {
		if (aQI->getDownloadedSegments() > 0) {
			removeFinishedSegment(aQI->getDownloadedSegments());
		}

		decreaseSize(aQI->getSize());
		setFlag(Bundle::FLAG_UPDATE_SIZE);
	} else {
		addFinishedItem(aQI, true);
	}
}

bool Bundle::isSource(const UserPtr& aUser) const noexcept {
	return ranges::find(sources, aUser, &BundleSource::getUser) != sources.end();
}

bool Bundle::isBadSource(const UserPtr& aUser) const noexcept  {
	return ranges::find(badSources, aUser, &BundleSource::getUser) != badSources.end();
}

void Bundle::addUserQueue(const QueueItemPtr& qi) noexcept {
	for(const auto& s: qi->getSources())
		addUserQueue(qi, s.getUser());
}

bool Bundle::addUserQueue(const QueueItemPtr& qi, const HintedUser& aUser, bool isBad /*false*/) noexcept {
	auto& l = userQueue[static_cast<int>(qi->getPriority())][aUser.user];
	dcassert(ranges::find(l, qi) == l.end());

	if (l.size() >= 1) {
		if (!seqOrder) {
			// Randomize the downloading order for each user if the bundle dir date is newer than 7 days to boost partial bundle sharing
			auto position = ValueGenerator::rand(0, static_cast<uint32_t>(l.size()));
			l.insert(l.begin() + position, qi);
		} else {
			// Sequential order
			l.insert(upper_bound(l.begin(), l.end(), qi, QueueItem::AlphaSortOrder()), qi);
		}
	} else {
		l.push_back(qi);
	}

	if (isBad) {
		auto i = ranges::find(badSources, aUser, &BundleSource::getUser);
		dcassert(i != badSources.end());
		if (i != badSources.end()) {
			(*i).files--;
			(*i).size  -= qi->getSize();

			if ((*i).files == 0) {
				badSources.erase(i);
			}
		}
	}

	auto i = ranges::find(sources, aUser, &BundleSource::getUser);
	if (i != sources.end()) {
		(*i).files++;
		(*i).size += qi->getSize();
		return false;
	} else {
		sources.emplace_back(aUser, qi->getSize() - qi->getDownloadedSegments());
		return true;
	}
}

QueueItemPtr Bundle::getNextQI(const QueueDownloadQuery& aQuery, string& lastError_, bool aAllowOverlap) noexcept {
	int p = static_cast<int>(Priority::LAST) - 1;
	do {
		if(auto i = userQueue[p].find(aQuery.user); i != userQueue[p].end()) {
			dcassert(!i->second.empty());
			for (const auto& qi: i->second) {
				if (qi->hasSegment(aQuery, lastError_, aAllowOverlap)) {
					return qi;
				}
			}
		}
		p--;
	} while(p >= static_cast<int>(aQuery.minPrio));

	return nullptr;
}

void Bundle::getSourceUsers(HintedUserList& l) const noexcept {
	for (const auto& st : sources) {
		l.push_back(st.getUser());
	}
}

void Bundle::getDirQIs(const string& aDir, QueueItemList& ql) const noexcept {
	if (aDir == target) {
		ql = queueItems;
		return;
	}

	for (const auto& q: queueItems) {
		if (PathUtil::isSubLocal(q->getTarget(), aDir)) {
			ql.push_back(q);
		}
	}
}

bool Bundle::isFailedStatus(Status aStatus) noexcept {
	return aStatus == STATUS_VALIDATION_ERROR || aStatus == STATUS_DOWNLOAD_ERROR;
}

bool Bundle::isFailed() const noexcept {
	return isFailedStatus(status);
}

void Bundle::rotateUserQueue(const QueueItemPtr& qi, const UserPtr& aUser) noexcept {
	dcassert(qi->isSource(aUser));
	auto& ulm = userQueue[static_cast<int>(qi->getPriority())];
	auto j = ulm.find(aUser);
	dcassert(j != ulm.end());
	if (j == ulm.end()) {
		return;
	}
	auto& l = j->second;
	if (l.size() > 1) {
		auto s = ranges::find(l, qi);
		if (s != l.end()) {
			l.erase(s);
			l.push_back(qi);
		}
	}
}

void Bundle::removeUserQueue(const QueueItemPtr& qi) noexcept {
	for(auto& s: qi->getSources())
		removeUserQueue(qi, s.getUser(), 0);
}

bool Bundle::removeUserQueue(const QueueItemPtr& qi, const UserPtr& aUser, Flags::MaskType reason) noexcept{

	//remove from UserQueue
	dcassert(qi->isSource(aUser));
	auto& ulm = userQueue[static_cast<int>(qi->getPriority())];
	auto j = ulm.find(aUser);
	dcassert(j != ulm.end());
	if (j == ulm.end()) {
		return false;
	}
	auto& l = j->second;
	if (auto s = ranges::find(l, qi); s != l.end()) {
		l.erase(s);
	}

	if(l.empty()) {
		ulm.erase(j);
	}

	//remove from bundle sources
	auto m = ranges::find(sources, aUser, &BundleSource::getUser);
	dcassert(m != sources.end());

	if (reason > 0) {
		auto bsi = ranges::find(badSources, aUser, &BundleSource::getUser);
		if (bsi == badSources.end()) {
			badSources.emplace_back((*m).getUser(), qi->getSize(), reason);
		} else {
			(*bsi).files++;
			(*bsi).size += qi->getSize();
			(*bsi).setFlag(reason);
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
	
Priority Bundle::calculateProgressPriority() const noexcept {
	if(getAutoPriority()) {
		Priority p;
		auto percent = static_cast<int>(getDownloadedBytes() * 10.0 / size);
		switch(percent){
			case 0:
			case 1:
			case 2:
				p = Priority::LOW;
				break;
			case 3:
			case 4:
			case 5:	
				p = Priority::NORMAL;
				break;
			case 6:
			case 7:
			default:
				p = Priority::HIGH;
				break;
		}
		return p;			
	}
	return getPriority();
}

pair<int64_t, double> Bundle::getPrioInfo() const noexcept {
	int64_t bundleSpeed = 0;
	double bundleSources = 0;
	for (const auto& s: sources) {
		if (s.getUser().user->isOnline()) {
			bundleSpeed += s.getUser().user->getSpeed();
		}

		bundleSources += s.files;
	}

	bundleSources = bundleSources / queueItems.size();
	return { bundleSpeed, bundleSources };
}

multimap<QueueItemPtr, pair<int64_t, double>> Bundle::getQIBalanceMaps() const noexcept {
	multimap<QueueItemPtr, pair<int64_t, double>> speedSourceMap;

	for (const auto& q: queueItems) {
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
			speedSourceMap.emplace(q, make_pair(qiSpeed, qiSources));
		}
	}
	return speedSourceMap;
}

int Bundle::countOnlineUsers() const noexcept {
	int files=0, users=0;
	for(const auto& s: sources) {
		if(s.getUser().user->isOnline()) {
			users++;
			files += s.files;
		}
	}
	return (queueItems.size() == 0 ? 0 : (files / queueItems.size()));
}

bool Bundle::allowAutoSearch() const noexcept {
	if (isSet(FLAG_SCHEDULE_SEARCH))
		return false; // handle this via bundle updates

	if (countOnlineUsers() >= SETTING(AUTO_SEARCH_LIMIT))
		return false; // can't exceed the user limit

	if (ranges::find_if(queueItems, [](const QueueItemPtr& q) { return !q->isPausedPrio(); } ) == queueItems.end())
		return false; // must have valid queue items

	if (getSecondsLeft() < 20 && getSecondsLeft() != 0)
		return false; // items running and it will be finished soon already

	return true;
}

/* ONLY CALLED FROM DOWNLOADMANAGER BEGIN */

void Bundle::addDownload(Download* d) noexcept {
	if (start == 0) {
		start = GET_TICK();
	}

	downloads.push_back(d);
}

void Bundle::removeDownload(Download* d) noexcept {
	auto m = ranges::find(downloads, d);
	dcassert(m != downloads.end());
	if (m != downloads.end()) {
		downloads.erase(m);
	}

	if (downloads.empty()) {
		speed = 0;
		currentDownloaded = 0;
	}
}

bool Bundle::onDownloadTick() noexcept {
	double bundleRatio = 0;
	int64_t bundleSpeed = 0;
	int64_t bundlePos = 0;
	int down = 0;

	for (const auto& d: downloads) {
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

		bundleRatio = bundleRatio / down;
		actual = ((int64_t)((double)(finishedSegments+bundlePos) * (bundleRatio == 0 ? 1.00 : bundleRatio)));
		return true;
	}
	return false;
}

int Bundle::countRunningUsers() const noexcept {
	unordered_set<UserPtr, User::Hash> uniqueUsers;
	for (const auto& d : downloads) {
		uniqueUsers.insert(d->getUser());
	}

	return static_cast<int>(uniqueUsers.size());
}

/* ONLY CALLED FROM DOWNLOADMANAGER END */


void Bundle::save() {
	{
		File ff(getXmlFilePath() + ".tmp", File::WRITE, File::CREATE | File::TRUNCATE);
		BufferedOutputStream<false> f(&ff);
		f.write(SimpleXML::utf8Header);
		string tmp;
		string b32tmp;

		auto saveFiles = [&] {
			for (const auto& q : finishedFiles) {
				q->save(f, tmp, b32tmp);
			}

			for (const auto& q : queueItems) {
				q->save(f, tmp, b32tmp);
			}
		};

		if (isFileBundle()) {
			f.write(LIT("<File Version=\"" FILE_BUNDLE_VERSION));
			f.write(LIT("\" Token=\""));
			f.write(getStringToken());
			f.write(LIT("\" Date=\""));
			f.write(Util::toString(bundleDate));
			f.write(LIT("\" AddedByAutoSearch=\""));
			f.write(Util::toString(getAddedByAutoSearch()));

			if (resumeTime > 0) {
				f.write(LIT("\" ResumeTime=\""));
				f.write(Util::toString(resumeTime));
			}
			f.write(LIT("\">\r\n"));
			saveFiles();
			f.write(LIT("</File>\r\n"));
		} else {
			f.write(LIT("<Bundle Version=\"" DIR_BUNDLE_VERSION));
			f.write(LIT("\" Target=\""));
			f.write(SimpleXML::escape(target, tmp, true));
			f.write(LIT("\" Token=\""));
			f.write(getStringToken());
			f.write(LIT("\" Added=\""));
			f.write(Util::toString(getTimeAdded()));
			f.write(LIT("\" Date=\""));
			f.write(Util::toString(bundleDate));
			f.write(LIT("\" AddedByAutoSearch=\""));
			f.write(Util::toString(getAddedByAutoSearch()));
			if (!getAutoPriority()) {
				f.write(LIT("\" Priority=\""));
				f.write(Util::toString((int)getPriority()));
			}
			if (timeFinished > 0) {
				f.write(LIT("\" TimeFinished=\""));
				f.write(Util::toString(timeFinished));
			}
			if (resumeTime > 0) {
				f.write(LIT("\" ResumeTime=\""));
				f.write(Util::toString(resumeTime));
			}

			f.write(LIT("\">\r\n"));

			saveFiles();

			f.write(LIT("</Bundle>\r\n"));
		}
	}

	File::deleteFile(getXmlFilePath());
	File::renameFile(getXmlFilePath() + ".tmp", getXmlFilePath());
	
	dirty = false;
}

}
