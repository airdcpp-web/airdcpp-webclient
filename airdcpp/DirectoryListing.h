/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#ifndef DCPLUSPLUS_DCPP_DIRECTORY_LISTING_H
#define DCPLUSPLUS_DCPP_DIRECTORY_LISTING_H

#include "forward.h"
#include "typedefs.h"

#include "DirectoryListingListener.h"
#include "ClientManagerListener.h"
#include "ShareManagerListener.h"

#include "ActionHook.h"
#include "DirectoryContentInfo.h"
#include "DispatcherQueue.h"
#include "DupeType.h"
#include "GetSet.h"
#include "HintedUser.h"
#include "Message.h"
#include "MerkleTree.h"
#include "Priority.h"
#include "QueueAddInfo.h"
#include "Speaker.h"
#include "StreamBase.h"
#include "TrackableDownloadItem.h"
#include "UserInfoBase.h"

namespace dcpp {

class ListLoader;

class DirectoryListing : public UserInfoBase, public TrackableDownloadItem,
	public Speaker<DirectoryListingListener>, 
	private ClientManagerListener, private ShareManagerListener
{
public:
	class Directory;
	using DirectoryPtr = shared_ptr<Directory>;

	class File;
	using FilePtr = std::shared_ptr<File>;
	class VirtualDirectory;

	enum class DirectoryLoadType;

	using FileValidationHook = ActionHook<nullptr_t, const FilePtr&, const DirectoryListing&>;
	using DirectoryValidationHook = ActionHook<nullptr_t, const DirectoryPtr&, const DirectoryListing&>;

	struct ValidationHooks {
		DirectoryValidationHook directoryLoadHook;
		FileValidationHook fileLoadHook;

		bool hasSubscribers() const noexcept {
			return directoryLoadHook.hasSubscribers() || fileLoadHook.hasSubscribers();
		}
	};


	DirectoryListing(const HintedUser& aUser, bool aPartial, const string& aFileName, bool aIsClientView, ValidationHooks* aLoadHooks, bool aIsOwnList = false);
	~DirectoryListing() override;
	
	const CID& getToken() const noexcept {
		return hintedUser.user->getCID();
	}

	// Throws Exception, AbortException
	void loadFile();
	bool isLoaded() const noexcept;


	// Returns the number of loaded dirs
	// Throws AbortException
	int loadPartialXml(const string& aXml, const string& aAdcBase);

	optional<DirectoryBundleAddResult> createBundleHooked(const DirectoryPtr& aDir, const string& aTarget, const string& aName, Priority aPrio, string& errorMsg_) const noexcept;

	HintedUser getDownloadSourceUser() const noexcept;

	int64_t getTotalListSizeUnsafe() const noexcept;
	int64_t getDirectorySizeUnsafe(const string& aDir) const noexcept;
	size_t getTotalFileCountUnsafe() const noexcept;

	DirectoryPtr getRoot() const noexcept { return root; }

	// Throws ShareException
	void getLocalPathsUnsafe(const DirectoryPtr& d, StringList& ret) const;

	// Throws ShareException
	void getLocalPathsUnsafe(const FilePtr& f, StringList& ret) const;

	bool isMyCID() const noexcept;
	string getNick(bool firstOnly) const noexcept;
	static string getNickFromFilename(const string& fileName) noexcept;
	static UserPtr getUserFromFilename(const string& fileName) noexcept;

	OptionalProfileToken getShareProfile() const noexcept;

	void addShareProfileChangeTask(ProfileToken aProfile) noexcept;
	void addHubUrlChangeTask(const string& aHubUrl) noexcept;

	void getPartialListInfo(int64_t& totalSize_, size_t& totalFiles_) const noexcept;
	
	const UserPtr& getUser() const noexcept override { return hintedUser.user; }
	const HintedUser& getHintedUser() const noexcept { return hintedUser; }
	const string& getHubUrl() const noexcept override { return hintedUser.hint; }
		
	GETSET(bool, partialList, PartialList);
	GETSET(string, fileName, FileName);
	IGETSET(bool, closing, Closing, false);

	void addListDiffTask(const string& aFile, bool aOwnList) noexcept;

	void addPartialListLoadTask(const string& aXml, const string& aBasePath, bool aBackgroundTask = false, const AsyncF& aCompletionF = nullptr) noexcept;
	void addOwnListLoadTask(const string& aBasePath, bool aBackgroundTask = false) noexcept;

	void addFullListTask(const string& aDir) noexcept;
	void addQueueMatchTask() noexcept;

	void addAsyncTask(Callback&& f) noexcept;
	void close() noexcept;

	DirectoryPtr findDirectoryUnsafe(const string& aName) const noexcept { return findDirectoryUnsafe(aName, root.get()); }
	DirectoryPtr findDirectoryUnsafe(const string& aName, const Directory* current) const noexcept;
	
	bool supportsASCH() const noexcept;

	struct LocationInfo {
		int64_t totalSize = -1;
		int files = -1;
		int directories = -1;

		DirectoryPtr directory = nullptr;
	};

	const LocationInfo& getCurrentLocationInfo() const noexcept {
		return currentLocation;
	}

	void onListRemovedQueue(const string& aTarget, const string& aDir, bool aFinished) noexcept;
	void addDisableLoadingTask(const string& aTarget) noexcept;

	bool isRead() const noexcept {
		return read;
	}

	void setRead() noexcept;

	void addDirectoryChangeTask(const string& aPath, DirectoryLoadType aType, bool aForceQueue = false) noexcept;

	bool getIsOwnList() const noexcept {
		return isOwnList;
	}

	bool getIsClientView() const noexcept {
		return isClientView;
	}
protected:
	void onStateChanged() noexcept override;

private:
	ValidationHooks* loadHooks;

	const bool isOwnList;
	const bool isClientView;

	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;

	void setDirectoryLoadingState(const DirectoryPtr& aDir, DirectoryLoadType aLoading) noexcept;

	// Returns the number of loaded dirs
	// Throws AbortException
	int loadXML(InputStream& aXml, bool aUpdating, const string& aBase, time_t aListDate);

	// Create and insert a base directory with the given path (or return an existing one)
	DirectoryPtr createBaseDirectory(const string& aPath, time_t aDownloadDate);

	void changeDirectoryImpl(const string& aPath, DirectoryLoadType aType, bool aForceQueue = false) noexcept;

	void setShareProfileImpl(ProfileToken aProfile) noexcept;
	void setHubUrlImpl(const string_view& aHubUrl) noexcept;

	LocationInfo currentLocation;
	void updateCurrentLocation(const DirectoryPtr& aCurrentDirectory) noexcept;

	friend class ListLoader;

	DirectoryPtr root;

	void dispatch(Callback& aCallback) noexcept;

	atomic_flag running;

	// ClientManagerListener
	void on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool wasOffline) noexcept override;
	void on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept override;
	void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool wentOffline) noexcept override;

	void onUserUpdated(const UserPtr& aUser) noexcept;

	// ShareManagerListener
	void on(ShareManagerListener::RefreshCompleted, const ShareRefreshTask& aTask, bool aSucceed, const ShareRefreshStats&) noexcept override;

	// Throws Exception, AbortException
	int loadShareDirectory(const string& aPath, bool aRecurse = false);

	// Throws Exception, AbortException
	void listDiffImpl(const string& aFile, bool aOwnList);

	void updateStatus(const string& aMessage) noexcept;

	// Throws Exception, AbortException
	void loadFileImpl(const string& aInitialDir);

	// Throws Exception, AbortException
	void loadPartialImpl(const string& aXml, const string& aBasePath, bool aBackgroundTask, const AsyncF& aCompletionF);

	void matchQueueImpl() noexcept;

	HintedUser hintedUser;
	bool read = false;

	void checkDupes() noexcept;
	void onLoadingFinished(int64_t aStartTime, const string& aLoadedPath, const string& aCurrentPath, bool aBackgroundTask) noexcept;

	DispatcherQueue tasks;
};

} // namespace dcpp

#endif // !defined(DIRECTORY_LISTING_H)
