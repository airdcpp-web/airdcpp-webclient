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

#include <airdcpp/queue/FileQueue.h>
#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/util/text/Text.h>
#include <airdcpp/core/timer/TimerManager.h>

namespace dcpp {

using ranges::for_each;
using ranges::copy;

FileQueue::~FileQueue() { }

pair<QueueItemPtr, bool> FileQueue::add(const string& aTarget, int64_t aSize, Flags::MaskType aFlags, Priority p,
	const string& aTempTarget, time_t aAdded, const TTHValue& root) noexcept {

	auto qi = make_shared<QueueItem>(aTarget, aSize, p, aFlags, aAdded, root, aTempTarget);
	auto [iter, added] = add(qi);
	return { (added ? qi : iter->second), added };
}

pair<QueueItem::StringMap::const_iterator, bool> FileQueue::add(QueueItemPtr& qi) noexcept {
	auto ret = pathQueue.try_emplace(const_cast<string*>(&qi->getTarget()), qi);
	if (ret.second) {
		qi->setStatus(QueueItem::STATUS_QUEUED);
		tthIndex.emplace(const_cast<TTHValue*>(&qi->getTTH()), qi);
		tokenQueue.try_emplace(qi->getToken(), qi);
	}
	return ret;
}

void FileQueue::remove(const QueueItemPtr& qi) noexcept {
	//TargetMap
	if (auto f = pathQueue.find(const_cast<string*>(&qi->getTarget())); f != pathQueue.end()) {
		pathQueue.erase(f);
	}

	//TTHIndex
	auto s = tthIndex.equal_range(const_cast<TTHValue*>(&qi->getTTH()));
	dcassert(s.first != s.second);

	auto k = ranges::find(s | pair_to_range | views::values, qi);
	if (k.base() != s.second) {
		tthIndex.erase(k.base());
	}

	// Tokens
	tokenQueue.erase(qi->getToken());
}

QueueItemPtr FileQueue::findFile(const string& target) const noexcept {
	auto i = pathQueue.find(const_cast<string*>(&target));
	return (i == pathQueue.end()) ? nullptr : i->second;
}

QueueItemPtr FileQueue::findFile(QueueToken aToken) const noexcept {
	auto i = tokenQueue.find(aToken);
	return (i == tokenQueue.end()) ? nullptr : i->second;
}

void FileQueue::findFiles(const TTHValue& tth, QueueItemList& ql_) const noexcept {
	ranges::copy(tthIndex.equal_range(const_cast<TTHValue*>(&tth)) | pair_to_range | views::values, back_inserter(ql_));
}

void FileQueue::matchListing(const DirectoryListing& dl, QueueItemList& ql_) const noexcept {
	matchDir(dl.getRoot(), ql_);
}

void FileQueue::matchDir(const DirectoryListing::Directory::Ptr& aDir, QueueItemList& ql_) const noexcept {
	for (const auto& d : aDir->directories | views::values) {
		if (!d->isVirtual()) {
			matchDir(d, ql_);
		}
	}

	for (const auto& f : aDir->files) {
		auto tthRange = tthIndex.equal_range(const_cast<TTHValue*>(&f->getTTH()));

		ranges::for_each(tthRange | pair_to_range, [&](const pair<TTHValue*, QueueItemPtr>& tqp) {
			if (!tqp.second->isDownloaded() && tqp.second->getSize() == f->getSize() && ranges::find(ql_, tqp.second) == ql_.end()) {
				ql_.push_back(tqp.second);
			}
		});
	}
}

DupeType FileQueue::isFileQueued(const TTHValue& aTTH) const noexcept {
	if (auto qi = getQueuedFile(aTTH); qi) {
		return (qi->isDownloaded() ? DUPE_FINISHED_FULL : DUPE_QUEUE_FULL);
	}
	return DUPE_NONE;
}

QueueItemPtr FileQueue::getQueuedFile(const TTHValue& aTTH) const noexcept {
	auto p = tthIndex.find(const_cast<TTHValue*>(&aTTH));
	return p != tthIndex.end() ? p->second : nullptr;
}

} //dcpp
