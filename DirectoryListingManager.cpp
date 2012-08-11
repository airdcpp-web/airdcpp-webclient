/*
 * Copyright (C) 2011-2012 AirDC++ Project
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

#include "DirectoryListingManager.h"
#include "ClientManager.h"
#include "QueueManager.h"

namespace dcpp {
	DirectoryListingManager::DirectoryListingManager() {
		QueueManager::getInstance()->addListener(this);
	}

	DirectoryListingManager::~DirectoryListingManager() {
		QueueManager::getInstance()->removeListener(this);
	}

	void DirectoryListingManager::listWindowOpened(const DirectoryListing* aList) {

	}

	void DirectoryListingManager::on(QueueManagerListener::Finished, const QueueItemPtr qi, const string& dir, const HintedUser& aUser, int64_t aSpeed) noexcept {
		if (!qi->isSet(QueueItem::FLAG_CLIENT_VIEW) || !qi->isSet(QueueItem::FLAG_USER_LIST))
			return;

		//if (hasList(aUser.user))
		//	return;

		{
			RLock l(cs);
			auto p = fileLists.find(aUser.user);
			if (p != fileLists.end() && p->second->getPartialList()) {
				p->second->setFileName(qi->getListName());
				p->second->setspeed(aSpeed);
				p->second->loadFullList(dir);
				return;
			}
		}

		if (qi->isSet(QueueItem::FLAG_CLIENT_VIEW)) {
			createList(aUser, qi->getListName(), aSpeed, dir);
		} else {
			///
		}
	}

	void DirectoryListingManager::on(QueueManagerListener::PartialList, const HintedUser& aUser, const string& text) noexcept {
		{
			RLock l(cs);
			auto p = fileLists.find(aUser.user);
			if (p != fileLists.end()) {
				p->second->refreshDir(text);
				return;
			}
		}

		DirectoryListing* dl = new DirectoryListing(aUser, true, text, false);
		fire(DirectoryListingManagerListener::OpenPartialListing(), dl);

		WLock l(cs);
		fileLists[aUser.user] = dl;
	}

	void DirectoryListingManager::openOwnList(const string& aProfile) {
		auto me = HintedUser(ClientManager::getInstance()->getMe(), Util::emptyString);
		if (hasList(me.user))
			return;

		createList(me, aProfile, 0, Util::emptyString, true);
	}

	void DirectoryListingManager::openFileList(const HintedUser& aUser, const string& aFile) {
		if (hasList(aUser.user))
			return;

		createList(aUser, aFile, 0);
	}

	void DirectoryListingManager::createList(const HintedUser& aUser, const string& aFile, int64_t aSpeed, const string& aInitialDir /*Util::emptyString*/, bool isOwnList /*false*/) {
		DirectoryListing* dl = new DirectoryListing(aUser, false, aFile, aSpeed, isOwnList);
		fire(DirectoryListingManagerListener::OpenListing(), dl, aInitialDir);

		WLock l(cs);
		fileLists[aUser.user] = dl;
	}

	bool DirectoryListingManager::hasList(const UserPtr& aUser) {
		RLock l (cs);
		auto p = fileLists.find(aUser);
		if (p != fileLists.end()) {
			return true;
		}

		return false;
	}

	void DirectoryListingManager::removeList(const UserPtr& aUser) {
		WLock l (cs);
		auto p = fileLists.find(aUser);
		if (p != fileLists.end()) {
			fileLists.erase(p);
		}
	}
}