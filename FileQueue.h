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

#ifndef DCPLUSPLUS_DCPP_FILE_QUEUE_H
#define DCPLUSPLUS_DCPP_FILE_QUEUE_H

#include "stdinc.h"

#include "forward.h"
#include "typedefs.h"
#include "HintedUser.h"
#include "QueueItem.h"
#include "DirectoryListing.h"

#include "boost/unordered_map.hpp"

namespace dcpp {

/** All queue items indexed by user (this is a cache for the FileQueue really...) **/
class FileQueue {
public:
	FileQueue() : targetMapInsert(queue.end()), queueSize(0) { }
	~FileQueue();

	typedef vector<pair<QueueItem::SourceConstIter, const QueueItem*> > PFSSourceList;

	void add(QueueItem* qi, bool addFinished, bool addTTH = true);
	QueueItem* add(const string& aTarget, int64_t aSize, Flags::MaskType aFlags, QueueItem::Priority p, const string& aTempTarget, time_t aAdded, const TTHValue& root);

	QueueItem* find(const string& target);
	void find(QueueItemList& sl, int64_t aSize, const string& ext);
	uint8_t getMaxSegments(int64_t filesize) const;
	void find(StringList& sl, int64_t aSize, const string& ext);
	void find(const TTHValue& tth, QueueItemList& ql);
	void matchDir(const DirectoryListing::Directory* dir, QueueItemList& ql) noexcept;

	// find some PFS sources to exchange parts info
	void findPFSSources(PFSSourceList&);

	size_t getSize() { return queue.size(); }
	QueueItem::StringMap& getQueue() { return queue; }
	QueueItem::TTHMap& getTTHIndex() { return tthIndex; }
	void move(QueueItem* qi, const string& aTarget);
	void remove(QueueItem* qi, bool removeTTH);
	void removeTTH(QueueItem* qi);
	int isFileQueued(const TTHValue& aTTH, const string& aFile);
	QueueItem* getQueuedFile(const TTHValue& aTTH, const string& aFile);

	uint64_t getTotalQueueSize() { return queueSize; };
private:
	QueueItem::StringMap queue;
	QueueItem::TTHMap tthIndex;

	uint64_t queueSize;
	QueueItem::StringMap::iterator targetMapInsert;
};

} // namespace dcpp

#endif // !defined(FILE_QUEUE_H)
