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

#include "noexcept.h"

namespace dcpp {

FileQueue::~FileQueue() { }

QueueItem* FileQueue::add(const string& aTarget, int64_t aSize, Flags::MaskType aFlags, QueueItem::Priority p, 
	const string& aTempTarget, time_t aAdded, const TTHValue& root) noexcept {

	QueueItem* qi = new QueueItem(aTarget, aSize, p, aFlags, aAdded, root, aTempTarget);
	dcassert(find(aTarget) == NULL);
	add(qi, false);
	return qi;
}

void FileQueue::add(QueueItem* qi, bool addFinished) noexcept {
	if (!addFinished) {
		if (!qi->isSet(QueueItem::FLAG_USER_LIST)) {
			dcassert(qi->getSize() >= 0);
			queueSize += qi->getSize();
		}
		dcassert(queueSize >= 0);
		//queue.insert(make_pair(const_cast<string*>(&qi->getTarget()), qi)).first;
	}
	targetMapInsert = queue.insert(targetMapInsert, make_pair(const_cast<string*>(&qi->getTarget()), qi));
	tthIndex.insert(make_pair(qi->getTTH(), qi));
}

void FileQueue::remove(QueueItem* qi) noexcept {
	//TargetMap
	prev(targetMapInsert);
	queue.erase(const_cast<string*>(&qi->getTarget()));
	if (!qi->isSet(QueueItem::FLAG_USER_LIST) && !qi->isSet(QueueItem::FLAG_FINISHED)) {
		dcassert(qi->getSize() >= 0);
		queueSize -= qi->getSize();
	}
	dcassert(queueSize >= 0);

	//TTHIndex
	auto s = tthIndex.equal_range(qi->getTTH());
	dcassert(s.first != s.second);
	auto k = find_if(s.first, s.second, CompareSecond<TTHValue, QueueItem*>(qi));
	if (k != s.second) {
		tthIndex.erase(k);
	}
}

QueueItem* FileQueue::find(const string& target) noexcept {
	auto i = queue.find(const_cast<string*>(&target));
	return (i == queue.end()) ? NULL : i->second;
}

void FileQueue::find(QueueItemList& sl, int64_t aSize, const string& suffix) noexcept {
	for(auto i = queue.begin(); i != queue.end(); ++i) {
		if(i->second->getSize() == aSize) {
			const string& t = i->second->getTarget();
			if(suffix.empty() || (suffix.length() < t.length() &&
				stricmp(suffix.c_str(), t.c_str() + (t.length() - suffix.length())) == 0) )
				sl.push_back(i->second);
		}
	}
}

void FileQueue::find(const TTHValue& tth, QueueItemList& ql) noexcept {
	auto s = tthIndex.equal_range(tth);
	if (s.first != s.second) {
		for_each(s.first, s.second, [&](pair<TTHValue, QueueItem*> tqp) { ql.push_back(tqp.second); } );
	}
}

void FileQueue::matchDir(const DirectoryListing::Directory* dir, QueueItemList& ql) noexcept {
	for(auto j = dir->directories.begin(); j != dir->directories.end(); ++j) {
		if(!(*j)->getAdls())
			matchDir(*j, ql);
	}

	for(auto i = dir->files.begin(); i != dir->files.end(); ++i) {
		auto s = tthIndex.equal_range((*i)->getTTH());
		if (s.first != s.second) {
			DirectoryListing::File* f = *i;
			//LogManager::getInstance()->message("MATCH, PATH: " + f->getPath());
			for_each(s.first, s.second, [&](pair<TTHValue, QueueItem*> tqp) {
				if (!tqp.second->isFinished() && tqp.second->getSize() == f->getSize() && std::find(ql.begin(), ql.end(), tqp.second) == ql.end()) 
					ql.push_back(tqp.second); } );
		}
	}
}

int FileQueue::isFileQueued(const TTHValue& aTTH, const string& fileName) noexcept {
	QueueItem* qi = getQueuedFile(aTTH, fileName);
	if (qi) {
		return (qi->isFinished() ? 2 : 1);
	}
	return 0;
}

QueueItem* FileQueue::getQueuedFile(const TTHValue& aTTH, const string& fileName) noexcept {
	auto s = tthIndex.equal_range(aTTH);
	if (s.first != s.second) {
		auto k = find_if(s.first, s.second, [&](pair<TTHValue, QueueItem*> tqp) { return (stricmp(fileName.c_str(), tqp.second->getTargetFileName().c_str()) == 0); });
		if (k != s.second) {
			return k->second;
		}
	}
	return NULL;
}

void FileQueue::move(QueueItem* qi, const string& aTarget) noexcept {
	queue.erase(const_cast<string*>(&qi->getTarget()));
	qi->setTarget(aTarget);
	add(qi, false);
}

// compare nextQueryTime, get the oldest ones
void FileQueue::findPFSSources(PFSSourceList& sl) noexcept {
	typedef multimap<time_t, pair<QueueItem::SourceConstIter, const QueueItem*> > Buffer;
	Buffer buffer;
	uint64_t now = GET_TICK();

	for(auto i = queue.begin(); i != queue.end(); ++i) {
		QueueItem* q = i->second;

		if(q->getSize() < PARTIAL_SHARE_MIN_SIZE) continue;

		// don't share when file does not exist
		if(!Util::fileExists(q->isFinished() ? q->getTarget() : q->getTempTarget()))
			continue;

		const QueueItem::SourceList& sources = q->getSources();
		const QueueItem::SourceList& badSources = q->getBadSources();

		for(auto j = sources.begin(); j != sources.end(); ++j) {
			if(	(*j).isSet(QueueItem::Source::FLAG_PARTIAL) && (*j).getPartialSource()->getNextQueryTime() <= now &&
				(*j).getPartialSource()->getPendingQueryCount() < 10 && (*j).getPartialSource()->getUdpPort() > 0)
			{
				buffer.insert(make_pair((*j).getPartialSource()->getNextQueryTime(), make_pair(j, q)));
			}
		}

		for(auto j = badSources.begin(); j != badSources.end(); ++j) {
			if(	(*j).isSet(QueueItem::Source::FLAG_TTH_INCONSISTENCY) == false && (*j).isSet(QueueItem::Source::FLAG_PARTIAL) &&
				(*j).getPartialSource()->getNextQueryTime() <= now && (*j).getPartialSource()->getPendingQueryCount() < 10 &&
				(*j).getPartialSource()->getUdpPort() > 0)
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