/*
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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
#include "QueueItem.h"

#include "SimpleXML.h"
#include "ClientManager.h"
#include "HashManager.h"
#include "Download.h"
#include "File.h"
#include "Util.h"
#include "LogManager.h"
#include "SearchManager.h"

#include "AirUtil.h"

namespace dcpp {

namespace {
	const string TEMP_EXTENSION = ".dctmp";

	string getTempName(const string& aFileName, const TTHValue& aRoot) {
		string tmp(aFileName);
		tmp += "_" + Util::toString(Util::rand());
		tmp += "." + aRoot.toBase32();
		tmp += TEMP_EXTENSION;
		return tmp;
	}
}

QueueItem::QueueItem(const string& aTarget, int64_t aSize, Priority aPriority, Flags::MaskType aFlag,
		time_t aAdded, const TTHValue& tth, const string& aTempTarget) :
		QueueItemBase(aTarget, aSize, aPriority, aAdded, aFlag),
		tthRoot(tth), tempTarget(aTempTarget)
	{

	
	if(isSet(FLAG_USER_LIST) || isSet(FLAG_CLIENT_VIEW)) {
		/* Always use highest for the items without bundle */
		setPriority(HIGHEST);
	} else {
		if (getPriority() == DEFAULT) {
			if(aSize <= Util::convertSize(SETTING(PRIO_HIGHEST_SIZE), Util::KB)) {
				setPriority(HIGHEST);
			} else if (aSize <= Util::convertSize(SETTING(PRIO_HIGH_SIZE), Util::KB)) {
				setPriority(HIGH);
			} else if (aSize <= Util::convertSize(SETTING(PRIO_NORMAL_SIZE), Util::KB)) {
				setPriority(NORMAL);
			} else if (aSize <= Util::convertSize(SETTING(PRIO_LOW_SIZE), Util::KB)) {
				setPriority(LOW);
			} else if(SETTING(PRIO_LOWEST)) {
				setPriority(LOWEST);
			} else if(SETTING(AUTO_PRIORITY_DEFAULT)) {
				setAutoPriority(true);
				setPriority(LOW);
			} else {
				setPriority(NORMAL);
			}
		}

		maxSegments = getMaxSegments(size);
	}
}

int64_t QueueItem::getBlockSize() {
	if (blockSize == -1) {
		blockSize = HashManager::getInstance()->getBlockSize(tthRoot);
		if (blockSize == 0)
			blockSize = size; //don't recheck those as the block size will get automatically updated when the tree is downloaded...
	}
	return blockSize;
}

bool QueueItem::AlphaSortOrder::operator()(const QueueItemPtr& left, const QueueItemPtr& right) const {
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
bool QueueItem::SizeSortOrder::operator()(const QueueItemPtr& left, const QueueItemPtr& right) const {
	//partial lists always go first
	if (left->isSet(QueueItem::FLAG_PARTIAL_LIST)) return true;
	if (right->isSet(QueueItem::FLAG_PARTIAL_LIST)) return false;

	//small files go before full lists
	if (right->isSet(QueueItem::FLAG_USER_LIST) && left->getSize() < Util::convertSize(SETTING(PRIO_HIGHEST_SIZE), Util::KB)) return true;
	if (left->isSet(QueueItem::FLAG_USER_LIST) && right->getSize() < Util::convertSize(SETTING(PRIO_HIGHEST_SIZE), Util::KB)) return false;

	return left->getSize() < right->getSize();
}

bool QueueItem::PrioSortOrder::operator()(const QueueItemPtr& left, const QueueItemPtr& right) const {
	return left->getPriority() > right->getPriority();
}

QueueItemBase::Priority QueueItem::calculateAutoPriority() const {
	if(getAutoPriority()) {
		QueueItemBase::Priority p;
		int percent = static_cast<int>(getDownloadedBytes() * 10.0 / size);
		switch(percent){
				case 0:
				case 1:
				case 2:
					p = QueueItem::LOW;
					break;
				case 3:
				case 4:
				case 5:			
					p = QueueItem::NORMAL;
					break;
				case 6:
				case 7:
				default:
					p = QueueItem::HIGH;
					break;
		}
		return p;			
	}
	return getPriority();
}

bool QueueItem::hasPartialSharingTarget() {
	// don't share items that are being moved
	if (isFinished() && !isSet(QueueItem::FLAG_MOVED))
		return false;

	// don't share when the file does not exist
	if(!Util::fileExists(isFinished() ? target : getTempTarget()))
		return false;

	return true;
}

bool QueueItem::isBadSourceExcept(const UserPtr& aUser, Flags::MaskType exceptions, bool& isBad_) const {
	const auto i = getBadSource(aUser);
	if(i != badSources.end()) {
		isBad_ = true;
		return i->isAnySet((Flags::MaskType)(exceptions^Source::FLAG_MASK));
	}
	return false;
}

bool QueueItem::isChunkDownloaded(int64_t startPos, int64_t& len) const {
	if(len <= 0) return false;

	for(auto& i: done) {
		int64_t first  = i.getStart();
		int64_t second = i.getEnd();

		if(first <= startPos && startPos < second){
			len = min(len, second - startPos);
			return true;
		}
	}

	return false;
}

string QueueItem::getListName() const {
	dcassert(isSet(QueueItem::FLAG_USER_LIST));
	if(isSet(QueueItem::FLAG_XML_BZLIST)) {
		return target + ".xml.bz2";
	} else {
		return target + ".xml";
	}
}

/* INTERNAL */
uint8_t QueueItem::getMaxSegments(int64_t filesize) const noexcept {
	uint8_t MaxSegments = 1;

	if(SETTING(SEGMENTS_MANUAL)) {
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

int QueueItem::countOnlineUsers() const {
	return count_if(sources.begin(), sources.end(), [](const Source& s) { return s.getUser().user->isOnline(); } );
}

QueueItem::~QueueItem() { }

void QueueItem::getOnlineUsers(HintedUserList& l) const {
	for(auto& i: sources) {
		if(i.getUser().user->isOnline())
			l.push_back(i.getUser());
	}
}

void QueueItem::addSource(const HintedUser& aUser) {
	dcassert(!isSource(aUser.user));
	auto i = getBadSource(aUser);
	if(i != badSources.end()) {
		sources.push_back(*i);
		badSources.erase(i);
	} else {
		sources.emplace_back(aUser);
	}
}

void QueueItem::blockSourceHub(const HintedUser& aUser) {
	dcassert(isSource(aUser.user));
	auto s = getSource(aUser.user);
	s->blockedHubs.insert(aUser.hint);
}

bool QueueItem::isHubBlocked(const UserPtr& aUser, const string& aUrl) {
	auto s = getSource(aUser);
	return !s->blockedHubs.empty() && s->blockedHubs.find(aUrl) != s->blockedHubs.end();
}

void QueueItem::removeSource(const UserPtr& aUser, Flags::MaskType reason) {
	SourceIter i = getSource(aUser);
	dcassert(i != sources.end());
	if(i == sources.end())
		return;

	i->setFlag(reason);
	badSources.push_back(*i);
	sources.erase(i);
}

const string& QueueItem::getTempTarget() {
	if (isSet(FLAG_OPEN) || (isSet(FLAG_CLIENT_VIEW) && isSet(FLAG_TEXT))) {
		setTempTarget(target);
	} else if(!isSet(QueueItem::FLAG_USER_LIST) && tempTarget.empty()) {
		if (SETTING(DCTMP_STORE_DESTINATION)) {
			setTempTarget(target + TEMP_EXTENSION);
		} else if(!SETTING(TEMP_DOWNLOAD_DIRECTORY).empty() && (File::getSize(getTarget()) == -1)) {
#ifdef _WIN32
			ParamMap sm;
			if(target.length() >= 3 && target[1] == ':' && target[2] == '\\')
				sm["targetdrive"] = target.substr(0, 3);
			else
				sm["targetdrive"] = Util::getPath(Util::PATH_USER_CONFIG).substr(0, 3);
			setTempTarget(Util::formatParams(SETTING(TEMP_DOWNLOAD_DIRECTORY), sm, false) + getTempName(getTargetFileName(), getTTH()));
#else //_WIN32
			setTempTarget(SETTING(TEMP_DOWNLOAD_DIRECTORY) + getTempName(getTargetFileName(), getTTH()));
#endif //_WIN32
		}
	}
	return tempTarget;
}

uint64_t QueueItem::getAverageSpeed() const {
	uint64_t totalSpeed = 0;
	
	for(auto d: downloads) {
		totalSpeed += static_cast<int64_t>(d->getAverageSpeed());
	}

	return totalSpeed;
}

uint64_t QueueItem::getSecondsLeft() const {
	auto speed = getAverageSpeed();
	return speed > 0 ? (getSize() - getDownloadedBytes()) / static_cast<double>(speed) : 0;
}

void QueueItem::setTarget(const string& aTarget) {
	target = aTarget;
}

double QueueItem::getDownloadedFraction() const { 
	return static_cast<double>(getDownloadedBytes()) / size; 
}

bool QueueItem::isFinished() const {
	return done.size() == 1 && *done.begin() == Segment(0, size);
}

Segment QueueItem::getNextSegment(int64_t aBlockSize, int64_t wantedSize, int64_t lastSpeed, const PartialSource::Ptr partialSource, bool allowOverlap) const {
	if(size == -1 || aBlockSize == 0) {
		return Segment(0, -1);
	}
	
	if((!SETTING(MULTI_CHUNK) || aBlockSize >= size) /*&& (done.size() == 0 || (done.size() == 1 && *done.begin()->getStart() == 0))*/) {
		if(!downloads.empty()) {
			return checkOverlaps(aBlockSize, lastSpeed, partialSource, allowOverlap);
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
	
	if(!startDown() || downloads.size() >= maxSegments ||
		(SETTING(DONT_BEGIN_SEGMENT) && static_cast<uint64_t>(Util::convertSize(SETTING(DONT_BEGIN_SEGMENT_SPEED), Util::KB)) < getAverageSpeed()))
	{
		// no other segments if we have reached the speed or segment limit
		return Segment(-1, 0);
	}

	/* added for PFS */
	vector<int64_t> posArray;
	vector<Segment> neededParts;

	if(partialSource) {
		posArray.reserve(partialSource->getPartialInfo().size());

		// Convert block index to file position
		for(auto& i: partialSource->getPartialInfo())
			posArray.push_back(min(size, (int64_t)(i) * aBlockSize));
	}

	/***************************/

	double donePart = static_cast<double>(getDownloadedBytes()) / size;
		
	// We want smaller blocks at the end of the transfer, squaring gives a nice curve...
	int64_t targetSize = static_cast<int64_t>(static_cast<double>(wantedSize) * std::max(0.25, (1. - (donePart * donePart))));
		
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
			if(partialSource) {
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
		
		if(overlaps && (curSize > aBlockSize)) {
			curSize -= aBlockSize;
		} else {
			start = end;
			curSize = targetSize;
		}
	}

	if(!neededParts.empty()) {
		// select random chunk for download
		dcdebug("Found chunks: " SIZET_FMT "\n", neededParts.size());
		
		Segment& selected = neededParts[Util::rand(0, neededParts.size())];
		selected.setSize(std::min(selected.getSize(), targetSize));	// request only wanted size
		
		return selected;
	}

	return checkOverlaps(aBlockSize, lastSpeed, partialSource, allowOverlap);
}

Segment QueueItem::checkOverlaps(int64_t aBlockSize, int64_t aLastSpeed, const PartialSource::Ptr partialSource, bool allowOverlap) const {
	if(allowOverlap && !partialSource && bundle && SETTING(OVERLAP_SLOW_SOURCES) && aLastSpeed > 0) {
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

uint64_t QueueItem::getDownloadedSegments() const {
	uint64_t total = 0;
	// count done segments
	for(auto& i: done) {
		total += i.getSize();
	}
	return total;
}

uint64_t QueueItem::getDownloadedBytes() const {
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

void QueueItem::addFinishedSegment(const Segment& segment) {
#ifdef _DEBUG
	if (bundle)
		dcdebug("adding segment segment of size %u (%u, %u)...", segment.getSize(), segment.getStart(), segment.getEnd());
#endif

	dcassert(segment.getOverlapped() == false);
	done.insert(segment);

	// Consolidate segments

	bool added = false;
	if(done.size() != 1) {
		for(auto i = ++done.begin() ; i != done.end(); ) {
			auto prev = i;
			prev--;
			if(prev->getEnd() >= i->getStart()) {
				Segment big(prev->getStart(), i->getEnd() - prev->getStart());
				auto newBytes = big.getSize() - (*prev == segment ? i->getSize() : prev->getSize()); //minus the part that has been counted before...

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
		dcdebug("added " I64_FMT " for the bundle (no merging)\n", segment.getSize());
		bundle->addFinishedSegment(segment.getSize());
	}
}

bool QueueItem::isNeededPart(const PartsInfo& aPartsInfo, int64_t aBlockSize)
{
	dcassert(aPartsInfo.size() % 2 == 0);
	
	SegmentConstIter i  = done.begin();
	for(auto j = aPartsInfo.begin(); j != aPartsInfo.end(); j+=2){
		while(i != done.end() && (*i).getEnd() <= (*j) * aBlockSize)
			i++;

		if(i == done.end() || !((*i).getStart() <= (*j) * aBlockSize && (*i).getEnd() >= (*(j+1)) * aBlockSize))
			return true;
	}
	
	return false;
}

void QueueItem::getPartialInfo(PartsInfo& aPartialInfo, int64_t aBlockSize) const {
	size_t maxSize = min(done.size() * 2, (size_t)510);
	aPartialInfo.reserve(maxSize);

	SegmentConstIter i = done.begin();
	for(; i != done.end() && aPartialInfo.size() < maxSize; i++) {

		uint16_t s = (uint16_t)((*i).getStart() / aBlockSize);
		uint16_t e = (uint16_t)(((*i).getEnd() - 1) / aBlockSize + 1);

		aPartialInfo.push_back(s);
		aPartialInfo.push_back(e);
	}
}

void QueueItem::getChunksVisualisation(vector<Segment>& running_, vector<Segment>& downloaded_, vector<Segment>& done_) const {  // type: 0 - downloaded bytes, 1 - running chunks, 2 - done chunks
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

bool QueueItem::hasSegment(const UserPtr& aUser, const OrderedStringSet& onlineHubs, string& lastError, int64_t wantedSize, int64_t lastSpeed, DownloadType aType, bool allowOverlap) {
	if (!startDown())
		return false;

	auto source = getSource(aUser);
	if (!source->blockedHubs.empty() && includes(source->blockedHubs.begin(), source->blockedHubs.end(), onlineHubs.begin(), onlineHubs.end())) {
		lastError = STRING(NO_ACCESS_ONLINE_HUBS);
		return false;
	}

	//can't download a filelist if the hub is offline... don't be too strict with NMDC hubs
	if (!aUser->isSet(User::NMDC) && (isSet(FLAG_USER_LIST) && !isSet(FLAG_TTHLIST_BUNDLE)) && onlineHubs.find(source->getUser().hint) == onlineHubs.end()) {
		lastError = STRING(USER_OFFLINE);
		return false;
	}

	dcassert(isSource(aUser));
	if (isFinished()) {
		return false;
	}

	if(aType == TYPE_SMALL && !usesSmallSlot()) {
		//don't even think of stealing our priority channel
		return false;
	} else if (aType == TYPE_MCN_NORMAL && usesSmallSlot()) {
		return false;
	}

	if(isWaiting()) {
		return true;
	}
				
	// No segmented downloading when getting the tree
	if(getDownloads()[0]->getType() == Transfer::TYPE_TREE) {
		return false;
	}

	if(!isSet(QueueItem::FLAG_USER_LIST) && !isSet(QueueItem::FLAG_CLIENT_VIEW)) {
		Segment segment = getNextSegment(getBlockSize(), wantedSize, lastSpeed, source->getPartialSource(), allowOverlap);
		if(segment.getSize() == 0) {
			lastError = (segment.getStart() == -1 || getSize() < Util::convertSize(SETTING(MIN_SEGMENT_SIZE), Util::KB)) ? STRING(NO_FILES_AVAILABLE) : STRING(NO_FREE_BLOCK);
			//LogManager::getInstance()->message("NO SEGMENT: " + aUser->getCID().toBase32());
			dcdebug("No segment for %s (%s) in %s, block " I64_FMT "\n", aUser->getCID().toBase32().c_str(), Util::listToString(onlineHubs).c_str(), getTarget().c_str(), blockSize);
			return false;
		}
	} else if (!isWaiting()) {
		//don't try to create multiple connections for filelists or files viewed in client
		return false;
	}
	return true;
}

bool QueueItem::startDown() const {
	if(bundle && !bundle->isPausedPrio() && priority != PAUSED) {
		return true;
	} else if ((!bundle || bundle->getPriority() != PAUSED_FORCE) && priority == HIGHEST) {
		return true;
	}
	return false;
}

bool QueueItem::usesSmallSlot() const {
	return (isSet(FLAG_PARTIAL_LIST) || (size <= 65792 && !isSet(FLAG_USER_LIST) && isSet(FLAG_CLIENT_VIEW)));
}

void QueueItem::searchAlternates() {
	if (SettingsManager::lanMode)
		SearchManager::getInstance()->search(getTargetFileName(), size, SearchManager::TYPE_ANY, SearchManager::SIZE_EXACT, "qa", Search::ALT_AUTO);
	else
		SearchManager::getInstance()->search(tthRoot.toBase32(), 0, SearchManager::TYPE_TTH, SearchManager::SIZE_DONTCARE, "qa", Search::ALT_AUTO);
}

void QueueItem::addDownload(Download* d) {
	downloads.push_back(d);
}

void QueueItem::removeDownload(const string& aToken) {
	auto m = find_if(downloads.begin(), downloads.end(), [&](const Download* d) { return compare(d->getToken(), aToken) == 0; });
	dcassert(m != downloads.end());
	if (m != downloads.end()) {
		downloads.erase(m);
	} else {
		dcassert(0);
	}
}

void QueueItem::removeDownloads(const UserPtr& aUser) {
	//erase all downloads from this user
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

	if (isFinished()) {
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

	if (isFinished()) {
		f.write(LIT("\" TimeFinished=\""));
		f.write(Util::toString(fileFinished));
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

bool QueueItem::Source::updateHubUrl(const OrderedStringSet& onlineHubs, string& hubUrl, bool isFileList) {
	if (isFileList) {
		//we already know that the hub is online
		dcassert(onlineHubs.find(user.hint) != onlineHubs.end());
		hubUrl = user.hint;
		return true;
	} else if (blockedHubs.find(hubUrl) != blockedHubs.end()) {
		//we can't connect via a blocked hub
		StringList availableHubs;
		set_difference(onlineHubs.begin(), onlineHubs.end(), blockedHubs.begin(), blockedHubs.end(), back_inserter(availableHubs));
		hubUrl = availableHubs[0];
		return true;
	}

	return false;
}

}
