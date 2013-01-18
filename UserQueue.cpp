/*
 * Copyright (C) 2011-2013 AirDC++ Project
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
#include <algorithm>
#include <functional>
#include <vector>

#include "UserQueue.h"
#include "SettingsManager.h"
#include "HashManager.h"
#include "QueueManager.h"
#include "Download.h"

#include "noexcept.h"

namespace dcpp {


void UserQueue::addQI(QueueItemPtr& qi, bool newBundle /*false*/) {
	for(auto i: qi->getSources()) {
		addQI(qi, i.getUser(), newBundle);
	}
}

void UserQueue::addQI(QueueItemPtr& qi, const HintedUser& aUser, bool newBundle /*false*/, bool isBadSource /*false*/) {

	if (qi->getPriority() == QueueItem::HIGHEST) {
		auto& l = userPrioQueue[aUser.user];
		l.insert(upper_bound(l.begin(), l.end(), qi, QueueItem::SizeSortOrder()), qi);
	}

	BundlePtr bundle = qi->getBundle();
	if (bundle) {
		if (bundle->addUserQueue(qi, aUser, isBadSource)) {
			addBundle(bundle, aUser);
			if (!newBundle) {
				QueueManager::getInstance()->fire(QueueManagerListener::BundleSources(), bundle);
			}
		} else {
			dcassert(userBundleQueue.find(aUser.user) != userBundleQueue.end());
		}
	}
}

void UserQueue::getUserQIs(const UserPtr& aUser, QueueItemList& ql) {
	/* Returns all queued items from an user */

	/* Highest prio */
	auto i = userPrioQueue.find(aUser);
	if(i != userPrioQueue.end()) {
		dcassert(!i->second.empty());
		copy_if(i->second.begin(), i->second.end(), back_inserter(ql), [](QueueItemPtr q) { return !q->getBundle(); }); //bundle items will be added from the bundle queue
	}

	/* Bundles */
	auto s = userBundleQueue.find(aUser);
	if(s != userBundleQueue.end()) {
		dcassert(!s->second.empty());
		for(auto& b: s->second)
			b->getItems(aUser, ql);
	}
}

QueueItemPtr UserQueue::getNext(const UserPtr& aUser, const OrderedStringSet& onlineHubs, QueueItem::Priority minPrio, int64_t wantedSize, int64_t lastSpeed, bool smallSlot, bool allowOverlap /*false*/) {
	/* Using the PAUSED priority will list all files */
	QueueItemPtr qi = getNextPrioQI(aUser, onlineHubs, 0, 0, smallSlot, allowOverlap);
	if(!qi) {
		qi = getNextBundleQI(aUser, onlineHubs, (Bundle::Priority)minPrio, wantedSize, lastSpeed, smallSlot, allowOverlap);
	}

	if (!qi && !allowOverlap) {
		//no free segments. let's do another round and now check if there are slow sources which can be overlapped
		qi = getNext(aUser, onlineHubs, minPrio, wantedSize, lastSpeed, smallSlot, true);
	}
	return qi;
}

QueueItemPtr UserQueue::getNextPrioQI(const UserPtr& aUser, const OrderedStringSet& onlineHubs, int64_t wantedSize, int64_t lastSpeed, bool smallSlot, bool allowOverlap) {
	lastError = Util::emptyString;
	auto i = userPrioQueue.find(aUser);
	if(i != userPrioQueue.end()) {
		dcassert(!i->second.empty());
		for(auto& q: i->second) {
			if (q->hasSegment(aUser, onlineHubs, lastError, wantedSize, lastSpeed, smallSlot, allowOverlap)) {
				return q;
			}
		}
	}
	return nullptr;
}

QueueItemPtr UserQueue::getNextBundleQI(const UserPtr& aUser, const OrderedStringSet& onlineHubs, Bundle::Priority minPrio, int64_t wantedSize, int64_t lastSpeed, bool smallSlot, bool allowOverlap) {
	lastError = Util::emptyString;

	auto i = userBundleQueue.find(aUser);
	if(i != userBundleQueue.end()) {
		dcassert(!i->second.empty());
		for (auto& b: i->second) {
			if (b->getPriority() < minPrio) {
				break;
			}

			QueueItemPtr qi = b->getNextQI(aUser, onlineHubs, lastError, minPrio, wantedSize, lastSpeed, smallSlot, allowOverlap);
			if (qi) {
				return qi;
			}
		}
	}
	return nullptr;
}

void UserQueue::addDownload(QueueItemPtr& qi, Download* d) {
	qi->getDownloads().push_back(d);
}

void UserQueue::removeDownload(QueueItemPtr& qi, const UserPtr& aUser, const string& aToken) {
	if (!aToken.empty()) {
		//erase a specific download
		qi->removeDownload(aToken);
	} else {
		//erase all downloads from this user
		qi->removeDownloads(aUser);
	}
	return;
}

void UserQueue::setQIPriority(QueueItemPtr& qi, QueueItem::Priority p) {
	removeQI(qi, false);
	qi->setPriority(p);
	addQI(qi);
}

void UserQueue::removeQI(QueueItemPtr& qi, bool removeRunning /*true*/, bool fireSources /*false*/) {
	for(auto i: qi->getSources()) {
		removeQI(qi, i.getUser(), removeRunning, false, fireSources);
	}
}

void UserQueue::removeQI(QueueItemPtr& qi, const UserPtr& aUser, bool removeRunning /*true*/, bool addBad /*false*/, bool fireSources /*false*/) {

	if(removeRunning) {
		removeDownload(qi, aUser);
	}

	dcassert(qi->isSource(aUser));

	BundlePtr bundle = qi->getBundle();
	if (bundle) {
		if (!bundle->isSource(aUser)) {
			return;
		}
		if (qi->getBundle()->removeUserQueue(qi, aUser, addBad)) {
			removeBundle(bundle, aUser);
			if (!fireSources) {
				QueueManager::getInstance()->fire(QueueManagerListener::BundleSources(), bundle);
			}
		} else {
			dcassert(userBundleQueue.find(aUser) != userBundleQueue.end());
		}
	}

	if (qi->getPriority() == QueueItem::HIGHEST) {
		auto j = userPrioQueue.find(aUser);
		dcassert(j != userPrioQueue.end());
		if (j == userPrioQueue.end()) {
			return;
		}
		auto& l = j->second;
		auto i = find(l.begin(), l.end(), qi);
		dcassert(i != l.end());
		if (i == l.end()) {
			return;
		}
		l.erase(i);

		if(l.empty()) {
			userPrioQueue.erase(j);
		}
	}
}

void UserQueue::addBundle(BundlePtr& aBundle, const UserPtr& aUser) {
	auto& s = userBundleQueue[aUser];
	s.insert(upper_bound(s.begin(), s.end(), aBundle, Bundle::SortOrder()), aBundle);
}

void UserQueue::removeBundle(BundlePtr& aBundle, const UserPtr& aUser) {
	auto j = userBundleQueue.find(aUser);
	dcassert(j != userBundleQueue.end());
	if (j == userBundleQueue.end()) {
		return;
	}

	auto& l = j->second;
	auto s = find(l.begin(), l.end(), aBundle);
	dcassert(s != l.end());
	if (s == l.end()) {
		return;
	}

	l.erase(s);
	if(l.empty()) {
		userBundleQueue.erase(j);
	}
}

void UserQueue::setBundlePriority(BundlePtr& aBundle, Bundle::Priority p) {
	dcassert(!aBundle->isFinished());

	HintedUserList sources;
	aBundle->getSources(sources);

	for(auto& u: sources)
		removeBundle(aBundle, u);

	aBundle->setPriority(p);

	for(auto& u: sources) 
		addBundle(aBundle, u);
}

} //dcpp