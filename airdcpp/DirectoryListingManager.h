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
#include "TargetUtil.h"
#include "TimerManagerListener.h"

namespace dcpp {
	class DirectoryListingManager : public Singleton<DirectoryListingManager>, public Speaker<DirectoryListingManagerListener>, public QueueManagerListener {
	public:
		typedef unordered_map<UserPtr, DirectoryListingPtr, User::Hash> DirectoryListingMap;

		void openOwnList(ProfileToken aProfile, bool useADL=false) noexcept;
		void openFileList(const HintedUser& aUser, const string& aFile) noexcept;
		
		bool removeList(const UserPtr& aUser) noexcept;

		DirectoryListingManager() noexcept;
		~DirectoryListingManager() noexcept;

		void processList(const string& aFileName, const string& aXml, const HintedUser& user, const string& aRemotePath, int flags) noexcept;
		void processListAction(DirectoryListingPtr aList, const string& path, int flags) noexcept;

		void addDirectoryDownload(const string& aRemoteDir, const string& aBundleName, const HintedUser& aUser, const string& aTarget,
			Priority p = Priority::DEFAULT, bool useFullList = false, void* aOwner = nullptr, bool checkNameDupes = false, bool checkViewed = true) noexcept;

		void removeDirectoryDownload(const UserPtr& aUser, const string& aPath, bool isPartialList) noexcept;
		DirectoryListingMap getLists() const noexcept;
	private:
		class DirectoryDownloadInfo {
		public:
			DirectoryDownloadInfo() : priority(Priority::DEFAULT) { }
			DirectoryDownloadInfo(const UserPtr& aUser, const string& aBundleName, const string& aListPath, const string& aTarget, Priority p,
				void* aOwner, bool aRecursiveListAttempted) :
				listPath(aListPath), target(aTarget), priority(p), listing(nullptr), owner(aOwner), bundleName(aBundleName),
				recursiveListAttempted(aRecursiveListAttempted), user(aUser) {
			}
			~DirectoryDownloadInfo() { }

			typedef std::shared_ptr<DirectoryDownloadInfo> Ptr;
			typedef vector<DirectoryDownloadInfo::Ptr> List;

			UserPtr& getUser() { return user; }

			GETSET(string, listPath, ListPath);
			GETSET(string, target, Target);
			GETSET(Priority, priority, Priority);
			GETSET(DirectoryListingPtr, listing, Listing);
			GETSET(void*, owner, Owner);
			GETSET(string, bundleName, BundleName);
			GETSET(bool, recursiveListAttempted, RecursiveListAttempted);

			string getFinishedDirName() const noexcept { return target + bundleName; }

			struct HasOwner {
				HasOwner(void* aOwner, const string& s) : a(s), owner(aOwner) { }
				bool operator()(const DirectoryDownloadInfo::Ptr& ddi) const noexcept;

				const string& a;
				void* owner;

				HasOwner& operator=(const HasOwner&) = delete;
			};
		private:
			UserPtr user;
		};

		bool handleDownload(const DirectoryDownloadInfo::Ptr& di, const DirectoryListingPtr& aList) noexcept;

		DirectoryListingPtr createList(const HintedUser& aUser, bool aPartial, const string& aFileName, bool aIsOwnList) noexcept;

		friend class Singleton<DirectoryListingManager>;

		mutable SharedMutex cs;

		DirectoryListingPtr hasList(const UserPtr& aUser) noexcept;

		/** Directories queued for downloading */
		unordered_multimap<UserPtr, DirectoryDownloadInfo::Ptr, User::Hash> dlDirectories;


		/** Lists open in the client **/
		DirectoryListingMap viewedLists;

		void on(QueueManagerListener::ItemAdded, const QueueItemPtr& aQI) noexcept;
		void on(QueueManagerListener::ItemFinished, const QueueItemPtr& qi, const string& dir, const HintedUser& aUser, int64_t aSpeed) noexcept;
		void on(QueueManagerListener::ItemRemoved, const QueueItemPtr& qi, bool finished) noexcept;

		void on(QueueManagerListener::PartialListFinished, const HintedUser& aUser, const string& aXml, const string& aBase) noexcept;
	};

}

#endif /*DIRECTORYLISTINGMANAGER_ */