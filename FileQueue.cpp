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
#include "Wildcards.h"
#include "Text.h"
#include "QueueManager.h"
#include "Util.h"

#include "noexcept.h"

namespace dcpp {

FileQueue::~FileQueue() { }

QueueItem* FileQueue::add(const string& aTarget, int64_t aSize, 
						  Flags::MaskType aFlags, QueueItem::Priority p, const string& aTempTarget, 
						  time_t aAdded, const TTHValue& root)
{
//remember the default state so high_prio files are matched
//even if priority is set with priority by size setting
	bool Default = false;

	if(p == QueueItem::DEFAULT)
		Default = true;

	if(p == QueueItem::DEFAULT) {
		if(aSize <= SETTING(PRIO_HIGHEST_SIZE)*1024) {
			p = QueueItem::HIGHEST;
		} else if(aSize <= SETTING(PRIO_HIGH_SIZE)*1024) {
			p = QueueItem::HIGH;
		} else if(aSize <= SETTING(PRIO_NORMAL_SIZE)*1024) {
			p = QueueItem::NORMAL;
		} else if(aSize <= SETTING(PRIO_LOW_SIZE)*1024) {
			p = QueueItem::LOW;
		} else if(SETTING(PRIO_LOWEST)) {
			p = QueueItem::LOWEST;
		}
	}

	if((p != QueueItem::HIGHEST) && ( Default )){
		if(!SETTING(HIGH_PRIO_FILES).empty()){
			int pos = aTarget.rfind("\\")+1;
		
			if(BOOLSETTING(HIGHEST_PRIORITY_USE_REGEXP)){
				string str1 = SETTING(HIGH_PRIO_FILES);
				string str2 = aTarget.substr(pos);
				try {
					boost::regex reg(str1);
					if(boost::regex_search(str2.begin(), str2.end(), reg)){
						p = QueueItem::HIGH;
						Default = false;
					};
				} catch(...) {
				}
			}else{
				if(Wildcard::patternMatch(Text::utf8ToAcp(aTarget.substr(pos)), Text::utf8ToAcp(SETTING(HIGH_PRIO_FILES)), '|')) {
					p = QueueItem::HIGH;
					Default = false;
				}
			}
		}
	}



	QueueItem* qi = new QueueItem(aTarget, aSize, p, aFlags, aAdded, root);

	if(qi->isSet(QueueItem::FLAG_USER_LIST) || qi->isSet(QueueItem::FLAG_CLIENT_VIEW) || qi->isSet(QueueItem::FLAG_VIEW_NFO)) {
		qi->setPriority(QueueItem::HIGHEST);
	} else {
		qi->setMaxSegments(getMaxSegments(qi->getSize()));
		
		if(p == QueueItem::DEFAULT) {
			if(BOOLSETTING(AUTO_PRIORITY_DEFAULT)) {
				qi->setAutoPriority(true);
				qi->setPriority(QueueItem::LOW);
			} else {
				qi->setPriority(QueueItem::NORMAL);
			}
		}
	}

	qi->setTempTarget(aTempTarget);
	if(!Util::fileExists(aTempTarget) && Util::fileExists(aTempTarget + ".antifrag")) {
		// load old antifrag file
		File::renameFile(aTempTarget + ".antifrag", qi->getTempTarget());
	}
			
	dcassert(find(aTarget) == NULL);
	add(qi, false);
	return qi;
}

void FileQueue::add(QueueItem* qi, bool addFinished, bool addTTH) {
	if (!addFinished) {
		if (!qi->isSet(QueueItem::FLAG_USER_LIST)) {
			dcassert(qi->getSize() >= 0);
			queueSize += qi->getSize();
		}
		dcassert(queueSize >= 0);
		targetMapInsert = queue.insert(targetMapInsert, make_pair(const_cast<string*>(&qi->getTarget()), qi));
		//queue.insert(make_pair(const_cast<string*>(&qi->getTarget()), qi)).first;
	}

	if (addTTH) {
		tthIndex.insert(make_pair(qi->getTTH(), qi));
	}
}

void FileQueue::remove(QueueItem* qi, bool aRemoveTTH) {
	prev(targetMapInsert);
	queue.erase(const_cast<string*>(&qi->getTarget()));
	if  (!qi->isSet(QueueItem::FLAG_USER_LIST)) {
		dcassert(qi->getSize() >= 0);
		queueSize -= qi->getSize();
	}
	dcassert(queueSize >= 0);

	if (aRemoveTTH) {
		removeTTH(qi);
	}
}

void FileQueue::removeTTH(QueueItem* qi) {
	auto s = tthIndex.equal_range(qi->getTTH());
	dcassert(s.first != s.second);
	auto k = find_if(s.first, s.second, CompareSecond<TTHValue, QueueItem*>(qi));
	if (k != s.second) {
		tthIndex.erase(k);
	}
}

QueueItem* FileQueue::find(const string& target) {
	auto i = queue.find(const_cast<string*>(&target));
	return (i == queue.end()) ? NULL : i->second;
}

void FileQueue::find(QueueItemList& sl, int64_t aSize, const string& suffix) {
	for(auto i = queue.begin(); i != queue.end(); ++i) {
		if(i->second->getSize() == aSize) {
			const string& t = i->second->getTarget();
			if(suffix.empty() || (suffix.length() < t.length() &&
				stricmp(suffix.c_str(), t.c_str() + (t.length() - suffix.length())) == 0) )
				sl.push_back(i->second);
		}
	}
}

void FileQueue::find(const TTHValue& tth, QueueItemList& ql) {
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

int FileQueue::isFileQueued(const TTHValue& aTTH, const string& fileName) {
	QueueItem* qi = getQueuedFile(aTTH, fileName);
	if (qi) {
		return (qi->isFinished() ? 2 : 1);
	}
	return 0;
}

QueueItem* FileQueue::getQueuedFile(const TTHValue& aTTH, const string& fileName) {
	auto s = tthIndex.equal_range(aTTH);
	if (s.first != s.second) {
		auto k = find_if(s.first, s.second, [&](pair<TTHValue, QueueItem*> tqp) { return (stricmp(fileName.c_str(), tqp.second->getTargetFileName().c_str()) == 0); });
		if (k != s.second) {
			return k->second;
		}
	}
	return NULL;
}

void FileQueue::move(QueueItem* qi, const string& aTarget) {
	queue.erase(const_cast<string*>(&qi->getTarget()));
	qi->setTarget(aTarget);
	add(qi, false, false);
}

uint8_t FileQueue::getMaxSegments(int64_t filesize) const {
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

// compare nextQueryTime, get the oldest ones
void FileQueue::findPFSSources(PFSSourceList& sl) {
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