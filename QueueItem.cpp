/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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

#include "SimpleXml.h"
#include "ClientManager.h"
#include "HashManager.h"
#include "Download.h"
#include "File.h"
#include "Util.h"
#include "LogManager.h"
#include "SearchManager.h"

#include "AirUtil.h"
#include "Wildcards.h"

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
		Flags(aFlag), target(aTarget), maxSegments(1), fileBegin(0),
		size(aSize), priority(aPriority), added(aAdded),
		tthRoot(tth), autoPriority(false), nextPublishingTime(0), tempTarget(aTempTarget)
	{

	if(isSet(FLAG_USER_LIST) || isSet(FLAG_CLIENT_VIEW)) {
		/* Always use highest for the items without bundle */
		priority = QueueItem::HIGHEST;
	} else {
		if (priority == DEFAULT) {
			if(aSize <= SETTING(PRIO_HIGHEST_SIZE)*1024) {
				priority = HIGHEST;
			} else if(aSize <= SETTING(PRIO_HIGH_SIZE)*1024) {
				priority = HIGH;
			} else if(aSize <= SETTING(PRIO_NORMAL_SIZE)*1024) {
				priority = NORMAL;
			} else if(aSize <= SETTING(PRIO_LOW_SIZE)*1024) {
				priority = LOW;
			} else if(SETTING(PRIO_LOWEST)) {
				priority = LOWEST;
			} else if(BOOLSETTING(AUTO_PRIORITY_DEFAULT)) {
				autoPriority = true;
				priority = LOW;
			} else {
				priority = NORMAL;
			}
		}

		maxSegments = getMaxSegments(size);
	}
}

bool QueueItem::AlphaSortOrder::operator()(const QueueItemPtr left, const QueueItemPtr right) const {
	auto extLeft = left->getTarget().rfind('.');
	auto extRight = right->getTarget().rfind('.');
	if (extLeft != string::npos && extRight != string::npos && 
		compare(left->getTarget().substr(0, extLeft), right->getTarget().substr(0, extRight)) == 0) {

		//only the extensions differs, .rar comes before .rXX
		auto isRxx = [](const string& aPath, size_t extPos) {
			return aPath.length() - extPos == 4 && aPath[extPos+1] == 'r' && isdigit(aPath[extPos+2]);
		};

		if (stricmp(left->getTarget().substr(extLeft), ".rar") == 0 && isRxx(right->getTarget(), extRight)) {
			return true;
		}

		if (stricmp(right->getTarget().substr(extRight), ".rar") == 0 && isRxx(left->getTarget(), extLeft)) {
			return false;
		}
	}

	return compare(left->getTarget(), right->getTarget()) < 0;
}

/* This has a few extra checks because the size is unknown for filelists */
bool QueueItem::SizeSortOrder::operator()(const QueueItemPtr left, const QueueItemPtr right) const {
	//partial lists always go first
	if (left->isSet(QueueItem::FLAG_PARTIAL_LIST)) return true;
	if (right->isSet(QueueItem::FLAG_PARTIAL_LIST)) return false;

	//small files go before full lists
	if (right->isSet(QueueItem::FLAG_USER_LIST) && left->getSize() < SETTING(PRIO_HIGHEST_SIZE)*1024) return true;
	if (left->isSet(QueueItem::FLAG_USER_LIST) && right->getSize() < SETTING(PRIO_HIGHEST_SIZE)*1024) return false;

	return left->getSize() < right->getSize();
}

bool QueueItem::PrioSortOrder::operator()(const QueueItemPtr left, const QueueItemPtr right) const {
	return left->getPriority() > right->getPriority();
}

QueueItem::Priority QueueItem::calculateAutoPriority() const {
	if(autoPriority) {
		QueueItem::Priority p;
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
	return priority;
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

bool QueueItem::isChunkDownloaded(int64_t startPos, int64_t& len) const {
	if(len <= 0) return false;

	for(SegmentSet::const_iterator i = done.begin(); i != done.end(); ++i) {
		int64_t first  = (*i).getStart();
		int64_t second = (*i).getEnd();

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
		return getTarget() + ".xml.bz2";
	} else {
		return getTarget() + ".xml";
	}
}

/* INTERNAL */
uint8_t QueueItem::getMaxSegments(int64_t filesize) const noexcept {
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

size_t QueueItem::countOnlineUsers() const {
	return count_if(sources.begin(), sources.end(), [](const Source& s) { return s.getUser().user->isOnline(); } );
}

QueueItem::~QueueItem() { }

void QueueItem::getOnlineUsers(HintedUserList& l) const {
	for(auto i = sources.begin(), iend = sources.end(); i != iend; ++i)
		if(i->getUser().user->isOnline())
			l.push_back(i->getUser());
}

void QueueItem::addSource(const HintedUser& aUser, const string& aRemotePath) {
	dcassert(!isSource(aUser.user));
	auto i = getBadSource(aUser);
	if(i != badSources.end()) {
		sources.push_back(*i);
		badSources.erase(i);
	} else {
		sources.push_back(Source(aUser, aRemotePath));
	}
}

void QueueItem::blockSourceHub(const HintedUser& aUser) {
	dcassert(isSource(aUser.user));
	auto s = getSource(aUser.user);
	s->blockedHubs.insert(aUser.hint);
}

bool QueueItem::isHubBlocked(const HintedUser& aUser) {
	auto s = getSource(aUser.user);
	return !s->blockedHubs.empty() && s->blockedHubs.find(aUser.hint) != s->blockedHubs.end();
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
	if (isSet(FLAG_OPEN)) {
		setTempTarget(target);
	} else if(!isSet(QueueItem::FLAG_USER_LIST) && tempTarget.empty()) {
		if(!SETTING(TEMP_DOWNLOAD_DIRECTORY).empty() && (File::getSize(getTarget()) == -1)) {
#ifdef _WIN32
			ParamMap sm;
			if(target.length() >= 3 && target[1] == ':' && target[2] == '\\')
				sm["targetdrive"] = target.substr(0, 3);
			else
				sm["targetdrive"] = Util::getPath(Util::PATH_USER_LOCAL).substr(0, 3);
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
	
	for(auto i = downloads.begin(); i != downloads.end(); i++) {
		totalSpeed += static_cast<int64_t>((*i)->getAverageSpeed());
	}

	return totalSpeed;
}

Segment QueueItem::getNextSegment(int64_t  blockSize, int64_t wantedSize, int64_t lastSpeed, const PartialSource::Ptr partialSource, bool allowOverlap) const {
	if(getSize() == -1 || blockSize == 0) {
		return Segment(0, -1);
	}
	
	if(!BOOLSETTING(MULTI_CHUNK) || blockSize >= size) {
		if(!downloads.empty()) {
			return checkOverlaps(blockSize, lastSpeed, partialSource, allowOverlap);
		}

		int64_t start = 0;
		int64_t end = getSize();

		if(!done.empty()) {
			const Segment& first = *done.begin();

			if(first.getStart() > 0) {
				end = Util::roundUp(first.getStart(), blockSize);
			} else {
				start = Util::roundDown(first.getEnd(), blockSize);

				if(done.size() > 1) {
					const Segment& second = *(++done.begin());
					end = Util::roundUp(second.getStart(), blockSize);
				}
			}
		}

		return Segment(start, std::min(getSize(), end) - start);
	}
	
	if(downloads.size() >= maxSegments ||
		(BOOLSETTING(DONT_BEGIN_SEGMENT) && (size_t)(SETTING(DONT_BEGIN_SEGMENT_SPEED) * 1024) < getAverageSpeed()))
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
		for(PartsInfo::const_iterator i = partialSource->getPartialInfo().begin(); i != partialSource->getPartialInfo().end(); i++)
			posArray.push_back(min(getSize(), (int64_t)(*i) * blockSize));
	}

	/***************************/

	double donePart = static_cast<double>(getDownloadedBytes()) / getSize();
		
	// We want smaller blocks at the end of the transfer, squaring gives a nice curve...
	int64_t targetSize = static_cast<int64_t>(static_cast<double>(wantedSize) * std::max(0.25, (1. - (donePart * donePart))));
		
	if(targetSize > blockSize) {
		// Round off to nearest block size
		targetSize = Util::roundDown(targetSize, blockSize);
	} else {
		targetSize = blockSize;
	}		

	int64_t start = 0;
	int64_t curSize = targetSize;

	while(start < getSize()) {
		int64_t end = std::min(getSize(), start + curSize);
		Segment block(start, end - start);
		bool overlaps = false;
		for(SegmentConstIter i = done.begin(); !overlaps && i != done.end(); ++i) {
			if(curSize <= blockSize) {
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
						dcassert(b % blockSize == 0);
						dcassert(e % blockSize == 0 || e == getSize());

						neededParts.push_back(Segment(b, e - b));
					}
				}
			} else {
				return block;
			}
		}
		
		if(overlaps && (curSize > blockSize)) {
			curSize -= blockSize;
		} else {
			start = end;
			curSize = targetSize;
		}
	}

	if(!neededParts.empty()) {
		// select random chunk for download
		dcdebug("Found chunks: %d\n", neededParts.size());
		
		Segment& selected = neededParts[Util::rand(0, neededParts.size())];
		selected.setSize(std::min(selected.getSize(), targetSize));	// request only wanted size
		
		return selected;
	}

	return checkOverlaps(blockSize, lastSpeed, partialSource, allowOverlap);
}

Segment QueueItem::checkOverlaps(int64_t blockSize, int64_t lastSpeed, const PartialSource::Ptr partialSource, bool allowOverlap) const {
	if(allowOverlap && partialSource == NULL && bundle && BOOLSETTING(OVERLAP_SLOW_SOURCES) && lastSpeed > 0) {
		// overlap slow running chunk
		for(auto i = downloads.begin(); i != downloads.end(); ++i) {
			Download* d = *i;
			
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
			int64_t pos = d->getPos() - (d->getPos() % blockSize);
			int64_t size = d->getSize() - pos;

			// new user should finish this chunk more than 2x faster
			int64_t newChunkLeft = size / lastSpeed;
			if(2 * newChunkLeft < d->getSecondsLeft()) {
				dcdebug("Overlapping... old user: %I64d s, new user: %I64d s\n", d->getSecondsLeft(), newChunkLeft);
				return Segment(d->getStartPos() + pos, size, true);
			}
		}
	}
	return Segment(0, 0);
}

uint64_t QueueItem::getDownloadedSegments() const {
	uint64_t total = 0;
	// count done segments
	for(auto i = done.begin(); i != done.end(); ++i) {
		total += i->getSize();
	}
	return total;
}

uint64_t QueueItem::getDownloadedBytes() const {
	uint64_t total = 0;

	// count done segments
	for(auto i = done.begin(); i != done.end(); ++i) {
		total += i->getSize();
	}

	// count running segments
	for(auto i = downloads.begin(); i != downloads.end(); ++i) {
		total += (*i)->getPos();
	}

	return total;
}

void QueueItem::addFinishedSegment(const Segment& segment) {
	dcassert(segment.getOverlapped() == false);
	//LogManager::getInstance()->message("Adding segment with size " + Util::formatBytes(segment.getSize()) + ", total finished size " + Util::formatBytes(getDownloadedSegments()) + ", QI size " + Util::formatBytes(size), LogManager::LOG_INFO);
	done.insert(segment);

	// Consolidate segments
	if(done.size() == 1) {
		if (bundle)
			bundle->addFinishedSegment(segment.getSize());
		return;
	}
	
	for(auto i = ++done.begin() ; i != done.end(); ) {
		auto prev = i;
		prev--;
		if(prev->getEnd() >= i->getStart()) {
			Segment big(prev->getStart(), i->getEnd() - prev->getStart());
			auto newBytes = big.getSize() - prev->getSize(); //minus the part that has been counted before...
			done.erase(prev);
			done.erase(i++);
			done.insert(big);
			if (bundle)
				bundle->addFinishedSegment(newBytes);
		} else {
			++i;
		}
	}
}

bool QueueItem::isNeededPart(const PartsInfo& partsInfo, int64_t blockSize)
{
	dcassert(partsInfo.size() % 2 == 0);
	
	SegmentConstIter i  = done.begin();
	for(auto j = partsInfo.begin(); j != partsInfo.end(); j+=2){
		while(i != done.end() && (*i).getEnd() <= (*j) * blockSize)
			i++;

		if(i == done.end() || !((*i).getStart() <= (*j) * blockSize && (*i).getEnd() >= (*(j+1)) * blockSize))
			return true;
	}
	
	return false;

}

void QueueItem::getPartialInfo(PartsInfo& partialInfo, int64_t blockSize) const {
	size_t maxSize = min(done.size() * 2, (size_t)510);
	partialInfo.reserve(maxSize);

	SegmentConstIter i = done.begin();
	for(; i != done.end() && partialInfo.size() < maxSize; i++) {

		uint16_t s = (uint16_t)((*i).getStart() / blockSize);
		uint16_t e = (uint16_t)(((*i).getEnd() - 1) / blockSize + 1);

		partialInfo.push_back(s);
		partialInfo.push_back(e);
	}
}

vector<Segment> QueueItem::getChunksVisualisation(int type) const {  // type: 0 - downloaded bytes, 1 - running chunks, 2 - done chunks
	vector<Segment> v;

	switch(type) {
	case 0:
		v.reserve(downloads.size());
		for(auto i = downloads.begin(); i != downloads.end(); ++i) {
			v.push_back((*i)->getSegment());
		}
		break;
	case 1:
		v.reserve(downloads.size());
		for(auto i = downloads.begin(); i != downloads.end(); ++i) {
			v.push_back(Segment((*i)->getStartPos(), (*i)->getPos()));
		}
		break;
	case 2:
		v.reserve(done.size());
		for(auto i = done.begin(); i != done.end(); ++i) {
			v.push_back(*i);
		}
		break;
	}
	return v;
}

bool QueueItem::hasSegment(const UserPtr& aUser, const HubSet& onlineHubs, string& lastError, int64_t wantedSize, int64_t lastSpeed, bool smallSlot, bool allowOverlap) {
	auto source = getSource(aUser);
	if (!source->blockedHubs.empty() && includes(onlineHubs.begin(), onlineHubs.end(), source->blockedHubs.begin(), source->blockedHubs.end())) {
		lastError = STRING(NO_ACCESS_ONLINE_HUBS);
		return false;
	}

	//can't download a filelist if the hub is offline... don't be too strict with NMDC hubs
	if (!aUser->isSet(User::NMDC) && isSet(FLAG_USER_LIST) && onlineHubs.find(source->getUser().hint) == onlineHubs.end()) {
		lastError = STRING(USER_OFFLINE);
		return false;
	}

	dcassert(isSource(aUser));
	if (isFinished()) {
		return false;
	}

	if(smallSlot && !isSet(QueueItem::FLAG_PARTIAL_LIST) && getSize() > 65792) {
		//don't even think of stealing our priority channel
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
		int64_t blockSize = HashManager::getInstance()->getBlockSize(getTTH());
		if(blockSize == 0)
			blockSize = getSize();

		Segment segment = getNextSegment(blockSize, wantedSize, lastSpeed, source->getPartialSource(), allowOverlap);
		if(segment.getSize() == 0) {
			lastError = (segment.getStart() == -1 || getSize() < (SETTING(MIN_SEGMENT_SIZE)*1024)) ? STRING(NO_FILES_AVAILABLE) : STRING(NO_FREE_BLOCK);
			//LogManager::getInstance()->message("NO SEGMENT: " + aUser->getCID().toBase32());
			dcdebug("No segment for %s (%s) in %s, block " I64_FMT "\n", aUser->getCID().toBase32().c_str(), Util::toString(onlineHubs).c_str(), getTarget().c_str(), blockSize);
			return false;
		}
	} else if (!isWaiting()) {
		//don't try to create multiple connections for filelists or files viewed in client
		return false;
	}
	return true;
}

bool QueueItem::startDown() {
	if(bundle && bundle->getPriority() != PAUSED && priority != PAUSED) {
		return true;
	} else if (priority == HIGHEST) {
		return true;
	}
	return false;
}

void QueueItem::searchAlternates() {
	if (SettingsManager::lanMode)
		SearchManager::getInstance()->search(getTargetFileName(), size, SearchManager::TYPE_ANY, SearchManager::SIZE_EXACT, "qa", Search::ALT_AUTO);
	else
		SearchManager::getInstance()->search(tthRoot.toBase32(), 0, SearchManager::TYPE_TTH, SearchManager::SIZE_DONTCARE, "qa", Search::ALT_AUTO);
}

void QueueItem::removeDownload(const string& aToken) {
	auto m = find_if(downloads.begin(), downloads.end(), [&](const Download* d) { return compare(d->getToken(), aToken) == 0; });
	dcassert(m != downloads.end());
	if (m != downloads.end()) {
		downloads.erase(m);
		return;
	}
}

void QueueItem::removeDownloads(const UserPtr& aUser) {
	//erase all downloads from this user
	for(auto i = downloads.begin(); i != downloads.end();) {
		if((*i)->getUser() == aUser) {
			downloads.erase(i);
			i = downloads.begin();
		} else {
			i++;
		}
	}
}


void QueueItem::save(OutputStream &f, string tmp, string b32tmp) {
	string indent = "\t";
	//if (bundle)
	//	indent = "\t\t";

	f.write(indent);
	f.write(LIT("<Download Target=\""));
	f.write(SimpleXML::escape(target, tmp, true));
	f.write(LIT("\" Size=\""));
	f.write(Util::toString(size));
	f.write(LIT("\" Priority=\""));
	f.write(Util::toString((int)priority));
	f.write(LIT("\" Added=\""));
	f.write(Util::toString(added));
	if (!SettingsManager::lanMode) {
		b32tmp.clear();
		f.write(LIT("\" TTH=\""));
		f.write(tthRoot.toBase32(b32tmp));
	}
	if(!done.empty()) {
		f.write(LIT("\" TempTarget=\""));
		f.write(SimpleXML::escape(tempTarget, tmp, true));
	}
	f.write(LIT("\" AutoPriority=\""));
	f.write(Util::toString(autoPriority));
	f.write(LIT("\" MaxSegments=\""));
	f.write(Util::toString(maxSegments));

	f.write(LIT("\">\r\n"));

	for(auto i = done.begin(); i != done.end(); ++i) {
		f.write(indent);
		f.write(LIT("\t<Segment Start=\""));
		f.write(Util::toString(i->getStart()));
		f.write(LIT("\" Size=\""));
		f.write(Util::toString(i->getSize()));
		f.write(LIT("\"/>\r\n"));
	}

	for(auto j = sources.begin(); j != sources.end(); ++j) {
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

		if (SettingsManager::lanMode) {
			f.write(LIT("\" RemotePath=\""));
			f.write(j->getRemotePath());
		}
		f.write(LIT("\"/>\r\n"));
	}

	f.write(indent);
	f.write(LIT("</Download>\r\n"));
}

}
