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
#include "Bundle.h"
#include "TimerManager.h"
#include "Download.h"
#include "UserConnection.h"
#include "HashManager.h"
#include "QueueItem.h"

namespace dcpp {

uint64_t Bundle::getSecondsLeft() {
	double avg = getSpeed();
	int64_t bytesLeft =  getSize() - getDownloaded();
	return (avg > 0) ? static_cast<int64_t>(bytesLeft / avg) : 0;
}

string Bundle::getName() {
	if (!fileBundle) {
		return Util::getDir(target, false, true);
	} else {
		return Util::getFileName(target);
	}
}

void Bundle::setDirty(bool enable) {
	if (enable) {
		if(!dirty) {
			dirty = true;
			lastSave = GET_TICK();
		}
	} else {
		dirty = false;
		lastSave = GET_TICK();
	}
}

uint64_t Bundle::getDownloadedBytes() const {
	uint64_t total = 0;

	// count done segments
	for(auto i = queueItems.begin(); i != queueItems.end(); ++i) {
		total += (*i)->getDownloadedBytes();
	}

	// count running segments
	for(auto i = downloads.begin(); i != downloads.end(); ++i) {
		total += (*i)->getPos();
	}

	return total;
}

QueueItem* Bundle::findQI(const string& aTarget) const {
	for(auto i = queueItems.begin(); i != queueItems.end(); ++i) {
		QueueItem* qi = *i;
		if (qi->getTarget() == aTarget) {
			return qi;
		}
	}
	return NULL;
}

string Bundle::getBundleFile() {
	return Util::getPath(Util::PATH_BUNDLES) + "Bundle" + token + ".xml";
}

void Bundle::addSource(const HintedUser& aUser) {
	for (auto i = sources.begin(); i != sources.end(); ++i) {
		if (i->first == aUser) {
			i->second++;
			return;
		}
	}
	//sources[aUser] = 1;
	sources.push_back(make_pair(aUser, 1));
}

void Bundle::removeSource(const UserPtr& aUser) {
	for (auto i = sources.begin(); i != sources.end(); ++i) {
		if (i->first.user == aUser) {
			i->second--;
			if (i->second == 0) {
				sources.erase(i);
			}
			return;
		}
	}
}


void Bundle::BundleUserQueue::add(QueueItem* qi) {
	for(QueueItem::SourceConstIter i = qi->getSources().begin(); i != qi->getSources().end(); ++i) {
		add(qi, i->getUser());
	}
}

void Bundle::BundleUserQueue::add(QueueItem* qi, const UserPtr& aUser) {
	auto& l = userQueue[qi->getPriority()][aUser];

	if(qi->getDownloadedBytes() > 0 ) {
		l.push_front(qi);
	} else {
		l.push_back(qi);
	}
}

QueueItem* Bundle::BundleUserQueue::getNext(const UserPtr& aUser, string aLastError, Priority minPrio, int64_t wantedSize, int64_t lastSpeed, bool allowRemove, bool smallSlot) {
	int p = QueueItem::LAST - 1;
	//lastError = Util::emptyString;

	do {
		auto i = userQueue[p].find(aUser);
		if(i != userQueue[p].end()) {
			dcassert(!i->second.empty());
			for(auto j = i->second.begin(); j != i->second.end(); ++j) {
				QueueItem* qi = *j;
				
				QueueItem::SourceConstIter source = qi->getSource(aUser);
				/*user is not a source anymore?? removed but still in userqueue?
				item just finished, dont go further?, pick another one.*/
				if(!qi->isSource(aUser) || qi->isFinished())
					continue;

				if(smallSlot && !qi->isSet(QueueItem::FLAG_PARTIAL_LIST) && qi->getSize() > 65792) {
					//don't even think of stealing our priority channel
					continue;
				}

				if(source->isSet(QueueItem::Source::FLAG_PARTIAL)) {
					// check partial source
					int64_t blockSize = HashManager::getInstance()->getBlockSize(qi->getTTH());
					if(blockSize == 0)
						blockSize = qi->getSize();
					
					Segment segment = qi->getNextSegment(blockSize, wantedSize, lastSpeed, source->getPartialSource());
					if(allowRemove && segment.getStart() != -1 && segment.getSize() == 0) {
						// no other partial chunk from this user, remove him from queue
						remove(qi, aUser);
						//qi->removeSource(aUser, QueueItem::Source::FLAG_NO_NEED_PARTS);
						aLastError = STRING(NO_NEEDED_PART);
						p++;
						break;
					}
				}

				if(qi->isWaiting()) {
					return qi;
				}
				
				// No segmented downloading when getting the tree
				if(qi->getDownloads()[0]->getType() == Transfer::TYPE_TREE) {
					continue;
				}
				if(!qi->isSet(QueueItem::FLAG_USER_LIST)) {

					int64_t blockSize = HashManager::getInstance()->getBlockSize(qi->getTTH());
					if(blockSize == 0)
						blockSize = qi->getSize();

					Segment segment = qi->getNextSegment(blockSize, wantedSize, lastSpeed, source->getPartialSource());
					if(segment.getSize() == 0) {
						aLastError = (segment.getStart() == -1 || qi->getSize() < (SETTING(MIN_SEGMENT_SIZE)*1024)) ? STRING(NO_FILES_AVAILABLE) : STRING(NO_FREE_BLOCK);
						//LogManager::getInstance()->message("NO SEGMENT: " + aUser->getCID().toBase32());
						dcdebug("No segment for %s in %s, block " I64_FMT "\n", aUser->getCID().toBase32().c_str(), qi->getTarget().c_str(), blockSize);
						continue;
					}
				}
				return qi;
			}
		}
		p--;
	} while(p >= minPrio);

	return NULL;
}

void Bundle::BundleUserQueue::addDownload(QueueItem* qi, Download* d) {
	qi->getDownloads().push_back(d);

	auto i = running.find(d->getUser());
	if (i != running.end()) {
		i->second.push_back(qi);
		//LogManager::getInstance()->message("Running size for the user: " + Util::toString(i->second.size()));
	} else {
		QueueItemList tmp;
		tmp.push_back(qi);
		running[d->getUser()] = tmp;
	}
}

void Bundle::BundleUserQueue::removeDownload(QueueItem* qi, const UserPtr& user, const string& token) {
	auto i = running.find(user);
	if (i != running.end()) {
		for (auto s = i->second.begin(); s != i->second.end(); ++s) {
			if (qi == *s) {
				i->second.erase(s);
				//LogManager::getInstance()->message("Running size for the user: " + Util::toString(i->second.size()));
				if (i->second.empty()) {
					//LogManager::getInstance()->message("ERASE RUNNING USER");
					running.erase(i);
				}
				break;
			}
		}
	}

	if (!token.empty()) {
		for(DownloadList::iterator i = qi->getDownloads().begin(); i != qi->getDownloads().end(); ++i) {
			if ((*i)->getUserConnection().getToken() == token) {
				qi->getDownloads().erase(i);
				return;
			}
		}
	} else {
		//erase all downloads from this user
		for(DownloadList::iterator i = qi->getDownloads().begin(); i != qi->getDownloads().end();) {
			if((*i)->getUser() == user) {
				qi->getDownloads().erase(i);
				i = qi->getDownloads().begin();
			} else {
				i++;
			}
		}
	}
}

/*void Bundle::BundleUserQueue::setPriority(QueueItem* qi, QueueItem::Priority p) {
	remove(qi, false);
	qi->setPriority(p);
	add(qi);
} */

QueueItemList Bundle::BundleUserQueue::getRunning(const UserPtr& aUser) {
	QueueItemList ret;
	auto i = running.find(aUser);
	if (i != running.end()) {
		return i->second;
	}
	return ret;
}

void Bundle::BundleUserQueue::remove(QueueItem* qi, bool removeRunning) {
	for(QueueItem::SourceConstIter i = qi->getSources().begin(); i != qi->getSources().end(); ++i) {
		remove(qi, i->getUser(), removeRunning);
	}
}

void Bundle::BundleUserQueue::remove(QueueItem* qi, const UserPtr& aUser, bool removeRunning) {

	if(removeRunning) {
		QueueItemList runningItems = getRunning(aUser);
		for (auto s = runningItems.begin(); s != runningItems.end(); ++s) {
			if (qi == *s) {
				removeDownload(qi, aUser);
				break;
			}
		}
	}

	dcassert(qi->isSource(aUser));
	auto& ulm = userQueue[qi->getPriority()];
	auto j = ulm.find(aUser);
	dcassert(j != ulm.end());
	auto& l = j->second;
	auto i = find(l.begin(), l.end(), qi);
	dcassert(i != l.end());
	l.erase(i);

	if(l.empty()) {
		ulm.erase(j);
	}
}


}
