/*
 * Copyright (C) 2011-2021 AirDC++ Project
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
#include <boost/algorithm/cxx11/all_of.hpp>

namespace dcpp {

using boost::range::for_each;
using boost::range::find_if;

#define DIRECTORY_DOWNLOAD_REMOVAL_SECONDS 120


atomic<DirectoryDownloadId> directoryDownloadIdCounter { 0 };

DirectoryDownload::DirectoryDownload(const FilelistAddData& aListData, const string& aBundleName, const string& aTarget, Priority p, ErrorMethod aErrorMethod) :
	id(directoryDownloadIdCounter++), listData(aListData), target(aTarget), priority(p), bundleName(aBundleName), created(GET_TIME()), errorMethod(aErrorMethod) { }

bool DirectoryDownload::HasOwner::operator()(const DirectoryDownloadPtr& ddi) const noexcept {
	return owner == ddi->getOwner() && Util::stricmp(a, ddi->getBundleName()) != 0;
}

DirectoryListingManager::DirectoryListingManager() noexcept {
	QueueManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);
}

DirectoryListingManager::~DirectoryListingManager() noexcept {
	QueueManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
}


DirectoryDownloadPtr DirectoryListingManager::getPendingDirectoryDownloadUnsafe(const UserPtr& aUser, const string& aPath) const noexcept {
	auto ddlIter = find_if(dlDirectories, [&](const DirectoryDownloadPtr& ddi) {
		return ddi->getUser() == aUser &&
			ddi->getState() == DirectoryDownload::State::PENDING &&
			Util::stricmp(aPath.c_str(), ddi->getListPath().c_str()) == 0;
	});

	if (ddlIter == dlDirectories.end()) {
		dcassert(0);
		return nullptr;
	}

	return *ddlIter;
}

bool DirectoryListingManager::cancelDirectoryDownload(DirectoryDownloadId aId) noexcept {
	auto download = getDirectoryDownload(aId);
	if (!download) {
		return false;
	}

	if (download->getQueueItem()) {
		// Directory download removal will be handled through QueueManagerListener::ItemRemoved
		QueueManager::getInstance()->removeQI(download->getQueueItem());
	} else {
		// Completed download? Remove instantly
		removeDirectoryDownload(download);
	}

	return true;
}


void DirectoryListingManager::removeDirectoryDownload(const DirectoryDownloadPtr& aDownloadInfo) noexcept {
	{
		WLock l(cs);
		dlDirectories.erase(remove_if(dlDirectories.begin(), dlDirectories.end(), [=](const DirectoryDownloadPtr& aDownload) {
			return aDownload->getId() == aDownloadInfo->getId();
		}), dlDirectories.end());
	}

	fire(DirectoryListingManagerListener::DirectoryDownloadRemoved(), aDownloadInfo);
}

void DirectoryListingManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	DirectoryDownloadList toRemove;

	{
		RLock l(cs);
		boost::algorithm::copy_if(dlDirectories, back_inserter(toRemove), [=](const DirectoryDownloadPtr& aDownload) {
			return aDownload->getProcessedTick() > 0 && aDownload->getProcessedTick() + DIRECTORY_DOWNLOAD_REMOVAL_SECONDS * 1000 < aTick;
		});
	}

	for (const auto& ddl : toRemove) {
		removeDirectoryDownload(ddl);
	}
}

bool DirectoryListingManager::hasDirectoryDownload(const string& aBundleName, void* aOwner) const noexcept {
	RLock l(cs);
	return find_if(dlDirectories, DirectoryDownload::HasOwner(aOwner, aBundleName)) != dlDirectories.end();
}

DirectoryDownloadList DirectoryListingManager::getDirectoryDownloads() const noexcept {
	DirectoryDownloadList ret;

	{
		RLock l(cs);
		boost::range::copy(dlDirectories, back_inserter(ret));
	}

	return ret;
}


DirectoryDownloadList DirectoryListingManager::getPendingDirectoryDownloadsUnsafe(const UserPtr& aUser) const noexcept {
	DirectoryDownloadList ret;
	boost::algorithm::copy_if(dlDirectories, back_inserter(ret), [&](const DirectoryDownloadPtr& aDownload) {
		return aDownload->getUser() == aUser && aDownload->getState() == DirectoryDownload::State::PENDING;
	});

	return ret;
}

DirectoryDownloadPtr DirectoryListingManager::getDirectoryDownload(DirectoryDownloadId aId) const noexcept {
	RLock l(cs);
	auto i = find_if(dlDirectories, [&](const DirectoryDownloadPtr& aDownload) {
		return aDownload->getId() == aId;
	});

	return i == dlDirectories.end() ? nullptr : *i;
}

DirectoryDownloadPtr DirectoryListingManager::addDirectoryDownloadHooked(const FilelistAddData& aListData, const string& aBundleName, const string& aTarget, Priority p, DirectoryDownload::ErrorMethod aErrorMethod) {
	dcassert(!aTarget.empty() && !aListData.listPath.empty() && !aBundleName.empty());
	auto downloadInfo = make_shared<DirectoryDownload>(aListData, Util::cleanPathSeparators(aBundleName), aTarget, p, aErrorMethod);
	
	DirectoryListingPtr dl;
	{
		RLock l(cs);
		auto vl = viewedLists.find(aListData.user);
		if (vl != viewedLists.end()) {
			dl = vl->second;
		}
	}

	bool needList = false;
	{
		WLock l(cs);

		// Download already pending for this item?
		auto downloads = getPendingDirectoryDownloadsUnsafe(aListData.user);
		for (const auto& download: downloads) {
			if (Util::stricmp(aListData.listPath.c_str(), download->getListPath().c_str()) == 0) {
				return download;
			}
		}

		// Unique directory, fine...
		dlDirectories.push_back(downloadInfo);
		needList = aListData.user.user->isSet(User::NMDC) ? downloads.empty() : true;
	}

	fire(DirectoryListingManagerListener::DirectoryDownloadAdded(), downloadInfo);

	if (!dl && needList) {
		queueListHooked(downloadInfo);
	} else if(dl) {
		dl->addAsyncTask([=] { handleDownloadHooked(downloadInfo, dl, false); });
	}

	return downloadInfo;
}

void DirectoryListingManager::queueListHooked(const DirectoryDownloadPtr& aDownloadInfo) {
	auto user = aDownloadInfo->getUser();

	Flags flags = QueueItem::FLAG_DIRECTORY_DOWNLOAD;
	if (!user.user->isSet(User::NMDC)) {
		flags.setFlag(QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_RECURSIVE_LIST);
	}

	try {
		auto qi = QueueManager::getInstance()->addListHooked(aDownloadInfo->getListData(), flags.getFlags());
		aDownloadInfo->setQueueItem(qi);
	} catch(const DupeException&) {
		// We have a list queued already
	} catch (const Exception& e) {
		// Failed
		failDirectoryDownload(aDownloadInfo, e.getError());
		throw e;
	}
}

DirectoryListingManager::DirectoryListingMap DirectoryListingManager::getLists() const noexcept {
	RLock l(cs);
	return viewedLists;
}

void DirectoryListingManager::processListHooked(const string& aFileName, const string& aXml, const HintedUser& aUser, const string& aRemotePath, int aFlags) noexcept {
	auto isPartialList = (aFlags & QueueItem::FLAG_PARTIAL_LIST) > 0;

	{
		RLock l(cs);
		auto p = viewedLists.find(aUser.user);
		if (p != viewedLists.end()) {
			if (p->second->getPartialList() && isPartialList) {
				//we don't want multiple threads to load those simultaneously. load in the list thread and return here after that
				p->second->addPartialListTask(aXml, aRemotePath, true, [=] { processListActionHooked(p->second, aRemotePath, aFlags); });
				return;
			}
		}
	}

	auto dl = make_shared<DirectoryListing>(aUser, isPartialList, aFileName, false, false);
	try {
		if (isPartialList) {
			dl->loadPartialXml(aXml, aRemotePath);
		} else {
			dl->loadFile();
		}
	} catch (const Exception& e) {
		log(STRING_F(LIST_LOAD_FAILED, aFileName % e.getError()), LogMessage::SEV_ERROR);
		return;
	}

	processListActionHooked(dl, aRemotePath, aFlags);
}

void DirectoryListingManager::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(FILE_LISTS));
}

void DirectoryListingManager::handleDownloadHooked(const DirectoryDownloadPtr& aDownloadInfo, const DirectoryListingPtr& aList, bool aListDownloaded/* = true*/) noexcept {
	auto dir = aList->findDirectory(aDownloadInfo->getListPath());

	// Check the content
	{
		if (!dir) {
			// Downloading directory for an open list? Try to download a list from the dir...
			if (!aListDownloaded) {
				queueListHooked(aDownloadInfo);
			}
			return;
		}

		if (aList->getPartialList() && dir->findIncomplete()) {
			// Non-recursive partial list
			queueListHooked(aDownloadInfo);
			return;
		}
	}

	// Queue the directory

	string errorMsg;
	auto queueInfo = aList->createBundleHooked(dir, aDownloadInfo->getTarget(), aDownloadInfo->getBundleName(), aDownloadInfo->getPriority(), errorMsg);

	if (aDownloadInfo->getErrorMethod() == DirectoryDownload::ErrorMethod::LOG && !errorMsg.empty()) {
		log(STRING_F(ADD_BUNDLE_ERRORS_OCC, Util::joinDirectory(aDownloadInfo->getTarget(), aDownloadInfo->getBundleName()) % aList->getNick(false) % errorMsg), LogMessage::SEV_WARNING);
	}

	if (queueInfo) {
		aDownloadInfo->setError(errorMsg);
		aDownloadInfo->setQueueInfo(queueInfo);
		aDownloadInfo->setQueueItem(nullptr);
		aDownloadInfo->setProcessedTick(GET_TICK());
		aDownloadInfo->setState(DirectoryDownload::State::QUEUED);
		fire(DirectoryListingManagerListener::DirectoryDownloadProcessed(), aDownloadInfo, *queueInfo, errorMsg);
	} else {
		failDirectoryDownload(aDownloadInfo, errorMsg);
	}
}

void DirectoryListingManager::processListActionHooked(DirectoryListingPtr aList, const string& aPath, int aFlags) noexcept {
	if(aFlags & QueueItem::FLAG_DIRECTORY_DOWNLOAD) {
		DirectoryDownloadList downloadItems;

		{
			RLock l(cs);
			if (aFlags & QueueItem::FLAG_PARTIAL_LIST) {
				// Partial list
				auto ddl = getPendingDirectoryDownloadUnsafe(aList->getHintedUser(), aPath);
				if (ddl) {
					downloadItems.push_back(ddl);
				}
			} else {
				// Full filelist
				downloadItems = getPendingDirectoryDownloadsUnsafe(aList->getHintedUser());
			}
		}

		for (const auto& ddl: downloadItems) {
			handleDownloadHooked(ddl, aList);
		}
	}

	if(aFlags & QueueItem::FLAG_MATCH_QUEUE) {
		int matches=0, newFiles=0;
		BundleList bundles;
		QueueManager::getInstance()->matchListing(*aList, matches, newFiles, bundles);
		if ((aFlags & QueueItem::FLAG_PARTIAL_LIST) && (!SETTING(REPORT_ADDED_SOURCES) || newFiles == 0 || bundles.empty())) {
			return;
		}

		log(aList->getNick(false) + ": " +
			AirUtil::formatMatchResults(matches, newFiles, bundles), LogMessage::SEV_INFO);
	}
}

void DirectoryListingManager::on(QueueManagerListener::ItemFinished, const QueueItemPtr& qi, const string& aAdcDirectoryPath, const HintedUser& aUser, int64_t /*aSpeed*/) noexcept {
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
			dl->addFullListTask(aAdcDirectoryPath);
		} else {
			fire(DirectoryListingManagerListener::OpenListing(), dl, aAdcDirectoryPath, Util::emptyString);
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

	dl->addHubUrlChangeTask(aUser.hint);

	if (dl->hasCompletedDownloads()) {
		dl->addPartialListTask(aXML, aBase);
	} else {
		fire(DirectoryListingManagerListener::OpenListing(), dl, aBase, aXML);
	}
}

void DirectoryListingManager::on(QueueManagerListener::ItemRemoved, const QueueItemPtr& qi, bool aFinished) noexcept {
	if (!qi->isSet(QueueItem::FLAG_USER_LIST))
		return;

	auto u = qi->getSources()[0].getUser();
	if (qi->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD) && !aFinished) {
		DirectoryDownloadPtr ddl;

		{
			RLock l(cs);
			ddl = getPendingDirectoryDownloadUnsafe(u, qi->getListDirectoryPath());
		}

		if (ddl) {
			auto error = QueueItem::Source::formatError(qi->getSources()[0]);
			if (!error.empty()) {
				failDirectoryDownload(ddl, error);
			} else {
				removeDirectoryDownload(ddl);
			}
		}
	}

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

		dl->onListRemovedQueue(qi->getTarget(), qi->getListDirectoryPath(), aFinished);

		bool closing = (dl->getClosing() || !dl->hasCompletedDownloads());
		if (!aFinished && !dl->hasDownloads() && closing) {
			removeList(u);
		}
	}
}


void DirectoryListingManager::failDirectoryDownload(const DirectoryDownloadPtr& aDownloadInfo, const string& aError) noexcept {
	aDownloadInfo->setState(DirectoryDownload::State::FAILED);
	aDownloadInfo->setError(aError);
	aDownloadInfo->setProcessedTick(GET_TICK());
	aDownloadInfo->setQueueItem(nullptr);
	fire(DirectoryListingManagerListener::DirectoryDownloadFailed(), aDownloadInfo, aError);
}

DirectoryListingPtr DirectoryListingManager::openOwnList(ProfileToken aProfile, bool useADL /*false*/, const string& aDir/* = Util::emptyString*/) noexcept {
	auto me = HintedUser(ClientManager::getInstance()->getMe(), Util::emptyString);

	{
		auto dl = findList(me.user);
		if (dl) {
			// REMOVE
			dl->addShareProfileChangeTask(aProfile);
			return dl;
		}
	}

	auto dl = createList(me, !useADL, Util::toString(aProfile), true);
	dl->setMatchADL(useADL);

	fire(DirectoryListingManagerListener::OpenListing(), dl, aDir, Util::emptyString);
	return dl;
}

DirectoryListingPtr DirectoryListingManager::openLocalFileList(const HintedUser& aUser, const string& aFile, const string& aDir/* = Util::emptyString*/, bool aPartial/* = false*/) noexcept {
	{
		auto dl = findList(aUser.user);
		if (dl) {
			return nullptr;
		}
	}

	dcassert(aPartial || Util::fileExists(aFile));

	auto dl = createList(aUser, aPartial, aFile, false);
	fire(DirectoryListingManagerListener::OpenListing(), dl, aDir, Util::emptyString);
	return dl;
}

DirectoryListingPtr DirectoryListingManager::createList(const HintedUser& aUser, bool aPartial, const string& aFileName, bool aIsOwnList) noexcept {
	auto dl = make_shared<DirectoryListing>(aUser, aPartial, aFileName, true, aIsOwnList);

	{
		WLock l(cs);
		viewedLists[dl->getHintedUser()] = dl;
	}

	fire(DirectoryListingManagerListener::ListingCreated(), dl);
	return dl;
}

void DirectoryListingManager::on(QueueManagerListener::ItemAdded, const QueueItemPtr& aQI) noexcept {
	if (!aQI->isSet(QueueItem::FLAG_CLIENT_VIEW) || !aQI->isSet(QueueItem::FLAG_USER_LIST)) {
		return;
	}

	auto user = aQI->getSources()[0].getUser();
	auto dl = findList(user);
	if (dl) {
		dl->onAddedQueue(aQI->getTarget());
	}
}

DirectoryListingPtr DirectoryListingManager::findList(const UserPtr& aUser) noexcept {
	RLock l (cs);
	auto p = viewedLists.find(aUser);
	if (p != viewedLists.end()) {
		return p->second;
	}

	return nullptr;
}

DirectoryListingPtr DirectoryListingManager::openRemoteFileListHooked(const FilelistAddData& aListData, Flags::MaskType aFlags) {
	{
		auto dl = findList(aListData.user);
		if (dl) {
			return nullptr;
		}
	}

	auto user = ClientManager::getInstance()->checkDownloadUrl(aListData.user);
	auto qi = QueueManager::getInstance()->addListHooked(aListData, aFlags);
	if (!qi) {
		return nullptr;
	}

	DirectoryListingPtr dl;
	if (!qi->isSet(QueueItem::FLAG_PARTIAL_LIST)) {
		dl = createList(user, false, qi->getListName(), false);
	} else {
		dl = createList(user, true, Util::emptyString, false);
	}

	dl->onAddedQueue(qi->getTarget());
	return dl;
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