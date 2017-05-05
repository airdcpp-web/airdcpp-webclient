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

#ifndef DCPLUSPLUS_DCPP_FILE_QUEUE_H
#define DCPLUSPLUS_DCPP_FILE_QUEUE_H

#include "stdinc.h"
#include "forward.h"
#include "typedefs.h"

#include "DirectoryListing.h"
#include "DupeType.h"
#include "HashBloom.h"
#include "QueueItem.h"
#include "Util.h"

namespace dcpp {

/** All queue items indexed by user (this is a cache for the FileQueue really...) **/
class FileQueue {
public:
	FileQueue() { }
	~FileQueue();

	void getBloom(HashBloom& bloom) const noexcept;
	typedef vector<pair<QueueItem::SourceConstIter, const QueueItemPtr> > PFSSourceList;

	pair<QueueItem::StringMap::const_iterator, bool> add(QueueItemPtr& qi) noexcept;
	pair<QueueItemPtr, bool> add(const string& aTarget, int64_t aSize, Flags::MaskType aFlags, Priority p, const string& aTempTarget, time_t aAdded, const TTHValue& root) noexcept;

	QueueItemPtr findFile(const string& aTarget) const noexcept;
	QueueItemPtr findFile(QueueToken aToken) const noexcept;

	void findFiles(const TTHValue& tth, QueueItemList& ql_) const noexcept;
	void matchListing(const DirectoryListing& dl, QueueItemList& ql_) const noexcept;
	void matchDir(const DirectoryListing::Directory::Ptr& dir, QueueItemList& ql_) const noexcept;

	// find some PFS sources to exchange parts info
	void findPFSSources(PFSSourceList&) const noexcept;

	size_t getSize() noexcept { return pathQueue.size(); }
	QueueItem::StringMap& getPathQueue() noexcept { return pathQueue; }
	const QueueItem::StringMap& getPathQueue() const noexcept{ return pathQueue; }
	QueueItem::TTHMap& getTTHIndex() noexcept { return tthIndex; }

	void remove(const QueueItemPtr& qi) noexcept;

	DupeType isFileQueued(const TTHValue& aTTH) const noexcept;
	QueueItemPtr getQueuedFile(const TTHValue& aTTH) const noexcept;
private:
	QueueItem::StringMap pathQueue;
	QueueItem::TTHMap tthIndex;
	QueueItem::TokenMap tokenQueue;
};

} // namespace dcpp

#endif // !defined(FILE_QUEUE_H)
