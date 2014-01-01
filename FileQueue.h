/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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

#include "DirectoryListing.h"
#include "HashBloom.h"
#include "HintedUser.h"
#include "QueueItem.h"

namespace dcpp {

/** All queue items indexed by user (this is a cache for the FileQueue really...) **/
class FileQueue {
public:
	FileQueue() : queueSize(0) { }
	~FileQueue();

	void getBloom(HashBloom& bloom) const noexcept;
	void decreaseSize(uint64_t aSize) { queueSize -= aSize; }
	typedef vector<pair<QueueItem::SourceConstIter, const QueueItemPtr> > PFSSourceList;

	pair<QueueItem::StringMap::const_iterator, bool> add(QueueItemPtr& qi) noexcept;
	pair<QueueItemPtr, bool> add(const string& aTarget, int64_t aSize, Flags::MaskType aFlags, QueueItemBase::Priority p, const string& aTempTarget, time_t aAdded, const TTHValue& root) noexcept;

	QueueItemPtr findFile(const string& target) const noexcept;
	void findFiles(const TTHValue& tth, QueueItemList& ql) const noexcept;
	void matchListing(const DirectoryListing& dl, QueueItem::StringItemList& ql) const noexcept;
	void matchDir(const DirectoryListing::Directory::Ptr& dir, QueueItem::StringItemList& ql) const noexcept;

	// find some PFS sources to exchange parts info
	void findPFSSources(PFSSourceList&) noexcept;

	size_t getSize() noexcept { return queue.size(); }
	QueueItem::StringMap& getQueue() noexcept { return queue; }
	QueueItem::TTHMap& getTTHIndex() noexcept { return tthIndex; }
	void move(QueueItemPtr& qi, const string& aTarget) noexcept;
	void remove(QueueItemPtr& qi) noexcept;
	int isFileQueued(const TTHValue& aTTH) const noexcept;
	QueueItemPtr getQueuedFile(const TTHValue& aTTH) const noexcept;

	uint64_t getTotalQueueSize() const noexcept { return queueSize; }
private:
	QueueItem::StringMap queue;
	QueueItem::TTHMap tthIndex;

	uint64_t queueSize;
};

} // namespace dcpp

#endif // !defined(FILE_QUEUE_H)
