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
#include "Exception.h"
#include "DirectoryListing.h"
#include "Pointer.h"
#include "Singleton.h"
#include "TargetUtil.h"
#include "TimerManager.h"

namespace dcpp {
	class DirectoryListingManager : public Singleton<DirectoryListingManager>, public Speaker<DirectoryListingManagerListener>, public QueueManagerListener, 
		public TimerManagerListener {
	public:
		typedef unordered_map<UserPtr, DirectoryListingPtr, User::Hash> DirectoryListingMap;

		void openOwnList(ProfileToken aProfile, bool useADL=false) noexcept;
		void openFileList(const HintedUser& aUser, const string& aFile) noexcept;
		
		bool removeList(const UserPtr& aUser) noexcept;

		DirectoryListingManager() noexcept;
		~DirectoryListingManager() noexcept;

		void processList(const string& aFileName, const string& aXml, const HintedUser& user, const string& aRemotePath, int flags) noexcept;
		void processListAction(DirectoryListingPtr aList, const string& path, int flags) noexcept;

		void addDirectoryDownload(const string& aRemoteDir, const string& aBundleName, const HintedUser& aUser, const string& aTarget, TargetUtil::TargetType aTargetType, bool aSizeUnknown,
			QueueItemBase::Priority p = QueueItem::DEFAULT, bool useFullList = false, ProfileToken aAutoSearch = 0, bool checkNameDupes = false, bool checkViewed = true) noexcept;

		void removeDirectoryDownload(const UserPtr& aUser, const string& aPath, bool isPartialList) noexcept;
		DirectoryListingMap getLists() const noexcept;
	private:
		class DirectoryDownloadInfo : public intrusive_ptr_base<DirectoryDownloadInfo> {
		public:
			DirectoryDownloadInfo() : priority(QueueItemBase::DEFAULT) { }
			DirectoryDownloadInfo(const UserPtr& aUser, const string& aBundleName, const string& aListPath, const string& aTarget, TargetUtil::TargetType aTargetType, QueueItemBase::Priority p,
				bool aSizeUnknown, ProfileToken aAutoSearch, bool aRecursiveListAttempted) :
				listPath(aListPath), target(aTarget), priority(p), targetType(aTargetType), sizeUnknown(aSizeUnknown), listing(nullptr), autoSearch(aAutoSearch), bundleName(aBundleName),
				recursiveListAttempted(aRecursiveListAttempted), user(aUser) {
			}
			~DirectoryDownloadInfo() { }

			typedef boost::intrusive_ptr<DirectoryDownloadInfo> Ptr;
			typedef vector<DirectoryDownloadInfo::Ptr> List;

			UserPtr& getUser() { return user; }

			GETSET(string, listPath, ListPath);
			GETSET(string, target, Target);
			GETSET(QueueItemBase::Priority, priority, Priority);
			GETSET(TargetUtil::TargetType, targetType, TargetType);
			GETSET(bool, sizeUnknown, SizeUnknown);
			GETSET(DirectoryListingPtr, listing, Listing);
			GETSET(ProfileToken, autoSearch, AutoSearch);
			GETSET(string, bundleName, BundleName);
			GETSET(bool, recursiveListAttempted, RecursiveListAttempted);

			string getFinishedDirName() const noexcept { return target + bundleName + Util::toString(targetType); }

			struct HasASItem {
				HasASItem(ProfileToken aToken, const string& s) : a(s), t(aToken) { }
				bool operator()(const DirectoryDownloadInfo::Ptr& ddi) const noexcept{ return t == ddi->getAutoSearch() && Util::stricmp(a, ddi->getBundleName()) != 0; }
				const string& a;
				ProfileToken t;

				HasASItem& operator=(const HasASItem&) = delete;
			};
		private:
			UserPtr user;
		};

		// Stores information about finished items for a while so that consecutive downloads of the same directory don't get different targets
		class FinishedDirectoryItem : public intrusive_ptr_base<FinishedDirectoryItem> {
		public:
			typedef boost::intrusive_ptr<FinishedDirectoryItem> Ptr;
			typedef vector<FinishedDirectoryItem::Ptr> List;

			FinishedDirectoryItem(bool aUsePausedPrio, const string& aTargetPath) : usePausedPrio(aUsePausedPrio), targetPath(aTargetPath), timeDownloaded(GET_TICK()) { }

			GETSET(bool, usePausedPrio, UsePausedPrio);
			GETSET(string, targetPath, TargetPath); // real path to the location
			GETSET(uint64_t, timeDownloaded, TimeDownloaded); // time when this item was created
		private:

		};

		bool download(const DirectoryDownloadInfo::Ptr& di, const DirectoryListingPtr& aList, const string& aTarget, bool aHasFreeSpace) noexcept;
		void handleDownload(DirectoryDownloadInfo::Ptr& di, DirectoryListingPtr& aList) noexcept;

		DirectoryListingPtr createList(const HintedUser& aUser, bool aPartial, const string& aFileName, bool aIsOwnList) noexcept;

		friend class Singleton<DirectoryListingManager>;

		mutable SharedMutex cs;

		DirectoryListingPtr hasList(const UserPtr& aUser) noexcept;

		/** Directories queued for downloading */
		unordered_multimap<UserPtr, DirectoryDownloadInfo::Ptr, User::Hash> dlDirectories;

		/** Directories asking for size confirmation (later also directories added for scanning etc. ) **/
		unordered_map<string, FinishedDirectoryItem::Ptr> finishedListings;


		/** Lists open in the client **/
		DirectoryListingMap viewedLists;

		void on(QueueManagerListener::Added, QueueItemPtr& aQI) noexcept;
		void on(QueueManagerListener::Finished, const QueueItemPtr& qi, const string& dir, const HintedUser& aUser, int64_t aSpeed) noexcept;
		void on(QueueManagerListener::Removed, const QueueItemPtr& qi, bool finished) noexcept;

		void on(QueueManagerListener::PartialList, const HintedUser& aUser, const string& aXml, const string& aBase) noexcept;

		void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	};

}

#endif /*DIRECTORYLISTINGMANAGER_ */