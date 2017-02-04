/*
 * Copyright (C) 2001-2017 Jacek Sieka, arnetheduck on gmail point com
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

#include "QueueItemBase.h"

#include "FastAlloc.h"
#include "HintedUser.h"
#include "MerkleTree.h"
#include "Segment.h"
#include "Util.h"

namespace dcpp {

class QueueItem : public QueueItemBase {
public:
	typedef unordered_map<QueueToken, QueueItemPtr> TokenMap;
	typedef unordered_map<string*, QueueItemPtr, noCaseStringHash, noCaseStringEq> StringMap;
	typedef unordered_multimap<TTHValue*, QueueItemPtr> TTHMap;
	typedef vector<pair<QueueItemPtr, bool>> ItemBoolList;

	struct Hash {
		size_t operator()(const QueueItemPtr& x) const noexcept { return hash<string>()(x->getTarget()); }
	};

	struct HashComp {
		HashComp(const TTHValue& s) : a(s) { }
		bool operator()(const QueueItemPtr& q) const noexcept { return a == q->getTTH(); }

		HashComp& operator=(const HashComp&) = delete;
	private:
		const TTHValue& a;
	};

	struct AlphaSortOrder {
		bool operator()(const QueueItemPtr& left, const QueueItemPtr& right) const noexcept;
	};

	struct SizeSortOrder {
		bool operator()(const QueueItemPtr& left, const QueueItemPtr& right) const noexcept;
	};

	struct PrioSortOrder {
		bool operator()(const QueueItemPtr& left, const QueueItemPtr& right) const noexcept;
	};

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
		/** Open directly with an external program after the file has been downloaded */
		FLAG_OPEN				= 0x80,
		/** Find NFO from partial list and view it */
		FLAG_VIEW_NFO			= 0x100,
		/** Recursive partial list */
		FLAG_RECURSIVE_LIST		= 0x200,
		/** TTH list for partial bundle sharing */
		FLAG_TTHLIST_BUNDLE		= 0x400,
		/** All segments have been downloaded */
		FLAG_DOWNLOADED			= 0x800,
		/** The dctmp extension has been removed */
		FLAG_MOVED				= 0x1000,
		/** A private file that won't be added in share and it's not available via partial sharing */
		FLAG_PRIVATE			= 0x8000,
		/** Associated to a specific bundle for matching */
		FLAG_MATCH_BUNDLE		= 0x16000
	};

	/**
	 * Source parts info
	 * Meaningful only when Source::FLAG_PARTIAL is set
	 */
	class PartialSource : public FastAlloc<PartialSource> {
	public:
		PartialSource(const string& aMyNick, const string& aHubIpPort, const string& aIp, const string& udp) : 
			myNick(aMyNick), hubIpPort(aHubIpPort), ip(aIp), nextQueryTime(0), udpPort(udp), pendingQueryCount(0) {}
		
		~PartialSource() { }

		typedef shared_ptr<PartialSource> Ptr;

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
				| FLAG_REMOVED | FLAG_BAD_TREE | FLAG_SLOW_SOURCE
				| FLAG_NO_TREE | FLAG_TTH_INCONSISTENCY | FLAG_UNTRUSTED
		};

		Source(const HintedUser& aUser) : user(aUser), partialSource(nullptr) { }
		//Source(const Source& aSource) : Flags(aSource), user(aSource.user), partialSource(aSource.partialSource), remotePath(aSource.remotePath) { }

		bool operator==(const UserPtr& aUser) const { return user == aUser; }
		PartialSource::Ptr& getPartialSource() { return partialSource; }

		GETSET(HintedUser, user, User);
		IGETSET(PartialSource::Ptr, partialSource, PartialSource, nullptr);
		//GETSET(string, remotePath, RemotePath);
		OrderedStringSet blockedHubs;
		bool updateHubUrl(const OrderedStringSet& aOnlineHubs, string& hubUrl_, bool aIsFileList) noexcept;
	};

	typedef vector<Source> SourceList;
	typedef SourceList::iterator SourceIter;

	typedef SourceList::const_iterator SourceConstIter;

	typedef set<Segment> SegmentSet;
	typedef SegmentSet::const_iterator SegmentConstIter;
	
	QueueItem(const string& aTarget, int64_t aSize, Priority aPriority, Flags::MaskType aFlag, time_t aAdded, const TTHValue& tth, const string& aTempTarget);

	~QueueItem();

	bool usesSmallSlot() const noexcept;

	// Select a random item from the list to search for alternates
	static QueueItemPtr pickSearchItem(const QueueItemList& aItems) noexcept;

	void save(OutputStream &save, string tmp, string b32tmp);
	int countOnlineUsers() const noexcept;
	void getOnlineUsers(HintedUserList& l) const noexcept;
	bool hasSegment(const UserPtr& aUser, const OrderedStringSet& onlineHubs, string& lastError, int64_t wantedSize, int64_t lastSpeed, DownloadType aType, bool allowOverlap) noexcept;
	bool isPausedPrio() const noexcept;

	SourceList& getSources() noexcept { return sources; }
	const SourceList& getSources() const noexcept { return sources; }
	SourceList& getBadSources() noexcept { return badSources; }
	const SourceList& getBadSources() const noexcept { return badSources; }

	string getTargetFileName() const noexcept { return Util::getFileName(target); }
	string getFilePath() const noexcept { return Util::getFilePath(target); }

	SourceIter getSource(const UserPtr& aUser) noexcept { return find(sources.begin(), sources.end(), aUser); }
	SourceIter getBadSource(const UserPtr& aUser) noexcept { return find(badSources.begin(), badSources.end(), aUser); }
	SourceConstIter getSource(const UserPtr& aUser) const noexcept { return find(sources.begin(), sources.end(), aUser); }
	SourceConstIter getBadSource(const UserPtr& aUser) const noexcept { return find(badSources.begin(), badSources.end(), aUser); }

	bool isSource(const UserPtr& aUser) const noexcept { return getSource(aUser) != sources.end(); }
	bool isBadSource(const UserPtr& aUser) const noexcept { return getBadSource(aUser) != badSources.end(); }
	bool isBadSourceExcept(const UserPtr& aUser, Flags::MaskType exceptions, bool& isBad_) const noexcept;
	
	void getChunksVisualisation(vector<Segment>& running, vector<Segment>& downloaded, vector<Segment>& done) const noexcept;

	bool isChunkDownloaded(int64_t startPos, int64_t& len) const noexcept;

	/**
	 * Is specified parts needed by this download?
	 */
	bool isNeededPart(const PartsInfo& partsInfo, int64_t blockSize) const noexcept;

	/**
	 * Get shared parts info, max 255 parts range pairs
	 */
	void getPartialInfo(PartsInfo& partialInfo, int64_t blockSize) const noexcept;

	uint64_t getDownloadedBytes() const noexcept;
	uint64_t getDownloadedSegments() const noexcept;
	double getDownloadedFraction() const noexcept;
	
	void addDownload(Download* d) noexcept;

	// Erase a single download
	void removeDownload(const string& aToken) noexcept;

	// Erase all downloads from this user
	void removeDownloads(const UserPtr& aUser) noexcept;
	
	/** Next segment that is not done and not being downloaded, zero-sized segment returned if there is none is found */
	Segment getNextSegment(int64_t blockSize, int64_t wantedSize, int64_t aLastSpeed, const PartialSource::Ptr& aPartialSource, bool allowOverlap) const noexcept;
	Segment checkOverlaps(int64_t blockSize, int64_t aLastSpeed, const PartialSource::Ptr& aPartialSource, bool allowOverlap) const noexcept;
	
	void addFinishedSegment(const Segment& segment) noexcept;
	void resetDownloaded() noexcept;
	
	// Check that all segments have been downloaded (unsafe)
	bool segmentsDone() const noexcept;

	// The file has been flagged as downloaded
	bool isDownloaded() const noexcept;

	bool isRunning() const noexcept {
		return !isWaiting();
	}
	bool isWaiting() const noexcept {
		return downloads.empty();
	}

	bool hasPartialSharingTarget() noexcept;

	string getListName() const noexcept;
	string getStatusString(int64_t aDownloadedBytes, bool aIsWaiting) const noexcept;

	const string& getTempTarget() noexcept;
	void setTempTarget(const string& aTempTarget) noexcept { tempTarget = aTempTarget; }

	GETSET(TTHValue, tthRoot, TTH);
	GETSET(SegmentSet, done, Done);	
	IGETSET(uint64_t, fileBegin, FileBegin, 0);
	IGETSET(uint64_t, nextPublishingTime, NextPublishingTime, 0);
	IGETSET(uint8_t, maxSegments, MaxSegments, 1);
	IGETSET(BundlePtr, bundle, Bundle, nullptr);
	IGETSET(string, lastSource, LastSource, Util::emptyString);
	
	Priority calculateAutoPriority() const noexcept;

	uint64_t getAverageSpeed() const noexcept;
	uint64_t getSecondsLeft() const noexcept;

	int64_t getBlockSize() noexcept;
	void setBlockSize(int64_t aBlockSize) noexcept { blockSize = aBlockSize; }

	QueueItem& operator=(const QueueItem&) = delete;
private:
	friend class QueueManager;
	friend class UserQueue;
	SourceList sources;
	SourceList badSources;
	string tempTarget;

	void addSource(const HintedUser& aUser) noexcept;
	void blockSourceHub(const HintedUser& aUser) noexcept;
	bool isHubBlocked(const UserPtr& aUser, const string& aUrl) const noexcept;
	void removeSource(const UserPtr& aUser, Flags::MaskType reason) noexcept;
	uint8_t getMaxSegments(int64_t filesize) const noexcept;

	int64_t blockSize = -1;
};

} // namespace dcpp

#endif // !defined(QUEUE_ITEM_H)