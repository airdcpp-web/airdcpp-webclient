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

#include "stdinc.h"

#include <airdcpp/DirectoryListing.h>
#include <airdcpp/DirectoryListingDirectory.h>
#include <airdcpp/ListLoader.h>

#include <airdcpp/BZUtils.h>
#include <airdcpp/ClientManager.h>
#include <airdcpp/DupeUtil.h>
#include <airdcpp/FilteredFile.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/QueueManager.h>
#include <airdcpp/ResourceManager.h>
#include <airdcpp/ShareManager.h>
#include <airdcpp/SimpleXML.h>
#include <airdcpp/SimpleXMLReader.h>
#include <airdcpp/Streams.h>
#include <airdcpp/StringTokenizer.h>
#include <airdcpp/User.h>


namespace dcpp {

using ranges::for_each;
using ranges::find_if;

DirectoryListing::DirectoryListing(const HintedUser& aUser, bool aPartial, const string& aFileName, bool aIsClientView, ValidationHooks* aLoadHooks, bool aIsOwnList) :
	TrackableDownloadItem(aIsOwnList || (!aPartial && PathUtil::fileExists(aFileName))), // API requires the download state to be set correctly
	partialList(aPartial), fileName(aFileName), loadHooks(aLoadHooks), isOwnList(aIsOwnList), isClientView(aIsClientView),
	root(Directory::create(nullptr, ADC_ROOT_STR, Directory::TYPE_INCOMPLETE_NOCHILD, 0)), hintedUser(aUser),
	tasks(isClientView, Thread::NORMAL, std::bind_front(&DirectoryListing::dispatch, this))
{
	running.clear();

	ClientManager::getInstance()->addListener(this);
	if (isOwnList) {
		ShareManager::getInstance()->addListener(this);
	}
}

DirectoryListing::~DirectoryListing() {
	dcdebug("Filelist deleted\n");
	ClientManager::getInstance()->removeListener(this);
	ShareManager::getInstance()->removeListener(this);
}

bool DirectoryListing::isMyCID() const noexcept {
	return hintedUser.user == ClientManager::getInstance()->getMe();
}

string DirectoryListing::getNick(bool aFirstOnly) const noexcept {
	string ret;
	if (!hintedUser.user->isOnline()) {
		if (isOwnList) {
			ret = SETTING(NICK);
		} else if (!partialList) {
			ret = DirectoryListing::getNickFromFilename(fileName);
		}
	}

	if (ret.empty()) {
		if (aFirstOnly) {
			ret = ClientManager::getInstance()->getNick(hintedUser.user, hintedUser.hint, true);
		} else {
			ret = ClientManager::getInstance()->getFormattedNicks(hintedUser);
		}
	}

	return ret;
}

void stripExtensions(string& aName) noexcept {
	if(Util::stricmp(aName.c_str() + aName.length() - 4, ".bz2") == 0) {
		aName.erase(aName.length() - 4);
	}

	if(Util::stricmp(aName.c_str() + aName.length() - 4, ".xml") == 0) {
		aName.erase(aName.length() - 4);
	}
}

OptionalProfileToken DirectoryListing::getShareProfile() const noexcept {
	if (!isOwnList) {
		return nullopt;
	}

	return Util::toInt(fileName);
}

void DirectoryListing::addHubUrlChangeTask(const string& aHubUrl) noexcept {
	addAsyncTask([aHubUrl, this] {
		setHubUrlImpl(aHubUrl);
	});
}

void DirectoryListing::addShareProfileChangeTask(ProfileToken aProfile) noexcept {
	addAsyncTask([aProfile, this] {
		setShareProfileImpl(aProfile);
	});
}

void DirectoryListing::setHubUrlImpl(const string_view& aHubUrl) noexcept {
	if (aHubUrl == hintedUser.hint) {
		return;
	}

	hintedUser.hint = aHubUrl;
	fire(DirectoryListingListener::UserUpdated());

	QueueManager::getInstance()->updateFilelistUrl(hintedUser);
}

void DirectoryListing::setShareProfileImpl(ProfileToken aProfile) noexcept {
	if (*getShareProfile() == aProfile) {
		return;
	}

	setFileName(Util::toString(aProfile));
	if (partialList) {
		addDirectoryChangeTask(ADC_ROOT_STR, DirectoryLoadType::CHANGE_RELOAD);
	} else {
		addFullListTask(ADC_ROOT_STR);
	}

	SettingsManager::getInstance()->set(SettingsManager::LAST_LIST_PROFILE, aProfile);
	fire(DirectoryListingListener::ShareProfileChanged());
}

void DirectoryListing::getPartialListInfo(int64_t& totalSize_, size_t& totalFiles_) const noexcept {
	if (getShareProfile()) {
		ShareManager::getInstance()->getProfileInfo(*getShareProfile(), totalSize_, totalFiles_);
	}

	auto si = ClientManager::getInstance()->getShareInfo(hintedUser);
	if (si) {
		totalSize_ = (*si).size;
		totalFiles_ = (*si).fileCount;
	}
}

string DirectoryListing::getNickFromFilename(const string& fileName) noexcept {
	// General file list name format: [username].[CID].[xml|xml.bz2]

	string name = PathUtil::getFileName(fileName);

	// Strip off any extensions
	stripExtensions(name);

	// Find CID
	string::size_type i = name.rfind('.');
	if(i == string::npos) {
		return STRING(UNKNOWN);
	}

	return name.substr(0, i);
}

UserPtr DirectoryListing::getUserFromFilename(const string& fileName) noexcept {
	// General file list name format: [username].[CID].[xml|xml.bz2]

	string name = PathUtil::getFileName(fileName);

	// Strip off any extensions
	stripExtensions(name);

	// Find CID
	auto i = name.rfind('.');
	if (i == string::npos) {
		return nullptr;
	}

	auto n = name.length() - (i + 1);
	// CID's always 39 chars long...
	if (n != 39)
		return nullptr;

	CID cid(name.substr(i + 1));
	if (!cid)
		return UserPtr();

	return ClientManager::getInstance()->getUser(cid);
}

bool DirectoryListing::supportsASCH() const noexcept {
	return !partialList || isOwnList || hintedUser.user->isSet(User::ASCH);
}

void DirectoryListing::setDirectoryLoadingState(const DirectoryPtr& aDir, DirectoryLoadType aLoading) noexcept {
	aDir->setLoading(aLoading);
	onStateChanged();
}

void DirectoryListing::onStateChanged() noexcept {
	addAsyncTask([this]() { fire(DirectoryListingListener::StateChanged()); });
}

DirectoryListing::DirectoryPtr DirectoryListing::createBaseDirectory(const string& aBasePath, time_t aDownloadDate) {
	dcassert(PathUtil::isAdcDirectoryPath(aBasePath));
	auto cur = root;

	const auto sl = StringTokenizer<string>(aBasePath, ADC_SEPARATOR).getTokens();
	for (const auto& curDirName: sl) {
		auto s = cur->directories.find(&curDirName);
		if (s == cur->directories.end()) {
			auto d = DirectoryListing::Directory::create(cur.get(), curDirName, DirectoryListing::Directory::TYPE_INCOMPLETE_CHILD, aDownloadDate);
			cur = d;
		} else {
			cur = s->second;
		}
	}

	return cur;
}

void DirectoryListing::loadFile() {
	if (isOwnList) {
		loadShareDirectory(ADC_ROOT_STR, true);
	} else {

		// For now, we detect type by ending...
		string ext = PathUtil::getFileExt(fileName);

		dcpp::File ff(fileName, dcpp::File::READ, dcpp::File::OPEN, dcpp::File::BUFFER_AUTO);
		root->setLastUpdateDate(ff.getLastModified());
		if(Util::stricmp(ext, ".bz2") == 0) {
			FilteredInputStream<UnBZFilter, false> f(&ff);
			loadXML(f, false, ADC_ROOT_STR, ff.getLastModified());
		} else if(Util::stricmp(ext, ".xml") == 0) {
			loadXML(ff, false, ADC_ROOT_STR, ff.getLastModified());
		}
	}
}

int DirectoryListing::loadPartialXml(const string& aXml, const string& aBase) {
	MemoryInputStream mis(aXml);
	return loadXML(mis, true, aBase, GET_TIME());
}

int DirectoryListing::loadXML(InputStream& is, bool aUpdating, const string& aBase, time_t aListDate) {
	ListLoader ll(this, aBase, aUpdating, aListDate);
	try {
		dcpp::SimpleXMLReader(&ll).parse(is);
	}
	catch (SimpleXMLException& e) {
		throw AbortException(e.getError());
	}

	return ll.getLoadedDirs();
}

HintedUser DirectoryListing::getDownloadSourceUser() const noexcept {
	if (hintedUser.hint.empty() || (isMyCID() && !isOwnList)) {
		return HintedUser();
	}

	return hintedUser;
}

optional<DirectoryBundleAddResult> DirectoryListing::createBundleHooked(const DirectoryPtr& aDir, const string& aTarget, const string& aName, Priority aPriority, string& errorMsg_) const noexcept {
	auto bundleFiles = aDir->toBundleInfoList();

	try {
		auto addInfo = BundleAddData(aName, aPriority, aDir->getRemoteDate());
		auto options = BundleAddOptions(aTarget, getDownloadSourceUser(), this);
		auto result = QueueManager::getInstance()->createDirectoryBundleHooked(options, addInfo, bundleFiles, errorMsg_);

		return result;
	} catch (const std::bad_alloc&) {
		errorMsg_ = STRING(OUT_OF_MEMORY);
		log(STRING_F(BUNDLE_CREATION_FAILED, aTarget % STRING(OUT_OF_MEMORY)), LogMessage::SEV_ERROR);
	}

	return nullopt;
}

int64_t DirectoryListing::getDirectorySizeUnsafe(const string& aDir) const noexcept {
	dcassert(PathUtil::isAdcDirectoryPath(aDir));

	auto d = findDirectoryUnsafe(aDir);
	if (d) {
		return d->getTotalSize(false);
	}

	return 0;
}

DirectoryListing::Directory::Ptr DirectoryListing::findDirectoryUnsafe(const string& aName, const Directory* aCurrent) const noexcept {
	if (aName == ADC_ROOT_STR)
		return root;

	dcassert(PathUtil::isAdcDirectoryPath(aName));
	auto end = aName.find(ADC_SEPARATOR, 1);
	dcassert(end != string::npos);
	string name = aName.substr(1, end - 1);

	if (auto i = aCurrent->directories.find(&name); i != aCurrent->directories.end()) {
		if (end == (aName.size() - 1)) {
			return i->second;
		}
		else {
			return findDirectoryUnsafe(aName.substr(end), i->second.get());
		}
	}

	return nullptr;
}

int64_t DirectoryListing::getTotalListSizeUnsafe() const noexcept { 
	return root->getTotalSize(false); 
}

size_t DirectoryListing::getTotalFileCountUnsafe() const noexcept { 
	return root->getContentInfoRecursive(false).files; 
}

void DirectoryListing::getLocalPathsUnsafe(const FilePtr& f, StringList& ret) const {
	if (f->getParent()->isVirtual() && (f->getParent()->getParent() == root.get() || !isOwnList))
		return;

	return f->getLocalPathsUnsafe(ret, getShareProfile());
}

void DirectoryListing::getLocalPathsUnsafe(const DirectoryPtr& d, StringList& ret) const {
	if (d->isVirtual() && (d->getParent() == root.get() || !isOwnList))
		return;

	return d->getLocalPathsUnsafe(ret, getShareProfile());
}

void DirectoryListing::checkDupes() noexcept {
	root->checkDupesRecursive();
	root->setDupe(DUPE_NONE); //never show the root as a dupe or partial dupe.
}

void DirectoryListing::addListDiffTask(const string& aFile, bool aOwnList) noexcept {
	addAsyncTask([=, this] { listDiffImpl(aFile, aOwnList); });
}

void DirectoryListing::addPartialListLoadTask(const string& aXml, const string& aBase, bool aBackgroundTask /*false*/, const AsyncF& aCompletionF) noexcept {
	dcassert(PathUtil::isAdcDirectoryPath(aBase));
	addAsyncTask([=, this] { loadPartialImpl(aXml, aBase, aBackgroundTask, aCompletionF); });
}

void DirectoryListing::addOwnListLoadTask(const string& aBase, bool aBackgroundTask /*false*/) noexcept {
	dcassert(PathUtil::isAdcDirectoryPath(aBase));
	addAsyncTask([=, this] { loadPartialImpl(Util::emptyString, aBase, aBackgroundTask, nullptr); });
}

void DirectoryListing::addFullListTask(const string& aDir) noexcept {
	addAsyncTask([aDir, this] { loadFileImpl(aDir); });
}

void DirectoryListing::addQueueMatchTask() noexcept {
	addAsyncTask([this] { matchQueueImpl(); });
}

void DirectoryListing::close() noexcept {
	closing = true;
	tasks.stop([this] {
		fire(DirectoryListingListener::Close());
	});
}

void DirectoryListing::addAsyncTask(Callback&& f) noexcept {
	if (isClientView) {
		tasks.addTask(std::move(f));
	} else {
		dispatch(f);
	}
}

void DirectoryListing::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(FILE_LISTS));
}

void DirectoryListing::dispatch(Callback& aCallback) noexcept {
	try {
		aCallback();
	} catch (const std::bad_alloc&) {
		log(STRING_F(LIST_LOAD_FAILED, getNick(false) % STRING(OUT_OF_MEMORY)), LogMessage::SEV_ERROR);
		fire(DirectoryListingListener::LoadingFailed(), "Out of memory");
	} catch (const AbortException& e) {
		// The error is empty on user cancellations
		if (!e.getError().empty()) {
			log(STRING_F(LIST_LOAD_FAILED, getNick(false) % e.getError()), LogMessage::SEV_ERROR);
		}

		fire(DirectoryListingListener::LoadingFailed(), e.getError());
	} catch(const ShareException& e) {
		fire(DirectoryListingListener::LoadingFailed(), e.getError());
	} catch (const QueueException& e) {
		updateStatus("Queueing failed:" + e.getError());
	} catch (const Exception& e) {
		log(STRING_F(LIST_LOAD_FAILED, getNick(false) % e.getError()), LogMessage::SEV_ERROR);
		fire(DirectoryListingListener::LoadingFailed(), getNick(false) + ": " + e.getError());
	}
}

void DirectoryListing::updateStatus(const string& aMessage) noexcept {
	fire(DirectoryListingListener::UpdateStatusMessage(), aMessage);
}

void DirectoryListing::listDiffImpl(const string& aFile, bool aOwnList) {
	int64_t start = GET_TICK();
	if (isOwnList && partialList) {
		// we need the recursive list for this
		loadShareDirectory(ADC_ROOT_STR, true);
		partialList = false;
	}

	Directory::TTHSet l;

	// Get TTHs from the other list
	{
		DirectoryListing dirList(hintedUser, false, aFile, false, nullptr, aOwnList);
		dirList.loadFile();

		dirList.getRoot()->getHashList(l);
	}

	root->filterList(l);

	fire(DirectoryListingListener::LoadingFinished(), start, ADC_ROOT_STR, static_cast<uint8_t>(DirectoryLoadType::CHANGE_NORMAL));
}

void DirectoryListing::loadFileImpl(const string& aInitialDir) {
	int64_t start = GET_TICK();
	partialList = false;

	fire(DirectoryListingListener::LoadingStarted(), false);

	auto curDirectoryPath = !currentLocation.directory ? Util::emptyString : currentLocation.directory->getAdcPathUnsafe();

	// In case we are reloading...
	root->clearAll();

	loadFile();

	onLoadingFinished(start, aInitialDir, curDirectoryPath, false);
}

void DirectoryListing::onLoadingFinished(int64_t aStartTime, const string& aLoadedPath, const string& aCurrentPath, bool aBackgroundTask) noexcept {
	if (!isOwnList && SETTING(DUPES_IN_FILELIST) && isClientView) {
		checkDupes();
	}

	auto loadedDir = findDirectoryUnsafe(aLoadedPath);
	if (!loadedDir) {
		// Base path should have been validated while loading partial list
		dcassert(!partialList);
		loadedDir = root;
	}

	auto newCurrentDir = findDirectoryUnsafe(aCurrentPath.empty() ? aLoadedPath : aCurrentPath);
	if (aLoadedPath == aCurrentPath || newCurrentDir != currentLocation.directory) {
		if (!newCurrentDir || (!newCurrentDir->isComplete() && PathUtil::isParentOrExactAdc(aLoadedPath, aCurrentPath))) {
			// Non-recursive partial list was loaded for an upper level directory (e.g. own filelist after refreshing roots) and content of the current directory is not known
			// Move back to the newly loaded parent directory
			updateCurrentLocation(loadedDir);
			newCurrentDir = loadedDir;
		} else {
			// Update current location in case a parent directory was reloaded (or the current directory was not set before)
			updateCurrentLocation(newCurrentDir);
		}
	}

	read = false;

	auto changeType = loadedDir->getLoading();
	if (changeType == DirectoryLoadType::NONE) {
		// Initial loading/directory download
		changeType = aBackgroundTask || newCurrentDir != loadedDir ? DirectoryLoadType::LOAD_CONTENT : DirectoryLoadType::CHANGE_NORMAL;
	}

	setDirectoryLoadingState(loadedDir, DirectoryLoadType::NONE);
	fire(DirectoryListingListener::LoadingFinished(), aStartTime, loadedDir->getAdcPathUnsafe(), static_cast<uint8_t>(changeType));
}

void DirectoryListing::updateCurrentLocation(const DirectoryPtr& aCurrentDirectory) noexcept {
	currentLocation.directories = static_cast<int>(aCurrentDirectory->directories.size());
	currentLocation.files = static_cast<int>(aCurrentDirectory->files.size());
	currentLocation.totalSize = aCurrentDirectory->getTotalSize(false);
	currentLocation.directory = aCurrentDirectory;
}

void DirectoryListing::loadPartialImpl(const string& aXml, const string& aBasePath, bool aBackgroundTask, const AsyncF& aCompletionF) {
	if (!partialList)
		return;

	auto curDirectoryPath = !currentLocation.directory ? Util::emptyString : currentLocation.directory->getAdcPathUnsafe();
	DirectoryPtr optionalOldDirectory = nullptr;

	// Preparations
	{
		bool reloading = false;

		// Has this directory been loaded before? Existing content must be cleared in that case
		optionalOldDirectory = findDirectoryUnsafe(aBasePath);
		if (optionalOldDirectory) {
			reloading = optionalOldDirectory->isComplete();
		}

		// Let the window to be disabled before making any modifications
		fire(DirectoryListingListener::LoadingStarted(), !reloading);

		if (reloading) {
			// Remove all existing directories inside this path
			optionalOldDirectory->clearAll();
		}
	}

	// Load content
	try {
		if (isOwnList) {
			 loadShareDirectory(aBasePath);
		} else {
			 loadPartialXml(aXml, aBasePath);
		}
	} catch (const Exception&) {
		if (optionalOldDirectory) {
			setDirectoryLoadingState(optionalOldDirectory, DirectoryLoadType::NONE);
		}

		throw;
	}

	// Done
	onLoadingFinished(0, aBasePath, curDirectoryPath, aBackgroundTask);

	if (aCompletionF) {
		aCompletionF();
	}
}

bool DirectoryListing::isLoaded() const noexcept {
	return currentLocation.directory && currentLocation.directory->getLoading() == DirectoryLoadType::NONE;
}

void DirectoryListing::matchQueueImpl() noexcept {
	auto results = QueueManager::getInstance()->matchListing(*this);
	fire(DirectoryListingListener::QueueMatched(), results.format());
}

void DirectoryListing::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool /*wentOffline*/) noexcept {
	onUserUpdated(aUser);
}
void DirectoryListing::on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept {
	onUserUpdated(aUser);
}

void DirectoryListing::on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool /*wasOffline*/) noexcept {
	onUserUpdated(aUser);
}

void DirectoryListing::onUserUpdated(const UserPtr& aUser) noexcept {
	if (aUser != hintedUser.user) {
		return;
	}

	addAsyncTask([this] { fire(DirectoryListingListener::UserUpdated()); });
}

int DirectoryListing::loadShareDirectory(const string& aPath, bool aRecurse) {
	auto mis = ShareManager::getInstance()->generatePartialList(aPath, aRecurse, getShareProfile());
	if (mis) {
		return loadXML(*mis, true, aPath, GET_TIME());
	}

	//might happen if have refreshed the share meanwhile
	throw Exception(CSTRING(FILE_NOT_AVAILABLE));
}

void DirectoryListing::changeDirectoryImpl(const string& aRemoteAdcPath, DirectoryLoadType aType, bool aForceQueue) noexcept {
	DirectoryPtr dir;
	if (partialList) {
		// Directory may not exist when searching in partial lists 
		// or when opening directories from search (or via the API) for existing filelists
		dir = createBaseDirectory(aRemoteAdcPath, GET_TIME());
	} else {
		dir = findDirectoryUnsafe(aRemoteAdcPath);
		if (!dir) {
			dcassert(0);
			return;
		}
	}

	dcassert(findDirectoryUnsafe(aRemoteAdcPath) != nullptr);

	clearLastError();

	// The casing may differ e.g. when searching and the other user (or ShareManager) 
	// returns a merged path with a different casing
	auto listAdcPath = dir->getAdcPathUnsafe();

	if (aType != DirectoryLoadType::LOAD_CONTENT && (!currentLocation.directory || listAdcPath != currentLocation.directory->getAdcPathUnsafe())) {
		updateCurrentLocation(dir);
		fire(DirectoryListingListener::ChangeDirectory(), listAdcPath, static_cast<uint8_t>(aType));
	}

	if (!partialList || dir->getLoading() != DirectoryLoadType::NONE || (dir->isComplete() && aType != DirectoryLoadType::CHANGE_RELOAD)) {
		// No need to load anything
	} else if (partialList) {
		if (isOwnList || (getUser()->isOnline() || aForceQueue)) {
			setDirectoryLoadingState(dir, aType);

			if (isOwnList) {
				addOwnListLoadTask(listAdcPath, false);
			} else {
				try {
					auto listData = FilelistAddData(hintedUser, this, listAdcPath);
					QueueManager::getInstance()->addListHooked(listData, QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_CLIENT_VIEW);
				} catch (const Exception& e) {
					setDirectoryLoadingState(dir, DirectoryLoadType::NONE);
					fire(DirectoryListingListener::LoadingFailed(), e.getError());
				}
			}
		} else {
			updateStatus(STRING(USER_OFFLINE));
		}
	}
}

void DirectoryListing::addDirectoryChangeTask(const string& aPath, DirectoryLoadType aType, bool aForceQueue) noexcept {
	addAsyncTask([aPath, aType, aForceQueue, this] {
		changeDirectoryImpl(aPath, aType, aForceQueue);
	});
}

void DirectoryListing::setRead() noexcept {
	if (read) {
		return;
	}

	addAsyncTask([this] {
		read = true;
		fire(DirectoryListingListener::Read());
	});
}

void DirectoryListing::onListRemovedQueue(const string& aTarget, const string& aDir, bool aFinished) noexcept {
	if (!aFinished) {
		addDisableLoadingTask(aDir);
	}

	TrackableDownloadItem::onRemovedQueue(aTarget, aFinished);
}

void DirectoryListing::addDisableLoadingTask(const string& aDirectory) noexcept {
	addAsyncTask([aDirectory, this] {
		auto dir = findDirectoryUnsafe(aDirectory);
		if (dir) {
			setDirectoryLoadingState(dir, DirectoryLoadType::NONE);
			fire(DirectoryListingListener::RemovedQueue(), aDirectory);
		}
	});
}

void DirectoryListing::on(ShareManagerListener::RefreshCompleted, const ShareRefreshTask& aTask, bool aSucceed, const ShareRefreshStats&) noexcept{
	if (!partialList || !aSucceed)
		return;

	StringSet virtualPaths;
	for (const auto& p : aTask.dirs) {
		auto vPath = ShareManager::getInstance()->realToVirtualAdc(p, getShareProfile());
		if (!vPath.empty()) {
			virtualPaths.insert(vPath);
		}
	}

	addAsyncTask([virtualPaths, this] {
		for (const auto& virtualPath : virtualPaths) {
			if (auto directory = findDirectoryUnsafe(virtualPath); !directory) {
				continue;
			}

			// Reload
			addOwnListLoadTask(virtualPath, true);
		}
	});
}

} // namespace dcpp
