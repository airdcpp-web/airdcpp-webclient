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

#ifndef DCPLUSPLUS_DCPP_DIRECTORYLISTINGMANAGER_H_
#define DCPLUSPLUS_DCPP_DIRECTORYLISTINGMANAGER_H_

#include "forward.h"
#include "Exception.h"
#include "Thread.h"
#include "DirectoryListing.h"
#include "QueueManagerListener.h"
#include "DirectoryListingManagerListener.h"
#include "Singleton.h"
#include "TargetUtil.h"
#include "TimerManager.h"

#include "Singleton.h"

namespace dcpp {

	enum SizeCheckMode {
		NO_CHECK,
		REPORT_SYSLOG,
		ASK_USER
	};

	class DirectoryDownloadInfo;
	class FinishedDirectoryItem;
	class DirectoryListingManager : public Singleton<DirectoryListingManager>, public Speaker<DirectoryListingManagerListener>, public QueueManagerListener, 
		public TimerManagerListener {
	public:
		void openOwnList(ProfileToken aProfile, bool useADL=false);
		void openFileList(const HintedUser& aUser, const string& aFile);
		
		void removeList(const UserPtr& aUser);

		DirectoryListingManager();
		~DirectoryListingManager();

		void processList(const string& aFileName, const string& aXml, const HintedUser& user, const string& aRemotePath, int flags);
		void processListAction(DirectoryListingPtr aList, const string& path, int flags);

		void addDirectoryDownload(const string& aDir, const HintedUser& aUser, const string& aTarget, TargetUtil::TargetType aTargetType, SizeCheckMode aSizeCheckMode,
			QueueItemBase::Priority p = QueueItem::DEFAULT, bool useFullList = false, ProfileToken aAutoSearch = 0, bool checkNameDupes = false) noexcept;

		void removeDirectoryDownload(const UserPtr& aUser, const string& aPath, bool isPartialList);

		void handleSizeConfirmation(const string& aName, bool accept);
	private:
		friend class Singleton<DirectoryListingManager>;

		mutable SharedMutex cs;

		bool hasList(const UserPtr& aUser);
		void createList(const HintedUser& aUser, const string& aFile, const string& aInitialDir = Util::emptyString, bool isOwnList=false);
		void createPartialList(const HintedUser& aUser, const string& aXml, const string& aDir = Util::emptyString, ProfileToken aProfile = SETTING(DEFAULT_SP), bool isOwnList = false);

		/** Directories queued for downloading */
		unordered_multimap<UserPtr, DirectoryDownloadInfo*, User::Hash> dlDirectories;
		/** Directories asking for size confirmation (later also directories added for scanning etc. ) **/
		unordered_map<string, FinishedDirectoryItem*> finishedListings;
		/** Lists open in the client **/
		unordered_map<UserPtr, DirectoryListingPtr, User::Hash> viewedLists;

		void on(QueueManagerListener::Finished, const QueueItemPtr& qi, const string& dir, const HintedUser& aUser, int64_t aSpeed) noexcept;
		void on(QueueManagerListener::PartialList, const HintedUser& aUser, const string& aXml, const string& aBase) noexcept;

		void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
	};

}

#endif /*DIRECTORYLISTINGMANAGER_ */