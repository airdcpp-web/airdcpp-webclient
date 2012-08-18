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

#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm/for_each.hpp>

namespace dcpp {

using boost::adaptors::map_values;
using boost::range::for_each;

class DirectoryItem {
public:
	DirectoryItem() : priority(QueueItem::DEFAULT) { }
	DirectoryItem(const UserPtr& aUser, const string& aName, const string& aTarget, TargetUtil::TargetType aTargetType, 
		QueueItem::Priority p) : name(aName), target(aTarget), priority(p), user(aUser), targetType(aTargetType) { }
	~DirectoryItem() { }
	
	UserPtr& getUser() { return user; }
	void setUser(const UserPtr& aUser) { user = aUser; }
	
	GETSET(string, name, Name);
	GETSET(string, target, Target);
	GETSET(QueueItem::Priority, priority, Priority);
	GETSET(TargetUtil::TargetType, targetType, TargetType);
private:
	UserPtr user;
};

DirectoryListingManager::DirectoryListingManager() {
	QueueManager::getInstance()->addListener(this);
}

DirectoryListingManager::~DirectoryListingManager() {
	QueueManager::getInstance()->removeListener(this);
}

void DirectoryListingManager::removeDirectoryDownload(const UserPtr aUser) {
	WLock l(cs);
	auto dp = directories.equal_range(aUser);
	for(auto i = dp.first; i != dp.second; ++i) {
		delete i->second;
	}
	directories.erase(aUser);
}

void DirectoryListingManager::addDirectoryDownload(const string& aDir, const HintedUser& aUser, const string& aTarget, TargetUtil::TargetType aTargetType, QueueItem::Priority p /* = QueueItem::DEFAULT */, bool useFullList) noexcept {

	bool needList;
	{
		WLock l(cs);
		
		auto dp = directories.equal_range(aUser);
		
		for(auto i = dp.first; i != dp.second; ++i) {
			if(stricmp(aTarget.c_str(), i->second->getName().c_str()) == 0)
				return;
		}
		
		// Unique directory, fine...
		directories.insert(make_pair(aUser, new DirectoryItem(aUser, aDir, aTarget, aTargetType, p)));
		needList = aUser.user->isSet(User::NMDC) ? (dp.first == dp.second) : true;
	}

	if(needList) {
		try {
			if (!aUser.user->isSet(User::NMDC) && !useFullList) {
				QueueManager::getInstance()->addList(aUser, QueueItem::FLAG_DIRECTORY_DOWNLOAD | QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_RECURSIVE_LIST, aDir);
			} else {
				QueueManager::getInstance()->addList(aUser, QueueItem::FLAG_DIRECTORY_DOWNLOAD, aDir);
			}
		} catch(const Exception&) {
			// Ignore, we don't really care...
		}
	}
}

void DirectoryListingManager::processList(const string& name, const HintedUser& user, const string& path, int flags) {
	DirectoryListing dirList(user, (flags & QueueItem::FLAG_PARTIAL_LIST) > 0, name, false);
	try {
		if(flags & QueueItem::FLAG_TEXT) {
			MemoryInputStream mis(name);
			dirList.loadXML(mis, true);
		} else {
			dirList.loadFile(name);
		}
	} catch(const Exception&) {
		LogManager::getInstance()->message(STRING(UNABLE_TO_OPEN_FILELIST) + " " + name, LogManager::LOG_ERROR);
		return;
	}

	if(flags & QueueItem::FLAG_DIRECTORY_DOWNLOAD) {
		vector<DirectoryItemPtr> dl;
		{
			WLock l(cs);
			if ((flags & QueueItem::FLAG_PARTIAL_LIST) && !path.empty()) {
				//partial list
				auto dp = directories.equal_range(user);
				auto udp = find_if(dp.first, dp.second, [path](pair<UserPtr, DirectoryItemPtr> ud) { return stricmp(path.c_str(), ud.second->getName().c_str()) == 0; });
				if (udp != dp.second) {
					dl.push_back(udp->second);
					directories.erase(udp);
				}
			} else {
				//full filelist
				auto dpf = directories.equal_range(user) | map_values;
				dl.assign(boost::begin(dpf), boost::end(dpf));
				directories.erase(user);
			}
		}

		for_each(dl, [&dirList](DirectoryItem* di) {
			dirList.download(di->getName(), di->getTarget(), false, di->getPriority());
			delete di;
		});
	}

	if(flags & QueueItem::FLAG_MATCH_QUEUE) {
		int matches=0, newFiles=0;
		BundleList bundles;
		QueueManager::getInstance()->matchListing(dirList, matches, newFiles, bundles);
		if ((flags & QueueItem::FLAG_PARTIAL_LIST) && (!SETTING(REPORT_ADDED_SOURCES) || newFiles == 0 || bundles.empty())) {
			return;
		}
		LogManager::getInstance()->message(Util::toString(ClientManager::getInstance()->getNicks(user)) + ": " + 
			AirUtil::formatMatchResults(matches, newFiles, bundles, (flags & QueueItem::FLAG_PARTIAL_LIST) > 0), LogManager::LOG_INFO);
	} else if((flags & QueueItem::FLAG_VIEW_NFO) && (flags & QueueItem::FLAG_PARTIAL_LIST)) {
		dirList.findNfo(path);
	}
}

void DirectoryListingManager::on(QueueManagerListener::Finished, const QueueItemPtr qi, const string& dir, const HintedUser& aUser, int64_t aSpeed) noexcept {
	if (!qi->isSet(QueueItem::FLAG_CLIENT_VIEW) || !qi->isSet(QueueItem::FLAG_USER_LIST))
		return;

	{
		RLock l(cs);
		auto p = fileLists.find(aUser.user);
		if (p != fileLists.end() && p->second->getPartialList()) {
			p->second->setFileName(qi->getListName());
			p->second->setspeed(aSpeed);
			p->second->addFullListTask(dir);
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
			p->second->addPartialListTask(text);
			return;
		}
	}

	createPartialList(aUser, text);
}

void DirectoryListingManager::openOwnList(ProfileToken aProfile) {
	auto me = HintedUser(ClientManager::getInstance()->getMe(), Util::emptyString);
	if (hasList(me.user))
		return;

	createPartialList(me, Util::emptyString, aProfile, true);
}

void DirectoryListingManager::openFileList(const HintedUser& aUser, const string& aFile) {
	if (hasList(aUser.user))
		return;

	createList(aUser, aFile, 0);
}

void DirectoryListingManager::createList(const HintedUser& aUser, const string& aFile, int64_t aSpeed, const string& aInitialDir /*Util::emptyString*/, bool isOwnList /*false*/) {
	DirectoryListing* dl = new DirectoryListing(aUser, false, aFile, true, aSpeed, isOwnList);
	fire(DirectoryListingManagerListener::OpenListing(), dl, aInitialDir);

	WLock l(cs);
	fileLists[aUser.user] = dl;
}

void DirectoryListingManager::createPartialList(const HintedUser& aUser, const string& aXml, ProfileToken aProfile, bool isOwnList /*false*/) {
	DirectoryListing* dl = new DirectoryListing(aUser, true, Util::toString(aProfile), true, 0, isOwnList);
	fire(DirectoryListingManagerListener::OpenListing(), dl, aXml);

	WLock l(cs);
	fileLists[aUser] = dl;
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

} //dcpp