/*
 * Copyright (C) 2011-2014 AirDC++ Project
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

	enum SizeCheckMode {
		NO_CHECK,
		REPORT_SYSLOG,
		ASK_USER
	};

	class DirectoryListingManager : public Singleton<DirectoryListingManager>, public Speaker<DirectoryListingManagerListener>, public QueueManagerListener, 
		public TimerManagerListener {
	public:
		void openOwnList(ProfileToken aProfile, bool useADL=false) noexcept;
		void openFileList(const HintedUser& aUser, const string& aFile) noexcept;
		
		void removeList(const UserPtr& aUser) noexcept;

		DirectoryListingManager() noexcept;
		~DirectoryListingManager() noexcept;

		void processList(const string& aFileName, const string& aXml, const HintedUser& user, const string& aRemotePath, int flags) noexcept;
		void processListAction(DirectoryListingPtr aList, const string& path, int flags) noexcept;

		void addDirectoryDownload(const string& aRemoteDir, const string& aBundleName, const HintedUser& aUser, const string& aTarget, TargetUtil::TargetType aTargetType, SizeCheckMode aSizeCheckMode,
			QueueItemBase::Priority p = QueueItem::DEFAULT, bool useFullList = false, ProfileToken aAutoSearch = 0, bool checkNameDupes = false, bool checkViewed = true) noexcept;

		void removeDirectoryDownload(const UserPtr& aUser, const string& aPath, bool isPartialList) noexcept;
	private:
		class DirectoryDownloadInfo : public intrusive_ptr_base<DirectoryDownloadInfo> {
		public:
			DirectoryDownloadInfo() : priority(QueueItem::DEFAULT) { }
			DirectoryDownloadInfo(const UserPtr& aUser, const string& aBundleName, const string& aListPath, const string& aTarget, TargetUtil::TargetType aTargetType, QueueItemBase::Priority p,
				SizeCheckMode aPromptSizeConfirm, ProfileToken aAutoSearch, bool aRecursiveListAttempted) :
				listPath(aListPath), target(aTarget), priority(p), targetType(aTargetType), sizeConfirm(aPromptSizeConfirm), listing(nullptr), autoSearch(aAutoSearch), bundleName(aBundleName), 
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
			GETSET(SizeCheckMode, sizeConfirm, SizeConfirm);
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
			private:
				HasASItem& operator=(const HasASItem&) ;
			};
		private:
			UserPtr user;
		};

		// stores information about finished items for a while
		class FinishedDirectoryItem : public intrusive_ptr_base<FinishedDirectoryItem> {
		public:
			enum WaitingState {
				WAITING_ACTION,
				ACCEPTED,
				REJECTED
			};

			typedef boost::intrusive_ptr<FinishedDirectoryItem> Ptr;
			typedef vector<FinishedDirectoryItem::Ptr> List;

			FinishedDirectoryItem(DirectoryDownloadInfo::Ptr& aDDI, const string& aTargetPath) : state(WAITING_ACTION), usePausedPrio(false), targetPath(aTargetPath), timeDownloaded(0) {
				downloadInfos.push_back(aDDI);
			}

			FinishedDirectoryItem(bool aUsePausedPrio, const string& aTargetPath) : state(ACCEPTED), usePausedPrio(aUsePausedPrio), targetPath(aTargetPath), timeDownloaded(GET_TICK()) { }

			~FinishedDirectoryItem() {
				deleteListings();
			}

			void addInfo(DirectoryDownloadInfo::Ptr& aDDI) noexcept {
				downloadInfos.push_back(aDDI);
				//if (aDDI->getAutoSearch() > 0)
				//	autoSearches.insert(aDDI->getAutoSearch());
			}

			void setHandledState(bool accepted) noexcept {
				state = accepted ? ACCEPTED : REJECTED;
				timeDownloaded = GET_TICK();
				//deleteListings();
			}

			void deleteListings() {
				downloadInfos.clear();
			}

			/*void addAutoSearch(ProfileToken aAutoSearch) {
				if (aAutoSearch > 0)
					autoSearches.insert(aAutoSearch);
			}*/

			GETSET(WaitingState, state, State); // is this waiting action from an user?
			GETSET(bool, usePausedPrio, UsePausedPrio);
			GETSET(DirectoryDownloadInfo::List, downloadInfos, DownloadInfos); // lists that are waiting for disk space confirmation from the user
			GETSET(string, targetPath, TargetPath); // real path to the location
			GETSET(uint64_t, timeDownloaded, TimeDownloaded); // time when this item was created
			/*GETSET(ProfileTokenSet, autoSearches, AutoSearches); // list of all auto search items that have been associated to this dir

			struct HasASItem {
				HasASItem(ProfileToken aToken, const string& s) : a(s), t(aToken) { }
				bool operator()(const FinishedDirectoryItem::Ptr& ddi) const { return ddi->autoSearches.find(t) != ddi->autoSearches.end() && Util::stricmp(Util::getLastDir(ddi->targetPath), a) != 0; }
				const string& a;
				ProfileToken t;
			private:
				HasASItem& operator=(const HasASItem&) ;
			};*/
		private:

		};



		bool download(const DirectoryDownloadInfo::Ptr& di, const DirectoryListingPtr& aList, const string& aTarget) noexcept;
		void handleDownload(DirectoryDownloadInfo::Ptr& di, DirectoryListingPtr& aList) noexcept;

		friend class Singleton<DirectoryListingManager>;

		mutable SharedMutex cs;

		bool hasList(const UserPtr& aUser) noexcept;
		void createList(const HintedUser& aUser, const string& aFile, const string& aInitialDir = Util::emptyString, bool isOwnList = false) noexcept;
		void createPartialList(const HintedUser& aUser, const string& aXml, const string& aDir = Util::emptyString, ProfileToken aProfile = SETTING(DEFAULT_SP), bool isOwnList = false) noexcept;

		void handleSizeConfirmation(FinishedDirectoryItem::Ptr& aFinishedItem, bool accept) noexcept;

		/** Directories queued for downloading */
		unordered_multimap<UserPtr, DirectoryDownloadInfo::Ptr, User::Hash> dlDirectories;
		/** Directories asking for size confirmation (later also directories added for scanning etc. ) **/
		unordered_map<string, FinishedDirectoryItem::Ptr> finishedListings;
		/** Lists open in the client **/
		unordered_map<UserPtr, DirectoryListingPtr, User::Hash> viewedLists;

		void on(QueueManagerListener::Finished, const QueueItemPtr& qi, const string& dir, const HintedUser& aUser, int64_t aSpeed) noexcept;
		void on(QueueManagerListener::PartialList, const HintedUser& aUser, const string& aXml, const string& aBase) noexcept;
		void on(QueueManagerListener::Removed, const QueueItemPtr& qi, bool finished) noexcept;

		void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	};

}

#endif /*DIRECTORYLISTINGMANAGER_ */