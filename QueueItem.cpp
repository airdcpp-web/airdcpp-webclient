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
#include "QueueItem.h"

#include "SimpleXml.h"
#include "ClientManager.h"
#include "HashManager.h"
#include "Download.h"
#include "File.h"
#include "Util.h"

#include "AirUtil.h"
#include "Wildcards.h"

namespace dcpp {

namespace {
	const string TEMP_EXTENSION = ".dctmp";

	string getTempName(const string& aFileName, const TTHValue& aRoot) {
		string tmp(aFileName);
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

	if(priority != HIGHEST && BOOLSETTING(HIGHEST_PRIORITY_USE_REGEXP) ? AirUtil::stringRegexMatch(SETTING(HIGH_PRIO_FILES), Util::getFileName(aTarget)) :
		Wildcard::patternMatch(Text::utf8ToAcp(Util::getFileName(aTarget)), Text::utf8ToAcp(SETTING(HIGH_PRIO_FILES)), '|')) {
		priority = QueueItem::HIGH;
	} else if(priority == QueueItem::DEFAULT) {
		if(aSize <= SETTING(PRIO_HIGHEST_SIZE)*1024) {
			priority = QueueItem::HIGHEST;
		} else if(aSize <= SETTING(PRIO_HIGH_SIZE)*1024) {
			priority = QueueItem::HIGH;
		} else if(aSize <= SETTING(PRIO_NORMAL_SIZE)*1024) {
			priority = QueueItem::NORMAL;
		} else if(aSize <= SETTING(PRIO_LOW_SIZE)*1024) {
			priority = QueueItem::LOW;
		} else if(SETTING(PRIO_LOWEST)) {
			priority = QueueItem::LOWEST;
		}
	}

	setFlag(FLAG_AUTODROP);

	if(isSet(FLAG_USER_LIST) || isSet(FLAG_CLIENT_VIEW)) {
		/* Always use highest for the items without bundle */
		priority = QueueItem::HIGHEST;
	} else {
		maxSegments = getMaxSegments(aSize);
		if(priority == QueueItem::DEFAULT) {
			if(BOOLSETTING(AUTO_PRIORITY_DEFAULT)) {
				autoPriority = true;
				priority = LOW;
			} else {
				priority = NORMAL;
			}
		}
	}

	if(!Util::fileExists(aTempTarget) && Util::fileExists(aTempTarget + ".antifrag")) {
		// load old antifrag file
		File::renameFile(aTempTarget + ".antifrag", tempTarget);
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
	size_t n = 0;
	for(SourceConstIter i = sources.begin(), iend = sources.end(); i != iend; ++i) {
		if(i->getUser().user->isOnline())
			n++;
	}
	return n;
}

QueueItem::~QueueItem() {
	//bla
}

void QueueItem::getOnlineUsers(HintedUserList& l) const {
	for(SourceConstIter i = sources.begin(), iend = sources.end(); i != iend; ++i)
		if(i->getUser().user->isOnline())
			l.push_back(i->getUser());
}

void QueueItem::addSource(const HintedUser& aUser) {
	dcassert(!isSource(aUser.user));
	SourceIter i = getBadSource(aUser);
	if(i != badSources.end()) {
		sources.push_back(*i);
		badSources.erase(i);
	} else {
		sources.push_back(Source(aUser));
	}
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
	if(!isSet(QueueItem::FLAG_USER_LIST) && tempTarget.empty()) {
		if(!SETTING(TEMP_DOWNLOAD_DIRECTORY).empty() && (File::getSize(getTarget()) == -1)) {
#ifdef _WIN32
			dcpp::StringMap sm;
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
				for(vector<int64_t>::const_iterator j = posArray.begin(); j < posArray.end(); j += 2){
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

void QueueItem::addSegment(const Segment& segment, bool downloaded) {
	dcassert(segment.getOverlapped() == false);
	done.insert(segment);
	//cache for bundles
	if (bundle) {
		bundle->addSegment(segment.getSize(), downloaded);
	}

	// Consolidate segments
	if(done.size() == 1)
		return;
	
	for(SegmentSet::iterator i = ++done.begin() ; i != done.end(); ) {
		SegmentSet::iterator prev = i;
		prev--;
		if(prev->getEnd() >= i->getStart()) {
			Segment big(prev->getStart(), i->getEnd() - prev->getStart());
			done.erase(prev);
			done.erase(i++);
			done.insert(big);
		} else {
			++i;
		}
	}
}

bool QueueItem::isNeededPart(const PartsInfo& partsInfo, int64_t blockSize)
{
	dcassert(partsInfo.size() % 2 == 0);
	
	SegmentConstIter i  = done.begin();
	for(PartsInfo::const_iterator j = partsInfo.begin(); j != partsInfo.end(); j+=2){
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
		for(DownloadList::const_iterator i = downloads.begin(); i != downloads.end(); ++i) {
			v.push_back((*i)->getSegment());
		}
		break;
	case 1:
		v.reserve(downloads.size());
		for(DownloadList::const_iterator i = downloads.begin(); i != downloads.end(); ++i) {
			v.push_back(Segment((*i)->getStartPos(), (*i)->getPos()));
		}
		break;
	case 2:
		v.reserve(done.size());
		for(SegmentSet::const_iterator i = done.begin(); i != done.end(); ++i) {
			v.push_back(*i);
		}
		break;
	}
	return v;
}

bool QueueItem::hasSegment(const UserPtr& aUser, string& lastError, int64_t wantedSize, int64_t lastSpeed, bool smallSlot, bool allowOverlap) {
	QueueItem::SourceConstIter source = getSource(aUser);
	dcassert(isSource(aUser));
	//dcassert(!isFinished());
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
			dcdebug("No segment for %s in %s, block " I64_FMT "\n", aUser->getCID().toBase32().c_str(), getTarget().c_str(), blockSize);
			return false;
		}
	} else if (!isWaiting()) {
		//don't try to create multiple connections for filelists or files viewed in client
		return false;
	}
	return true;
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
	b32tmp.clear();
	f.write(LIT("\" TTH=\""));
	f.write(tthRoot.toBase32(b32tmp));
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
		f.write(LIT("\"/>\r\n"));
	}

	f.write(indent);
	f.write(LIT("</Download>\r\n"));
}

}
