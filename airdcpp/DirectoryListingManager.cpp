/*
 * Copyright (C) 2011-2016 AirDC++ Project
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

#include "AirUtil.h"
#include "ClientManager.h"
#include "DirectoryListingManager.h"
#include "LogManager.h"
#include "QueueManager.h"

#include <boost/range/algorithm/copy.hpp>

namespace dcpp {

using boost::range::for_each;
using boost::range::find_if;


DirectoryListingManager::DirectoryListingManager() noexcept {
	QueueManager::getInstance()->addListener(this);
}

DirectoryListingManager::~DirectoryListingManager() noexcept {
	QueueManager::getInstance()->removeListener(this);
}

void DirectoryListingManager::removeDirectoryDownload(const UserPtr& aUser, const string& aPath, bool isPartialList) noexcept {
	WLock l(cs);
	if (isPartialList) {
		auto dp = dlDirectories.equal_range(aUser) | map_values;
		auto udp = find_if(dp, [&aPath](const DirectoryDownloadInfo::Ptr& ddi) { return Util::stricmp(aPath.c_str(), ddi->getListPath().c_str()) == 0; });
		if (udp != dp.end()) {
			dlDirectories.erase(udp.base());
		} else {
			dcassert(0);
		}
	} else {
		dlDirectories.erase(aUser);
	}
}

bool DirectoryListingManager::DirectoryDownloadInfo::HasOwner::operator()(const DirectoryDownloadInfo::Ptr& ddi) const noexcept {
	return owner == ddi->getOwner() && Util::stricmp(a, ddi->getBundleName()) != 0; 
}

void DirectoryListingManager::addDirectoryDownload(const string& aRemoteDir, const string& aBundleName, const HintedUser& aUser, const string& aTarget,
	Priority aPriority, bool aUseFullList /*false*/, void* aOwner /*nullptr*/, bool aCheckNameDupes /*false*/, bool aCheckViewed /*true*/) noexcept {


	if (aCheckViewed) {
		RLock l(cs);
		auto i = viewedLists.find(aUser.user);
		if (i != viewedLists.end()) {
			auto dl = i->second;
			dl->addAsyncTask([=] {
				auto di = make_shared<DirectoryDownloadInfo>(aUser, aBundleName, aRemoteDir, aTarget, aPriority, aOwner, false);
				handleDownload(di, dl);
			});
			return;
		}
	}

	if (aUser.user && !aUser.user->isSet(User::NMDC) && !aUser.user->isSet(User::TLS) && SETTING(TLS_MODE) == SettingsManager::TLS_FORCED) {
		//this is the only thing that could cause queuing the filelist to fail.... remember to change if more are added
		LogManager::getInstance()->message(ClientManager::getInstance()->getFormatedNicks(aUser) + ": " + STRING(SOURCE_NO_ENCRYPTION), LogMessage::SEV_ERROR);
		return;
	}

	bool needList;
	{
		WLock l(cs);

		if (aCheckNameDupes && aOwner > 0) {
			//don't download different directories for auto search items that don't allow it
			if (find_if(dlDirectories | map_values, DirectoryDownloadInfo::HasOwner(aOwner, aBundleName)).base() != dlDirectories.end())  {
				return;
			}
		}
		
		// List already queued from this user?
		auto dp = dlDirectories.equal_range(aUser);
		for(auto i = dp.first; i != dp.second; ++i) {
			if (Util::stricmp(aRemoteDir.c_str(), i->second->getListPath().c_str()) == 0) {
				return;
			}
		}
		
		// Unique directory, fine...
		dlDirectories.emplace(aUser.user, make_shared<DirectoryDownloadInfo>(aUser, aBundleName, aRemoteDir, aTarget, aPriority, aOwner, true));
		needList = aUser.user->isSet(User::NMDC) ? (dp.first == dp.second) : true;
	}

	if (needList) {
		try {
			if (!aUser.user->isSet(User::NMDC) && !aUseFullList) {
				QueueManager::getInstance()->addList(aUser, QueueItem::FLAG_DIRECTORY_DOWNLOAD | QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_RECURSIVE_LIST, aRemoteDir);
			} else {
				QueueManager::getInstance()->addList(aUser, QueueItem::FLAG_DIRECTORY_DOWNLOAD, aRemoteDir);
			}
		} catch(const Exception&) {
			//We have a list queued already
		}
	}
}

DirectoryListingManager::DirectoryListingMap DirectoryListingManager::getLists() const noexcept {
	RLock l(cs);
	return viewedLists;
}

void DirectoryListingManager::processList(const string& aFileName, const string& aXml, const HintedUser& aUser, const string& aRemotePath, int aFlags) noexcept {
	auto isPartialList = (aFlags & QueueItem::FLAG_PARTIAL_LIST) > 0;

	{
		RLock l(cs);
		auto p = viewedLists.find(aUser.user);
		if (p != viewedLists.end()) {
			if (p->second->getPartialList() && isPartialList) {
				//we don't want multiple threads to load those simultaneously. load in the list thread and return here after that
				p->second->addPartialListTask(aXml, aRemotePath, true, [=] { processListAction(p->second, aRemotePath, aFlags); });
				return;
			}
		}
	}

	auto dirList = DirectoryListingPtr(new DirectoryListing(aUser, isPartialList, aFileName, false, false));
	try {
		if (isPartialList) {
			dirList->loadPartialXml(aXml, Util::toAdcFile(aRemotePath));
		} else {
			dirList->loadFile();
		}
	} catch (const Exception& e) {
		LogManager::getInstance()->message(STRING_F(LIST_LOAD_FAILED, aFileName % e.getError()), LogMessage::SEV_ERROR);
		return;
	}

	processListAction(dirList, aRemotePath, aFlags);
}

bool DirectoryListingManager::handleDownload(const DirectoryDownloadInfo::Ptr& di, const DirectoryListingPtr& aList) noexcept {
	auto getList = [&] {
		addDirectoryDownload(di->getListPath(), di->getBundleName(), aList->getHintedUser(), di->getTarget(), di->getPriority(), di->getRecursiveListAttempted() ? true : false, di->getOwner(), false, false);
	};

	auto dir = aList->findDirectory(di->getListPath());
	if (!dir) {
		// Downloading directory for an open list? But don't queue anything if it's a fresh list and the directory is missing.
		if (aList->getisClientView()) {
			getList();
		}
		return false;
	}

	if (aList->getPartialList() && dir->findIncomplete()) {
		// Non-recursive partial list
		getList();
		return false;
	}

	// Queue the directory
	return aList->downloadDirImpl(dir, di->getTarget() + di->getBundleName() + PATH_SEPARATOR, di->getPriority(), di->getOwner());
}

void DirectoryListingManager::processListAction(DirectoryListingPtr aList, const string& aPath, int aFlags) noexcept {
	if(aFlags & QueueItem::FLAG_DIRECTORY_DOWNLOAD) {
		DirectoryDownloadInfo::List dl;
		{
			WLock l(cs);
			auto dp = dlDirectories.equal_range(aList->getHintedUser().user) | map_values;
			if ((aFlags & QueueItem::FLAG_PARTIAL_LIST) && !aPath.empty()) {
				//partial list
				auto udp = find_if(dp, [&aPath](const DirectoryDownloadInfo::Ptr& ddi) { return Util::stricmp(aPath.c_str(), ddi->getListPath().c_str()) == 0; });
				if (udp != dp.end()) {
					dl.push_back(*udp);
				}
			} else {
				//full filelist
				dl.assign(boost::begin(dp), boost::end(dp));
			}
		}

		if (dl.empty())
			return;

		for (const auto& di: dl) {
			handleDownload(di, aList);
		}

		{
			WLock l(cs);
			if (aFlags & QueueItem::FLAG_PARTIAL_LIST) {
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

	if(aFlags & QueueItem::FLAG_MATCH_QUEUE) {
		int matches=0, newFiles=0;
		BundleList bundles;
		QueueManager::getInstance()->matchListing(*aList, matches, newFiles, bundles);
		if ((aFlags & QueueItem::FLAG_PARTIAL_LIST) && (!SETTING(REPORT_ADDED_SOURCES) || newFiles == 0 || bundles.empty())) {
			return;
		}

		LogManager::getInstance()->message(aList->getNick(false) + ": " + 
			AirUtil::formatMatchResults(matches, newFiles, bundles), LogMessage::SEV_INFO);
	} else if((aFlags & QueueItem::FLAG_VIEW_NFO) && (aFlags & QueueItem::FLAG_PARTIAL_LIST)) {
		aList->addViewNfoTask(aPath, false);
	}
}

void DirectoryListingManager::on(QueueManagerListener::ItemFinished, const QueueItemPtr& qi, const string& dir, const HintedUser& aUser, int64_t /*aSpeed*/) noexcept {
	if (!qi->isSet(QueueItem::FLAG_CLIENT_VIEW) || !qi->isSet(QueueItem::FLAG_USER_LIST))
		return;

	DirectoryListingPtr dl;
	{
		RLock l(cs);
		auto p = viewedLists.find(aUser.user);
		if (p == viewedLists.end()) {
			return;
		}

		dl = p->second;
	}

	if (dl) {
		dl->setFileName(qi->getListName());
		if (dl->hasCompletedDownloads()) {
			dl->addFullListTask(dir);
		} else {
			fire(DirectoryListingManagerListener::OpenListing(), dl, dir, Util::emptyString);
		}
	}
}

void DirectoryListingManager::on(QueueManagerListener::PartialListFinished, const HintedUser& aUser, const string& aXML, const string& aBase) noexcept {
	if (aXML.empty())
		return;

	DirectoryListingPtr dl;
	{
		RLock l(cs);
		auto p = viewedLists.find(aUser.user);
		if (p != viewedLists.end() && p->second->getPartialList()) {
			dl = p->second;
		} else {
			return;
		}
	}

	if (dl->hasCompletedDownloads()) {
		dl->addHubUrlChangeTask(aUser.hint);
		dl->addPartialListTask(aXML, aBase);
	} else {
		fire(DirectoryListingManagerListener::OpenListing(), dl, aBase, aXML);
	}
}

void DirectoryListingManager::on(QueueManagerListener::ItemRemoved, const QueueItemPtr& qi, bool aFinished) noexcept {
	if (!qi->isSet(QueueItem::FLAG_USER_LIST))
		return;

	auto u = qi->getSources()[0].getUser();
	if (qi->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) && !aFinished)
		removeDirectoryDownload(u, qi->getTempTarget(), qi->isSet(QueueItem::FLAG_PARTIAL_LIST));

	if (qi->isSet(QueueItem::FLAG_CLIENT_VIEW)) {
		DirectoryListingPtr dl = nullptr;

		{
			RLock l(cs);
			auto p = viewedLists.find(u);
			if (p == viewedLists.end()) {
				dcassert(0);
				return;
			}

			dl = p->second;
		}

		dl->onListRemovedQueue(qi->getTarget(), qi->getTempTarget(), aFinished);

		bool closing = (dl->getClosing() || !dl->hasCompletedDownloads());
		if (!aFinished && !dl->hasDownloads() && closing) {
			removeList(u);
		}
	}
}

void DirectoryListingManager::openOwnList(ProfileToken aProfile, bool useADL /*false*/) noexcept {
	auto me = HintedUser(ClientManager::getInstance()->getMe(), Util::emptyString);

	auto dl = hasList(me.user);
	if (dl) {
		dl->addShareProfileChangeTask(aProfile);
		return;
	}

	dl = createList(me, !useADL, Util::toString(aProfile), true);
	dl->setMatchADL(useADL);

	fire(DirectoryListingManagerListener::OpenListing(), dl, Util::emptyString, Util::emptyString);
}

void DirectoryListingManager::openFileList(const HintedUser& aUser, const string& aFile) noexcept {
	if (hasList(aUser.user))
		return;

	auto dl = createList(aUser, false, aFile, false);
	fire(DirectoryListingManagerListener::OpenListing(), dl, Util::emptyString, Util::emptyString);
}

DirectoryListingPtr DirectoryListingManager::createList(const HintedUser& aUser, bool aPartial, const string& aFileName, bool aIsOwnList) noexcept {
	auto dl = DirectoryListingPtr(new DirectoryListing(aUser, aPartial, aFileName, true, aIsOwnList));

	{
		WLock l(cs);
		viewedLists[dl->getHintedUser()] = dl;
	}

	fire(DirectoryListingManagerListener::ListingCreated(), dl);
	return dl;
}

void DirectoryListingManager::on(QueueManagerListener::ItemAdded, const QueueItemPtr& aQI) noexcept {
	if (!aQI->isSet(QueueItem::FLAG_CLIENT_VIEW) || !aQI->isSet(QueueItem::FLAG_USER_LIST))
		return;

	auto user = aQI->getSources()[0].getUser();
	auto dl = hasList(user);
	if (dl) {
		dl->onAddedQueue(aQI->getTarget());
		return;
	}

	if (!aQI->isSet(QueueItem::FLAG_PARTIAL_LIST)) {
		dl = createList(user, false, aQI->getListName(), false);
	} else {
		dl = createList(user, true, Util::emptyString, false);
	}

	dl->onAddedQueue(aQI->getTarget());
}

DirectoryListingPtr DirectoryListingManager::hasList(const UserPtr& aUser) noexcept {
	RLock l (cs);
	auto p = viewedLists.find(aUser);
	if (p != viewedLists.end()) {
		p->second->setActive();
		return p->second;
	}

	return nullptr;
}

bool DirectoryListingManager::removeList(const UserPtr& aUser) noexcept {
	DirectoryListingPtr dl;

	{
		RLock l(cs);
		auto p = viewedLists.find(aUser);
		if (p == viewedLists.end()) {
			return false;
		}

		dl = p->second;
	}

	auto downloads = dl->getDownloads();
	if (!downloads.empty()) {
		dl->setClosing(true);

		// It will come back here after being removed from the queue
		for (const auto& p : downloads) {
			QueueManager::getInstance()->removeFile(p);
		}
	} else {
		{
			WLock l(cs);
			viewedLists.erase(aUser);
		}

		dl->close();
		fire(DirectoryListingManagerListener::ListingClosed(), dl);
	}

	return true;
}

} //dcpp