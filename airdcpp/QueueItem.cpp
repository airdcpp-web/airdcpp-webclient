/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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
#include "QueueItem.h"

#include "ActionHook.h"
#include "Bundle.h"
#include "ClientManager.h"
#include "Download.h"
#include "File.h"
#include "HashManager.h"
#include "PathUtil.h"
#include "SimpleXML.h"
#include "Util.h"
#include "ValueGenerator.h"

namespace dcpp {

namespace {
	const string TEMP_EXTENSION = ".dctmp";

	string getTempName(const string& aFileName, const TTHValue& aRoot) noexcept {
		string tmp(aFileName);
		tmp += "_" + Util::toString(ValueGenerator::rand());
		tmp += "." + aRoot.toBase32();
		tmp += TEMP_EXTENSION;
		return tmp;
	}
}

QueueItem::QueueItem(const string& aTarget, int64_t aSize, Priority aPriority, Flags::MaskType aFlag,
		time_t aAdded, const TTHValue& tth, const string& aTempTarget) :
		QueueItemBase(aTarget, aSize, aPriority, aAdded, ValueGenerator::rand(), aFlag),
		tthRoot(tth), tempTarget(aTempTarget)
	{

	
	if(isSet(FLAG_USER_LIST) || isSet(FLAG_CLIENT_VIEW)) {
		/* Always use highest for the items without bundle */
		setPriority(Priority::HIGHEST);
	} else {
		if (priority == Priority::DEFAULT) {
			if(aSize <= Util::convertSize(SETTING(PRIO_HIGHEST_SIZE), Util::KB)) {
				setPriority(Priority::HIGHEST);
			} else if (aSize <= Util::convertSize(SETTING(PRIO_HIGH_SIZE), Util::KB)) {
				setPriority(Priority::HIGH);
			} else if (aSize <= Util::convertSize(SETTING(PRIO_NORMAL_SIZE), Util::KB)) {
				setPriority(Priority::NORMAL);
			} else if (aSize <= Util::convertSize(SETTING(PRIO_LOW_SIZE), Util::KB)) {
				setPriority(Priority::LOW);
			} else if(SETTING(PRIO_LOWEST)) {
				setPriority(Priority::LOWEST);
			} else if(SETTING(AUTO_PRIORITY_DEFAULT)) {
				setAutoPriority(true);
				setPriority(Priority::LOW);
			} else {
				setPriority(Priority::NORMAL);
			}
		}

		maxSegments = getMaxSegments(size);
	}
}

int64_t QueueItem::getBlockSize() noexcept {
	if (blockSize == -1) {
		blockSize = HashManager::getInstance()->getBlockSize(tthRoot);
		if (blockSize == 0)
			blockSize = size; //don't recheck those as the block size will get automatically updated when the tree is downloaded...
	}
	return blockSize;
}

bool QueueItem::AlphaSortOrder::operator()(const QueueItemPtr& left, const QueueItemPtr& right) const noexcept {
	auto extLeft = left->getTarget().rfind('.');
	auto extRight = right->getTarget().rfind('.');
	if (extLeft != string::npos && extRight != string::npos && 
		compare(left->getTarget().substr(0, extLeft), right->getTarget().substr(0, extRight)) == 0) {

		//only the extensions differs, .rar comes before .rXX
		auto isRxx = [](const string& aPath, size_t extPos) {
			return aPath.length() - extPos == 4 && aPath[extPos+1] == 'r' && isdigit(aPath[extPos+2]);
		};

		if (Util::stricmp(left->getTarget().substr(extLeft), ".rar") == 0 && isRxx(right->getTarget(), extRight)) {
			return true;
		}

		if (Util::stricmp(right->getTarget().substr(extRight), ".rar") == 0 && isRxx(left->getTarget(), extLeft)) {
			return false;
		}
	}

	return compare(left->getTarget(), right->getTarget()) < 0;
}

/* This has a few extra checks because the size is unknown for filelists */
bool QueueItem::SizeSortOrder::operator()(const QueueItemPtr& left, const QueueItemPtr& right) const noexcept {
	//partial lists always go first
	if (left->isSet(QueueItem::FLAG_PARTIAL_LIST)) return true;
	if (right->isSet(QueueItem::FLAG_PARTIAL_LIST)) return false;

	//small files go before full lists
	if (right->isSet(QueueItem::FLAG_USER_LIST) && left->getSize() < Util::convertSize(SETTING(PRIO_HIGHEST_SIZE), Util::KB)) return true;
	if (left->isSet(QueueItem::FLAG_USER_LIST) && right->getSize() < Util::convertSize(SETTING(PRIO_HIGHEST_SIZE), Util::KB)) return false;

	return left->getSize() < right->getSize();
}

bool QueueItem::PrioSortOrder::operator()(const QueueItemPtr& left, const QueueItemPtr& right) const noexcept {
	return left->getPriority() > right->getPriority();
}

bool QueueItem::isFailedStatus(Status aStatus) noexcept {
	return aStatus == STATUS_VALIDATION_ERROR;
}

Priority QueueItem::calculateAutoPriority() const noexcept {
	if (getAutoPriority()) {
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

bool QueueItem::hasPartialSharingTarget() noexcept {
	// don't share when the file does not exist
	if(!PathUtil::fileExists(isDownloaded() ? target : getTempTarget()))
		return false;

	return true;
}

bool QueueItem::isBadSourceExcept(const UserPtr& aUser, Flags::MaskType aExceptions, bool& isBad_) const noexcept {
	const auto i = getBadSource(aUser);
	if (i != badSources.end()) {
		isBad_ = true;
		return i->isAnySet((Flags::MaskType)(aExceptions ^Source::FLAG_MASK));
	}

	return false;
}

bool QueueItem::isChunkDownloaded(const Segment& aSegment) const noexcept {
	auto requestStart = aSegment.getStart();
	auto requestLen = aSegment.getSize();

	if (requestLen <= 0) return false;

	for (auto& i: done) {
		auto start  = i.getStart();
		auto end = i.getEnd();

		if (start <= requestStart && requestStart < end && aSegment.getEnd() <= end){
			// len_ = min(len_, end - requestStart);
			return true;
		}
	}

	return false;
}

string QueueItem::getStatusString(int64_t aDownloadedBytes, bool aIsWaiting) const noexcept {
	switch (status) {
		case STATUS_NEW:
		case STATUS_QUEUED: {
			auto percentage = getPercentage(aDownloadedBytes);
			if (isPausedPrio()) {
				return STRING_F(PAUSED_PCT, percentage);
			} else if (aIsWaiting) {
				return STRING_F(WAITING_PCT, percentage);
			} else {
				return STRING_F(RUNNING_PCT, percentage);
			}
		}
		case STATUS_DOWNLOADED: return STRING(DOWNLOADED);
		case STATUS_VALIDATION_RUNNING: return STRING(VALIDATING_CONTENT);
		case STATUS_VALIDATION_ERROR: {
			dcassert(hookError);
			if (hookError) {
				return ActionHookRejection::formatError(hookError);
			}

			return Util::emptyString;
		}
		case STATUS_COMPLETED: return STRING(FINISHED);
	}

	dcassert(0);
	return Util::emptyString;
}

string QueueItem::getListName() const noexcept {
	dcassert(isSet(QueueItem::FLAG_USER_LIST));
	if (isSet(QueueItem::FLAG_PARTIAL_LIST)) {
		return target;
	} else if(isSet(QueueItem::FLAG_XML_BZLIST)) {
		return target + ".xml.bz2";
	} else {
		return target + ".xml";
	}
}

/* INTERNAL */
uint8_t QueueItem::getMaxSegments(int64_t aFileSize) noexcept {
	uint8_t ret = 1;

	if(SETTING(SEGMENTS_MANUAL)) {
		ret = min((uint8_t)SETTING(NUMBER_OF_SEGMENTS), (uint8_t)10);
	} else {
		if ((aFileSize >= 2*1048576) && (aFileSize < 15*1048576)) {
			ret = 2;
		} else if((aFileSize >= (int64_t)15*1048576) && (aFileSize < (int64_t)30*1048576)) {
			ret = 3;
		} else if((aFileSize >= (int64_t)30*1048576) && (aFileSize < (int64_t)60*1048576)) {
			ret = 4;
		} else if((aFileSize >= (int64_t)60*1048576) && (aFileSize < (int64_t)120*1048576)) {
			ret = 5;
		} else if((aFileSize >= (int64_t)120*1048576) && (aFileSize < (int64_t)240*1048576)) {
			ret = 6;
		} else if((aFileSize >= (int64_t)240*1048576) && (aFileSize < (int64_t)480*1048576)) {
			ret = 7;
		} else if((aFileSize >= (int64_t)480*1048576) && (aFileSize < (int64_t)960*1048576)) {
			ret = 8;
		} else if((aFileSize >= (int64_t)960*1048576) && (aFileSize < (int64_t)1920*1048576)) {
			ret = 9;
		} else if(aFileSize >= (int64_t)1920*1048576) {
			ret = 10;
		}
	}

#ifdef _DEBUG
	return 88;
#else
	return ret;
#endif
}

int QueueItem::countOnlineUsers() const noexcept {
	return static_cast<int>(ranges::count_if(sources, [](const Source& s) { return s.getUser().user->isOnline(); } ));
}

QueueItem::~QueueItem() { }

void QueueItem::getOnlineUsers(HintedUserList& l) const noexcept {
	for(auto& i: sources) {
		if(i.getUser().user->isOnline())
			l.push_back(i.getUser());
	}
}

void QueueItem::addSource(const HintedUser& aUser) noexcept {
	dcassert(!isSource(aUser.user));
	auto i = getBadSource(aUser);
	if(i != badSources.end()) {
		sources.push_back(*i);
		badSources.erase(i);
	} else {
		sources.emplace_back(aUser);
	}
}

void QueueItem::blockSourceHub(const HintedUser& aUser) noexcept {
	dcassert(isSource(aUser.user));
	auto s = getSource(aUser.user);
	s->addBlockedHub(aUser.hint);
}

bool QueueItem::validateHub(const UserPtr& aUser, const string& aUrl) const noexcept {
	auto s = getSource(aUser);
	return s->validateHub(aUrl, allowUrlChange());
}

void QueueItem::removeSource(const UserPtr& aUser, Flags::MaskType reason) noexcept {
	SourceIter i = getSource(aUser);
	dcassert(i != sources.end());
	if(i == sources.end())
		return;

	i->setFlag(reason);
	badSources.push_back(*i);
	sources.erase(i);
}

const string& QueueItem::getTempTarget() noexcept {
	if (isFilelist()) {
		// tempTarget is used for the directory path
		return Util::emptyString;
	}

	if (isSet(FLAG_OPEN) || isSet(FLAG_CLIENT_VIEW)) {
		setTempTarget(target);
	} else if (tempTarget.empty()) {
		setTempTarget(target + TEMP_EXTENSION);
	}

	return tempTarget;
}

void QueueItem::setTempTarget(const string& aTempTarget) noexcept {
	if (isFilelist()) {
		// The list path can't be changed
		return;
	}

	tempTarget = aTempTarget;
}

const string& QueueItem::getListDirectoryPath() const noexcept {
	dcassert(isFilelist());
	dcassert(!tempTarget.empty());
	return tempTarget;
}

uint64_t QueueItem::getAverageSpeed() const noexcept {
	uint64_t totalSpeed = 0;
	
	for(auto d: downloads) {
		totalSpeed += static_cast<int64_t>(d->getAverageSpeed());
	}

	return totalSpeed;
}

uint64_t QueueItem::getSecondsLeft() const noexcept {
	auto speed = getAverageSpeed();
	return speed > 0 ? (getSize() - getDownloadedBytes()) / speed : 0;
}

double QueueItem::getDownloadedFraction() const noexcept { 
	return static_cast<double>(getDownloadedBytes()) / size; 
}

bool QueueItem::segmentsDone() const noexcept {
	return done.size() == 1 && *done.begin() == Segment(0, size);
}

bool QueueItem::isDownloaded() const noexcept {
	return status >= STATUS_DOWNLOADED;
}

bool QueueItem::isCompleted() const noexcept {
	return status >= STATUS_COMPLETED;
}

bool QueueItem::isFilelist() const noexcept {
	return isSet(FLAG_USER_LIST);
}

Segment QueueItem::getNextSegment(int64_t aBlockSize, int64_t aWantedSize, int64_t aLastSpeed, const PartsInfo* aPartsInfo, bool aAllowOverlap) const noexcept {
	if(size == -1 || aBlockSize == 0) {
		return Segment(0, -1);
	}
	
	if((!SETTING(MULTI_CHUNK) || aBlockSize >= size) /*&& (done.size() == 0 || (done.size() == 1 && *done.begin()->getStart() == 0))*/) {
		if(!downloads.empty()) {
			return checkOverlaps(aBlockSize, aLastSpeed, aPartsInfo, aAllowOverlap);
		}

		int64_t start = 0;
		int64_t end = size;

		if(!done.empty()) {
			const Segment& first = *done.begin();

			if(first.getStart() > 0) {
				end = Util::roundUp(first.getStart(), aBlockSize);
			} else {
				start = Util::roundDown(first.getEnd(), aBlockSize);

				if(done.size() > 1) {
					const Segment& second = *(++done.begin());
					end = Util::roundUp(second.getStart(), aBlockSize);
				}
			}
		}

		return Segment(start, std::min(size, end) - start);
	}
	
	if(isPausedPrio() || downloads.size() >= maxSegments) {
		// no other segments if we have reached the speed or segment limit
		return Segment(-1, 0);
	}

	/* added for PFS */
	vector<int64_t> posArray;
	vector<Segment> neededParts;

	if (aPartsInfo) {
		posArray.reserve(aPartsInfo->size());

		// Convert block index to file position
		for (auto index: *aPartsInfo)
			posArray.push_back(min(size, (int64_t)(index) * aBlockSize));
	}

	/***************************/

	double donePart = static_cast<double>(getDownloadedBytes()) / size;
		
	// We want smaller blocks at the end of the transfer, squaring gives a nice curve...
	int64_t targetSize = static_cast<int64_t>(static_cast<double>(aWantedSize) * std::max(0.25, (1. - (donePart * donePart))));
		
	if(targetSize > aBlockSize) {
		// Round off to nearest block size
		targetSize = Util::roundDown(targetSize, aBlockSize);
	} else {
		targetSize = aBlockSize;
	}		

	int64_t start = 0;
	int64_t curSize = targetSize;

	while(start < size) {
		int64_t end = std::min(size, start + curSize);
		Segment block(start, end - start);
		bool overlaps = false;
		for(auto i = done.begin(); !overlaps && i != done.end(); ++i) {
			if(curSize <= aBlockSize) {
				int64_t dstart = i->getStart();
				int64_t dend = i->getEnd();
				// We accept partial overlaps, only consider the block done if it is fully consumed by the done block
				if(dstart <= start && dend >= end) {
					overlaps = true;
				}
			} else {
				overlaps = block.overlaps(*i);
			}
		}
		
		for(auto i = downloads.begin(); !overlaps && i != downloads.end(); ++i) {
			overlaps = block.overlaps((*i)->getSegment());
		}
		
		if(!overlaps) {
			if (aPartsInfo) {
				// store all chunks we could need
				for(auto j = posArray.begin(); j < posArray.end(); j += 2){
					if( (*j <= start && start < *(j+1)) || (start <= *j && *j < end) ) {
						int64_t b = max(start, *j);
						int64_t e = min(end, *(j+1));

						// segment must be blockSize aligned
						dcassert(b % aBlockSize == 0);
						dcassert(e % aBlockSize == 0 || e == size);

						neededParts.emplace_back(b, e - b);
					}
				}
			} else {
				//dcassert(find_if(downloads.begin(), downloads.end(), [&block](const Download* d) { return block.getEnd() == d->getSegment().getEnd(); }) == downloads.end());
				return block;
			}
		}
		
		if (overlaps && (curSize > aBlockSize)) {
			curSize -= aBlockSize;
		} else {
			start = end;
			curSize = targetSize;
		}
	}

	if (!neededParts.empty()) {
		// select random chunk for download
		dcdebug("Found chunks: " SIZET_FMT "\n", neededParts.size());
		
		Segment& selected = neededParts[ValueGenerator::rand(0, neededParts.size() - 1)];
		selected.setSize(std::min(selected.getSize(), targetSize));	// request only wanted size
		
		return selected;
	}

	return checkOverlaps(aBlockSize, aLastSpeed, aPartsInfo, aAllowOverlap);
}

Segment QueueItem::checkOverlaps(int64_t aBlockSize, int64_t aLastSpeed, const PartsInfo* aPartsInfo, bool aAllowOverlap) const noexcept {
	if(aAllowOverlap && !aPartsInfo && bundle && SETTING(OVERLAP_SLOW_SOURCES) && aLastSpeed > 0) {
		// overlap slow running chunk
		for(auto d: downloads) {
			// current chunk mustn't be already overlapped
			if(d->getOverlapped())
				continue;

			// current chunk must be running at least for 4 seconds
			if(d->getStart() == 0 || GET_TICK() - d->getStart() < 4000) 
				continue;

			// current chunk mustn't be finished in next 20 seconds
			if(d->getSecondsLeft() < 20)
				continue;

			// overlap current chunk at last block boundary
			int64_t pos = d->getPos() - (d->getPos() % aBlockSize);
			int64_t chunkSize = d->getSegmentSize() - pos;

			// new user should finish this chunk more than 2x faster
			int64_t newChunkLeft = chunkSize / aLastSpeed;
			if(2 * newChunkLeft < d->getSecondsLeft()) {
				dcdebug("Overlapping... old user: " I64_FMT " s, new user: " I64_FMT " s\n", d->getSecondsLeft(), newChunkLeft);
				return Segment(d->getStartPos() + pos, chunkSize, true);
			}
		}
	}
	return Segment(0, 0);
}

uint64_t QueueItem::getDownloadedSegments() const noexcept {
	uint64_t total = 0;
	// count done segments
	for(auto& i: done) {
		total += i.getSize();
	}
	return total;
}

uint64_t QueueItem::getDownloadedBytes() const noexcept {
	uint64_t total = 0;

	// count done segments
	for(auto& i: done) {
		total += i.getSize();
	}

	// count running segments
	for(auto d: downloads) {
		total += d->getPos();
	}

	return total;
}

void QueueItem::addFinishedSegment(const Segment& aSegment) noexcept {
#ifdef _DEBUG
	if (bundle)
		dcdebug("QueueItem::addFinishedSegment: adding segment of size " I64_FMT " (" I64_FMT ", " I64_FMT ")...", aSegment.getSize(), aSegment.getStart(), aSegment.getEnd());
#endif

	dcassert(aSegment.getOverlapped() == false);
	done.insert(aSegment);

	// Consolidate segments

	bool added = false;
	if(done.size() != 1) {
		for(auto i = ++done.begin() ; i != done.end(); ) {
			auto prev = i;
			prev--;
			if(prev->getEnd() >= i->getStart()) {
				Segment big(prev->getStart(), i->getEnd() - prev->getStart());
				auto newBytes = big.getSize() - (*prev == aSegment ? i->getSize() : prev->getSize()); //minus the part that has been counted before...

				done.erase(prev);
				done.erase(i++);
				done.insert(big);
				if (bundle && !added) {
					dcdebug("added " I64_FMT " for the bundle (segments merged)\n", newBytes);
					bundle->addFinishedSegment(newBytes);
				}
				added = true;
			} else {
				++i;
			}
		}
	}

	if (!added && bundle) {
		dcdebug("added " I64_FMT " for the bundle (no merging)\n", aSegment.getSize());
		bundle->addFinishedSegment(aSegment.getSize());
	}
}

bool QueueItem::isNeededPart(const PartsInfo& aPartsInfo, int64_t aBlockSize) const noexcept {
	dcassert(aPartsInfo.size() % 2 == 0);
	
	auto i = done.begin();
	for(auto j = aPartsInfo.begin(); j != aPartsInfo.end(); j+=2){
		while(i != done.end() && (*i).getEnd() <= (*j) * aBlockSize)
			i++;

		if(i == done.end() || !((*i).getStart() <= (*j) * aBlockSize && (*i).getEnd() >= (*(j+1)) * aBlockSize))
			return true;
	}
	
	return false;
}

void QueueItem::getPartialInfo(PartsInfo& aPartialInfo, int64_t aBlockSize) const noexcept {
	size_t maxSize = min(done.size() * 2, (size_t)510);
	aPartialInfo.reserve(maxSize);

	auto i = done.begin();
	for(; i != done.end() && aPartialInfo.size() < maxSize; i++) {

		uint16_t s = (uint16_t)((*i).getStart() / aBlockSize);
		uint16_t e = (uint16_t)(((*i).getEnd() - 1) / aBlockSize + 1);

		aPartialInfo.push_back(s);
		aPartialInfo.push_back(e);
	}
}

void QueueItem::getChunksVisualisation(vector<Segment>& running_, vector<Segment>& downloaded_, vector<Segment>& done_) const noexcept {  // type: 0 - downloaded bytes, 1 - running chunks, 2 - done chunks
	running_.reserve(downloads.size());
	for(auto d: downloads) {
		running_.push_back(d->getSegment());
	}

	downloaded_.reserve(downloads.size());
	for(auto d: downloads) {
		downloaded_.emplace_back(d->getStartPos(), d->getPos());
	}

	done_.reserve(done.size());
	for(auto& i: done) {
		done_.push_back(i);
	}
}

bool QueueItem::allowUrlChange() const noexcept {
	return !isSet(QueueItem::FLAG_USER_LIST) || isSet(QueueItem::FLAG_TTHLIST_BUNDLE);
}

bool QueueItem::Source::validateHub(const OrderedStringSet& aOnlineHubs, bool aAllowUrlChange, string& lastError_) const noexcept {
	// Only blocked hubs?
	if (!blockedHubs.empty() && includes(blockedHubs.begin(), blockedHubs.end(), aOnlineHubs.begin(), aOnlineHubs.end())) {
		lastError_ = STRING(NO_ACCESS_ONLINE_HUBS);
		return false;
	}

	// Can't download a filelist if the hub is offline... don't be too strict with NMDC hubs
	if (!user.user->isSet(User::NMDC)) {
		if (!aAllowUrlChange && aOnlineHubs.find(user.hint) == aOnlineHubs.end()) {
			lastError_ = STRING(USER_OFFLINE);
			return false;
		}
	}

	return true;
}

bool QueueItem::Source::validateHub(const string& aHubUrl, bool aAllowUrlChange) const noexcept {
	string lastError;
	OrderedStringSet onlineHubs({ aHubUrl });
	return validateHub(onlineHubs, aAllowUrlChange, lastError);
}

bool QueueItem::matchesDownloadType(QueueDownloadType aType) const noexcept {
	if (aType == QueueDownloadType::SMALL && !usesSmallSlot()) {
		//don't even think of stealing our priority channel
		return false;
	} else if (aType == QueueDownloadType::MCN_NORMAL && usesSmallSlot()) {
		return false;
	}

	return true;
}

bool QueueItem::allowSegmentedDownloads() const noexcept {
	// Don't try to create multiple connections for filelists or files viewed in client
	if (isSet(QueueItem::FLAG_USER_LIST) || isSet(QueueItem::FLAG_CLIENT_VIEW)) {
		return false;
	}

	// No segmented downloading when getting the tree
	if (getDownloads()[0]->getType() == Transfer::TYPE_TREE) {
		return false;
	}

	return true;
}

bool QueueItem::hasSegment(const QueueDownloadQuery& aQuery, string& lastError_, bool aAllowOverlap) noexcept {
	if (isPausedPrio())
		return false;

	dcassert(isSource(aQuery.user));
	auto source = getSource(aQuery.user);

	// Check source
	if (!source->validateHub(aQuery.onlineHubs, allowUrlChange(), lastError_)) {
		return false;
	}

	// Finished?
	if (segmentsDone()) {
		return false;
	}

	// Slot type
	if (!matchesDownloadType(aQuery.downloadType)) {
		return false;
	}

	// See if we have an available segment

	if (isWaiting()) {
		return true;
	}

	// Running item

	if (!allowSegmentedDownloads()) {
		return false;
	}

	// File segment?
	auto segment = getNextSegment(getBlockSize(), aQuery.wantedSize, aQuery.lastSpeed, source->getPartsInfo(), aAllowOverlap);
	if (segment.getSize() == 0) {
		lastError_ = (segment.getStart() == -1 || getSize() < Util::convertSize(SETTING(MIN_SEGMENT_SIZE), Util::KB)) ? STRING(NO_FILES_AVAILABLE) : STRING(NO_FREE_BLOCK);
		dcdebug("No segment for %s (%s) in %s, block " I64_FMT "\n", aQuery.user->getCID().toBase32().c_str(), Util::listToString(aQuery.onlineHubs).c_str(), getTarget().c_str(), blockSize);
		return false;
	}

	return true;
}

bool QueueItem::isPausedPrio() const noexcept {
	if (bundle) {
		// Highest priority files will continue to run even if the bundle is paused (non-forced)
		if (priority == Priority::HIGHEST && bundle->getPriority() != Priority::PAUSED_FORCE) {
			return false;
		}

		if (bundle->isPausedPrio()) {
			return true;
		}
	}

	return QueueItemBase::isPausedPrio();
}

bool QueueItem::usesSmallSlot() const noexcept {
	return (isSet(FLAG_PARTIAL_LIST) || (size <= 65792 && !isSet(FLAG_USER_LIST) && isSet(FLAG_CLIENT_VIEW)));
}


string QueueItem::getTargetFileName() const noexcept {
	return PathUtil::getFileName(target); 
}

string QueueItem::getFilePath() const noexcept {
	return PathUtil::getFilePath(target); 
}

QueueItemPtr QueueItem::pickSearchItem(const QueueItemList& aItems) noexcept {
	QueueItemPtr searchItem = nullptr;

	for (size_t s = 0; s < aItems.size(); s++) {
		searchItem = aItems[ValueGenerator::rand(0, aItems.size() - 1)];

		if (!searchItem->isRunning() && !searchItem->isPausedPrio()) {
			break;
		}

		// See if we can find a better one
	}

	return searchItem;
}

void QueueItem::addDownload(Download* d) noexcept {
	downloads.push_back(d);
}

void QueueItem::removeDownload(const Download* d) noexcept {
	auto m = ranges::find(downloads, d);
	dcassert(m != downloads.end());
	if (m != downloads.end()) {
		downloads.erase(m);
	} else {
		dcassert(0);
	}
}

void QueueItem::removeDownloads(const UserPtr& aUser) noexcept {
	for(auto i = downloads.begin(); i != downloads.end();) {
		if((*i)->getUser() == aUser) {
			i = downloads.erase(i);
		} else {
			i++;
		}
	}
}


void QueueItem::save(OutputStream &f, string tmp, string b32tmp) {
	string indent = "\t";

	if (segmentsDone()) {
		f.write(LIT("\t<Finished"));
	} else {
		f.write(LIT("\t<Download"));
	}

	f.write(indent);
	f.write(LIT(" Target=\""));
	f.write(SimpleXML::escape(target, tmp, true));
	f.write(LIT("\" Size=\""));
	f.write(Util::toString(size));
	f.write(LIT("\" Added=\""));
	f.write(Util::toString(timeAdded));

	b32tmp.clear();
	f.write(LIT("\" TTH=\""));
	f.write(tthRoot.toBase32(b32tmp));

	if (segmentsDone()) {
		f.write(LIT("\" TimeFinished=\""));
		f.write(Util::toString(timeFinished));
		f.write(LIT("\" LastSource=\""));
		f.write(lastSource);
		f.write(LIT("\"/>\r\n"));
		return;
	}

	f.write(LIT("\" Priority=\""));
	f.write(Util::toString((int) getPriority()));

	if(!done.empty()) {
		f.write(LIT("\" TempTarget=\""));
		f.write(SimpleXML::escape(tempTarget, tmp, true));
	}
	f.write(LIT("\" AutoPriority=\""));
	f.write(Util::toString(getAutoPriority()));
	f.write(LIT("\" MaxSegments=\""));
	f.write(Util::toString(maxSegments));

	f.write(LIT("\">\r\n"));

	for(const auto& s: done) {
		f.write(indent);
		f.write(LIT("\t<Segment Start=\""));
		f.write(Util::toString(s.getStart()));
		f.write(LIT("\" Size=\""));
		f.write(Util::toString(s.getSize()));
		f.write(LIT("\"/>\r\n"));
	}

	for(const auto& j: sources) {
		if(j.isSet(QueueItem::Source::FLAG_PARTIAL)) continue;
					
		const CID& cid = j.getUser().user->getCID();
		const string& hint = j.getUser().hint;

		f.write(indent);
		f.write(LIT("\t<Source CID=\""));
		f.write(cid.toBase32());
		f.write(LIT("\" Nick=\""));
		f.write(SimpleXML::escape(ClientManager::getInstance()->getNick(j.getUser(), hint), tmp, true));
		if(!hint.empty()) {
			f.write(LIT("\" HubHint=\""));
			f.write(hint);
		}

		/*if (SettingsManager::lanMode) {
			f.write(LIT("\" RemotePath=\""));
			f.write(j.getRemotePath());
		}*/
		f.write(LIT("\"/>\r\n"));
	}

	f.write(indent);
	f.write(LIT("</Download>\r\n"));
}

bool QueueItem::Source::updateDownloadHubUrl(const OrderedStringSet& aOnlineHubs, string& hubUrl_, bool aAllowUrlChange) const noexcept {
	if (!aAllowUrlChange) {
		// we already know that the hub is online
		dcassert(aOnlineHubs.find(user.hint) != aOnlineHubs.end());
		hubUrl_ = user.hint;
		return true;
	} else if (blockedHubs.find(hubUrl_) != blockedHubs.end()) {
		//we can't connect via a blocked hub
		StringList availableHubs;
		set_difference(aOnlineHubs.begin(), aOnlineHubs.end(), blockedHubs.begin(), blockedHubs.end(), back_inserter(availableHubs));
		hubUrl_ = availableHubs[0];
		return true;
	}

	return false;
}

void QueueItem::Source::setHubUrl(const string& aHubUrl) noexcept {
	user.hint = aHubUrl;
}

string QueueItem::Source::formatError(const Flags& aFlags) noexcept {
	if (aFlags.isSet(QueueItem::Source::FLAG_FILE_NOT_AVAILABLE)) {
		return STRING(FILE_NOT_AVAILABLE);
	} else if (aFlags.isSet(QueueItem::Source::FLAG_BAD_TREE)) {
		return STRING(INVALID_TREE);
	} else if (aFlags.isSet(QueueItem::Source::FLAG_NO_NEED_PARTS)) {
		return STRING(NO_NEEDED_PART);
	} else if (aFlags.isSet(QueueItem::Source::FLAG_NO_TTHF)) {
		return STRING(SOURCE_TOO_OLD);
	} else if (aFlags.isSet(QueueItem::Source::FLAG_SLOW_SOURCE)) {
		return STRING(SLOW_USER);
	} else if (aFlags.isSet(QueueItem::Source::FLAG_UNTRUSTED)) {
		return STRING(CERTIFICATE_NOT_TRUSTED);
	} else if (aFlags.isSet(QueueItem::Source::FLAG_TTH_INCONSISTENCY)) {
		return STRING(TTH_INCONSISTENCY);
	}


	return Util::emptyString;
}

void QueueItem::resetDownloaded() noexcept {
	if (bundle) {
		bundle->removeFinishedSegment(getDownloadedSegments());
	}

	done.clear();
}

}
