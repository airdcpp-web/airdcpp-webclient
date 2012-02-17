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

#ifndef DCPLUSPLUS_DCPP_QUEUE_ITEM_H
#define DCPLUSPLUS_DCPP_QUEUE_ITEM_H

#include "Pointer.h"
#include "User.h"
#include "FastAlloc.h"
#include "MerkleTree.h"
#include "Flags.h"
#include "forward.h"
#include "Segment.h"
#include "HintedUser.h"
#include "Bundle.h"
#include "GetSet.h"

#include "boost/unordered_map.hpp"

namespace dcpp {

class QueueManager;
class UserQueue;
class FileQueue;
class Download;

class QueueItem : public Flags, public intrusive_ptr_base<QueueItem> {
public:
	typedef boost::unordered_map<string*, QueueItemPtr, noCaseStringHash, noCaseStringEq> StringMap;
	typedef unordered_multimap<TTHValue, QueueItemPtr> TTHMap;

	enum Priority {
		DEFAULT = -1,
		PAUSED = 0,
		LOWEST,
		LOW,
		NORMAL,
		HIGH,
		HIGHEST,
		LAST
	};

	typedef vector<pair<QueueItemPtr, Priority>> PrioList;

	enum FileFlags {
		/** Normal download, no flags set */
		FLAG_NORMAL				= 0x00, 
		/** This is a user file listing download */
		FLAG_USER_LIST			= 0x01,
		/** The file list is downloaded to use for directory download (used with USER_LIST) */
		FLAG_DIRECTORY_DOWNLOAD = 0x02,
		/** The file is downloaded to be viewed in the gui */
		FLAG_CLIENT_VIEW		= 0x04,
		/** Flag to indicate that file should be viewed as a text file */
		FLAG_TEXT				= 0x08,
		/** Match the queue against this list */
		FLAG_MATCH_QUEUE		= 0x10,
		/** The file list downloaded was actually an .xml.bz2 list */
		FLAG_XML_BZLIST			= 0x20,
		/** Only download a part of the file list */
		FLAG_PARTIAL_LIST 		= 0x40,
		/** Find NFO from partial list and view it */
		FLAG_VIEW_NFO			= 0x100,
		/** Recursive partial list */
		FLAG_RECURSIVE_LIST		= 0x200,
		/** TTH list for partial bundle sharing */
		FLAG_TTHLIST_BUNDLE		= 0x400,
		/** A finished bundle item */
		FLAG_FINISHED			= 0x800
	};

	/**
	 * Source parts info
	 * Meaningful only when Source::FLAG_PARTIAL is set
	 */
	class PartialSource : public FastAlloc<PartialSource>, public intrusive_ptr_base<PartialSource> {
	public:
		PartialSource(const string& aMyNick, const string& aHubIpPort, const string& aIp, const string& udp) : 
		  myNick(aMyNick), hubIpPort(aHubIpPort), ip(aIp), udpPort(udp), nextQueryTime(0), pendingQueryCount(0) { }
		
		~PartialSource() { }

		typedef boost::intrusive_ptr<PartialSource> Ptr;

		GETSET(PartsInfo, partialInfo, PartialInfo);
		GETSET(string, myNick, MyNick);			// for NMDC support only
		GETSET(string, hubIpPort, HubIpPort);
		GETSET(string, ip, Ip);
		GETSET(uint64_t, nextQueryTime, NextQueryTime);
		GETSET(string, udpPort, UdpPort);
		GETSET(uint8_t, pendingQueryCount, PendingQueryCount);
	};

	class Source : public Flags {
	public:
		enum {
			FLAG_NONE				= 0x00,
			FLAG_FILE_NOT_AVAILABLE = 0x01,
			FLAG_PASSIVE			= 0x02,
			FLAG_REMOVED			= 0x04,
			FLAG_NO_TTHF			= 0x08,
			FLAG_BAD_TREE			= 0x10,
			FLAG_SLOW_SOURCE		= 0x20,
			FLAG_NO_TREE			= 0x40,
			FLAG_NO_NEED_PARTS		= 0x80,
			FLAG_PARTIAL			= 0x100,
			FLAG_TTH_INCONSISTENCY	= 0x200,
			FLAG_UNTRUSTED			= 0x400,
			FLAG_MASK				= FLAG_FILE_NOT_AVAILABLE
				| FLAG_PASSIVE | FLAG_REMOVED | FLAG_BAD_TREE | FLAG_SLOW_SOURCE
				| FLAG_NO_TREE | FLAG_TTH_INCONSISTENCY | FLAG_UNTRUSTED
		};

		Source(const HintedUser& aUser) : user(aUser), partialSource(nullptr) { }
		Source(const Source& aSource) : Flags(aSource), user(aSource.user), partialSource(aSource.partialSource) { }

		bool operator==(const UserPtr& aUser) const { return user == aUser; }
		PartialSource::Ptr& getPartialSource() { return partialSource; }

		GETSET(HintedUser, user, User);
		GETSET(PartialSource::Ptr, partialSource, PartialSource);
	};

	typedef vector<Source> SourceList;
	typedef SourceList::iterator SourceIter;

	typedef SourceList::const_iterator SourceConstIter;

	typedef set<Segment> SegmentSet;
	typedef SegmentSet::const_iterator SegmentConstIter;
	
	QueueItem(const string& aTarget, int64_t aSize, Priority aPriority, Flags::MaskType aFlag, time_t aAdded, const TTHValue& tth, const string& aTempTarget);

	QueueItem(const QueueItem& rhs) : 
		Flags(rhs), done(rhs.done), downloads(rhs.downloads), target(rhs.target), 
		size(rhs.size), priority(rhs.priority), added(rhs.added), tthRoot(rhs.tthRoot),
		autoPriority(rhs.autoPriority), maxSegments(rhs.maxSegments), fileBegin(rhs.fileBegin),
		sources(rhs.sources), badSources(rhs.badSources), tempTarget(rhs.tempTarget), nextPublishingTime(rhs.nextPublishingTime)
	{ }

	~QueueItem();

	void save(OutputStream &save, string tmp, string b32tmp);
	size_t countOnlineUsers() const;
	void getOnlineUsers(HintedUserList& l) const;
	bool hasSegment(const UserPtr& aUser, string& lastError, int64_t wantedSize, int64_t lastSpeed, bool smallSlot, bool allowOverlap);

	string getFolder() const { return Util::getDir(target, false, false); };
	SourceList& getSources() { return sources; }
	const SourceList& getSources() const { return sources; }
	SourceList& getBadSources() { return badSources; }
	const SourceList& getBadSources() const { return badSources; }

	string getTargetFileName() const { return Util::getFileName(getTarget()); }

	SourceIter getSource(const UserPtr& aUser) { return find(sources.begin(), sources.end(), aUser); }
	SourceIter getBadSource(const UserPtr& aUser) { return find(badSources.begin(), badSources.end(), aUser); }
	SourceConstIter getSource(const UserPtr& aUser) const { return find(sources.begin(), sources.end(), aUser); }
	SourceConstIter getBadSource(const UserPtr& aUser) const { return find(badSources.begin(), badSources.end(), aUser); }

	bool isSource(const UserPtr& aUser) const { return getSource(aUser) != sources.end(); }
	bool isBadSource(const UserPtr& aUser) const { return getBadSource(aUser) != badSources.end(); }
	bool isBadSourceExcept(const UserPtr& aUser, Flags::MaskType exceptions) const {
		SourceConstIter i = getBadSource(aUser);
		if(i != badSources.end())
			return i->isAnySet((Flags::MaskType)(exceptions^Source::FLAG_MASK));
		return false;
	}
	
	vector<Segment> getChunksVisualisation(int type) const;

	bool isChunkDownloaded(int64_t startPos, int64_t& len) const {
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

	/**
	 * Is specified parts needed by this download?
	 */
	bool isNeededPart(const PartsInfo& partsInfo, int64_t blockSize);

	/**
	 * Get shared parts info, max 255 parts range pairs
	 */
	void getPartialInfo(PartsInfo& partialInfo, int64_t blockSize) const;

	uint64_t getDownloadedBytes() const;
	uint64_t getDownloadedSegments() const;
	double getDownloadedFraction() const { return static_cast<double>(getDownloadedBytes()) / getSize(); }
	
	DownloadList& getDownloads() { return downloads; }
	void removeDownload(const string& aToken);
	void removeDownloads(const UserPtr& aUser);
	
	/** Next segment that is not done and not being downloaded, zero-sized segment returned if there is none is found */
	Segment getNextSegment(int64_t blockSize, int64_t wantedSize, int64_t lastSpeed, const PartialSource::Ptr partialSource, bool allowOverlap) const;
	Segment checkOverlaps(int64_t blockSize, int64_t lastSpeed, const PartialSource::Ptr partialSource, bool allowOverlap) const;
	
	void addSegment(const Segment& segment, bool downloaded, bool finishedQI=false);
	void resetDownloaded() { done.clear(); }
	
	bool isFinished() const {
		return done.size() == 1 && *done.begin() == Segment(0, getSize());
	}

	bool isRunning() const {
		return !isWaiting();
	}
	bool isWaiting() const {
		return downloads.empty();
	}

	string getListName() const {
		dcassert(isSet(QueueItem::FLAG_USER_LIST));
		if(isSet(QueueItem::FLAG_XML_BZLIST)) {
			return getTarget() + ".xml.bz2";
		} else {
			return getTarget() + ".xml";
		}
	}

	const string& getTempTarget();
	void setTempTarget(const string& aTempTarget) { tempTarget = aTempTarget; }

	GETSET(TTHValue, tthRoot, TTH);
	GETSET(SegmentSet, done, Done);	
	GETSET(DownloadList, downloads, Downloads);
	GETSET(string, target, Target);
	GETSET(uint64_t, fileBegin, FileBegin);
	GETSET(uint64_t, nextPublishingTime, NextPublishingTime);
	GETSET(int64_t, size, Size);
	GETSET(time_t, added, Added);
	GETSET(Priority, priority, Priority);
	GETSET(uint8_t, maxSegments, MaxSegments);
	GETSET(bool, autoPriority, AutoPriority);
	GETSET(BundlePtr, bundle, Bundle);
	
	QueueItem::Priority calculateAutoPriority() const {
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
					default:
						p = QueueItem::NORMAL;
						break;
					case 6:
					case 7:
					case 8:
						p = QueueItem::HIGH;
						break;
					case 9:
					case 10:
						p = QueueItem::HIGHEST;			
						break;
			}
			return p;			
		}
		return priority;
	}

	uint64_t getAverageSpeed() const;

private:
	QueueItem& operator=(const QueueItem&);

	friend class QueueManager;
	friend class UserQueue;
	SourceList sources;
	SourceList badSources;
	string tempTarget;

	void addSource(const HintedUser& aUser);
	void removeSource(const UserPtr& aUser, Flags::MaskType reason);
	uint8_t getMaxSegments(int64_t filesize) const noexcept;
};

} // namespace dcpp

#endif // !defined(QUEUE_ITEM_H)

/**
* @file
* $Id: QueueItem.h 568 2011-07-24 18:28:43Z bigmuscle $
*/
