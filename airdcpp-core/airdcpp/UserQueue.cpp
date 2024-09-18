/*
 * Copyright (C) 2011-2024 AirDC++ Project
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

#include "Download.h"
#include "QueueManager.h"
#include "SettingsManager.h"
#include "UserQueue.h"

namespace dcpp {


void UserQueue::addQI(const QueueItemPtr& qi) noexcept {
	for(const auto& i: qi->getSources()) {
		addQI(qi, i.getUser());
	}
}

void UserQueue::addQI(const QueueItemPtr& qi, const HintedUser& aUser, bool aIsBadSource /*false*/) noexcept{

	if (qi->getPriority() == Priority::HIGHEST) {
		auto& l = userPrioQueue[aUser.user];
		l.insert(upper_bound(l.begin(), l.end(), qi, QueueItem::SizeSortOrder()), qi);
	}

	BundlePtr bundle = qi->getBundle();
	if (bundle) {
		aUser.user->addQueued(qi->getSize());
		if (bundle->addUserQueue(qi, aUser, aIsBadSource)) {
			addBundle(bundle, aUser);
		} else {
			dcassert(userBundleQueue.find(aUser.user) != userBundleQueue.end());
		}
	}
}

void UserQueue::getUserQIs(const UserPtr& aUser, QueueItemList& ql) noexcept{
	/* Returns all queued items from an user */

	/* Highest prio */
	if(auto i = userPrioQueue.find(aUser); i != userPrioQueue.end()) {
		dcassert(!i->second.empty());
		copy_if(i->second.begin(), i->second.end(), back_inserter(ql), [](const QueueItemPtr& q) { return !q->getBundle(); }); //bundle items will be added from the bundle queue
	}

	/* Bundles */
	auto s = userBundleQueue.find(aUser);
	if(s != userBundleQueue.end()) {
		dcassert(!s->second.empty());
		for(const auto& b: s->second)
			b->getItems(aUser, ql);
	}
}

QueueItemPtr UserQueue::getNext(const QueueDownloadQuery& aQuery, string& lastError_, bool& hasDownload_, bool aAllowOverlap) noexcept {

	// Using the PAUSED priority will list all files (?)
	auto qi = getNextPrioQI(aQuery, lastError_, aAllowOverlap /*, 0, 0*/);
	if(!qi) {
		qi = getNextBundleQI(aQuery, lastError_, hasDownload_, aAllowOverlap);
	}

	if (!qi && !aAllowOverlap) {
		// No free segments
		// Let's do another round and check if there are slow sources which can be overlapped
		qi = getNext(aQuery, lastError_, hasDownload_, true);
	}

	if (qi) {
		hasDownload_ = true;
	}

	return qi;
}

QueueItemPtr UserQueue::getNextPrioQI(const QueueDownloadQuery& aQuery, string& lastError_, bool aAllowOverlap) noexcept{

	lastError_ = Util::emptyString;
	if(auto i = userPrioQueue.find(aQuery.user); i != userPrioQueue.end()) {
		dcassert(!i->second.empty());
		for(const auto& q: i->second) {
			if (q->hasSegment(aQuery, lastError_, aAllowOverlap)) {
				return q;
			}
		}
	}
	return nullptr;
}

QueueItemPtr UserQueue::getNextBundleQI(const QueueDownloadQuery& aQuery, string& lastError_, bool& hasDownload_, bool aAllowOverlap) noexcept{

	lastError_ = Util::emptyString;

	auto bundleLimit = SETTING(MAX_RUNNING_BUNDLES);
	if(auto i = userBundleQueue.find(aQuery.user); i != userBundleQueue.end()) {
		dcassert(!i->second.empty());
		for (const auto& b: i->second) {
			if (bundleLimit > 0 && static_cast<int>(aQuery.runningBundles.size()) >= bundleLimit && !aQuery.runningBundles.contains(b->getToken())) {
				hasDownload_ = true;
				lastError_ = STRING(MAX_BUNDLES_RUNNING);
				continue;
			}

			if (b->getPriority() < aQuery.minPrio) {
				break;
			}

			auto qi = b->getNextQI(aQuery, lastError_, aAllowOverlap);
			if (qi) {
				return qi;
			}
		}
	}
	return nullptr;
}

void UserQueue::addDownload(const QueueItemPtr& qi, Download* d) noexcept {
	qi->addDownload(d);
}

void UserQueue::removeDownload(const QueueItemPtr& qi, const Download* d) noexcept {
	qi->removeDownload(d);
}

void UserQueue::setQIPriority(const QueueItemPtr& qi, Priority p) noexcept {
	removeQI(qi, false);
	qi->setPriority(p);
	addQI(qi);
}

void UserQueue::removeQI(const QueueItemPtr& qi, bool removeRunning /*true*/) noexcept{
	for(const auto& i: qi->getSources()) {
		removeQI(qi, i.getUser(), removeRunning, 0);
	}
}

void UserQueue::removeQI(const QueueItemPtr& qi, const UserPtr& aUser, bool removeRunning /*true*/, Flags::MaskType reason) noexcept{

	if(removeRunning) {
		qi->removeDownloads(aUser);
	}

	dcassert(qi->isSource(aUser));

	if (auto bundle = qi->getBundle(); bundle) {
		if (!bundle->isSource(aUser)) {
			return;
		}

		aUser->removeQueued(qi->getSize());
		if (qi->getBundle()->removeUserQueue(qi, aUser, reason)) {
			removeBundle(bundle, aUser);
		} else {
			dcassert(userBundleQueue.find(aUser) != userBundleQueue.end());
		}
	}

	if (qi->getPriority() == Priority::HIGHEST) {
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

void UserQueue::addBundle(const BundlePtr& aBundle, const UserPtr& aUser) noexcept{
	auto& s = userBundleQueue[aUser];
	s.insert(upper_bound(s.begin(), s.end(), aBundle, Bundle::SortOrder()), aBundle);
}

void UserQueue::removeBundle(const BundlePtr& aBundle, const UserPtr& aUser) noexcept {
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

void UserQueue::setBundlePriority(const BundlePtr& aBundle, Priority p) noexcept {
	dcassert(!aBundle->isDownloaded());

	HintedUserList sources;
	aBundle->getSourceUsers(sources);

	for(const auto& u: sources)
		removeBundle(aBundle, u);

	aBundle->setPriority(p);

	for(const auto& u: sources) 
		addBundle(aBundle, u);
}

} //dcpp