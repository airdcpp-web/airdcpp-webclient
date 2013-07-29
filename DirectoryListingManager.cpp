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

#include "DirectoryListingManager.h"
#include "ClientManager.h"
#include "QueueManager.h"
#include "format.h"

#include <boost/range/algorithm/copy.hpp>

namespace dcpp {

using boost::range::for_each;
using boost::range::find_if;


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
			finishedListings.erase(i);
			i = finishedListings.begin();
		} else {
			i++;
		}
	}
}

void DirectoryListingManager::removeDirectoryDownload(const UserPtr& aUser, const string& aPath, bool isPartialList) {
	WLock l(cs);
	if (isPartialList) {
		auto dp = dlDirectories.equal_range(aUser) | map_values;
		auto udp = find_if(dp, [&aPath](const DirectoryDownloadInfo::Ptr& ddi) { return stricmp(aPath.c_str(), ddi->getListPath().c_str()) == 0; });
		if (udp != dp.end()) {
			dlDirectories.erase(udp.base());
		} else {
			dcassert(0);
		}
	} else {
		dlDirectories.erase(aUser);
	}
}

void DirectoryListingManager::addDirectoryDownload(const string& aRemoteDir, const string& aBundleName, const HintedUser& aUser, const string& aTarget, TargetUtil::TargetType aTargetType, SizeCheckMode aSizeCheckMode,
	QueueItemBase::Priority p, bool useFullList /*false*/, ProfileToken aAutoSearch /*0*/, bool checkNameDupes /*false*/, bool checkViewed /*true*/) noexcept {


	if (checkViewed) {
		RLock l(cs);
		auto i = viewedLists.find(aUser.user);
		if (i != viewedLists.end()) {
			i->second->addAsyncTask([=] {
				auto di = DirectoryDownloadInfo::Ptr(new DirectoryDownloadInfo(aUser, aBundleName, aRemoteDir, aTarget, aTargetType, p, aSizeCheckMode, aAutoSearch, false));
				handleDownload(di, i->second); 
			});
			return;
		}
	}

	if (aUser.user && !aUser.user->isSet(User::NMDC) && !aUser.user->isSet(User::TLS) && SETTING(TLS_MODE) == SettingsManager::TLS_FORCED) {
		//this is the only thing that could cause queuing the filelist to fail.... remember to change if more are added
		LogManager::getInstance()->message(ClientManager::getInstance()->getFormatedNicks(aUser) + ": " + STRING(SOURCE_NO_ENCRYPTION), LogManager::LOG_ERROR);
		return;
	}

	bool needList;
	{
		WLock l(cs);

		if (checkNameDupes && aAutoSearch > 0) {
			//don't download different directories for auto search items that don't allow it
			if (find_if(dlDirectories | map_values, DirectoryDownloadInfo::HasASItem(aAutoSearch, aBundleName)).base() != dlDirectories.end()  ||
				find_if(finishedListings | map_values, FinishedDirectoryItem::HasASItem(aAutoSearch, aBundleName)).base() != finishedListings.end())  {
					return;
			}
		}
		
		auto dp = dlDirectories.equal_range(aUser);
		
		for(auto i = dp.first; i != dp.second; ++i) {
			if (stricmp(aRemoteDir.c_str(), i->second->getListPath().c_str()) == 0)
				return;
		}
		
		// Unique directory, fine...
		dlDirectories.emplace(aUser.user, new DirectoryDownloadInfo(aUser, aBundleName, aRemoteDir, aTarget, aTargetType, p, aSizeCheckMode, aAutoSearch, true));
		needList = aUser.user->isSet(User::NMDC) ? (dp.first == dp.second) : true;
	}

	if(needList) {
		try {
			if (!aUser.user->isSet(User::NMDC) && !useFullList) {
				QueueManager::getInstance()->addList(aUser, QueueItem::FLAG_DIRECTORY_DOWNLOAD | QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_RECURSIVE_LIST, aRemoteDir);
			} else {
				QueueManager::getInstance()->addList(aUser, QueueItem::FLAG_DIRECTORY_DOWNLOAD, aRemoteDir);
			}
		} catch(const Exception&) {
			//We have a list queued already
		}
	}
}

void DirectoryListingManager::processList(const string& aFileName, const string& aXml, const HintedUser& user, const string& aRemotePath, int flags) {
	{
		RLock l(cs);
		auto p = viewedLists.find(user.user);
		if (p != viewedLists.end()) {
			if (p->second->getPartialList()) {
				if(flags & QueueItem::FLAG_TEXT) {
					//we don't want multiple threads to load those simultaneously. load in the list thread and return here after that
					p->second->addPartialListTask(aXml, aRemotePath, false, [=] { processListAction(p->second, aRemotePath, flags); });
					return;
				}
			}
		}
	}

	auto dirList = DirectoryListingPtr(new DirectoryListing(user, (flags & QueueItem::FLAG_PARTIAL_LIST) > 0, aFileName, false, false));
	try {
		if (flags & QueueItem::FLAG_TEXT) {
			MemoryInputStream mis(aXml);
			dirList->loadXML(mis, true, aRemotePath);
		}
		else {
			dirList->loadFile();
		}
	}
	catch (const Exception&) {
		LogManager::getInstance()->message(STRING(UNABLE_TO_OPEN_FILELIST) + " " + aFileName, LogManager::LOG_ERROR);
		return;
	}

	processListAction(dirList, aRemotePath, flags);
}

void DirectoryListingManager::handleDownload(DirectoryDownloadInfo::Ptr& di, DirectoryListingPtr& aList) {
	auto getList = [&] {
		addDirectoryDownload(di->getListPath(), di->getBundleName(), aList->getHintedUser(), di->getTarget(), di->getTargetType(), di->getSizeConfirm(), di->getPriority(), di->getRecursiveListAttempted() ? true : false, 0, false, false);
	};

	auto doDownload = [&](const string& aTargetPath) -> bool {
		auto dir = aList->findDirectory(di->getListPath());
		if (!dir) {
			//don't queue anything if it's a fresh list
			if (aList->getisClientView()) {
				getList();
			}
			return false;
		}

		if (aList->getPartialList() && dir->findIncomplete()) {
			getList();
			return false;
		}

		return aList->downloadDirImpl(dir, aTargetPath, di->getPriority(), di->getAutoSearch());
	};

	bool download = false;
	{
		RLock l(cs);
		auto p = finishedListings.find(di->getFinishedDirName());
		if (p != finishedListings.end()) {
			//we have downloaded with this dirname before...
			if (p->second->getState() == FinishedDirectoryItem::REJECTED) {
				return;
			}
			else if (p->second->getState() == FinishedDirectoryItem::ACCEPTED) {
				//download directly
				di->setTargetType(TargetUtil::TARGET_PATH);
				di->setTarget(p->second->getTargetPath());
				di->setPriority(p->second->getUsePausedPrio() ? QueueItem::PAUSED : di->getPriority());
				di->setSizeConfirm(NO_CHECK);
				p->second->addAutoSearch(di->getAutoSearch());
				download = true;
			}
			else if (p->second->getState() == FinishedDirectoryItem::WAITING_ACTION) {
				//add in the list to wait for action
				di->setListing(aList);
				di->setTarget(p->second->getTargetPath());
				p->second->addInfo(di);
				return;
			}
		}
	}

	if (download) {
		doDownload(di->getTarget());
		return;
	}

	//we have a new directory
	TargetUtil::TargetInfo ti;
	int64_t dirSize = aList->getDirSize(di->getListPath());
	TargetUtil::getVirtualTarget(di->getTarget(), di->getTargetType(), ti, dirSize);
	bool hasFreeSpace = ti.getFreeSpace() >= dirSize;
	ti.targetDir += Util::getLastDir(di->getListPath()) + PATH_SEPARATOR;

	if (di->getSizeConfirm() == REPORT_SYSLOG) {
		auto queued = doDownload(ti.targetDir);
		if (!hasFreeSpace && queued)
			TargetUtil::reportInsufficientSize(ti, dirSize);

		if (queued) {
			WLock l(cs);
			finishedListings.emplace(di->getFinishedDirName(), new FinishedDirectoryItem(!hasFreeSpace, ti.targetDir));
		}
	} else if (di->getSizeConfirm() == ASK_USER && !hasFreeSpace) {
		di->setListing(aList);
		auto fi = FinishedDirectoryItem::Ptr(new FinishedDirectoryItem(di, ti.targetDir));

		{
			WLock l(cs);
			finishedListings.emplace(di->getFinishedDirName(), fi);
		}

		string msg = TargetUtil::getInsufficientSizeMessage(ti, dirSize);
		fire(DirectoryListingManagerListener::PromptAction(), [&](bool accepted) { handleSizeConfirmation(fi, accepted); }, msg);
	}
	else {
		if (doDownload(ti.targetDir)) {
			WLock l(cs);
			finishedListings.emplace(di->getFinishedDirName(), new FinishedDirectoryItem(false, ti.targetDir));
		}
	}
}

void DirectoryListingManager::processListAction(DirectoryListingPtr aList, const string& path, int flags) {
	if(flags & QueueItem::FLAG_DIRECTORY_DOWNLOAD) {
		DirectoryDownloadInfo::List dl;
		{
			WLock l(cs);
			auto dp = dlDirectories.equal_range(aList->getHintedUser().user) | map_values;
			if ((flags & QueueItem::FLAG_PARTIAL_LIST) && !path.empty()) {
				//partial list
				auto udp = find_if(dp, [&path](const DirectoryDownloadInfo::Ptr& ddi) { return stricmp(path.c_str(), ddi->getListPath().c_str()) == 0; });
				if (udp != dp.end()) {
					dl.push_back(*udp);
					//dlDirectories.erase(udp.base());
				}
			} else {
				//full filelist
				dl.assign(boost::begin(dp), boost::end(dp));
				//dlDirectories.erase(aList->getHintedUser().user);
			}
		}

		if (dl.empty())
			return;

		for(auto& di: dl) {
			handleDownload(di, aList);
		}

		{
			WLock l(cs);
			if (flags & QueueItem::FLAG_PARTIAL_LIST) {
				auto dp = dlDirectories.equal_range(aList->getHintedUser().user);
				auto p = find(dp | map_values, dl.front());
				if (p.base() != dp.second) {
					dlDirectories.erase(p.base());
				}
			} else {
				dlDirectories.erase(aList->getHintedUser().user);
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

		LogManager::getInstance()->message(aList->getNick(false) + ": " + 
			AirUtil::formatMatchResults(matches, newFiles, bundles, (flags & QueueItem::FLAG_PARTIAL_LIST) > 0), LogManager::LOG_INFO);
	} else if((flags & QueueItem::FLAG_VIEW_NFO) && (flags & QueueItem::FLAG_PARTIAL_LIST)) {
		aList->findNfo(path);
	}
}

void DirectoryListingManager::handleSizeConfirmation(FinishedDirectoryItem::Ptr& aFinishedItem, bool accepted) {
	{
		WLock l(cs);
		aFinishedItem->setHandledState(accepted);
	}

	if (accepted) {
		for (auto& di : aFinishedItem->getDownloadInfos()) {
			di->getListing()->downloadDir(di->getListPath(), di->getTarget(), di->getPriority(), di->getAutoSearch());
		}
	}
	aFinishedItem->deleteListings();
}

void DirectoryListingManager::on(QueueManagerListener::Finished, const QueueItemPtr& qi, const string& dir, const HintedUser& aUser, int64_t /*aSpeed*/) noexcept {
	if (!qi->isSet(QueueItem::FLAG_CLIENT_VIEW) || !qi->isSet(QueueItem::FLAG_USER_LIST))
		return;

	{
		RLock l(cs);
		auto p = viewedLists.find(aUser.user);
		if (p != viewedLists.end()) {
			p->second->setFileName(qi->getListName());
			p->second->addFullListTask(dir);
			return;
		}
	}

	createList(aUser, qi->getListName(), dir);
}

void DirectoryListingManager::on(QueueManagerListener::PartialList, const HintedUser& aUser, const string& aXML, const string& aBase) noexcept {
	if (aXML.empty())
		return;

	{
		RLock l(cs);
		auto p = viewedLists.find(aUser.user);
		if (p != viewedLists.end()) {
			if (p->second->getPartialList()) {
				p->second->addPartialListTask(aXML, aBase);
			}
			return;
		}
	}

	createPartialList(aUser, aBase, aXML);
}

void DirectoryListingManager::on(QueueManagerListener::Removed, const QueueItemPtr& qi, bool finished) noexcept {
	if (finished)
		return;

	if (qi->isSet(QueueItem::FLAG_USER_LIST)) {
		auto u = qi->getSources()[0].getUser();
		if (qi->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD))
			removeDirectoryDownload(u, qi->getTempTarget(), qi->isSet(QueueItem::FLAG_PARTIAL_LIST));

		if (qi->isSet(QueueItem::FLAG_CLIENT_VIEW) && qi->isSet(QueueItem::FLAG_PARTIAL_LIST)) {
			RLock l(cs);
			auto p = viewedLists.find(u);
			if (p != viewedLists.end()) {
				if (p->second->getPartialList()) {
					p->second->onRemovedQueue(qi->getTempTarget());
				}
			}
		}
	}
}

void DirectoryListingManager::openOwnList(ProfileToken aProfile, bool useADL /*false*/) {
	auto me = HintedUser(ClientManager::getInstance()->getMe(), Util::emptyString);
	if (hasList(me.user))
		return;

	if (!useADL) {
		createPartialList(me, Util::emptyString, Util::emptyString, aProfile, true);
	} else {
		DirectoryListing* dl = new DirectoryListing(me, false, Util::toString(aProfile), true, true);
		dl->setMatchADL(true);
		fire(DirectoryListingManagerListener::OpenListing(), dl, Util::emptyString, Util::emptyString);

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
	fire(DirectoryListingManagerListener::OpenListing(), dl, aInitialDir, Util::emptyString);

	WLock l(cs);
	viewedLists[aUser.user] = DirectoryListingPtr(dl);
}

void DirectoryListingManager::createPartialList(const HintedUser& aUser, const string& aXml, const string& aDir /*emptyString*/, ProfileToken aProfile /*SP_DEFAULT*/, bool isOwnList /*false*/) {
	DirectoryListing* dl = new DirectoryListing(aUser, true, Util::toString(aProfile), true, isOwnList);
	fire(DirectoryListingManagerListener::OpenListing(), dl, aDir, aXml);

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