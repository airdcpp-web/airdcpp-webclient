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
#include "boost\regex.hpp"

#include "FileQueue.h"
#include "SettingsManager.h"
#include "Text.h"
#include "QueueManager.h"
#include "Util.h"
#include "Pointer.h"

#include "noexcept.h"

#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm_ext/for_each.hpp>
#include <boost/range/algorithm/copy.hpp>

namespace dcpp {

using boost::adaptors::map_values;
using boost::range::for_each;
using boost::range::copy;

FileQueue::~FileQueue() { }

void FileQueue::getBloom(HashBloom& bloom) const {
	for(auto i = tthIndex.begin(); i != tthIndex.end(); ++i) {
		bloom.add(*i->first);
	}
}

QueueItemPtr FileQueue::add(const string& aTarget, int64_t aSize, Flags::MaskType aFlags, QueueItem::Priority p, 
	const string& aTempTarget, time_t aAdded, const TTHValue& root) noexcept {

	QueueItemPtr qi = new QueueItem(aTarget, aSize, p, aFlags, aAdded, root, aTempTarget);
	dcassert(findFile(aTarget) == nullptr);
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
	tthIndex.insert(make_pair(const_cast<TTHValue*>(&qi->getTTH()), qi));
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
	auto s = tthIndex.equal_range(const_cast<TTHValue*>(&qi->getTTH()));
	dcassert(s.first != s.second);

	auto k = find(s | map_values, qi);
	if (k.base() != s.second) {
		tthIndex.erase(k.base());
	}
}

QueueItemPtr FileQueue::findFile(const string& target) noexcept {
	auto i = queue.find(const_cast<string*>(&target));
	return (i == queue.end()) ? nullptr : i->second;
}

void FileQueue::findFiles(const TTHValue& tth, QueueItemList& ql) noexcept {
	copy(tthIndex.equal_range(const_cast<TTHValue*>(&tth)) | map_values, back_inserter(ql));
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
		auto s = tthIndex.equal_range(const_cast<TTHValue*>(&(*i)->getTTH()));
		if (s.first != s.second) {
			DirectoryListing::File* f = *i;
			for_each(s | map_values, [f, &ql](const QueueItemPtr q) {
				if (!q->isFinished() && q->getSize() == f->getSize() && boost::find_if(ql, CompareSecond<string, QueueItemPtr>(q)) == ql.end()) 
					ql.push_back(make_pair(Util::emptyString, q)); 
			});
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
					ql.push_back(make_pair(f->getPath(), tqp.second)); 
			});
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

QueueItemPtr FileQueue::getQueuedFile(const TTHValue& aTTH, const string& fileName) noexcept {
	auto s = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
	if (s.first != s.second) {
		auto k = find_if(s | map_values, [&fileName](const QueueItemPtr q) { return (stricmp(fileName, q->getTargetFileName()) == 0); });
		if (k.base() != s.second) {
			return *k;
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