/*
 * Copyright (C) 2011-2019 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_DIRECTORYLISTINGMANAGER_H_
#define DCPLUSPLUS_DCPP_DIRECTORYLISTINGMANAGER_H_

#include "forward.h"

#include "QueueManagerListener.h"
#include "DirectoryListingManagerListener.h"

#include "CriticalSection.h"
#include "DirectoryDownload.h"
#include "DirectoryListing.h"
#include "Singleton.h"
#include "TimerManagerListener.h"

namespace dcpp {
	class DirectoryListingManager : public Singleton<DirectoryListingManager>, public Speaker<DirectoryListingManagerListener>, public QueueManagerListener, public TimerManagerListener {
	public:
		typedef unordered_map<UserPtr, DirectoryListingPtr, User::Hash> DirectoryListingMap;

		// Browse own share, will always success
		DirectoryListingPtr openOwnList(ProfileToken aProfile, bool useADL=false, const string& aDir = ADC_ROOT_STR) noexcept;

		// Open local file, returns nullptr on duplicates
		DirectoryListingPtr openLocalFileList(const HintedUser& aUser, const string& aFile, const string& aDir = ADC_ROOT_STR, bool aPartial = false) noexcept;
		
		// Add a managed filelist session from remove user, throws queueing errors
		// Returns nullptr on duplicates
		DirectoryListingPtr openRemoteFileList(const HintedUser& HintedUser, Flags::MaskType aFlags, const string& aInitialDir = ADC_ROOT_STR);
		bool removeList(const UserPtr& aUser) noexcept;

		DirectoryListingManager() noexcept;
		~DirectoryListingManager() noexcept;

		void processList(const string& aFileName, const string& aXml, const HintedUser& user, const string& aRemotePath, int flags) noexcept;
		void processListAction(DirectoryListingPtr aList, const string& path, int flags) noexcept;

		// Throws on queueing errors (such as invalid source)
		// If owner is specified, no errors are logged if queueing of the directory fails
		DirectoryDownloadPtr addDirectoryDownload(const HintedUser& aUser, const string& aBundleName, const string& aListPath, const string& aTarget, Priority p, const void* aOwner = nullptr);
		DirectoryDownloadList getDirectoryDownloads() const noexcept;
		DirectoryDownloadPtr getDirectoryDownload(DirectoryDownloadId aId) const noexcept;

		bool hasDirectoryDownload(const string& aBundleName, void* aOwner) const noexcept;
		bool cancelDirectoryDownload(DirectoryDownloadId aId) noexcept;

		DirectoryListingMap getLists() const noexcept;
		DirectoryListingPtr findList(const UserPtr& aUser) noexcept;
	private:
		// bool removeDirectoryDownload(const UserPtr& aUser, const string& aPath, bool aForced) noexcept;
		void removeDirectoryDownload(const DirectoryDownloadPtr& aDownloadInfo) noexcept;
		DirectoryDownloadList getPendingDirectoryDownloadsUnsafe(const UserPtr& aUser) const noexcept;
		DirectoryDownloadPtr getPendingDirectoryDownloadUnsafe(const UserPtr& aUser, const string& aPath) const noexcept;
		void failDirectoryDownload(const DirectoryDownloadPtr& aDownloadInfo, const string& aError) noexcept;

		// Throws on errors
		void queueList(const DirectoryDownloadPtr& aDownloadInfo);

		void handleDownload(const DirectoryDownloadPtr& aDownloadInfo, const DirectoryListingPtr& aList, bool aListDownloaded = true) noexcept;

		DirectoryListingPtr createList(const HintedUser& aUser, bool aPartial, const string& aFileName, bool aIsOwnList) noexcept;

		friend class Singleton<DirectoryListingManager>;

		mutable SharedMutex cs;

		/** Directories queued for downloading */
		DirectoryDownloadList dlDirectories;

		/** Lists open in the client **/
		DirectoryListingMap viewedLists;

		void on(QueueManagerListener::ItemAdded, const QueueItemPtr& aQI) noexcept;
		void on(QueueManagerListener::ItemFinished, const QueueItemPtr& qi, const string& dir, const HintedUser& aUser, int64_t aSpeed) noexcept;
		void on(QueueManagerListener::ItemRemoved, const QueueItemPtr& qi, bool finished) noexcept;

		void on(QueueManagerListener::PartialListFinished, const HintedUser& aUser, const string& aXml, const string& aBase) noexcept;


		void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	};

}

#endif /*DIRECTORYLISTINGMANAGER_ */