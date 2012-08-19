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
#include "format.h"

#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm/for_each.hpp>

namespace dcpp {

using boost::adaptors::map_values;
using boost::range::for_each;

class FinishedDirectoryItem {
public:
	enum WaitingState {
		WAITING_ACTION,
		ACCEPTED,
		REJECTED
	};

	FinishedDirectoryItem(DirectoryDownloadInfo* aDDI) : state(WAITING_ACTION), usePausedPrio(false) {  
		downloadInfos.push_back(aDDI); 
	}

	FinishedDirectoryItem(bool aUsePausedPrio) : state(ACCEPTED), usePausedPrio(aUsePausedPrio) { }

	~FinishedDirectoryItem() { 
		deleteListings();
	}
	
	void addInfo(DirectoryDownloadInfo* aDDI) {
		downloadInfos.push_back(aDDI);
	}

	void handleAction(bool accepted) {
		state = accepted ? ACCEPTED : REJECTED;
		//deleteListings();
	}

	void deleteListings() {
		boost::for_each(downloadInfos, DeleteFunction());
		downloadInfos.clear();
	}

	GETSET(WaitingState, state, State);
	GETSET(vector<DirectoryDownloadInfo*>, downloadInfos, DownloadInfos);
	GETSET(string, targetPath, TargetPath);
	GETSET(bool, usePausedPrio, UsePausedPrio);
private:
	
};

class DirectoryDownloadInfo {
public:
	DirectoryDownloadInfo() : priority(QueueItem::DEFAULT) { }
	DirectoryDownloadInfo(const UserPtr& aUser, const string& aListPath, const string& aTarget, TargetUtil::TargetType aTargetType, QueueItem::Priority p, SizeCheckMode aPromptSizeConfirm) : 
		listPath(aListPath), target(aTarget), priority(p), user(aUser), targetType(aTargetType), sizeConfirm(aPromptSizeConfirm), listing(nullptr) { }
	~DirectoryDownloadInfo() { 
		if (listing)
			delete listing;
	}
	
	UserPtr& getUser() { return user; }
	void setUser(const UserPtr& aUser) { user = aUser; }
	
	GETSET(SizeCheckMode, sizeConfirm, SizeConfirm);
	GETSET(string, listPath, ListPath);
	GETSET(string, target, Target);
	GETSET(QueueItem::Priority, priority, Priority);
	GETSET(TargetUtil::TargetType, targetType, TargetType);
	GETSET(DirectoryListing*, listing, Listing);

	//string getLocalPath() { return target + Util::getLastDir(listPath); }
	string getDirName() { return Util::getLastDir(listPath); }
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

void DirectoryListingManager::addDirectoryDownload(const string& aDir, const HintedUser& aUser, const string& aTarget, TargetUtil::TargetType aTargetType, SizeCheckMode aSizeCheckMode,  
	QueueItem::Priority p /* = QueueItem::DEFAULT */, bool useFullList) noexcept {

	bool needList;
	{
		WLock l(cs);
		
		auto dp = directories.equal_range(aUser);
		
		for(auto i = dp.first; i != dp.second; ++i) {
			if(stricmp(aTarget.c_str(), i->second->getListPath().c_str()) == 0)
				return;
		}
		
		// Unique directory, fine...
		directories.insert(make_pair(aUser, new DirectoryDownloadInfo(aUser, aDir, aTarget, aTargetType, p, aSizeCheckMode)));
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
	DirectoryListing* dirList =  new DirectoryListing(user, (flags & QueueItem::FLAG_PARTIAL_LIST) > 0, name, false);
	try {
		if(flags & QueueItem::FLAG_TEXT) {
			MemoryInputStream mis(name);
			dirList->loadXML(mis, true);
		} else {
			dirList->loadFile(name);
		}
	} catch(const Exception&) {
		LogManager::getInstance()->message(STRING(UNABLE_TO_OPEN_FILELIST) + " " + name, LogManager::LOG_ERROR);
		delete dirList;
		return;
	}

	if(flags & QueueItem::FLAG_DIRECTORY_DOWNLOAD) {
		vector<DirectoryDownloadInfo*> dl;
		{
			WLock l(cs);
			if ((flags & QueueItem::FLAG_PARTIAL_LIST) && !path.empty()) {
				//partial list
				auto dp = directories.equal_range(user);
				auto udp = find_if(dp.first, dp.second, [path](pair<UserPtr, DirectoryDownloadInfo*> ud) { return stricmp(path.c_str(), ud.second->getListPath().c_str()) == 0; });
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

		// TODO: check that the targettype matches for existing directories
		for(auto i = dl.begin(); i != dl.end(); ++i) {
			auto di = *i;

			{
				RLock l (cs);
				auto p = finishedListings.find(di->getDirName());
				if (p != finishedListings.end()) {
					//we have downloaded with this dirname before...
					if (p->second->getState() == FinishedDirectoryItem::REJECTED) {
						delete di;
					} else if (p->second->getState() == FinishedDirectoryItem::ACCEPTED) {
						//download directly
						dirList->download(di->getListPath(), p->second->getTargetPath(), di->getTargetType(), false, p->second->getUsePausedPrio() ? QueueItem::PAUSED : di->getPriority());
						delete di;
					} else if (p->second->getState() == FinishedDirectoryItem::WAITING_ACTION) {
						//add in the list to wait for action
						di->setListing(dirList);
						di->setTarget(p->second->getTargetPath());
						p->second->addInfo(di);
					}
					return;
				}
			}

			string path;
			TargetUtil::TargetInfo ti;
			int64_t dirSize = dirList->getDirSize(di->getListPath());


			//we have a new directory
			bool hasFreeSpace =  TargetUtil::getVirtualTarget(di->getTarget(), di->getTargetType(), ti, dirSize);
			di->setTarget(ti.targetDir);

			if (di->getSizeConfirm() == REPORT_SYSLOG) {
				if (!hasFreeSpace)
					TargetUtil::reportInsufficientSize(ti, dirSize);

				dirList->download(di->getListPath(), di->getTarget(), di->getTargetType(), false, !hasFreeSpace ? QueueItem::PAUSED : di->getPriority());
				{
					WLock l (cs);
					finishedListings[di->getDirName()] = new FinishedDirectoryItem(!hasFreeSpace);
				}
				delete di;
			} else if (di->getSizeConfirm() == ASK_USER) {
				di->setListing(dirList);
				{
					WLock l (cs);
					finishedListings[di->getDirName()] = new FinishedDirectoryItem(di);
				}

				string msg = TargetUtil::getInsufficientSizeMessage(ti, dirSize);
				fire(DirectoryListingManagerListener::PromptAction(), di->getDirName(), msg);
			}
		}
	}

	if(flags & QueueItem::FLAG_MATCH_QUEUE) {
		int matches=0, newFiles=0;
		BundleList bundles;
		QueueManager::getInstance()->matchListing(*dirList, matches, newFiles, bundles);
		if ((flags & QueueItem::FLAG_PARTIAL_LIST) && (!SETTING(REPORT_ADDED_SOURCES) || newFiles == 0 || bundles.empty())) {
			delete dirList;
			return;
		}

		LogManager::getInstance()->message(Util::toString(ClientManager::getInstance()->getNicks(user)) + ": " + 
			AirUtil::formatMatchResults(matches, newFiles, bundles, (flags & QueueItem::FLAG_PARTIAL_LIST) > 0), LogManager::LOG_INFO);
	} else if((flags & QueueItem::FLAG_VIEW_NFO) && (flags & QueueItem::FLAG_PARTIAL_LIST)) {
		dirList->findNfo(path);
	}

	delete dirList;
}

void DirectoryListingManager::handleSizeConfirmation(const string& aName, bool accepted) {
	FinishedDirectoryItem* wdi = nullptr;
	{
		WLock l(cs);
		auto p = finishedListings.find(aName);
		if (p != finishedListings.end()) {
			p->second->handleAction(accepted);
			finishedListings.erase(p);
			wdi = p->second;
		} else {
			dcassert(0);
			return;
		}
	}

	if (accepted) {
		boost::for_each(wdi->getDownloadInfos(), [](DirectoryDownloadInfo* di) {
			di->getListing()->download(di->getListPath(), di->getTarget(), di->getTargetType(), false, di->getPriority());
		});
	}
	wdi->deleteListings();
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
	if (text.empty())
		return;

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