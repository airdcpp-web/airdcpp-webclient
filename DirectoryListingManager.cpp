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
#include <boost/range/algorithm/find_if.hpp>

namespace dcpp {

using boost::adaptors::map_values;
using boost::range::for_each;
using boost::range::find_if;


class DirectoryDownloadInfo {
public:
	DirectoryDownloadInfo() : priority(QueueItem::DEFAULT) { }
	DirectoryDownloadInfo(const UserPtr& aUser, const string& aListPath, const string& aTarget, TargetUtil::TargetType aTargetType, QueueItem::Priority p, 
		SizeCheckMode aPromptSizeConfirm, ProfileToken aAutoSearch) : 
		listPath(aListPath), target(aTarget), priority(p), user(aUser), targetType(aTargetType), sizeConfirm(aPromptSizeConfirm), listing(nullptr), autoSearch(aAutoSearch) { }
	~DirectoryDownloadInfo() { }
	
	UserPtr& getUser() { return user; }
	void setUser(const UserPtr& aUser) { user = aUser; }
	
	GETSET(SizeCheckMode, sizeConfirm, SizeConfirm);
	GETSET(string, listPath, ListPath);
	GETSET(string, target, Target);
	GETSET(QueueItem::Priority, priority, Priority);
	GETSET(TargetUtil::TargetType, targetType, TargetType);
	GETSET(DirectoryListingPtr, listing, Listing);
	GETSET(ProfileToken, autoSearch, AutoSearch);

	string getFinishedDirName() { return target + Util::getLastDir(listPath) + Util::toString(targetType); }

	struct HasASItem {
		HasASItem(ProfileToken aToken, const string& s) : a(s), t(aToken) { }
		bool operator()(const DirectoryDownloadInfo* ddi) const { return t == ddi->getAutoSearch() && stricmp(a, Util::getLastDir(ddi->getListPath())) != 0; }
		const string& a;
		ProfileToken t;
	private:
		HasASItem& operator=(const HasASItem&);
	};
private:
	UserPtr user;
};

class FinishedDirectoryItem {
public:
	enum WaitingState {
		WAITING_ACTION,
		ACCEPTED,
		REJECTED
	};

	FinishedDirectoryItem(DirectoryDownloadInfo* aDDI, const string& aTargetPath) : state(WAITING_ACTION), usePausedPrio(false), targetPath(aTargetPath), timeDownloaded(0) {  
		downloadInfos.push_back(aDDI); 
	}

	FinishedDirectoryItem(bool aUsePausedPrio, const string& aTargetPath) : state(ACCEPTED), usePausedPrio(aUsePausedPrio), targetPath(aTargetPath), timeDownloaded(GET_TICK()) { }

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
	GETSET(uint64_t, timeDownloaded, TimeDownloaded);

	bool hasASItems(ProfileToken as, const string& aName) const {
		return find_if(downloadInfos, DirectoryDownloadInfo::HasASItem(as, aName)) != downloadInfos.end();
	}
private:
	
};

DirectoryListingManager::DirectoryListingManager() {
	TimerManager::getInstance()->addListener(this);
	QueueManager::getInstance()->addListener(this);
}

DirectoryListingManager::~DirectoryListingManager() {
	QueueManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
}

void DirectoryListingManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	WLock l(cs);
	for(auto i = finishedListings.begin(); i != finishedListings.end();) {
		if(i->second->getState() != FinishedDirectoryItem::WAITING_ACTION && i->second->getTimeDownloaded() + 5*60*1000 < aTick) {
			delete i->second;
			finishedListings.erase(i);
			i = finishedListings.begin();
		} else {
			i++;
		}
	}
}

void DirectoryListingManager::removeDirectoryDownload(const UserPtr aUser) {
	WLock l(cs);
	auto dp = dlDirectories.equal_range(aUser);
	for(auto i = dp.first; i != dp.second; ++i) {
		delete i->second;
	}
	dlDirectories.erase(aUser);
}

void DirectoryListingManager::addDirectoryDownload(const string& aDir, const HintedUser& aUser, const string& aTarget, TargetUtil::TargetType aTargetType, SizeCheckMode aSizeCheckMode,  
	QueueItem::Priority p /* = QueueItem::DEFAULT */, bool useFullList /*false*/, ProfileToken aAutoSearch) noexcept {

	bool needList;
	{
		WLock l(cs);

		if (aAutoSearch > 0) {
			//check for dupes
			auto d = move(Util::getLastDir(aDir));
			if (find_if(dlDirectories | map_values, DirectoryDownloadInfo::HasASItem(aAutoSearch, d)).base() != dlDirectories.end() ||
				find_if(finishedListings | map_values, [aAutoSearch, &d](const FinishedDirectoryItem* fdi) { return fdi->hasASItems(aAutoSearch, d); }).base() != finishedListings.end())
				return;
		}
		
		auto dp = dlDirectories.equal_range(aUser);
		
		for(auto i = dp.first; i != dp.second; ++i) {
			if(stricmp(aTarget.c_str(), i->second->getListPath().c_str()) == 0)
				return;
		}
		
		// Unique directory, fine...
		dlDirectories.insert(make_pair(aUser, new DirectoryDownloadInfo(aUser, aDir, aTarget, aTargetType, p, aSizeCheckMode, aAutoSearch)));
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
	{
		RLock l(cs);
		auto p = viewedLists.find(user.user);
		if (p != viewedLists.end()) {
			if (p->second->getPartialList()) {
				if(flags & QueueItem::FLAG_TEXT) {
					//we don't want multiple threads to load those simultaneously. load in the list thread and return here after that
					p->second->addPartialListTask(name, [p, this, path, flags] { processListAction(p->second, path, flags); });
					return;
				}
			}
		}
	}

	DirectoryListing* dirList = new DirectoryListing(user, (flags & QueueItem::FLAG_PARTIAL_LIST) > 0, name, false, false);
	try {
		if(flags & QueueItem::FLAG_TEXT) {
			MemoryInputStream mis(name);
			dirList->loadXML(mis, true);
		} else {
			dirList->loadFile(name);
		}
	} catch(const Exception&) {
		LogManager::getInstance()->message(STRING(UNABLE_TO_OPEN_FILELIST) + " " + name, LogManager::LOG_ERROR);
		return;
	}

	processListAction(DirectoryListingPtr(dirList), path, flags);
}

void DirectoryListingManager::processListAction(DirectoryListingPtr aList, const string& path, int flags) {
	if(flags & QueueItem::FLAG_DIRECTORY_DOWNLOAD) {
		vector<DirectoryDownloadInfo*> dl;
		{
			WLock l(cs);
			if ((flags & QueueItem::FLAG_PARTIAL_LIST) && !path.empty()) {
				//partial list
				auto dp = dlDirectories.equal_range(aList->getHintedUser().user);
				auto udp = find_if(dp, [path](pair<UserPtr, DirectoryDownloadInfo*> ud) { return stricmp(path.c_str(), ud.second->getListPath().c_str()) == 0; });
				if (udp != dp.second) {
					dl.push_back(udp->second);
					dlDirectories.erase(udp);
				}
			} else {
				//full filelist
				auto dpf = dlDirectories.equal_range(aList->getHintedUser().user) | map_values;
				dl.assign(boost::begin(dpf), boost::end(dpf));
				dlDirectories.erase(aList->getHintedUser().user);
			}
		}

		for(auto i = dl.begin(); i != dl.end(); ++i) {
			auto di = *i;

			bool download = false;
			{
				RLock l (cs);
				auto p = finishedListings.find(di->getFinishedDirName());
				if (p != finishedListings.end()) {
					//we have downloaded with this dirname before...
					if (p->second->getState() == FinishedDirectoryItem::REJECTED) {
						delete di;
						continue;
					} else if (p->second->getState() == FinishedDirectoryItem::ACCEPTED) {
						//download directly
						di->setTarget(p->second->getTargetPath());
						di->setPriority(p->second->getUsePausedPrio() ? QueueItem::PAUSED : di->getPriority());
						download = true;
					} else if (p->second->getState() == FinishedDirectoryItem::WAITING_ACTION) {
						//add in the list to wait for action
						di->setListing(aList);
						di->setTarget(p->second->getTargetPath());
						p->second->addInfo(di);
						continue;
					}
				}
			}

			if (download) {
				aList->downloadDir(di->getListPath(), di->getTarget(), TargetUtil::TARGET_PATH, false, di->getPriority(), di->getAutoSearch());
				delete di;
				continue;
			}

			//we have a new directory
			TargetUtil::TargetInfo ti;
			int64_t dirSize = aList->getDirSize(di->getListPath());
			TargetUtil::getVirtualTarget(di->getTarget(), di->getTargetType(), ti, dirSize);
			bool hasFreeSpace = ti.getFreeSpace() >= dirSize;

			if (di->getSizeConfirm() == REPORT_SYSLOG) {
				if (!hasFreeSpace)
					TargetUtil::reportInsufficientSize(ti, dirSize);

				aList->downloadDir(di->getListPath(), ti.targetDir, TargetUtil::TARGET_PATH, false, !hasFreeSpace ? QueueItem::PAUSED : di->getPriority(), di->getAutoSearch());
				{
					WLock l (cs);
					finishedListings[di->getFinishedDirName()] = new FinishedDirectoryItem(!hasFreeSpace, ti.targetDir);
				}
				delete di;
			} else if (di->getSizeConfirm() == ASK_USER && !hasFreeSpace) {
				di->setListing(aList);
				{
					WLock l (cs);
					finishedListings[di->getFinishedDirName()] = new FinishedDirectoryItem(di, ti.targetDir);
				}

				string msg = TargetUtil::getInsufficientSizeMessage(ti, dirSize);
				fire(DirectoryListingManagerListener::PromptAction(), di->getFinishedDirName(), msg);
			} else {
				aList->downloadDir(di->getListPath(), ti.targetDir, TargetUtil::TARGET_PATH, false, di->getPriority(), di->getAutoSearch());

				WLock l (cs);
				finishedListings[di->getFinishedDirName()] = new FinishedDirectoryItem(false, ti.targetDir);
				delete di;
			}
		}
	}

	if(flags & QueueItem::FLAG_MATCH_QUEUE) {
		int matches=0, newFiles=0;
		BundleList bundles;
		QueueManager::getInstance()->matchListing(*aList, matches, newFiles, bundles);
		if ((flags & QueueItem::FLAG_PARTIAL_LIST) && (!SETTING(REPORT_ADDED_SOURCES) || newFiles == 0 || bundles.empty())) {
			return;
		}

		LogManager::getInstance()->message(Util::toString(ClientManager::getInstance()->getNicks(aList->getHintedUser())) + ": " + 
			AirUtil::formatMatchResults(matches, newFiles, bundles, (flags & QueueItem::FLAG_PARTIAL_LIST) > 0), LogManager::LOG_INFO);
	} else if((flags & QueueItem::FLAG_VIEW_NFO) && (flags & QueueItem::FLAG_PARTIAL_LIST)) {
		aList->findNfo(path);
	}
}

void DirectoryListingManager::handleSizeConfirmation(const string& aTarget, bool accepted) {
	FinishedDirectoryItem* wdi = nullptr;
	{
		WLock l(cs);
		auto p = finishedListings.find(aTarget);
		if (p != finishedListings.end()) {
			p->second->handleAction(accepted);
			wdi = p->second;
		} else {
			dcassert(0);
			return;
		}
	}

	if (accepted) {
		for_each(wdi->getDownloadInfos(), [](DirectoryDownloadInfo* di) {
			di->getListing()->downloadDir(di->getListPath(), di->getTarget(), di->getTargetType(), false, di->getPriority(), di->getAutoSearch());
		});
	}
	wdi->deleteListings();
}

void DirectoryListingManager::on(QueueManagerListener::Finished, const QueueItemPtr qi, const string& dir, const HintedUser& aUser, int64_t /*aSpeed*/) noexcept {
	if (!qi->isSet(QueueItem::FLAG_CLIENT_VIEW) || !qi->isSet(QueueItem::FLAG_USER_LIST))
		return;

	{
		RLock l(cs);
		auto p = viewedLists.find(aUser.user);
		if (p != viewedLists.end()) {
			p->second->setReloading(true);
			p->second->setFileName(qi->getListName());
			p->second->addFullListTask(dir);
			return;
		}
	}

	createList(aUser, qi->getListName(), dir);
}

void DirectoryListingManager::on(QueueManagerListener::PartialList, const HintedUser& aUser, const string& text) noexcept {
	if (text.empty())
		return;

	{
		RLock l(cs);
		auto p = viewedLists.find(aUser.user);
		if (p != viewedLists.end()) {
			if (p->second->getPartialList()) {
				p->second->addPartialListTask(text);
			}
			return;
		}
	}

	createPartialList(aUser, text);
}

void DirectoryListingManager::openOwnList(ProfileToken aProfile, bool useADL /*false*/) {
	auto me = HintedUser(ClientManager::getInstance()->getMe(), Util::emptyString);
	if (hasList(me.user))
		return;

	if (!useADL) {
		createPartialList(me, Util::emptyString, aProfile, true);
	} else {
		DirectoryListing* dl = new DirectoryListing(me, false, Util::toString(aProfile), true, true);
		dl->setMatchADL(true);
		fire(DirectoryListingManagerListener::OpenListing(), dl, Util::emptyString);

		WLock l(cs);
		viewedLists[me.user] = DirectoryListingPtr(dl);
	}
}

void DirectoryListingManager::openFileList(const HintedUser& aUser, const string& aFile) {
	if (hasList(aUser.user))
		return;

	createList(aUser, aFile);
}

void DirectoryListingManager::createList(const HintedUser& aUser, const string& aFile, const string& aInitialDir /*empty*/, bool isOwnList /*false*/) {
	DirectoryListing* dl = new DirectoryListing(aUser, false, aFile, true, isOwnList);
	fire(DirectoryListingManagerListener::OpenListing(), dl, aInitialDir);

	WLock l(cs);
	viewedLists[aUser.user] = DirectoryListingPtr(dl);
}

void DirectoryListingManager::createPartialList(const HintedUser& aUser, const string& aXml, ProfileToken aProfile, bool isOwnList /*false*/) {
	DirectoryListing* dl = new DirectoryListing(aUser, true, Util::toString(aProfile), true, isOwnList);
	fire(DirectoryListingManagerListener::OpenListing(), dl, aXml);

	WLock l(cs);
	viewedLists[aUser] = DirectoryListingPtr(dl);
}

bool DirectoryListingManager::hasList(const UserPtr& aUser) {
	RLock l (cs);
	auto p = viewedLists.find(aUser);
	if (p != viewedLists.end()) {
		return true;
	}

	return false;
}

void DirectoryListingManager::removeList(const UserPtr& aUser) {
	WLock l (cs);
	auto p = viewedLists.find(aUser);
	if (p != viewedLists.end()) {
		viewedLists.erase(p);
	}
}

} //dcpp