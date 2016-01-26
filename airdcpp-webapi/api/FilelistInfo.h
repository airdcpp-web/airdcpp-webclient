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

#ifndef DCPLUSPLUS_DCPP_FILELIST_H
#define DCPLUSPLUS_DCPP_FILELIST_H

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/GetSet.h>

#include <airdcpp/DirectoryListing.h>
#include <airdcpp/QueueItemBase.h>
#include <airdcpp/TargetUtil.h>

#include <api/HierarchicalApiModule.h>
#include <api/common/ListViewController.h>

namespace webserver {
	//typedef uint32_t ResultToken;
	class FilelistItemInfo : public FastAlloc<FilelistItemInfo> {
	public:
		typedef shared_ptr<FilelistItemInfo> Ptr;
		typedef vector<Ptr> List;

		enum ItemType {
			FILE,
			DIRECTORY
		};

		union {
			const DirectoryListing::File::Ptr file;
			const DirectoryListing::Directory::Ptr dir;
		};

		FilelistItemInfo(const DirectoryListing::File::Ptr& f) : type(FILE), file(f) { }
		FilelistItemInfo(const DirectoryListing::Directory::Ptr& d) : type(DIRECTORY), dir(d) {}
		~FilelistItemInfo() { }

		DupeType getDupe() const noexcept { return type == DIRECTORY ? dir->getDupe() : file->getDupe(); }
		const string& getName() const noexcept { return type == DIRECTORY ? dir->getName() : file->getName(); }
		string getPath() const noexcept { return type == DIRECTORY ? dir->getPath() : file->getPath(); }
		bool isAdl() const noexcept { return type == DIRECTORY ? dir->getAdls() : file->getAdls(); }
		//bool isComplete() const noexcept { return type == DIRECTORY ? dir->isComplete() : true; }

		time_t getDate() const noexcept { return type == DIRECTORY ? dir->getRemoteDate() : file->getRemoteDate(); }
		time_t getSize() const noexcept { return type == DIRECTORY ? dir->getTotalSize(false) : file->getSize(); }

		DirectoryListingToken getToken() const noexcept;

		ItemType getType() const noexcept {
			return type;
		}
	private:
		const ItemType type;
	};

	typedef FilelistItemInfo::Ptr FilelistItemInfoPtr;


	class FilelistInfo : public SubApiModule<CID, FilelistInfo, std::string>, private DirectoryListingListener {
	public:
		typedef ParentApiModule<CID, FilelistInfo> ParentType;
		typedef shared_ptr<FilelistInfo> Ptr;

		static const StringList subscriptionList;

		//typedef vector<Ptr> List;
		//typedef unordered_map<TTHValue, Ptr> Map;

		static const PropertyList properties;

		enum Properties {
			PROP_TOKEN = -1,
			PROP_NAME,
			PROP_TYPE,
			PROP_SIZE,
			PROP_DATE,
			PROP_PATH,
			PROP_TTH,
			PROP_DUPE,
			PROP_COMPLETE,
			PROP_LAST
		};

		FilelistInfo(ParentType* aParentModule, const DirectoryListingPtr& aFilelist);
		~FilelistInfo();

		DirectoryListingPtr getList() const noexcept { return dl; }

		static string formatState(const DirectoryListingPtr& aList) noexcept;
		static json serializeState(const DirectoryListingPtr& aList) noexcept;
		static json serializeLocation(const DirectoryListingPtr& aListing) noexcept;

		void init() noexcept;
	private:
		api_return handleChangeDirectory(ApiRequest& aRequest);
		api_return handleSetRead(ApiRequest& aRequest);

		void on(DirectoryListingListener::LoadingFinished, int64_t aStart, const string& aDir, bool reloadList, bool changeDir) noexcept;
		void on(DirectoryListingListener::LoadingFailed, const string& aReason) noexcept;
		void on(DirectoryListingListener::LoadingStarted, bool changeDir) noexcept;
		void on(DirectoryListingListener::ChangeDirectory, const string& aDir, bool isSearchChange) noexcept;
		void on(DirectoryListingListener::UpdateStatusMessage, const string& aMessage) noexcept;
		void on(DirectoryListingListener::UserUpdated) noexcept;
		void on(DirectoryListingListener::StateChanged) noexcept;
		void on(DirectoryListingListener::Read) noexcept;
		void on(DirectoryListingListener::ShareProfileChanged) noexcept;

		void addListTask(CallBack&& aTask) noexcept;

		/*void on(DirectoryListingListener::QueueMatched, const string& aMessage) noexcept;
		void on(DirectoryListingListener::Close) noexcept;
		void on(DirectoryListingListener::SearchStarted) noexcept;
		void on(DirectoryListingListener::SearchFailed, bool timedOut) noexcept;
		void on(DirectoryListingListener::RemovedQueue, const string& aDir) noexcept;
		void on(DirectoryListingListener::SetActive) noexcept;
		void on(DirectoryListingListener::HubChanged) noexcept;*/

		FilelistItemInfo::List getCurrentViewItems();

		typedef PropertyItemHandler<FilelistItemInfoPtr> Handler;
		static const Handler itemHandler;

		typedef ListViewController<FilelistItemInfoPtr, PROP_LAST> DirectoryView;
		DirectoryView directoryView;

		DirectoryListingPtr dl;

		void onSessionUpdated(const json& aData) noexcept;

		FilelistItemInfo::List currentViewItems;

		void updateItems(const string& aPath) noexcept;
		DirectoryListing::Directory::Ptr currentDirectory = nullptr;

		SharedMutex cs;
	};

	typedef FilelistInfo::Ptr FilelistInfoPtr;
}

#endif