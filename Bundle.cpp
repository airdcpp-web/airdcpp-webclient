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
#include "LogManager.h"

namespace dcpp {

uint64_t Bundle::getSecondsLeft() {
	double avg = getSpeed();
	return (avg > 0) ? static_cast<int64_t>((size - bytesDownloaded) / avg) : 0;
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
		}
	} else {
		dirty = false;
	}
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


QueueItemList Bundle::getItems(const UserPtr& aUser) const {
	QueueItemList ret;
	for(int i = 0; i < QueueItem::LAST; ++i) {
		auto j = userQueue[i].find(aUser);
		if(j != userQueue[i].end()) {
			for(auto m = j->second.begin(); m != j->second.end(); ++m) {
				ret.push_back(*m);
			}
		}
	}
	return ret;
}

void Bundle::removeQueue(QueueItem* qi) {
	int pos = 0;
	for (auto s = queueItems.begin(); s != queueItems.end(); ++s) {
		if ((*s) == qi) {
			swap(queueItems[pos], queueItems[queueItems.size()-1]);
			queueItems.pop_back();
			return;
		}
		pos++;
	}
}

bool Bundle::isSource(const UserPtr& aUser) {
	return find_if(sources.begin(), sources.end(), [&](const UserRunningPair& urp) { return urp.first.user == aUser; }) != sources.end();
}

void Bundle::addUserQueue(QueueItem* qi) {
	for(QueueItem::SourceConstIter i = qi->getSources().begin(); i != qi->getSources().end(); ++i) {
		addUserQueue(qi, i->getUser());
	}
}

bool Bundle::addUserQueue(QueueItem* qi, const HintedUser& aUser) {
	auto& l = userQueue[qi->getPriority()][aUser.user];

	l.push_back(qi);
	if (l.size() > 1) {
		auto i = l.begin();
		auto start = (size_t)Util::rand((uint32_t)(l.size() < 200 ? l.size() : 200)); //limit the max value to lower the required moving distance
		advance(i, start);
		swap(queueItems[start], queueItems[queueItems.size()-1]);
		//l.insert(i, qi);
	}

	auto i = find_if(sources.begin(), sources.end(), [&](const UserRunningPair& urp) { return urp.first == aUser; });
	if (i != sources.end()) {
		i->second++;
		//LogManager::getInstance()->message("ADD, SOURCE FOR " + Util::toString(i->second) + " ITEMS");
		return false;
	} else {
		sources.push_back(make_pair(aUser, 1));
		dcassert(!sources.empty());
		return true;
	}
	//LogManager::getInstance()->message("ADD QI FOR BUNDLE USERQUEUE, total items for the user " + aUser->getCID().toBase32() + ": " + Util::toString(l.size()));
}

QueueItem* Bundle::getNextQI(const UserPtr& aUser, string aLastError, Priority minPrio, int64_t wantedSize, int64_t lastSpeed, bool smallSlot) {
	int p = QueueItem::LAST - 1;
	//lastError = Util::emptyString;

	do {
		auto i = userQueue[p].find(aUser);
		if(i != userQueue[p].end()) {
			dcassert(!i->second.empty());
			for(auto j = i->second.begin(); j != i->second.end(); ++j) {
				QueueItem* qi = *j;
				if (qi->hasSegment(aUser, aLastError, wantedSize, lastSpeed, smallSlot) || minPrio == PAUSED) {
					return qi;
				}
			}
		}
		p--;
	} while(p >= minPrio);

	return NULL;
}

void Bundle::getDownloadsQI(DownloadList& l) {
	for (auto s = queueItems.begin(); s != queueItems.end(); ++s) {
		QueueItem* qi = *s;
		for (auto k = qi->getDownloads().begin(); k != qi->getDownloads().end(); ++k) {
			l.push_back(*k);
		}
	}
}

void Bundle::getQISources(HintedUserList& l) {
	for (auto s = queueItems.begin(); s != queueItems.end(); ++s) {
		QueueItem* qi = *s;
		for (auto k = qi->getSources().begin(); k != qi->getSources().end(); ++k) {
			bool add = true;
			for (auto j = l.begin(); j != l.end(); ++j) {
				if ((*j) == (*k).getUser()) {
					add=false;
					break;
				}
			}
			if (add) {
				l.push_back((*k).getUser());
			}
		}
	}
	//LogManager::getInstance()->message("getQISources, size: " + Util::toString(l.size()));
}

bool Bundle::addDownload(Download* d) {
	bool downloadsEmpty = downloads.empty();
	downloads.push_back(d);
	return downloadsEmpty;
}

int Bundle::removeDownload(const string& token) {
	for(DownloadList::iterator i = downloads.begin(); i != downloads.end(); ++i) {
		if ((*i)->getUserConnection().getToken() == token) {
			downloads.erase(i);
			break;
		}
	}
	return downloads.size(); 
}

QueueItemList Bundle::getRunningQIs(const UserPtr& aUser) {
	QueueItemList ret;
	auto i = runningItems.find(aUser);
	if (i != runningItems.end()) {
		return i->second;
	}
	return ret;
}

void Bundle::removeUserQueue(QueueItem* qi, bool removeRunning) {
	for(QueueItem::SourceConstIter i = qi->getSources().begin(); i != qi->getSources().end(); ++i) {
		removeUserQueue(qi, i->getUser(), removeRunning);
	}
}

bool Bundle::removeUserQueue(QueueItem* qi, const UserPtr& aUser, bool removeRunning) {

	dcassert(qi->isSource(aUser));
	auto& ulm = userQueue[qi->getPriority()];
	auto j = ulm.find(aUser);
	dcassert(j != ulm.end());
	auto& l = j->second;
	int pos = 0;
	for (auto s = l.begin(); s != l.end(); ++s) {
		if ((*s) == qi) {
			swap(l[pos], l[l.size()-1]);
			l.pop_back();
			break;
		}
		pos++;
	}

	if(l.empty()) {
		ulm.erase(j);
	}

	//check bundle sources
	auto m = find_if(sources.begin(), sources.end(), [&](const UserRunningPair& urp) { return urp.first.user == aUser; });
	dcassert(m != sources.end());
	m->second--;
	//LogManager::getInstance()->message("REMOVE, SOURCE FOR " + Util::toString(m->second) + " ITEMS");
	if (m->second == 0) {
		sources.erase(m);   //crashed when nothing found to erase with only 1 source and removing multiple bundles.
		return true;
	}
	return false;
}

	
Bundle::Priority Bundle::calculateAutoPriority() const {
	if(autoPriority) {
		Bundle::Priority p;
		int percent = static_cast<int>(getDownloadedBytes() * 10.0 / size);
		switch(percent){
				case 0:
				case 1:
				case 2:
					p = Bundle::LOW;
					break;
				case 3:
				case 4:
				case 5:						
				default:
					p = Bundle::NORMAL;
					break;
				case 6:
				case 7:
				case 8:
					p = Bundle::HIGH;
					break;
					case 9:
					case 10:
					p = Bundle::HIGHEST;			
					break;
		}
		return p;			
	}
	return priority;
}

size_t Bundle::countOnlineUsers() const {
	size_t users = 0;
	int files = 0;
	for(auto i = sources.begin(); i != sources.end(); ++i) {
		if(i->first.user->isOnline()) {
			users++;
			files += i->second;
		}
	}
	return (queueItems.size() == 0 ? 0 : (files / queueItems.size()));
}

}
