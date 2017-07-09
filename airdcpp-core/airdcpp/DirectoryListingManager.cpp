/*
 * Copyright (C) 2011-2017 AirDC++ Project
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


atomic<DirectoryDownloadId> directoryDownloadIdCounter { 0 };

DirectoryDownload::DirectoryDownload(const HintedUser& aUser, const string& aBundleName, const string& aListPath, const string& aTarget, Priority p, const void* aOwner) :
	id(directoryDownloadIdCounter++), listPath(aListPath), target(aTarget), priority(p), owner(aOwner), bundleName(aBundleName), user(aUser) { }

bool DirectoryDownload::HasOwner::operator()(const DirectoryDownloadPtr& ddi) const noexcept {
	return owner == ddi->getOwner() && Util::stricmp(a, ddi->getBundleName()) != 0;
}

DirectoryListingManager::DirectoryListingManager() noexcept {
	QueueManager::getInstance()->addListener(this);
}

DirectoryListingManager::~DirectoryListingManager() noexcept {
	QueueManager::getInstance()->removeListener(this);
}

bool DirectoryListingManager::removeDirectoryDownload(const UserPtr& aUser, const string& aPath) noexcept {
	DirectoryDownloadPtr download = nullptr;

	{
		WLock l(cs);
		auto dp = dlDirectories.equal_range(aUser) | map_values;
		auto udp = find_if(dp, [&aPath](const DirectoryDownloadPtr& ddi) { return Util::stricmp(aPath.c_str(), ddi->getListPath().c_str()) == 0; });
		if (udp == dp.end()) {
			dcassert(0);
			return false;
		}

		download = *udp;
		dlDirectories.erase(udp.base());
	}

	fire(DirectoryListingManagerListener::DirectoryDownloadRemoved(), download);
	return true;
}

bool DirectoryListingManager::removeDirectoryDownload(DirectoryDownloadId aId) noexcept {
	QueueItemPtr qi = nullptr;
	{
		WLock l(cs);
		auto i = find_if(dlDirectories | map_values, [&](const DirectoryDownloadPtr& aDownload) {
			return aDownload->getId() == aId;
		}).base();

		if (i == dlDirectories.end()) {
			return false;
		}

		qi = i->second->getQueueItem();
	}

	// Directory download removal will be handled through QueueManagerListener::ItemRemoved
	if (qi) {
		QueueManager::getInstance()->removeQI(qi);
	}

	return true;
}

bool DirectoryListingManager::hasDirectoryDownload(const string& aBundleName, void* aOwner) const noexcept {
	RLock l(cs);
	return find_if(dlDirectories | map_values, DirectoryDownload::HasOwner(aOwner, aBundleName)).base() != dlDirectories.end();
}

DirectoryDownloadList DirectoryListingManager::getDirectoryDownloads() const noexcept {
	DirectoryDownloadList ret;

	{
		RLock l(cs);
		boost::range::copy(dlDirectories | map_values, back_inserter(ret));
	}

	return ret;
}

DirectoryDownloadPtr DirectoryListingManager::addDirectoryDownload(const HintedUser& aUser, const string& aBundleName, const string& aListPath, const string& aTarget, Priority p, const void* aOwner) {
	dcassert(!aTarget.empty() && !aListPath.empty() && !aBundleName.empty());
	auto downloadInfo = make_shared<DirectoryDownload>(aUser, Util::cleanPathSeparators(aBundleName), aListPath, aTarget, p, aOwner);
	
	DirectoryListingPtr dl;
	{
		RLock l(cs);
		auto vl = viewedLists.find(aUser.user);
		if (vl != viewedLists.end()) {
			dl = vl->second;
		}
	}

	bool needList = false;
	{
		WLock l(cs);

		// Download already pending for this item?
		auto dp = dlDirectories.equal_range(aUser);
		for (auto i = dp.first; i != dp.second; ++i) {
			if (Util::stricmp(aListPath.c_str(), i->second->getListPath().c_str()) == 0) {
				return i->second;
			}
		}

		// Unique directory, fine...
		dlDirectories.emplace(aUser, downloadInfo);
		needList = aUser.user->isSet(User::NMDC) ? (dp.first == dp.second) : true;
	}

	fire(DirectoryListingManagerListener::DirectoryDownloadAdded(), downloadInfo);

	if (!dl && needList) {
		queueList(downloadInfo);
	} else if(dl) {
		dl->addAsyncTask([=] { handleDownload(downloadInfo, dl, false); });
	}

	return downloadInfo;
}

void DirectoryListingManager::queueList(const DirectoryDownloadPtr& aDownloadInfo) {
	auto user = aDownloadInfo->getUser();

	Flags flags = QueueItem::FLAG_DIRECTORY_DOWNLOAD;
	if (!user.user->isSet(User::NMDC)) {
		flags.setFlag(QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_RECURSIVE_LIST);
	}

	try {
		auto qi = QueueManager::getInstance()->addList(user, flags.getFlags(), aDownloadInfo->getListPath());
		aDownloadInfo->setQueueItem(qi);
	} catch(const DupeException&) {
		// We have a list queued already
	} catch (const Exception& e) {
		// Failed
		removeDirectoryDownload(user, aDownloadInfo->getListPath());
		throw e;
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

	auto dirList = make_shared<DirectoryListing>(aUser, isPartialList, aFileName, false, false);
	try {
		if (isPartialList) {
			dirList->loadPartialXml(aXml, aRemotePath);
		} else {
			dirList->loadFile();
		}
	} catch (const Exception& e) {
		LogManager::getInstance()->message(STRING_F(LIST_LOAD_FAILED, aFileName % e.getError()), LogMessage::SEV_ERROR);
		return;
	}

	processListAction(dirList, aRemotePath, aFlags);
}

void DirectoryListingManager::handleDownload(const DirectoryDownloadPtr& aDownloadInfo, const DirectoryListingPtr& aList, bool aListDownloaded/* = true*/) noexcept {
	auto dir = aList->findDirectory(aDownloadInfo->getListPath());

	// Check the content
	{
		if (!dir) {
			// Downloading directory for an open list? Try to download a list from the dir...
			if (!aListDownloaded) {
				queueList(aDownloadInfo);
			}
			return;
		}

		if (aList->getPartialList() && dir->findIncomplete()) {
			// Non-recursive partial list
			queueList(aDownloadInfo);
			return;
		}
	}

	// Queue the directory
	auto target = aDownloadInfo->getTarget() + aDownloadInfo->getBundleName() + PATH_SEPARATOR;

	string errorMsg;
	auto queueInfo = aList->createBundle(dir, target, aDownloadInfo->getPriority(), errorMsg);

	// Owner, when available, is responsible for error reporting
	if (!aDownloadInfo->getOwner() && !errorMsg.empty()) {
		LogManager::getInstance()->message(STRING_F(ADD_BUNDLE_ERRORS_OCC, target % aList->getNick(false) % errorMsg), LogMessage::SEV_WARNING);
	}

	if (queueInfo) {
		fire(DirectoryListingManagerListener::DirectoryDownloadProcessed(), aDownloadInfo, *queueInfo, errorMsg);
	} else {
		fire(DirectoryListingManagerListener::DirectoryDownloadFailed(), aDownloadInfo, errorMsg);
	}

	removeDirectoryDownload(aDownloadInfo->getUser(), aDownloadInfo->getListPath());
}

void DirectoryListingManager::processListAction(DirectoryListingPtr aList, const string& aPath, int aFlags) noexcept {
	if(aFlags & QueueItem::FLAG_DIRECTORY_DOWNLOAD) {
		DirectoryDownloadList dl;
		{
			WLock l(cs);
			auto dp = dlDirectories.equal_range(aList->getHintedUser().user) | map_values;
			if (aFlags & QueueItem::FLAG_PARTIAL_LIST) {
				//partial list
				auto udp = find_if(dp, [&aPath](const DirectoryDownloadPtr& ddi) { return Util::stricmp(aPath.c_str(), ddi->getListPath().c_str()) == 0; });
				if (udp != dp.end()) {
					dl.push_back(*udp);
				}
			} else {
				//full filelist
				dl.assign(boost::begin(dp), boost::end(dp));
			}
		}

		for (const auto& di: dl) {
			handleDownload(di, aList);
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
		removeDirectoryDownload(u, qi->getTempTarget());
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

		dl->onListRemovedQueue(qi->getTarget(), qi->getTempTarget(), aFinished);

		bool closing = (dl->getClosing() || !dl->hasCompletedDownloads());
		if (!aFinished && !dl->hasDownloads() && closing) {
			removeList(u);
		}
	}
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

DirectoryListingPtr DirectoryListingManager::openRemoteFileList(const HintedUser& aUser, Flags::MaskType aFlags, const string& aInitialDir) {
	{
		auto dl = findList(aUser);
		if (dl) {
			return nullptr;
		}
	}

	auto user = ClientManager::getInstance()->checkDownloadUrl(aUser);
	auto qi = QueueManager::getInstance()->addList(user, aFlags, aInitialDir);
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