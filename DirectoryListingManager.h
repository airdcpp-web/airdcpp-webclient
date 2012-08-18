/*
 * Copyright (C) 2011-2012 AirDC++ Project
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

#include "Singleton.h"

namespace dcpp {

	class DirectoryListingManager : public Singleton<DirectoryListingManager>, public Speaker<DirectoryListingManagerListener>, public QueueManagerListener {
	public:
		void openOwnList(ProfileToken aProfile);
		void openFileList(const HintedUser& aUser, const string& aFile);
		
		void removeList(const UserPtr& aUser);

		DirectoryListingManager();
		~DirectoryListingManager();

		void processList(const string& name, const HintedUser& user, const string& path, int flags);
		void addDirectoryDownload(const string& aDir, const HintedUser& aUser, const string& aTarget, TargetUtil::TargetType aTargetType,
			QueueItem::Priority p = QueueItem::DEFAULT, bool useFullList = false) noexcept;

		void removeDirectoryDownload(const UserPtr aUser);
	private:
		friend class Singleton<DirectoryListingManager>;

		mutable SharedMutex cs;
		unordered_map<UserPtr, DirectoryListing*, User::Hash> fileLists;

		bool hasList(const UserPtr& aUser);
		void createList(const HintedUser& aUser, const string& aFile, int64_t aSpeed, const string& aInitialDir = Util::emptyString, bool isOwnList=false);
		void createPartialList(const HintedUser& aUser, const string& aXml, ProfileToken aProfile=SP_DEFAULT, bool isOwnList=false);
		void on(QueueManagerListener::Finished, const QueueItemPtr qi, const string& dir, const HintedUser& aUser, int64_t aSpeed) noexcept;
		void on(QueueManagerListener::PartialList, const HintedUser& aUser, const string& text) noexcept;

		/** Directories queued for downloading */
		unordered_multimap<UserPtr, DirectoryItemPtr, User::Hash> directories;
	};

}

#endif /*DIRECTORYLISTINGMANAGER_ */