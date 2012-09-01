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
#include "boost\regex.hpp"

#include "FileQueue.h"
#include "SettingsManager.h"
#include "Text.h"
#include "QueueManager.h"
#include "Util.h"
#include "Pointer.h"

#include "noexcept.h"

#include <boost/range/algorithm/find_if.hpp>

namespace dcpp {

FileQueue::~FileQueue() { }

QueueItemPtr FileQueue::add(const string& aTarget, int64_t aSize, Flags::MaskType aFlags, QueueItem::Priority p, 
	const string& aTempTarget, time_t aAdded, const TTHValue& root) noexcept {

	QueueItemPtr qi = new QueueItem(aTarget, aSize, p, aFlags, aAdded, root, aTempTarget);
	dcassert(find(aTarget) == NULL);
	add(qi);
	return qi;
}

void FileQueue::add(QueueItemPtr qi) noexcept {
	if (!qi->isSet(QueueItem::FLAG_USER_LIST) && !qi->isSet(QueueItem::FLAG_CLIENT_VIEW) && !qi->isSet(QueueItem::FLAG_FINISHED)) {
		dcassert(qi->getSize() >= 0);
		queueSize += qi->getSize();
	}
	dcassert(queueSize >= 0);
	queue.insert(make_pair(const_cast<string*>(&qi->getTarget()), qi));
	tthIndex.insert(make_pair(qi->getTTH(), qi));
}

void FileQueue::remove(QueueItemPtr qi) noexcept {
	//TargetMap
	auto f = queue.find(const_cast<string*>(&qi->getTarget()));
	if (f != queue.end()) {
		queue.erase(f);
		if (!qi->isSet(QueueItem::FLAG_USER_LIST) && !qi->isSet(QueueItem::FLAG_FINISHED) && !qi->isSet(QueueItem::FLAG_CLIENT_VIEW)) {
			dcassert(qi->getSize() >= 0);
			queueSize -= qi->getSize();
		}
	}
	dcassert(queueSize >= 0);

	//TTHIndex
	auto s = tthIndex.equal_range(qi->getTTH());
	dcassert(s.first != s.second);
	auto k = find_if(s.first, s.second, CompareSecond<TTHValue, QueueItemPtr>(qi));
	if (k != s.second) {
		tthIndex.erase(k);
	}
}

QueueItemPtr FileQueue::find(const string& target) noexcept {
	auto i = queue.find(const_cast<string*>(&target));
	return (i == queue.end()) ? NULL : i->second;
}

void FileQueue::find(const TTHValue& tth, QueueItemList& ql) noexcept {
	auto s = tthIndex.equal_range(tth);
	if (s.first != s.second) {
		for_each(s.first, s.second, [&](pair<TTHValue, QueueItemPtr> tqp) { ql.push_back(tqp.second); } );
	}
}

void FileQueue::find(const string& aFileName, int64_t aSize, QueueItemList& ql) noexcept {
	for(auto j = tthIndex.begin(); j != tthIndex.end(); ++j) {
		if (j->second->getSize() == aSize && j->second->getTargetFileName() == aFileName) {
			ql.push_back(j->second);
		}
	}
}

void FileQueue::matchListing(const DirectoryListing& dl, QueueItem::StringList& ql) {
	if (SettingsManager::lanMode) {
		QueueItem::StringMultiMap qsm;
		for(auto j = tthIndex.begin(); j != tthIndex.end(); ++j) {
			qsm.insert(make_pair(j->second->getTargetFileName(), j->second));
		}
		matchDir(dl.getRoot(), ql, qsm);
	} else {
		matchDir(dl.getRoot(), ql);
	}
}

void FileQueue::matchDir(const DirectoryListing::Directory* dir, QueueItem::StringList& ql) {
	for(auto j = dir->directories.begin(); j != dir->directories.end(); ++j) {
		if(!(*j)->getAdls())
			matchDir(*j, ql);
	}

	for(auto i = dir->files.begin(); i != dir->files.end(); ++i) {
		auto s = tthIndex.equal_range((*i)->getTTH());
		if (s.first != s.second) {
			DirectoryListing::File* f = *i;
			for_each(s.first, s.second, [f, &ql](const pair<TTHValue, QueueItemPtr> tqp) {
				if (!tqp.second->isFinished() && tqp.second->getSize() == f->getSize() && boost::find_if(ql, CompareSecond<string, QueueItemPtr>(tqp.second)) == ql.end()) 
					ql.push_back(make_pair(Util::emptyString, tqp.second)); } );
		}
	}
}

void FileQueue::matchDir(const DirectoryListing::Directory* dir, QueueItem::StringList& ql, const QueueItem::StringMultiMap& qsm) {
	for(auto j = dir->directories.begin(); j != dir->directories.end(); ++j) {
		if(!(*j)->getAdls())
			matchDir(*j, ql, qsm);
	}

	for(auto i = dir->files.begin(); i != dir->files.end(); ++i) {
		auto s = qsm.equal_range((*i)->getName());
		if (s.first != s.second) {
			DirectoryListing::File* f = *i;
			for_each(s.first, s.second, [f, &ql](const pair<string, QueueItemPtr> tqp) {
				if (tqp.second->getSize() == f->getSize() && !tqp.second->isFinished() && boost::find_if(ql, CompareSecond<string, QueueItemPtr>(tqp.second)) == ql.end()) 
					ql.push_back(make_pair(f->getPath(), tqp.second)); } );
		}
	}
}

int FileQueue::isFileQueued(const TTHValue& aTTH, const string& fileName) noexcept {
	QueueItemPtr qi = getQueuedFile(aTTH, fileName);
	if (qi) {
		return (qi->isFinished() ? 2 : 1);
	}
	return 0;
}

int FileQueue::isFileQueued(const string& aFileName, int64_t aSize) noexcept {
	QueueItemPtr qi = getQueuedFile(aFileName, aSize);
	if (qi) {
		return (qi->isFinished() ? 2 : 1);
	}
	return 0;
}

QueueItemPtr FileQueue::getQueuedFile(const string& aFileName, int64_t aSize) noexcept {
	auto k = boost::find_if(tthIndex, [&aFileName, aSize](pair<TTHValue, QueueItemPtr> tqp) { return stricmp(aFileName.c_str(), tqp.second->getTargetFileName().c_str()) == 0 && tqp.second->getSize() == aSize; });
	if (k != tthIndex.end()) {
		return k->second;
	}
	return nullptr;
}

QueueItemPtr FileQueue::getQueuedFile(const TTHValue& aTTH, const string& fileName) noexcept {
	auto s = tthIndex.equal_range(aTTH);
	if (s.first != s.second) {
		auto k = find_if(s.first, s.second, [&fileName](pair<TTHValue, QueueItemPtr> tqp) { return (stricmp(fileName.c_str(), tqp.second->getTargetFileName().c_str()) == 0); });
		if (k != s.second) {
			return k->second;
		}
	}
	return nullptr;
}

void FileQueue::move(QueueItemPtr qi, const string& aTarget) noexcept {
	queue.erase(const_cast<string*>(&qi->getTarget()));
	qi->setTarget(aTarget);
	queue.insert(make_pair(const_cast<string*>(&qi->getTarget()), qi));
}

// compare nextQueryTime, get the oldest ones
void FileQueue::findPFSSources(PFSSourceList& sl) noexcept {
	typedef multimap<time_t, pair<QueueItem::SourceConstIter, const QueueItemPtr> > Buffer;
	Buffer buffer;
	uint64_t now = GET_TICK();

	for(auto i = queue.begin(); i != queue.end(); ++i) {
		QueueItemPtr q = i->second;

		if(q->getSize() < PARTIAL_SHARE_MIN_SIZE) continue;

		// don't share when file does not exist
		if(!Util::fileExists(q->isFinished() ? q->getTarget() : q->getTempTarget()))
			continue;

		const QueueItem::SourceList& sources = q->getSources();
		const QueueItem::SourceList& badSources = q->getBadSources();

		for(auto j = sources.begin(); j != sources.end(); ++j) {
			if(	(*j).isSet(QueueItem::Source::FLAG_PARTIAL) && (*j).getPartialSource()->getNextQueryTime() <= now &&
				(*j).getPartialSource()->getPendingQueryCount() < 10 && !(*j).getPartialSource()->getUdpPort().empty())
			{
				buffer.insert(make_pair((*j).getPartialSource()->getNextQueryTime(), make_pair(j, q)));
			}
		}

		for(auto j = badSources.begin(); j != badSources.end(); ++j) {
			if(	(*j).isSet(QueueItem::Source::FLAG_TTH_INCONSISTENCY) == false && (*j).isSet(QueueItem::Source::FLAG_PARTIAL) &&
				(*j).getPartialSource()->getNextQueryTime() <= now && (*j).getPartialSource()->getPendingQueryCount() < 10 &&
				!(*j).getPartialSource()->getUdpPort().empty())
			{
				buffer.insert(make_pair((*j).getPartialSource()->getNextQueryTime(), make_pair(j, q)));
			}
		}
	}

	// copy to results
	dcassert(sl.empty());
	const int32_t maxElements = 10;
	sl.reserve(maxElements);
	for(auto i = buffer.begin(); i != buffer.end() && sl.size() < maxElements; i++){
		sl.push_back(i->second);
	}
}

} //dcpp