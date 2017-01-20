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

#ifndef DCPLUSPLUS_DCPP_DIRECTORYLISTINGMANAGER_H_
#define DCPLUSPLUS_DCPP_DIRECTORYLISTINGMANAGER_H_

#include "forward.h"

#include "QueueManagerListener.h"
#include "DirectoryListingManagerListener.h"

#include "CriticalSection.h"
#include "DirectoryListing.h"
#include "Pointer.h"
#include "Singleton.h"
#include "TimerManagerListener.h"

namespace dcpp {
	typedef uint32_t DirectoryDownloadId;
	class DirectoryDownload {
	public:
		DirectoryDownload(const HintedUser& aUser, const string& aBundleName, const string& aListPath, const string& aTarget, Priority p, const void* aOwner = nullptr);

		// All clients don't support sending of recursive partial lists
		IGETSET(bool, partialListFailed, PartialListFailed, false);
		IGETSET(QueueItemPtr, queueItem, QueueItem, nullptr);

		struct HasOwner {
			HasOwner(void* aOwner, const string& s) : a(s), owner(aOwner) { }
			bool operator()(const DirectoryDownloadPtr& ddi) const noexcept;

			const string& a;
			void* owner;

			HasOwner& operator=(const HasOwner&) = delete;
		};

		const HintedUser& getUser() const noexcept { return user; }
		const string& getBundleName() const noexcept { return bundleName; }
		const string& getTarget() const noexcept { return target; }
		const string& getListPath() const noexcept { return listPath; }
		Priority getPriority() const noexcept { return priority; }
		const void* getOwner() const noexcept { return owner; }
		DirectoryDownloadId getId() const noexcept { return id; }
	private:
		const DirectoryDownloadId id;
		const Priority priority;
		const HintedUser user;
		const string target;
		const string bundleName;
		const string listPath;
		const void* owner;
	};

	class DirectoryListingManager : public Singleton<DirectoryListingManager>, public Speaker<DirectoryListingManagerListener>, public QueueManagerListener {
	public:
		typedef unordered_map<UserPtr, DirectoryListingPtr, User::Hash> DirectoryListingMap;

		// Browse own share, will always success
		DirectoryListingPtr openOwnList(ProfileToken aProfile, bool useADL=false, const string& aDir = Util::emptyString) noexcept;

		// Open local file, returns nullptr on duplicates
		DirectoryListingPtr openFileList(const HintedUser& aUser, const string& aFile, const string& aDir = Util::emptyString) noexcept;
		
		// Add a managed filelist session from remove user, throws queueing errors
		// Returns nullptr on duplicates
		DirectoryListingPtr createList(const HintedUser& HintedUser, Flags::MaskType aFlags, const string& aInitialDir = Util::emptyString);
		bool removeList(const UserPtr& aUser) noexcept;

		DirectoryListingManager() noexcept;
		~DirectoryListingManager() noexcept;

		void processList(const string& aFileName, const string& aXml, const HintedUser& user, const string& aRemotePath, int flags) noexcept;
		void processListAction(DirectoryListingPtr aList, const string& path, int flags) noexcept;

		// Throws on queueing errors (such as invalid source)
		// If owner is specified, no errors are logged if queueing of the directory fails
		DirectoryDownloadPtr addDirectoryDownload(const HintedUser& aUser, const string& aBundleName, const string& aListPath, const string& aTarget, Priority p, const void* aOwner = nullptr);
		DirectoryDownloadList getDirectoryDownloads() const noexcept;

		bool hasDirectoryDownload(const string& aBundleName, void* aOwner) const noexcept;
		bool removeDirectoryDownload(DirectoryDownloadId aId) noexcept;

		DirectoryListingMap getLists() const noexcept;
	private:
		bool removeDirectoryDownload(const UserPtr& aUser, const string& aPath) noexcept;

		// Throws on errors
		void queueList(const DirectoryDownloadPtr& aDownloadInfo);

		void handleDownload(const DirectoryDownloadPtr& aDownloadInfo, const DirectoryListingPtr& aList) noexcept;

		DirectoryListingPtr createList(const HintedUser& aUser, bool aPartial, const string& aFileName, bool aIsOwnList) noexcept;

		friend class Singleton<DirectoryListingManager>;

		mutable SharedMutex cs;

		DirectoryListingPtr hasList(const UserPtr& aUser) noexcept;

		/** Directories queued for downloading */
		unordered_multimap<UserPtr, DirectoryDownloadPtr, User::Hash> dlDirectories;


		/** Lists open in the client **/
		DirectoryListingMap viewedLists;

		void on(QueueManagerListener::ItemAdded, const QueueItemPtr& aQI) noexcept;
		void on(QueueManagerListener::ItemFinished, const QueueItemPtr& qi, const string& dir, const HintedUser& aUser, int64_t aSpeed) noexcept;
		void on(QueueManagerListener::ItemRemoved, const QueueItemPtr& qi, bool finished) noexcept;

		void on(QueueManagerListener::PartialListFinished, const HintedUser& aUser, const string& aXml, const string& aBase) noexcept;
	};

}

#endif /*DIRECTORYLISTINGMANAGER_ */