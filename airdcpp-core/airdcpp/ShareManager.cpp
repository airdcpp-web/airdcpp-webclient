/*
 * Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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

#include "stdinc.h"
#include "ShareManager.h"

#include "AirUtil.h"
#include "BZUtils.h"
#include "ClientManager.h"
#include "File.h"
#include "FilteredFile.h"
#include "LogManager.h"
#include "HashManager.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "ScopedFunctor.h"
#include "SearchResult.h"
#include "ShareScannerManager.h"
#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "Transfer.h"
#include "UserConnection.h"

#include "version.h"

#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/algorithm/cxx11/all_of.hpp>

#include "concurrency.h"

#ifdef _WIN32
# include <ShlObj.h>
#endif

namespace dcpp {

using std::string;
using boost::adaptors::filtered;
using boost::range::find_if;
using boost::range::for_each;
using boost::range::copy;
using boost::algorithm::copy_if;
using boost::range::remove_if;

#define SHARE_CACHE_VERSION "3"

#ifdef ATOMIC_FLAG_INIT
atomic_flag ShareManager::refreshing = ATOMIC_FLAG_INIT;
#else
atomic_flag ShareManager::refreshing;
#endif

ShareManager::ShareManager() : bloom(new ShareBloom(1 << 20)), monitor(1, false)
{ 
	SettingsManager::getInstance()->addListener(this);
#ifdef _WIN32
	// don't share Windows directory
	TCHAR path[MAX_PATH];
	::SHGetFolderPath(NULL, CSIDL_WINDOWS, NULL, SHGFP_TYPE_CURRENT, path);
	winDir = Text::toLower(Text::fromT(path)) + PATH_SEPARATOR;
#endif

	File::ensureDirectory(Util::getPath(Util::PATH_SHARECACHE));
}

ShareManager::~ShareManager() {
	SettingsManager::getInstance()->removeListener(this);
}

void ShareManager::startup(function<void(const string&)> splashF, function<void(float)> progressF) noexcept {
	AirUtil::updateCachedSettings();
	if (!getShareProfile(SETTING(DEFAULT_SP))) {
		if (shareProfiles.empty()) {
			auto sp = std::make_shared<ShareProfile>(STRING(DEFAULT), SETTING(DEFAULT_SP));
			shareProfiles.push_back(sp);
		} else {
			SettingsManager::getInstance()->set(SettingsManager::DEFAULT_SP, shareProfiles.front()->getToken());
		}
	}

	ShareProfilePtr hidden = std::make_shared<ShareProfile>(STRING(SHARE_HIDDEN), SP_HIDDEN);
	shareProfiles.push_back(hidden);

	setSkipList();

	bool refreshed = false;
	if(!loadCache(progressF)) {
		if (splashF)
			splashF(STRING(REFRESHING_SHARE));
		refresh(false, TYPE_STARTUP_BLOCKING, progressF);
		refreshed = true;
	}

	addAsyncTask([=] {
		if (!refreshed)
			fire(ShareManagerListener::ShareLoaded());

		monitor.addListener(this);

		//this requires disk access
		StringList monitorPaths;

		{
			RLock l(cs);
			for(const auto& d: rootPaths | map_values | filtered(Directory::IsParent())) {
				if (d->getProfileDir()->useMonitoring())
					monitorPaths.push_back(d->getProfileDir()->getPath());
			}
		}

		addMonitoring(monitorPaths);
		TimerManager::getInstance()->addListener(this);
		QueueManager::getInstance()->addListener(this);

		if (SETTING(STARTUP_REFRESH) && !refreshed)
			refresh(false, TYPE_STARTUP_DELAYED);
	});
}

void ShareManager::addMonitoring(const StringList& aPaths) noexcept {
	int added = 0;
	for(const auto& p: aPaths) {
		try {
			if (monitor.addDirectory(p))
				added++;
		} catch (MonitorException& e) {
			LogManager::getInstance()->message(STRING_F(FAILED_ADD_MONITORING, p % e.getError()), LogMessage::SEV_ERROR);
		}
	}

	if (added > 0)
		LogManager::getInstance()->message(STRING_F(X_MONITORING_ADDED, added), LogMessage::SEV_INFO);
}

void ShareManager::removeMonitoring(const StringList& aPaths) noexcept {
	int removed = 0;
	for(const auto& p: aPaths) {
		try {
			if (monitor.removeDirectory(p))
				removed++;
		} catch (MonitorException& e) {
			LogManager::getInstance()->message("Error occurred when trying to remove the foldrer " + p + " from monitoring: " + e.getError(), LogMessage::SEV_ERROR);
		}
	}

	if (removed > 0)
		LogManager::getInstance()->message(STRING_F(X_MONITORING_REMOVED, removed), LogMessage::SEV_INFO);
}

optional<pair<string, bool>> ShareManager::checkModifiedPath(const string& aPath) const noexcept {
	// TODO: FIX LINUX
	FileFindIter f(aPath);
	if (f != FileFindIter()) {
		if (!SETTING(SHARE_HIDDEN) && f->isHidden())
			return boost::none;

		if (!SETTING(SHARE_FOLLOW_SYMLINKS) && f->isLink())
			return boost::none;

		bool isDir = f->isDirectory();
		auto path = isDir ? aPath + PATH_SEPARATOR : aPath;
		if (!checkSharedName(path, Text::toLower(path), isDir, true, f->getSize()))
			return boost::none;

		return make_pair(path, isDir);
	}

	return boost::none;
}

void ShareManager::addModifyInfo(const string& aPath, bool isDirectory, DirModifyInfo::ActionType aAction) noexcept {
	auto filePath = isDirectory ? aPath : Util::getFilePath(aPath);

	auto p = findModifyInfo(filePath);
	if (p == fileModifications.end()) {
		//add a new modify info
		fileModifications.emplace_front(aPath, isDirectory, aAction);
	} else {
		if (isDirectory && filePath == (*p).path) {
			// update the directory action
			p->dirAction = aAction;
		} else {
			// is this a parent ?
			if (AirUtil::isSub((*p).path, filePath))
				(*p).setPath(filePath);

			if (!isDirectory) {
				// add the file
				p->addFile(aPath, aAction);
			}
		}
	}
}

void ShareManager::DirModifyInfo::addFile(const string& aFile, ActionType aAction, const string& aOldPath /*Util::emptyString*/) noexcept {
	auto p = files.find(aFile);
	if (p != files.end()) {
		if (p->second.action == DirModifyInfo::ACTION_CREATED && aAction == DirModifyInfo::ACTION_DELETED) {
			files.erase(p);
		} else {
			p->second.action = aAction;
		}
	} else {
		files.emplace(aFile, DirModifyInfo::FileInfo(aAction, aOldPath));
	}

	lastFileActivity = GET_TICK();
}

void ShareManager::DirModifyInfo::setPath(const string& aPath) noexcept {
	path = aPath;
}

ShareManager::DirModifyInfo::DirModifyInfo(const string& aFile, bool isDirectory, ActionType aAction, const string& aOldPath /*Util::emptyString*/) {
	volume = File::getMountPath(aFile);
	if (isDirectory) {
		dirAction = aAction;
		setPath(aFile);
		oldPath = aOldPath;
	} else {
		dirAction = ACTION_NONE;
		setPath(Util::getFilePath(aFile));
		addFile(aFile, aAction, aOldPath);
	}
}

ShareManager::DirModifyInfo::List::iterator ShareManager::findModifyInfo(const string& aFile) noexcept {
	return find_if(fileModifications, [&aFile](const DirModifyInfo& dmi) { return AirUtil::isParentOrExact(dmi.path, aFile) || AirUtil::isSub(dmi.path, aFile); });
}

void ShareManager::handleChangedFiles() noexcept {
	monitor.callAsync([this] { handleChangedFiles(GET_TICK(), true); });
}

bool ShareManager::handleModifyInfo(DirModifyInfo& info, optional<StringList>& bundlePaths_, ProfileTokenSet& dirtyProfiles_, StringList& refresh_, uint64_t aTick, bool forced) noexcept{
	//not enough delay from the last change?
	if (!forced) {
		if (SETTING(DELAY_COUNT_MODE) == SettingsManager::DELAY_DIR) {
			if (info.lastFileActivity + static_cast<uint64_t>(SETTING(MONITORING_DELAY) * 1000) > aTick) {
				return false;
			}
		} else {
			if (any_of(fileModifications.begin(), fileModifications.end(), [&info, aTick](DirModifyInfo& aInfo) {
				if (SETTING(DELAY_COUNT_MODE) == SettingsManager::DELAY_VOLUME && compare(aInfo.volume, info.volume) != 0)
					return false;
				return aInfo.lastFileActivity + static_cast<uint64_t>(SETTING(MONITORING_DELAY) * 1000) > aTick;
			})) {
				return false;
			}
		}
	}

	//handle deleted files first
	if (info.dirAction == DirModifyInfo::ACTION_DELETED) {
		WLock l(cs);
		//the whole dir removed
		handleDeletedFile(info.path, true, dirtyProfiles_);
		LogManager::getInstance()->message(STRING_F(SHARED_DIR_REMOVED, info.path), LogMessage::SEV_INFO);
		return true;
	} else {
		// handle the files that have been deleted
		int removed = 0;
		string removedPath;

		{
			WLock l(cs);
			for (auto i = info.files.begin(); i != info.files.end();) {
				if (i->second.action == DirModifyInfo::ACTION_DELETED) {
					bool isDir = i->first.back() == PATH_SEPARATOR;
					if (handleDeletedFile(i->first, isDir, dirtyProfiles_)) {
						if (removed == 0) {
							removedPath = i->first; // for reporting
						}
						removed++;
					}

					i = info.files.erase(i);
				} else {
					i++;
				}
			}
		}

		// report deleted
		if (removed > 0) {
			if (removed == 1) {
				LogManager::getInstance()->message((removedPath.back() == PATH_SEPARATOR ? STRING_F(SHARED_DIR_REMOVED, removedPath) : STRING_F(SHARED_FILE_DELETED, removedPath)), LogMessage::SEV_INFO);
			} else {
				LogManager::getInstance()->message(STRING_F(X_SHARED_FILES_REMOVED, removed % info.path), LogMessage::SEV_INFO);
			}
		}

		// no modified files?
		if (info.files.empty() && info.dirAction == DirModifyInfo::ACTION_NONE) {
			return true;
		}
	}

	//handle modified files
	Directory::Ptr dir = nullptr;

	// directories with subdirectories will always be refreshed
	if (boost::algorithm::all_of(info.files | map_keys, [&info](const string& fileName) { 
		return fileName.find(PATH_SEPARATOR, info.path.length() + 1) == string::npos; 
	})) {
		RLock l(cs);
		dir = findDirectory(info.path);
	}

	// new directory?
	if (!dir && !allowAddDir(info.path)) {
		return true;
	}

	// fetch the queued bundles
	if (!bundlePaths_) {
		bundlePaths_ = StringList();
		QueueManager::getInstance()->getUnfinishedPaths(*bundlePaths_);
	}

	// don't handle queued bundles in here (parent directories for bundles shouldn't be totally ignored really...)
	if (find_if(*bundlePaths_, IsParentOrExactOrSub(info.path)) != (*bundlePaths_).end()) {
		return true;
	} /*else {
		// remove files inside bundle directories if this is a parent directory
		StringList subBundles;
		copy_if(*bundlePaths_, back_inserter(subBundles), IsSub<false>(info.path));
		if (!subBundles.empty()) {
			for (auto i = info.files.begin(); i != info.files.end();) {
				auto p = find_if(subBundles, IsParentOrExact<false>(i->first));
				if (p != subBundles.end()) {
					i = info.files.erase(i);
				} else {
					i++;
				}
			}

			// no other files? we don't care about the parent directory action
			if (info.files.empty()) {
				return true;
			}

			// break it
			decltype(fileModifications) infosNew;
			for (const auto& p : info.files) {
				addModifyInfo(p.first, false, p.second.action);
			}

			for (const auto& i : infosNew) {

			}
		}
	}*/

	// scan for missing/extra files
	if (info.lastReportedError == 0 || info.lastReportedError < info.lastFileActivity) {
		bool report = forced || info.lastReportedError == 0 || (info.lastReportedError + static_cast<uint64_t>(10 * 60 * 1000) < aTick); //don't spam with reports on every minute
		if (!ShareScannerManager::getInstance()->onScanSharedDir(info.path, report)) {
			if (report) {
				info.lastReportedError = aTick;
			}

			//keep in the main list and try again later
			return false;
		}

		info.lastReportedError = 0;
	} else if (info.lastReportedError > 0) {
		// nothing has changed since the last error
		return false;
	}

	bool hasExistingFiles = false;

	int64_t hashSize = 0;
	int filesToHash = 0;
	int hashedFiles = 0;
	string hashFile;

	// Check that the file can be accessed, FileFindIter won't show it (also files being copied will come here)
	for (const auto& fi : info.files | map_keys) {
		//check for file bundles
		if (binary_search((*bundlePaths_).begin(), (*bundlePaths_).end(), Text::toLower(fi))) {
			return false;
		}

		if (!Util::fileExists(fi))
			continue;

		hasExistingFiles = true;
		try {
			File ff(fi, File::READ, File::SHARED_WRITE | File::OPEN, File::BUFFER_AUTO);
			if (dir) {
				// we can add it here
				auto size = ff.getSize();
				try {
					HashedFile hashedFile(ff.getLastModified(), size);
					if (HashManager::getInstance()->checkTTH(Text::toLower(fi), fi, hashedFile)) {
						WLock l(cs);
						addFile(Util::getFileName(fi), dir, hashedFile, dirtyProfiles_);
						hashedFiles++;
						continue;
					}
				} catch (...) {}

				// not hashed
				filesToHash++;
				hashSize += size;
				if (hashFile.empty()) {
					hashFile = fi;
				}

				dir->updateModifyDate();
			}
		} catch (...) {
			// try again later
			info.lastFileActivity = aTick;
			return false;
		}
	}

	if (!hasExistingFiles && (!info.files.empty() || !Util::fileExists(info.path))) {
		// no need to keep items in the list if all files have been removed...
		return true;
	}

	if (!dir) {
		//add new directories via normal refresh
		refresh_.push_back(info.path);
	} else if (hashedFiles > 0) {
		LogManager::getInstance()->message(STRING_F(X_SHARED_FILES_ADDED, hashedFiles % info.path), LogMessage::SEV_INFO);
	}  
	
	if (hashSize > 0) {
		if (filesToHash == 1)
			LogManager::getInstance()->message(STRING_F(FILE_X_ADDED_FOR_HASH, hashFile % Util::formatBytes(hashSize)), LogMessage::SEV_INFO);
		else
			LogManager::getInstance()->message(STRING_F(X_FILES_ADDED_FOR_HASH, filesToHash % Util::formatBytes(hashSize) % info.path), LogMessage::SEV_INFO);
	}

	return true;
}

void ShareManager::handleChangedFiles(uint64_t aTick, bool forced /*false*/) noexcept {
	ProfileTokenSet dirtyProfiles;
	optional<StringList> bundlePaths;
	StringList refresh;

	for (auto k = fileModifications.begin(); k != fileModifications.end();) {
		if (handleModifyInfo(*k, bundlePaths, dirtyProfiles, refresh, aTick, forced)) {
			k = fileModifications.erase(k);
		} else {
			k++;
		}
	}


	// add directories for refresh
	if (!refresh.empty()) {
		addRefreshTask(REFRESH_DIRS, refresh, TYPE_MONITORING);
	}

	setProfilesDirty(dirtyProfiles, false);
}

void ShareManager::on(DirectoryMonitorListener::DirectoryFailed, const string& aPath, const string& aError) noexcept {
	LogManager::getInstance()->message(STRING_F(MONITOR_DIR_FAILED, aPath % aError), LogMessage::SEV_ERROR);
}

void ShareManager::on(DirectoryMonitorListener::FileCreated, const string& aPath) noexcept {
	if (monitorDebug)
		LogManager::getInstance()->message("File added: " + aPath, LogMessage::SEV_INFO);

	auto ret = checkModifiedPath(aPath);
	if (ret) {
		addModifyInfo((*ret).first, (*ret).second, DirModifyInfo::ACTION_CREATED);
	}
}

void ShareManager::on(DirectoryMonitorListener::FileModified, const string& aPath) noexcept {
	if (monitorDebug)
		LogManager::getInstance()->message("File modified: " + aPath, LogMessage::SEV_INFO);

	auto ret = checkModifiedPath(aPath);
	if (ret) {
		// modified directories won't matter at the moment
		if ((*ret).second)
			return;

		addModifyInfo((*ret).first, false, DirModifyInfo::ACTION_MODIFIED);
	}
}

void ShareManager::Directory::getRenameInfoList(const string& aPath, RenameList& aRename) noexcept {
	for (const auto& f: files) {
		aRename.emplace_back(aPath + f->name.getNormal(), HashedFile(f->getTTH(), f->getLastWrite(), f->getSize()));
	}

	string path = aPath + realName.getNormal() + PATH_SEPARATOR;
	for (const auto& d: directories) {
		d->getRenameInfoList(path + realName.getNormal() + PATH_SEPARATOR, aRename);
	}
}

void ShareManager::on(DirectoryMonitorListener::FileRenamed, const string& aOldPath, const string& aNewPath) noexcept {
	if (monitorDebug)
		LogManager::getInstance()->message("File renamed, old: " + aOldPath + " new: " + aNewPath, LogMessage::SEV_INFO);

	ProfileTokenSet dirtyProfiles;
	RenameList toRename;
	bool found = true;
	bool noSharing = false;

	{
		WLock l(cs);
		auto parent = findDirectory(Util::getFilePath(aOldPath));
		if (parent) {
			auto fileNameOldLower = Text::toLower(Util::getFileName(aOldPath));
			auto p = parent->directories.find(fileNameOldLower);
			if (p != parent->directories.end()) {
				if (!checkSharedName(aNewPath + PATH_SEPARATOR, Text::toLower(aNewPath) + PATH_SEPARATOR, true)) {
					noSharing = true;
				} else {
					auto d = *p;
					d->copyRootProfiles(dirtyProfiles, true);

					// remove from the dir name map
					removeDirName(*d, dirNameMap);

					//rename
					parent->directories.erase(p);
					d->realName = DualString(Util::getFileName(aNewPath));
					parent->directories.insert_sorted(d);
					parent->updateModifyDate();

					//add in bloom and dir name map
					addDirName(d, dirNameMap, *bloom.get());

					//get files to convert in the hash database (recursive)
					d->getRenameInfoList(Util::emptyString, toRename);

					LogManager::getInstance()->message(STRING_F(SHARED_DIR_RENAMED, (aOldPath + PATH_SEPARATOR) % (aNewPath + PATH_SEPARATOR)), LogMessage::SEV_INFO);
				}
			} else {
				auto f = parent->files.find(fileNameOldLower);
				if (f != parent->files.end()) {
					if (!checkSharedName(aNewPath, Text::toLower(aNewPath), false, true, (*f)->getSize())) {
						noSharing = true;
					} else {
						//get the info
						HashedFile fi((*f)->getTTH(), (*f)->getLastWrite(), (*f)->getSize());

						//remove old
						cleanIndices(*parent, *f);
						parent->files.erase(f);

						//add new
						addFile(Util::getFileName(aNewPath), parent, fi, dirtyProfiles);
						parent->updateModifyDate();

						//add for renaming in file index
						toRename.emplace_back(Util::emptyString, fi);

						LogManager::getInstance()->message(STRING_F(SHARED_FILE_RENAMED, aOldPath % aNewPath), LogMessage::SEV_INFO);
					}
				} else {
					found = false;
				}
			}
		}
	}

	if (!found) {
		// consider it as new file
		auto ret = checkModifiedPath(aNewPath);
		if (ret)
			addModifyInfo((*ret).first, (*ret).second, DirModifyInfo::ACTION_CREATED);

		// remove possible queued notifications
		onFileDeleted(aOldPath);
		return;
	}

	if (noSharing) {
		// delete the old file
		onFileDeleted(aOldPath);
		return;
	}

	//rename in hash database
	for (const auto& ri: toRename) {
		try {
			HashManager::getInstance()->renameFile(aOldPath + (ri.first.empty() ? Util::emptyString : PATH_SEPARATOR + ri.first), aNewPath + (ri.first.empty() ? Util::emptyString : PATH_SEPARATOR + ri.first), ri.second);
		} catch (const HashException&) {
			//...
		}
	}

	setProfilesDirty(dirtyProfiles, false);
}

void ShareManager::on(DirectoryMonitorListener::FileDeleted, const string& aPath) noexcept {
	if (monitorDebug)
		LogManager::getInstance()->message("File deleted: " + aPath, LogMessage::SEV_INFO);

	onFileDeleted(aPath);
}

void ShareManager::onFileDeleted(const string& aPath) {
	{
		RLock l(cs);
		auto parent = findDirectory(Util::getFilePath(aPath));
		if (parent) {
			auto fileNameLower = Text::toLower(Util::getFileName(aPath));
			auto p = parent->directories.find(fileNameLower);
			if (p != parent->directories.end()) {
				addModifyInfo(aPath + PATH_SEPARATOR, true, DirModifyInfo::ACTION_DELETED);
				return;
			} else {
				auto f = parent->files.find(fileNameLower);
				if (f != parent->files.end()) {
					addModifyInfo(aPath, false, DirModifyInfo::ACTION_DELETED);
					return;
				}
			}
		}
	}

	string path = aPath;

	// check if it's being added
	auto p = findModifyInfo(aPath);
	if (p != fileModifications.end()) {
		removeNotifications(p, path);
	}
}

void ShareManager::removeNotifications(DirModifyInfo::List::iterator p, const string& aPath) noexcept {
	if (p->path == aPath || Util::strnicmp(p->path, aPath, aPath.length()) == 0) {
		fileModifications.erase(p);
	} else {
		// remove subitems
		for (auto i = p->files.begin(); i != p->files.end();) {
			if (AirUtil::isParentOrExact(aPath, i->first)) {
				i = p->files.erase(i);
			} else {
				i++;
			}
		}

		p->lastFileActivity = GET_TICK();
		if (p->files.empty() && p->dirAction == DirModifyInfo::ACTION_NONE) {
			fileModifications.erase(p);
		}
	}
}

bool ShareManager::handleDeletedFile(const string& aPath, bool isDirectory, ProfileTokenSet& dirtyProfiles_) noexcept {
	bool deleted = false;
	auto parent = findDirectory(isDirectory ? Util::getParentDir(aPath) : Util::getFilePath(aPath));
	if (parent) {
		parent->copyRootProfiles(dirtyProfiles_, true);
		if (isDirectory) {
			auto dirNameLower = Text::toLower(Util::getLastDir(aPath));
			auto p = parent->directories.find(dirNameLower);
			if (p != parent->directories.end()) {
				cleanIndices(**p);
				parent->directories.erase(p);
				deleted = true;
			}
		} else {
			auto fileNameLower = Text::toLower(Util::getFileName(aPath));
			auto f = parent->files.find(fileNameLower);
			if (f != parent->files.end()) {
				cleanIndices(*parent, *f);
				parent->files.erase(f);
				deleted = true;
			}
		}

		parent->updateModifyDate();
		if (SETTING(SKIP_EMPTY_DIRS_SHARE) && parent->directories.empty() && parent->files.empty() && parent->getParent()) {
			//remove the parent
			cleanIndices(*parent);
			parent->getParent()->directories.erase_key(parent->realName.getLower());
		}
	}

	return deleted;
}

void ShareManager::on(DirectoryMonitorListener::Overflow, const string& aRootPath) noexcept {
	if (monitorDebug)
		LogManager::getInstance()->message("Monitoring overflow: " + aRootPath, LogMessage::SEV_INFO);

	// refresh the dir
	addRefreshTask(REFRESH_DIRS, { aRootPath }, TYPE_MONITORING);
}

void ShareManager::abortRefresh() noexcept {
	//abort buildtree and refresh, we are shutting down.
	aShutdown = true;
}

void ShareManager::shutdown(function<void(float)> progressF) noexcept {
	monitor.removeListener(this);
	saveXmlList(progressF);

	try {
		RLock l (cs);
		//clear refs so we can delete filelists.
		auto lists = File::findFiles(Util::getPath(Util::PATH_USER_CONFIG), "files?*.xml.bz2", File::TYPE_FILE);
		for(auto& f: shareProfiles) {
			if(f->getProfileList() && f->getProfileList()->bzXmlRef.get()) 
				f->getProfileList()->bzXmlRef.reset(); 
		}

		for_each(lists, File::deleteFile);
	} catch(...) { }

	TimerManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);
	join();
}

void ShareManager::setProfilesDirty(ProfileTokenSet aProfiles, bool aIsMajorChange /*false*/) noexcept {
	if (!aProfiles.empty()) {
		RLock l(cs);
		for(const auto token: aProfiles) {
			auto i = find(shareProfiles.begin(), shareProfiles.end(), token);
			if(i != shareProfiles.end()) {
				if (aIsMajorChange)
					(*i)->getProfileList()->setForceXmlRefresh(true);
				(*i)->getProfileList()->setXmlDirty(true);
				(*i)->setProfileInfoDirty(true);
			}
		}
	}

	for (const auto token : aProfiles) {
		fire(ShareManagerListener::ProfileUpdated(), token, aIsMajorChange);
	}
}

ShareManager::Directory::Directory(DualString&& aRealName, const ShareManager::Directory::Ptr& aParent, uint64_t aLastWrite, ProfileDirectory::Ptr aProfileDir) :
	size(0),
	parent(aParent.get()),
	profileDir(aProfileDir),
	lastWrite(aLastWrite),
	realName(move(aRealName))
{
}

ShareManager::Directory::~Directory() { 
	for_each(files, DeleteFunction());
}

void ShareManager::Directory::updateModifyDate() {
	lastWrite = dcpp::File::getLastModified(getRealPath());
}

void ShareManager::Directory::getContentInfo(int64_t& size_, size_t& files_, size_t& folders_) const noexcept {
	for(const auto& d: directories) {
		d->getContentInfo(size_, files_, folders_);
	}

	folders_ += directories.size();
	size_ += size;
	files_ += files.size();
}

int64_t ShareManager::Directory::getSize() const noexcept {
	int64_t tmp = size;
	for(const auto& d: directories) {
		tmp += d->getSize();
	}
	return tmp;
}

int64_t ShareManager::Directory::getTotalSize() const noexcept {
	int64_t tmp = size;
	for(const auto& d: directories)
		tmp += d->getTotalSize();

	return tmp;
}

string ShareManager::Directory::getADCPath() const noexcept {
	if (profileDir) {
		return '/' + profileDir->getName() + '/';
	}

	return parent->getADCPath() + realName.getNormal() + '/';
}

string ShareManager::Directory::getVirtualName() const noexcept {
	if (profileDir) {
		return profileDir->getName();
	}

	return realName.getNormal();
}

const string& ShareManager::Directory::getVirtualNameLower() const noexcept {
	if (profileDir) {
		return profileDir->getNameLower();
	}

	return realName.getLower();
}

string ShareManager::Directory::getFullName() const noexcept {
	if(profileDir)
		return profileDir->getName() + '\\';
	dcassert(parent);
	return parent->getFullName() + realName.getNormal() + '\\';
}

StringList ShareManager::getRealPaths(const TTHValue& root) const noexcept {
	StringList ret;

	RLock l(cs);
	const auto i = tthIndex.equal_range(const_cast<TTHValue*>(&root)); 
	for (auto f = i.first; f != i.second; ++f) {
		ret.push_back((*f).second->getRealPath());
	}

	const auto k = tempShares.find(root);
	if (k != tempShares.end()) {
		ret.push_back(k->second.path);
	}

	return ret;
}


bool ShareManager::isTTHShared(const TTHValue& tth) const noexcept {
	RLock l(cs);
	return tthIndex.find(const_cast<TTHValue*>(&tth)) != tthIndex.end();
}

string ShareManager::Directory::getRealPath(const string& path) const noexcept {
	if(getParent()) {
		return getParent()->getRealPath(realName.getNormal() + PATH_SEPARATOR_STR + path);
	}

	return profileDir->getPath() + path;
}

bool ShareManager::Directory::isRoot() const noexcept {
	return profileDir ? true : false;
}

bool ShareManager::Directory::hasProfile(const ProfileTokenSet& aProfiles) const noexcept {
	if (profileDir && profileDir->hasRootProfile(aProfiles)) {
		return true;
	}

	if (parent)
		return parent->hasProfile(aProfiles);
	return false;
}


void ShareManager::Directory::copyRootProfiles(ProfileTokenSet& profiles_, bool aSetCacheDirty) const noexcept {
	if (profileDir) {
		boost::copy(profileDir->getRootProfiles(), inserter(profiles_, profiles_.begin()));
		if (aSetCacheDirty)
			profileDir->setCacheDirty(true);
	}

	if (parent)
		parent->copyRootProfiles(profiles_, aSetCacheDirty);
}

bool ShareManager::ProfileDirectory::hasRootProfile(const ProfileTokenSet& aProfiles) const noexcept {
	for(const auto ap: aProfiles) {
		if (rootProfiles.find(ap) != rootProfiles.end())
			return true;
	}
	return false;
}

bool ShareManager::Directory::hasProfile(const OptionalProfileToken& aProfile) const noexcept {
	if(!aProfile || (profileDir && profileDir->hasRootProfile(*aProfile))) {
		return true;
	} 
	
	if (parent) {
		return parent->hasProfile(aProfile);
	}

	return false;
}

bool ShareManager::ProfileDirectory::hasRootProfile(ProfileToken aProfile) const noexcept {
	return rootProfiles.find(aProfile) != rootProfiles.end();
}

ShareManager::ProfileDirectory::ProfileDirectory(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming) noexcept :
	path(aRootPath), cacheDirty(false), virtualName(unique_ptr<DualString>(new DualString(aVname))), 
	incoming(aIncoming), rootProfiles(aProfiles) {

}

ShareManager::ProfileDirectory::Ptr ShareManager::ProfileDirectory::create(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming, Map& profileDirectories_) noexcept {
	auto pd = new ProfileDirectory(aRootPath, aVname, aProfiles, aIncoming);
	profileDirectories_[aRootPath] = pd;
	return pd;
}

void ShareManager::ProfileDirectory::addRootProfile(ProfileToken aProfile) noexcept {
	rootProfiles.emplace(aProfile);
}

bool ShareManager::ProfileDirectory::removeRootProfile(ProfileToken aProfile) noexcept {
	rootProfiles.erase(aProfile);
	return rootProfiles.empty();
}

string ShareManager::toVirtual(const TTHValue& tth, ProfileToken aProfile) const throw(ShareException) {
	
	RLock l(cs);

	FileList* fl = getFileList(aProfile);
	if(tth == fl->getBzXmlRoot()) {
		return Transfer::USER_LIST_NAME_BZ;
	} else if(tth == fl->getXmlRoot()) {
		return Transfer::USER_LIST_NAME;
	}

	auto i = tthIndex.find(const_cast<TTHValue*>(&tth)); 
	if (i != tthIndex.end()) {
		return i->second->getADCPath();
	}

	//nothing found throw;
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

FileList* ShareManager::getFileList(ProfileToken aProfile) const throw(ShareException) {
	const auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
	if(i != shareProfiles.end()) {
		dcassert((*i)->getProfileList());
		return (*i)->getProfileList();
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

pair<int64_t, string> ShareManager::getFileListInfo(const string& virtualFile, ProfileToken aProfile) throw(ShareException) {
	if(virtualFile == "MyList.DcLst") 
		throw ShareException("NMDC-style lists no longer supported, please upgrade your client");

	if(virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
		FileList* fl = generateXmlList(aProfile);
		return { fl->getBzXmlListLen(), fl->getFileName() };
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

void ShareManager::toRealWithSize(const string& aVirtualFile, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_) throw(ShareException) {
	if(aVirtualFile.compare(0, 4, "TTH/") == 0) {
		TTHValue tth(aVirtualFile.substr(4));

		RLock l(cs);
		if(any_of(aProfiles.begin(), aProfiles.end(), [](ProfileToken s) { return s != SP_HIDDEN; })) {
			const auto flst = tthIndex.equal_range(const_cast<TTHValue*>(&tth));
			for(auto f = flst.first; f != flst.second; ++f) {
				noAccess_ = false; //we may throw if the file doesn't exist on the disk so always reset this to prevent invalid access denied messages
				auto profiles = aProfiles;
				if (f->second->getParent()->hasProfile(profiles)) {
					path_ = f->second->getRealPath();
					size_ = f->second->getSize();
					return;
				} else {
					noAccess_ = true;
				}
			}
		}

		const auto files = tempShares.equal_range(tth);
		for(auto i = files.first; i != files.second; ++i) {
			noAccess_ = false;
			if(i->second.key.empty() || (i->second.key == aUser.user->getCID().toBase32())) { // if no key is set, it means its a hub share.
				path_ = i->second.path;
				size_ = i->second.size;
				return;
			} else {
				noAccess_ = true;
			}
		}
	} else {
		Directory::List dirs;

		RLock l (cs);
		findVirtuals<ProfileTokenSet>(aVirtualFile, aProfiles, dirs);

		auto fileName = Text::toLower(Util::getAdcFileName(aVirtualFile));
		for(const auto& d: dirs) {
			auto it = d->files.find(fileName);
			if(it != d->files.end()) {
				path_ = (*it)->getRealPath();
				size_ = (*it)->getSize();
				return;
			}
		}
	}

	throw ShareException(noAccess_ ? "You don't have access to this file" : UserConnection::FILE_NOT_AVAILABLE);
}

TTHValue ShareManager::getListTTH(const string& virtualFile, ProfileToken aProfile) const throw(ShareException) {
	RLock l(cs);
	if(virtualFile == Transfer::USER_LIST_NAME_BZ) {
		return getFileList(aProfile)->getBzXmlRoot();
	} else if(virtualFile == Transfer::USER_LIST_NAME) {
		return getFileList(aProfile)->getXmlRoot();
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

MemoryInputStream* ShareManager::getTree(const string& virtualFile, ProfileToken aProfile) const noexcept {
	TigerTree tree;
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		if(!HashManager::getInstance()->getTree(TTHValue(virtualFile.substr(4)), tree))
			return nullptr;
	} else {
		try {
			TTHValue tth = getListTTH(virtualFile, aProfile);
			HashManager::getInstance()->getTree(tth, tree);
		} catch(const Exception&) {
			return nullptr;
		}
	}

	ByteVector buf = tree.getLeafData();
	return new MemoryInputStream(&buf[0], buf.size());
}

AdcCommand ShareManager::getFileInfo(const string& aFile, ProfileToken aProfile) throw(ShareException) {
	if(aFile == Transfer::USER_LIST_NAME) {
		FileList* fl = generateXmlList(aProfile);
		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(fl->getXmlListLen()));
		cmd.addParam("TR", fl->getXmlRoot().toBase32());
		return cmd;
	} else if(aFile == Transfer::USER_LIST_NAME_BZ) {
		FileList* fl = generateXmlList(aProfile);
		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(fl->getBzXmlListLen()));
		cmd.addParam("TR", fl->getBzXmlRoot().toBase32());
		return cmd;
	}

	if(aFile.compare(0, 4, "TTH/") != 0)
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);

	TTHValue val(aFile.substr(4));
	
	RLock l(cs);
	auto i = tthIndex.find(const_cast<TTHValue*>(&val)); 
	if(i != tthIndex.end()) {
		const Directory::File* f = i->second;
		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", f->getADCPath());
		cmd.addParam("SI", Util::toString(f->getSize()));
		cmd.addParam("TR", f->getTTH().toBase32());
		return cmd;
	}

	//not found throw
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

bool ShareManager::isTempShared(const string& aKey, const TTHValue& tth) {
	RLock l(cs);
	const auto fp = tempShares.equal_range(tth);
	for(auto i = fp.first; i != fp.second; ++i) {
		if(i->second.key.empty() || (i->second.key == aKey)) // if no key is set, it means its a hub share.
			return true;
	}
	return false;
}

void ShareManager::addTempShare(const string& aKey, const TTHValue& tth, const string& filePath, int64_t aSize, ProfileToken aProfile) {
	
	//first check if already exists in Share.
	if(isFileShared(tth, aProfile)) {
		return;
	} else {
		WLock l(cs);
		const auto files = tempShares.equal_range(tth);
		for(auto i = files.first; i != files.second; ++i) {
			if(i->second.key == aKey)
				return;
		}
		//didnt exist.. fine, add it.
		tempShares.emplace(tth, TempShareInfo(aKey, filePath, aSize));
	}
}
void ShareManager::removeTempShare(const string& aKey, const TTHValue& tth) {
	WLock l(cs);
	const auto files = tempShares.equal_range(tth);
	for(auto i = files.first; i != files.second; ++i) {
		if(i->second.key == aKey) {
			tempShares.erase(i);
			break;
		}
	}
}

void ShareManager::removeTempShare(const string& aPath) {
	WLock l(cs);
	tempShares.erase(boost::remove_if(tempShares | map_values, [&](const TempShareInfo& ti) { return Util::stricmp(aPath, ti.path) == 0; }).base(), tempShares.end());
}

void ShareManager::clearTempShares() {
	WLock l(cs);
	tempShares.clear();
}

void ShareManager::getRealPaths(const string& aPath, StringList& realPaths_, const OptionalProfileToken& aProfile) const throw(ShareException) {
	if (aPath.empty())
		throw ShareException("empty virtual path");

	if (aPath == "/") {
		getRootPaths(realPaths_);
		return;
	}

	Directory::List dirs;

	RLock l(cs);
	findVirtuals<OptionalProfileToken>(aPath, aProfile, dirs);

	if (aPath.back() == '/') {
		// Directory
		for (const auto& d : dirs) {
			realPaths_.push_back(d->getRealPath());
		}
	} else {
		// File
		auto fileName = Text::toLower(Util::getAdcFileName(aPath));
		for(const auto& d: dirs) {
			auto it = d->files.find(fileName);
			if(it != d->files.end()) {
				realPaths_.push_back((*it)->getRealPath());
				return;
			}
		}
	}
}

bool ShareManager::isRealPathShared(const string& aPath) const noexcept {
	RLock l (cs);
	auto d = findDirectory(Util::getFilePath(aPath));
	if (d) {
		if (!aPath.empty() && aPath.back() == PATH_SEPARATOR) {
			// It's a directory
			return true;
		}

		// It's a file
		auto it = d->files.find(Text::toLower(Util::getFileName(aPath)));
		if(it != d->files.end()) {
			return true;
		}
	}

	return false;
}

string ShareManager::realToVirtual(const string& aPath, const OptionalProfileToken& aToken) const noexcept{
	RLock l(cs);
	auto d = findDirectory(Util::getFilePath(aPath));
	if (!d || !d->hasProfile(aToken)) {
		return Util::emptyString;
	}

	auto vPath = d->getFullName();
	if (aPath.back() == PATH_SEPARATOR) {
		// Directory
		return vPath;
	}

	// It's a file
	return vPath + "\\" + Util::getFileName(aPath);
}

string ShareManager::validateVirtualName(const string& aVirt) const noexcept {
	string tmp = aVirt;
	string::size_type idx = 0;

	while( (idx = tmp.find_first_of("\\/"), idx) != string::npos) {
		tmp[idx] = '_';
	}
	return tmp;
}

void ShareManager::loadProfile(SimpleXML& aXml, const string& aName, ProfileToken aToken) {
	auto sp = std::make_shared<ShareProfile>(aName, aToken);
	shareProfiles.push_back(sp);

	aXml.stepIn();
	while(aXml.findChild("Directory")) {
		auto realPath = Util::validatePath(aXml.getChildData(), true);
		if(realPath.empty()) {
			continue;
		}

		const auto& loadedVirtualName = aXml.getChildAttrib("Virtual");

		// Validate in case we have changed the rules
		auto vName = validateVirtualName(loadedVirtualName.empty() ? Util::getLastDir(realPath) : loadedVirtualName);

		ProfileDirectory::Ptr pd = nullptr;
		auto p = profileDirs.find(realPath);
		if (p != profileDirs.end()) {
			pd = p->second;
			pd->addRootProfile(aToken);
		} else {
			pd = ProfileDirectory::create(realPath, vName, { aToken }, aXml.getBoolChildAttrib("Incoming"), profileDirs);
			pd->setLastRefreshTime(aXml.getLongLongChildAttrib("LastRefreshTime"));
		}

		auto j = rootPaths.find(realPath);
		if (j == rootPaths.end()) {
			Directory::createRoot(vName, 0, pd, rootPaths, dirNameMap, *bloom.get());
		}
	}

	aXml.resetCurrentChild();
	if(aXml.findChild("NoShare")) {
		aXml.stepIn();
		while(aXml.findChild("Directory")) {
			auto path = aXml.getChildData();

			excludedPaths.insert(path);
		}
		aXml.stepOut();
	}
	aXml.stepOut();
}

void ShareManager::load(SimpleXML& aXml) {
	//WLock l(cs);
	aXml.resetCurrentChild();

	if(aXml.findChild("Share")) {
		const auto& name = aXml.getChildAttrib("Name");
		loadProfile(aXml, !name.empty() ? name : STRING(DEFAULT), aXml.getIntChildAttrib("Token"));
	}

	aXml.resetCurrentChild();
	while(aXml.findChild("ShareProfile")) {
		const auto& token = aXml.getIntChildAttrib("Token");
		const auto& name = aXml.getChildAttrib("Name");
		if (token != SP_HIDDEN && !name.empty()) //reserve a few numbers for predefined profiles
			loadProfile(aXml, name, token);
	}

	{
		// Validate loaded paths
		auto rootPathsCopy = rootPaths;
		for (const auto& dp : rootPathsCopy) {
			if (find_if(rootPathsCopy | map_keys, [&dp](const string& aPath) { 
				return AirUtil::isSub(dp.first, aPath); 
			}).base() != rootPathsCopy.end()) {
				removeDirName(*dp.second.get(), dirNameMap);
				rootPaths.erase(dp.first);

				LogManager::getInstance()->message("The directory " + dp.first + " was not loaded: parent of this directory is shared in another profile, which is not supported in this client version.", LogMessage::SEV_WARNING);
			}
		}
	}
}

ShareProfilePtr ShareManager::getShareProfile(ProfileToken aProfile, bool allowFallback /*false*/) const noexcept {
	RLock l (cs);
	const auto p = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
	if (p != shareProfiles.end()) {
		return *p;
	} else if (allowFallback) {
		dcassert(aProfile != SETTING(DEFAULT_SP));
		return *shareProfiles.begin();
	}
	return nullptr;
}

OptionalProfileToken ShareManager::getProfileByName(const string& aName) const noexcept {
	RLock l(cs);
	if (aName.empty()) {
		return SETTING(DEFAULT_SP);
	}

	auto p = find_if(shareProfiles, [&](const ShareProfilePtr& aProfile) { return Util::stricmp(aProfile->getPlainName(), aName) == 0; });
	if (p == shareProfiles.end())
		return boost::none;
	return (*p)->getToken();
}

ShareManager::Directory::Ptr ShareManager::Directory::createNormal(DualString&& aRealName, const Ptr& aParent, uint64_t aLastWrite, Directory::MultiMap& dirNameMap_, ShareBloom& bloom) noexcept {
	auto dir = Ptr(new Directory(move(aRealName), aParent, aLastWrite, nullptr));

	if (aParent) {
		aParent->directories.insert_sorted(dir);
	}

	addDirName(dir, dirNameMap_, bloom);
	return dir;
}

ShareManager::Directory::Ptr ShareManager::Directory::createRoot(DualString&& aRealName, uint64_t aLastWrite, const ProfileDirectory::Ptr& aProfileDir, Map& rootPaths_, Directory::MultiMap& dirNameMap_, ShareBloom& bloom) noexcept {
	auto dir = Ptr(new Directory(move(aRealName), nullptr, aLastWrite, aProfileDir));
	rootPaths_[aProfileDir->getPath()] = dir;
	addDirName(dir, dirNameMap_, bloom);
	return dir;
}

static const string SDIRECTORY = "Directory";
static const string SFILE = "File";
static const string SNAME = "Name";
static const string SSIZE = "Size";
static const string DATE = "Date";
static const string SHARE = "Share";
static const string SVERSION = "Version";

struct ShareManager::ShareLoader : public SimpleXMLReader::ThreadedCallBack, public ShareManager::RefreshInfo {
	ShareLoader(const string& aPath, const ShareManager::Directory::Ptr& aOldRoot, ShareManager::ShareBloom& aBloom) :
		ShareManager::RefreshInfo(aPath, aOldRoot, 0, aBloom),
		ThreadedCallBack(aOldRoot->getProfileDir()->getCacheXmlPath()),
		curDirPath(aOldRoot->getProfileDir()->getPath()),
		curDirPathLower(Text::toLower(aOldRoot->getProfileDir()->getPath())),
		bloom(aBloom)
	{ 
		cur = newShareDirectory;
	}


	void startTag(const string& aName, StringPairList& attribs, bool simple) {
		if(compare(aName, SDIRECTORY) == 0) {
			const string& name = getAttrib(attribs, SNAME, 0);
			const string& date = getAttrib(attribs, DATE, 1);

			if(!name.empty()) {
				curDirPath += name + PATH_SEPARATOR;

				cur = ShareManager::Directory::createNormal(name, cur, Util::toUInt32(date), dirNameMapNew, bloom);
				curDirPathLower += cur->realName.getLower() + PATH_SEPARATOR;
			}

			if(simple) {
				if(cur) {
					cur = cur->getParent();
				}
			}
		} else if (cur && compare(aName, SFILE) == 0) {
			const string& fname = getAttrib(attribs, SNAME, 0);
			if(fname.empty()) {
				dcdebug("Invalid file found: %s\n", fname.c_str());
				return;
			}

			try {
				DualString name(fname);
				HashedFile fi;
				HashManager::getInstance()->getFileInfo(curDirPathLower + name.getLower(), curDirPath + fname, fi);
				auto pos = cur->files.insert_sorted(new ShareManager::Directory::File(move(name), cur, fi));
				ShareManager::updateIndices(*cur, *pos.first, bloom, addedSize, tthIndexNew);
			}catch(Exception& e) {
				hashSize += File::getSize(curDirPath + fname);
				dcdebug("Error loading file list %s \n", e.getError().c_str());
			}
		} else if (compare(aName, SHARE) == 0) {
			int version = Util::toInt(getAttrib(attribs, SVERSION, 0));
			if (version > Util::toInt(SHARE_CACHE_VERSION))
				throw("Newer cache version"); //don't load those...

			cur->setLastWrite(Util::toUInt32(getAttrib(attribs, DATE, 2)));
		}
	}
	void endTag(const string& name) {
		if(compare(name, SDIRECTORY) == 0) {
			if(cur) {
				curDirPath = Util::getParentDir(curDirPath);
				curDirPathLower = Util::getParentDir(curDirPathLower);
				cur = cur->getParent();
			}
		}
	}

private:
	friend struct SizeSort;

	ShareManager::Directory::Ptr cur;

	string curDirPathLower;
	string curDirPath;
	ShareManager::ShareBloom& bloom;
};

typedef shared_ptr<ShareManager::ShareLoader> ShareLoaderPtr;
typedef vector<ShareLoaderPtr> LoaderList;

bool ShareManager::loadCache(function<void(float)> progressF) noexcept{
	HashManager::HashPauser pauser;

	Util::migrate(Util::getPath(Util::PATH_SHARECACHE), "ShareCache_*");

	// Get all cache XMLs
	StringList fileList = File::findFiles(Util::getPath(Util::PATH_SHARECACHE), "ShareCache_*", File::TYPE_FILE);

	if (fileList.empty()) {
		return rootPaths.empty();
	}

	LoaderList ll;

	// Create loaders
	for (const auto& p : fileList) {
		if (Util::getFileExt(p) == ".xml") {
			// Find the corresponding directory pointer for this path
			auto rp = find_if(rootPaths | map_values, [&p](const Directory::Ptr& aDir) {
				return Util::stricmp(aDir->getProfileDir()->getCacheXmlPath(), p) == 0; 
			});

			if (rp.base() != rootPaths.end()) {
				try {
					auto loader = std::make_shared<ShareLoader>(rp.base()->first, *rp, *bloom.get());
					ll.emplace_back(loader);
					continue;
				} catch (...) {}
			}
		}

		// No use for this cache file
		File::deleteFile(p);
	}

	{
		const auto dirCount = ll.size();

		//ll.sort(SimpleXMLReader::ThreadedCallBack::SizeSort());

		// Parse the actual cache files
		atomic<long> loaded(0);
		bool hasFailedCaches = false;

		try {
			parallel_for_each(ll.begin(), ll.end(), [&](ShareLoaderPtr& i) {
				//LogManager::getInstance()->message("Thread: " + Util::toString(::GetCurrentThreadId()) + "Size " + Util::toString(loader.size), LogMessage::SEV_INFO);
				auto& loader = *i;
				try {
					SimpleXMLReader(&loader).parse(*loader.file);
				} catch (SimpleXMLException& e) {
					LogManager::getInstance()->message(STRING_F(LOAD_FAILED_X, loader.xmlPath % e.getError()), LogMessage::SEV_ERROR);
					hasFailedCaches = true;
					File::deleteFile(loader.xmlPath);
				} catch (...) {
					hasFailedCaches = true;
					File::deleteFile(loader.xmlPath);
				}

				if (progressF) {
					progressF(static_cast<float>(loaded++) / static_cast<float>(dirCount));
				}
			});
		} catch (std::exception& e) {
			hasFailedCaches = true;
			LogManager::getInstance()->message("Loading the share cache failed: " + string(e.what()), LogMessage::SEV_INFO);
		}

		if (hasFailedCaches) {
			// Refresh all
			return false;
		}
	}

	// Apply the changes
	int64_t hashSize = 0;
	//Directory::Map newRoots;

	for (const auto& l : ll) {
		l->mergeRefreshChanges(dirNameMap, rootPaths, tthIndex, hashSize, sharedSize, nullptr);
	}

	// Were all roots loaded?
	/*StringList refreshDirs;
	for (auto& i: rootPaths) {
		auto p = newRoots.find(i.first);
		if (p == newRoots.end()) {
			//add for refresh
			refreshDirs.push_back(i.first);
		}
	}

	addRefreshTask(REFRESH_DIRS, refreshDirs, TYPE_MANUAL, Util::emptyString);*/

	if (hashSize > 0) {
		LogManager::getInstance()->message(STRING_F(FILES_ADDED_FOR_HASH_STARTUP, Util::formatBytes(hashSize)), LogMessage::SEV_INFO);
	}

	return true;
}

void ShareManager::save(SimpleXML& aXml) {
	RLock l(cs);
	for(const auto& sp: shareProfiles) {
		if (sp->getToken() == SP_HIDDEN) {
			continue;
		}

		aXml.addTag(sp->getToken() == SETTING(DEFAULT_SP) ? "Share" : "ShareProfile");
		aXml.addChildAttrib("Token", sp->getToken());
		aXml.addChildAttrib("Name", sp->getPlainName());
		aXml.stepIn();

		for(const auto& d: rootPaths | map_values) {
			if (!d->getProfileDir()->hasRootProfile(sp->getToken()))
				continue;
			aXml.addTag("Directory", d->getRealPath());
			aXml.addChildAttrib("Virtual", d->getProfileDir()->getName());
			aXml.addChildAttrib("Incoming", d->getProfileDir()->getIncoming());
			aXml.addChildAttrib("LastRefreshTime", d->getProfileDir()->getLastRefreshTime());
		}

		aXml.addTag("NoShare");
		aXml.stepIn();
		for(const auto& path: excludedPaths) {
			aXml.addTag("Directory", path);
		}
		aXml.stepOut();
		aXml.stepOut();
	}
}

void ShareManager::Directory::countStats(uint64_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles_, size_t& lowerCaseFiles_, size_t& totalStrLen_) const noexcept{
	for(auto& d: directories) {
		d->countStats(totalAge_, totalDirs_, totalSize_, totalFiles_, lowerCaseFiles_, totalStrLen_);
	}

	for(auto& f: files) {
		totalSize_ += f->getSize();
		totalAge_ += f->getLastWrite();
		totalStrLen_ += f->name.getLower().length();
		if (f->name.lowerCaseOnly()) {
			lowerCaseFiles_++;
		}
	}

	totalStrLen_ += realName.getLower().length();
	totalDirs_ += directories.size();
	totalFiles_ += files.size();
}

void ShareManager::countStats(uint64_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles_, size_t& lowerCaseFiles_, size_t& totalStrLen_, size_t& roots_) const noexcept{
	RLock l(cs);
	for (const auto& d : rootPaths | map_values | filtered(Directory::IsParent())) {
		totalDirs_++;
		roots_++;
		d->countStats(totalAge_, totalDirs_, totalSize_, totalFiles_, lowerCaseFiles_, totalStrLen_);
	}
}

optional<ShareManager::ShareStats> ShareManager::getShareStats() const noexcept {
	unordered_set<TTHValue*> uniqueTTHs;

	{
		RLock l(cs);
		for (auto tth : tthIndex | map_keys) {
			uniqueTTHs.insert(tth);
		}
	}

	ShareStats stats;
	stats.profileCount = shareProfiles.size() - 1; // remove hidden
	stats.uniqueFileCount = uniqueTTHs.size();

	uint64_t totalAge = 0;
	size_t lowerCaseFiles = 0;
	countStats(totalAge, stats.totalDirectoryCount, stats.totalSize, stats.totalFileCount, lowerCaseFiles, stats.totalNameSize, stats.profileDirectoryCount);

	if (stats.uniqueFileCount == 0 || stats.totalDirectoryCount == 0) {
		return boost::none;
	}

	stats.uniqueFilePercentage = (static_cast<double>(stats.uniqueFileCount) / static_cast<double>(stats.totalFileCount))*100.00;
	stats.lowerCasePercentage = (static_cast<double>(lowerCaseFiles) / static_cast<double>(stats.totalFileCount))*100.00;
	stats.filesPerDirectory = static_cast<double>(stats.totalFileCount) / static_cast<double>(stats.totalDirectoryCount);
	stats.averageFileAge = GET_TIME() - (stats.totalFileCount == 0 ? 0 : totalAge / stats.totalFileCount);
	stats.averageNameLength = static_cast<double>(stats.totalNameSize) / static_cast<double>(stats.totalFileCount + stats.totalDirectoryCount);
	stats.rootDirectoryPercentage = (static_cast<double>(stats.profileDirectoryCount) / static_cast<double>(rootPaths.size())) *100.00;
	return stats;
}

string ShareManager::printStats() const noexcept {
	auto optionalStats = getShareStats();
	if (!optionalStats) {
		return "No files shared";
	}

	auto stats = *optionalStats;
	auto upseconds = static_cast<double>(GET_TICK()) / 1000.00;

	string ret = boost::str(boost::format(
"\r\n\r\n-=[ Share statistics ]=-\r\n\r\n\
Share profiles: %d\r\n\
Shared paths: %d (of which %d%% are roots)\r\n\
Total share size: %s\r\n\
Total shared files: %d (of which %d%% are lowercase)\r\n\
Unique TTHs: %d (%d%%)\r\n\
Total shared directories: %d (%d files per directory)\r\n\
Average age of a file: %s\r\n\
Average name length of a shared item: %d bytes (total size %s)")

		% stats.profileCount
		% stats.profileDirectoryCount % stats.rootDirectoryPercentage
		% Util::formatBytes(stats.totalSize)
		% stats.totalFileCount % stats.lowerCasePercentage
		% stats.uniqueFileCount % stats.uniqueFilePercentage
		% stats.totalDirectoryCount % stats.filesPerDirectory
		% Util::formatTime(stats.averageFileAge, false, true)
		% stats.averageNameLength
		% Util::formatBytes(stats.totalNameSize)
	);

	ret += boost::str(boost::format(
"\r\n\r\n-=[ Search statistics ]=-\r\n\r\n\
Total incoming searches: %d (%d per second)\r\n\
Incoming text searches: %d (of which %d were matched per second)\r\n\
Filtered text searches: %d%% (%d%% of the matched ones returned results)\r\n\
Average search tokens (non-filtered only): %d (%d bytes per token)\r\n\
Auto searches (text, ADC only): %d%%\r\n\
Average time for matching a recursive search: %d ms\r\n\
TTH searches: %d%% (hash bloom mode: %s)")

		% totalSearches % (totalSearches / upseconds)
		% recursiveSearches % ((recursiveSearches - filteredSearches) / upseconds)
		% (recursiveSearches == 0 ? 0 : (static_cast<double>(filteredSearches) / static_cast<double>(recursiveSearches))*100.00) // filtered
		% (recursiveSearches - filteredSearches == 0 ? 0 : (static_cast<double>(recursiveSearchesResponded) / static_cast<double>(recursiveSearches - filteredSearches))*100.00) // recursive searches with results
		% (recursiveSearches - filteredSearches == 0 ? 0 : static_cast<double>(searchTokenCount) / static_cast<double>(recursiveSearches - filteredSearches)) // search token count
		% (searchTokenCount == 0 ? 0 : static_cast<double>(searchTokenLength) / static_cast<double>(searchTokenCount)) // search token length
		% (recursiveSearches == 0 ? 0 : (static_cast<double>(autoSearches) / static_cast<double>(recursiveSearches))*100.00) // auto searches
		% (recursiveSearches - filteredSearches == 0 ? 0 : recursiveSearchTime / (recursiveSearches - filteredSearches)) // search matching time
		% (totalSearches == 0 ? 0 : (static_cast<double>(tthSearches) / static_cast<double>(totalSearches))*100.00) // TTH searches
		% (SETTING(BLOOM_MODE) != SettingsManager::BLOOM_DISABLED ? "Enabled" : "Disabled") // bloom mode
	);

	ret += "\r\n\r\n-=[ Monitoring statistics ]=-\r\n\r\n";
	if (monitor.hasDirectories()) {
		ret += "Debug mode: ";
		ret += (monitorDebug ? "Enabled" : "Disabled");
		ret += " \r\n\r\nMonitored paths:\r\n";
		ret += monitor.getStats();
	} else {
		ret += "No folders are being monitored\r\n";
	}
	return ret;
}

void ShareManager::validateNewRootProfiles(const string& realPath, const ProfileTokenSet& aProfiles) const throw(ShareException) {
	RLock l(cs);
	for (const auto& p : rootPaths) {
		auto rootProfileNames = ShareProfile::getProfileNames(p.second->getProfileDir()->getRootProfiles(), shareProfiles);
		if (AirUtil::isParentOrExact(p.first, realPath)) {
			if (Util::stricmp(p.first, realPath) != 0) {
				// Subdirectory of an existing directory is not allowed
				throw ShareException(STRING_F(DIRECTORY_PARENT_SHARED, Util::listToString(rootProfileNames)));
			}

			// Exact match is fine unless it's in this profile already
			if (p.second->getProfileDir()->hasRootProfile(aProfiles)) {
				throw ShareException(STRING(DIRECTORY_SHARED));
			}
		}

		if (AirUtil::isSub(p.first, realPath)) {
			throw ShareException(STRING_F(DIRECTORY_SUBDIRS_SHARED, Util::listToString(rootProfileNames)));
		}
	}
}

void ShareManager::validateRootPath(const string& realPath) const throw(ShareException) {
	if(realPath.empty()) {
		throw ShareException(STRING(NO_DIRECTORY_SPECIFIED));
	}

	if (!SETTING(SHARE_HIDDEN) && File::isHidden(realPath)) {
		throw ShareException(STRING(DIRECTORY_IS_HIDDEN));
	}
#ifdef _WIN32
	//need to throw here, so throw the error and dont use airutil
	TCHAR path[MAX_PATH];
	::SHGetFolderPath(NULL, CSIDL_WINDOWS, NULL, SHGFP_TYPE_CURRENT, path);
	string windows = Text::fromT((tstring)path) + PATH_SEPARATOR;
	// don't share Windows directory
	if (Util::strnicmp(realPath, windows, windows.length()) == 0) {
		throw ShareException(STRING_F(CHECK_FORBIDDEN, realPath));
	}
#endif

	if (realPath == Util::getAppFilePath() || realPath == Util::getPath(Util::PATH_USER_CONFIG) || realPath == Util::getPath(Util::PATH_USER_LOCAL)) {
		throw ShareException(STRING(DONT_SHARE_APP_DIRECTORY));
	}
}

void ShareManager::getRoots(const OptionalProfileToken& aProfile, Directory::List& dirs_) const noexcept {
	copy(rootPaths | map_values | filtered(Directory::HasRootProfile(aProfile)), back_inserter(dirs_));
}

void ShareManager::getRootsByVirtual(const string& aVirtualName, const OptionalProfileToken& aProfile, Directory::List& dirs_) const noexcept {
	for(const auto& d: rootPaths | map_values | filtered(Directory::HasRootProfile(aProfile))) {
		if(Util::stricmp(d->getProfileDir()->getName(), aVirtualName) == 0) {
			dirs_.push_back(d);
		}
	}
}

void ShareManager::getRootsByVirtual(const string& aVirtualName, const ProfileTokenSet& aProfiles, Directory::List& dirs_) const noexcept {
	for(const auto& d: rootPaths | map_values) {
		// Compare name
		if (Util::stricmp(d->getProfileDir()->getNameLower(), aVirtualName) != 0) {
			continue;
		}

		// Find any matching profile
		if (ShareProfile::hasCommonProfiles(d->getProfileDir()->getRootProfiles(), aProfiles)) {
			dirs_.push_back(d);
		}
	}
}

void ShareManager::Directory::getProfileInfo(ProfileToken aProfile, int64_t& totalSize, size_t& filesCount) const noexcept {
	totalSize += size;
	filesCount += files.size();

	for(const auto& d: directories) {
		d->getProfileInfo(aProfile, totalSize, filesCount);
	}
}

void ShareManager::getProfileInfo(ProfileToken aProfile, int64_t& size, size_t& files) const noexcept {
	auto sp = getShareProfile(aProfile);
	if (!sp)
		return;

	if (sp->getProfileInfoDirty()) {
		{
			RLock l(cs);
			for (const auto& d : rootPaths | map_values) {
				if (d->getProfileDir()->hasRootProfile(aProfile)) {
					d->getProfileInfo(aProfile, size, files);
				}
			}
		}

		sp->setSharedFiles(files);
		sp->setShareSize(size);
		sp->setProfileInfoDirty(false);
	}

	size = sp->getShareSize();
	files = sp->getSharedFiles();
}

int64_t ShareManager::getTotalShareSize(ProfileToken aProfile) const noexcept {
	int64_t ret = 0;

	RLock l(cs);
	for(const auto& d: rootPaths | map_values) {
		if(d->getProfileDir()->hasRootProfile(aProfile)) {
			ret += d->getSize();
		}
	}
	return ret;
}

bool ShareManager::isDirShared(const string& aDir) const noexcept{
	Directory::List dirs;

	RLock l (cs);
	getDirsByName(aDir, dirs);
	return !dirs.empty();
}

DupeType ShareManager::isDirShared(const string& aDir, int64_t aSize) const noexcept{
	Directory::List dirs;

	RLock l (cs);
	getDirsByName(aDir, dirs);
	if (dirs.empty())
		return DUPE_NONE;

	return dirs.front()->getTotalSize() == aSize ? DUPE_SHARE_FULL : DUPE_SHARE_PARTIAL;
}

StringList ShareManager::getDirPaths(const string& aDir) const noexcept{
	StringList ret;
	Directory::List dirs;

	RLock l(cs);
	getDirsByName(aDir, dirs);
	for (const auto& dir : dirs) {
		ret.push_back(dir->getRealPath());
	}

	return ret;
}

void ShareManager::getDirsByName(const string& aPath, Directory::List& dirs_) const noexcept {
	if (aPath.size() < 3)
		return;

	// get the last meaningful directory to look up
	auto p = AirUtil::getDirName(aPath, '\\');

	auto nameLower = Text::toLower(p.first);
	const auto directories = dirNameMap.equal_range(&nameLower);
	if (directories.first == directories.second)
		return;

	for (auto s = directories.first; s != directories.second; ++s) {
		if (p.second != string::npos) {
			// confirm that we have the subdirectory as well
			auto dir = s->second->findDirByPath(aPath.substr(p.second), '\\');
			if (dir) {
				dirs_.push_back(dir);
			}
		} else {
			dirs_.push_back(s->second);
		}
	}
}

ShareManager::Directory::Ptr ShareManager::Directory::findDirByPath(const string& aPath, char separator) const noexcept {
	auto p = aPath.find(separator);
	auto d = directories.find(Text::toLower(p != string::npos ? aPath.substr(0, p) : aPath));
	if (d != directories.end()) {
		if (p == aPath.size() || p == aPath.size() - 1)
			return *d;

		return (*d)->findDirByPath(aPath.substr(p+1), separator);
	}

	return nullptr;
}

bool ShareManager::isFileShared(const TTHValue& aTTH) const noexcept{
	RLock l (cs);
	return tthIndex.find(const_cast<TTHValue*>(&aTTH)) != tthIndex.end();
}

bool ShareManager::isFileShared(const TTHValue& aTTH, ProfileToken aProfile) const noexcept{
	RLock l (cs);
	const auto files = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
	for(auto i = files.first; i != files.second; ++i) {
		if(i->second->getParent()->hasProfile(aProfile)) {
			return true;
		}
	}

	return false;
}

void ShareManager::buildTree(const string& aPath, const string& aPathLower, const Directory::Ptr& aDir, Directory::MultiMap& directoryNameMapNew_, int64_t& hashSize_,
	int64_t& addedSize_, HashFileMap& tthIndexNew_, ShareBloom& bloomNew_) {

	FileFindIter end;
	for(FileFindIter i(aPath, "*"); i != end && !aShutdown; ++i) {
		string name = i->getFileName();
		if(name.empty()) {
			LogManager::getInstance()->message("Invalid file name found while hashing folder " + aPath + ".", LogMessage::SEV_WARNING);
			return;
		}

		if(!SETTING(SHARE_HIDDEN) && i->isHidden())
			continue;

		if (!SETTING(SHARE_FOLLOW_SYMLINKS) && i->isLink())
			continue;

		if(i->isDirectory()) {
			DualString dualName(name);
			string curPath = aPath + name + PATH_SEPARATOR;
			string curPathLower = aPathLower + dualName.getLower() + PATH_SEPARATOR;

			{
				RLock l (dirNames);
				if (!checkSharedName(curPath, curPathLower, true)) {
					continue;
				}

				//check queue so we dont add incomplete stuff to share.
				if(binary_search(bundleDirs.begin(), bundleDirs.end(), curPathLower)) {
					continue;
				}

				if (excludedPaths.find(curPath) != excludedPaths.end()) {
					continue;
				}
			}

			auto dir = Directory::createNormal(move(dualName), aDir, i->getLastWriteTime(), directoryNameMapNew_, bloomNew_);
			buildTree(curPath, curPathLower, dir, directoryNameMapNew_, hashSize_, addedSize_, tthIndexNew_, bloomNew_);

			// Empty directory?
			if (SETTING(SKIP_EMPTY_DIRS_SHARE) && dir->directories.empty() && dir->files.empty()) {
				// Remove from parent
				//cleanIndices(*dir.get());
				removeDirName(*dir.get(), directoryNameMapNew_);
				aDir->directories.erase_key(dir->realName.getLower());
				continue;
			}
		} else {
			// Not a directory, assume it's a file...
			int64_t size = i->getSize();

			DualString dualName(name);
			if (!checkSharedName(aPath + name, aPathLower + dualName.getLower(), false, true, size)) {
				continue;
			}

			try {
				HashedFile fi(i->getLastWriteTime(), size);
				if(HashManager::getInstance()->checkTTH(aPathLower + dualName.getLower(), aPath + name, fi)) {
					auto pos = aDir->files.insert_sorted(new ShareManager::Directory::File(move(dualName), aDir, fi));
					updateIndices(*aDir, *pos.first, bloomNew_, addedSize_, tthIndexNew_);
				} else {
					hashSize_ += size;
				}
			} catch(const HashException&) {
			}
		}
	}
}

void ShareManager::updateIndices(Directory::Ptr& dir, ShareBloom& aBloom, int64_t& sharedSize, HashFileMap& tthIndex, Directory::MultiMap& aDirNames) noexcept {
	// update all sub items
	for(auto& d: dir->directories) {
		updateIndices(d, aBloom, sharedSize, tthIndex, aDirNames);
	}

	for(auto i = dir->files.begin(); i != dir->files.end(); i++) {
		updateIndices(*dir, *i, aBloom, sharedSize, tthIndex);
	}
}

void ShareManager::updateIndices(Directory& dir, const Directory::File* f, ShareBloom& aBloom, int64_t& sharedSize, HashFileMap& tthIndex) noexcept {
	dir.size += f->getSize();
	sharedSize += f->getSize();

#ifdef _DEBUG
	auto flst = tthIndex.equal_range(const_cast<TTHValue*>(&f->getTTH()));
	auto p = find(flst | map_values, f);
	dcassert(p.base() == flst.second);
#endif

	tthIndex.emplace(const_cast<TTHValue*>(&f->getTTH()), f);
	aBloom.add(f->name.getLower());
}

ShareManager::RefreshResult ShareManager::refreshVirtualName(const string& aVirtualName) noexcept {
	StringList refreshDirs;

	{
		RLock l(cs);
		for(const auto& d: rootPaths | map_values) {
			if (Util::stricmp(d->getProfileDir()->getNameLower(), aVirtualName) == 0) {
				refreshDirs.push_back(d->getRealPath());
			}
		}
	}

	return addRefreshTask(REFRESH_DIRS, refreshDirs, TYPE_MANUAL, aVirtualName);
}


ShareManager::RefreshResult ShareManager::refresh(bool aIncoming, RefreshType aType, function<void(float)> progressF /*nullptr*/) noexcept {
	StringList dirs;

	{
		RLock l (cs);
		for(const auto& d: rootPaths | map_values | filtered(Directory::IsParent())) {
			if (aIncoming && !d->getProfileDir()->getIncoming())
				continue;

			dirs.push_back(d->getProfileDir()->getPath());
		}
	}

	return addRefreshTask(aIncoming ? REFRESH_INCOMING : REFRESH_ALL, dirs, aType, Util::emptyString, progressF);
}

struct ShareTask : public Task {
	ShareTask(const RefreshPathList& aDirs, const string& aDisplayName, ShareManager::RefreshType aRefreshType) : dirs(aDirs), displayName(aDisplayName), type(aRefreshType) { }
	RefreshPathList dirs;
	string displayName;
	ShareManager::RefreshType type;
};

void ShareManager::addAsyncTask(AsyncF aF) noexcept {
	tasks.add(ASYNC, unique_ptr<Task>(new AsyncTask(aF)));
	if (!refreshing.test_and_set()) {
		start();
	}
}

ShareManager::RefreshResult ShareManager::refreshPaths(const StringList& aPaths, const string& aDisplayName /*Util::emptyString*/, function<void(float)> aProgressF /*nullptr*/) noexcept {
	for (const auto& path : aPaths) {
		auto d = findDirectory(path);
		if (!d) {
			return RefreshResult::REFRESH_PATH_NOT_FOUND;
		}
	}

	return addRefreshTask(REFRESH_DIRS, aPaths, RefreshType::TYPE_MANUAL, aDisplayName, aProgressF);
}

void ShareManager::validateRefreshTask(StringList& dirs_) noexcept {
	Lock l(tasks.cs);
	auto& tq = tasks.getTasks();

	//remove directories that have already been queued for refreshing
	for (const auto& i : tq) {
		if (i.first != ASYNC) {
			auto t = static_cast<ShareTask*>(i.second.get());
			dirs_.erase(boost::remove_if(dirs_, [t](const string& p) {
				return boost::find(t->dirs, p) != t->dirs.end();
			}), dirs_.end());
		}
	}
}

void ShareManager::reportPendingRefresh(TaskType aTaskType, const RefreshPathList& aDirectories, const string& aDisplayName) const noexcept {
	string msg;
	switch (aTaskType) {
		case(REFRESH_ALL) :
			msg = STRING(REFRESH_QUEUED);
			break;
		case(REFRESH_DIRS) :
			if (!aDisplayName.empty()) {
				msg = STRING_F(VIRTUAL_REFRESH_QUEUED, aDisplayName);
			} else if (aDirectories.size() == 1) {
				msg = STRING_F(DIRECTORY_REFRESH_QUEUED, *aDirectories.begin());
			}
			break;
		case(ADD_DIR) :
			if (aDirectories.size() == 1) {
				msg = STRING_F(ADD_DIRECTORY_QUEUED, *aDirectories.begin());
			} else {
				msg = STRING_F(ADD_DIRECTORIES_QUEUED, aDirectories.size());
			}
					  break;
		case(REFRESH_INCOMING) :
			msg = STRING(INCOMING_REFRESH_QUEUED);
			break;
		default:
			break;
	};

	if (!msg.empty()) {
		LogManager::getInstance()->message(msg, LogMessage::SEV_INFO);
	}
}

ShareManager::RefreshResult ShareManager::addRefreshTask(TaskType aTaskType, const StringList& aDirs, RefreshType aRefreshType, const string& aDisplayName, function<void(float)> aProgressF) noexcept {
	if (aDirs.empty()) {
		return RefreshResult::REFRESH_PATH_NOT_FOUND;
	}

	auto dirs = aDirs;
	validateRefreshTask(dirs);

	if (dirs.empty()) {
		return RefreshResult::REFRESH_ALREADY_QUEUED;
	}

	RefreshPathList paths;
	for (auto& path : dirs) {
		setRefreshState(path, RefreshState::STATE_PENDING, false);
		paths.insert(path);
	}

	tasks.add(aTaskType, unique_ptr<Task>(new ShareTask(paths, aDisplayName, aRefreshType)));

	if(refreshing.test_and_set()) {
		if (aRefreshType != TYPE_STARTUP_DELAYED) {
			//this is always called from the task thread...
			reportPendingRefresh(aTaskType, paths, aDisplayName);
		}
		return RefreshResult::REFRESH_IN_PROGRESS;
	}

	if (aRefreshType == TYPE_STARTUP_BLOCKING && aTaskType == REFRESH_ALL) {
		runTasks(aProgressF);
	} else {
		try {
			start();
			setThreadPriority(aRefreshType == TYPE_MANUAL ? Thread::NORMAL : Thread::IDLE);
		} catch(const ThreadException& e) {
			LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_FAILED) + " " + e.getError(), LogMessage::SEV_WARNING);
			refreshing.clear();
		}
	}

	return RefreshResult::REFRESH_STARTED;
}

void ShareManager::getRootPaths(StringList& paths_) const noexcept {
	RLock l(cs);
	boost::copy(rootPaths | map_keys, back_inserter(paths_));
}

void ShareManager::setDefaultProfile(ProfileToken aNewDefault) noexcept {
	auto oldDefault = SETTING(DEFAULT_SP);

	{
		WLock l(cs);
		// Put the default profile on top
		auto p = find(shareProfiles, aNewDefault);
		rotate(shareProfiles.begin(), p, shareProfiles.end());
	}

	SettingsManager::getInstance()->set(SettingsManager::DEFAULT_SP, aNewDefault);

	fire(ShareManagerListener::DefaultProfileChanged(), oldDefault, aNewDefault);
	fire(ShareManagerListener::ProfileUpdated(), aNewDefault, true);
	fire(ShareManagerListener::ProfileUpdated(), oldDefault, true);
}

void ShareManager::addProfiles(const ShareProfileInfo::List& aProfiles) noexcept {
	for (auto& sp : aProfiles) {
		addProfile(std::make_shared<ShareProfile>(sp->name, sp->token));
	}
}

void ShareManager::removeProfiles(const ShareProfileInfo::List& aProfiles) noexcept{
	for (auto& sp : aProfiles) {
		removeProfile(sp->token);
	}
}

void ShareManager::renameProfiles(const ShareProfileInfo::List& aProfiles) noexcept {
	for (auto& sp : aProfiles) {
		auto p = getShareProfile(sp->token);
		if (p) {
			p->setPlainName(sp->name);
			updateProfile(p);
		}
	}
}

void ShareManager::addProfile(const ShareProfilePtr& aProfile) noexcept {
	{
		WLock l(cs);

		// Hidden profile should always be the last one
		shareProfiles.insert(shareProfiles.end() - 1, aProfile);
	}

	fire(ShareManagerListener::ProfileAdded(), aProfile->getToken());
}

void ShareManager::updateProfile(const ShareProfilePtr& aProfile) noexcept {
	fire(ShareManagerListener::ProfileUpdated(), aProfile->getToken(), true);
}

bool ShareManager::removeProfile(ProfileToken aToken) noexcept {
	StringList removedPaths;

	{
		WLock l(cs);
		// Remove all directories
		for (auto& root : rootPaths) {
			auto profiles = root.second->getProfileDir()->getRootProfiles();
			profiles.erase(aToken);
			root.second->getProfileDir()->setRootProfiles(profiles);

			if (profiles.empty()) {
				removedPaths.push_back(root.first);
			}
		}

		// Remove profile
		auto i = find(shareProfiles.begin(), shareProfiles.end(), aToken);
		if (i == shareProfiles.end()) {
			return false;
		}

		shareProfiles.erase(remove(shareProfiles.begin(), shareProfiles.end(), aToken), shareProfiles.end());
	}

	removeRootDirectories(removedPaths);

	fire(ShareManagerListener::ProfileRemoved(), aToken);
	return true;
}

bool ShareManager::addRootDirectory(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept {
	dcassert(!aDirectoryInfo->profiles.empty());
	Directory::Ptr newRoot = nullptr;
	const auto& path = aDirectoryInfo->path;

	{
		WLock l(cs);
		auto i = rootPaths.find(path);
		if (i != rootPaths.end()) {
			return false;
		} else {
			dcassert(find_if(rootPaths | map_keys, IsParentOrExact(path)).base() == rootPaths.end());

			// It's a new parent, will be handled in the task thread
			auto profileDir = ProfileDirectory::create(path, aDirectoryInfo->virtualName, aDirectoryInfo->profiles, aDirectoryInfo->incoming, profileDirs);

			newRoot = Directory::createRoot(Util::getLastDir(path), File::getLastModified(path), profileDir, rootPaths, dirNameMap, *bloom.get());
		}
	}

	fire(ShareManagerListener::RootCreated(), path);
	addRefreshTask(ADD_DIR, { path }, TYPE_MANUAL);

	return true;
}

void ShareManager::addRootDirectories(const ShareDirectoryInfoList& aNewDirs) noexcept {
	for(const auto& d: aNewDirs) {
		addRootDirectory(d);
	}
}

bool ShareManager::removeRootDirectory(const string& aPath) noexcept {
	ProfileTokenSet dirtyProfiles;

	{
		WLock l(cs);
		auto k = rootPaths.find(aPath);
		if (k == rootPaths.end()) {
			return false;
		}

		auto sd = k->second;

		dirtyProfiles = k->second->getProfileDir()->getRootProfiles();

		rootPaths.erase(k);

		// Remove the root
		cleanIndices(*sd);
		File::deleteFile(sd->getProfileDir()->getCacheXmlPath());
	}

	removeMonitoring({ aPath });
	HashManager::getInstance()->stopHashing(aPath);

	LogManager::getInstance()->message(STRING_F(SHARED_DIR_REMOVED, aPath), LogMessage::SEV_INFO);

	fire(ShareManagerListener::RootRemoved(), aPath);
	setProfilesDirty(dirtyProfiles, true);
	return true;
}

void ShareManager::removeRootDirectories(const StringList& aRemoveDirs) noexcept{

	for(const auto& path: aRemoveDirs) {
		removeRootDirectory(path);
	}

	//if (stopHashing.size() == 1)
	//	LogManager::getInstance()->message(STRING_F(SHARED_DIR_REMOVED, stopHashing.front()), LogMessage::SEV_INFO);
	//else if (!stopHashing.empty())
	//	LogManager::getInstance()->message(STRING_F(X_SHARED_DIRS_REMOVED, stopHashing.size()), LogMessage::SEV_INFO);
}

bool ShareManager::updateRootDirectory(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept {
	dcassert(!aDirectoryInfo->profiles.empty());
	ProfileDirectory::Ptr profileDir;
	ProfileTokenSet dirtyProfiles = aDirectoryInfo->profiles;

	{
		WLock l(cs);
		auto vName = validateVirtualName(aDirectoryInfo->virtualName);

		auto p = rootPaths.find(aDirectoryInfo->path);
		if (p != rootPaths.end()) {
			profileDir = p->second->getProfileDir();

			// Make sure that all removed profiles are set dirty as well
			dirtyProfiles.insert(profileDir->getRootProfiles().begin(), profileDir->getRootProfiles().end());

			removeDirName(*p->second, dirNameMap);
			profileDir->setName(vName);
			addDirName(p->second, dirNameMap, *bloom.get());

			profileDir->setIncoming(aDirectoryInfo->incoming);
			profileDir->setRootProfiles(aDirectoryInfo->profiles);
		} else {
			return false;
		}
	}

	if (profileDir->useMonitoring()) {
		addMonitoring({ aDirectoryInfo->path });
	} else {
		removeMonitoring({ aDirectoryInfo->path });
	}

	setProfilesDirty(dirtyProfiles, true);

	fire(ShareManagerListener::RootUpdated(), aDirectoryInfo->path);
	return true;
}

void ShareManager::updateRootDirectories(const ShareDirectoryInfoList& changedDirs) noexcept {
	for(const auto& dirInfo: changedDirs) {
		updateRootDirectory(dirInfo);
	}
}

void ShareManager::rebuildMonitoring() noexcept {
	StringList monAdd;
	StringList monRem;

	{
		RLock l(cs);
		for(auto& dp: rootPaths) {
			if (dp.second->getProfileDir()->useMonitoring()) {
				monAdd.push_back(dp.first);
			} else {
				monRem.push_back(dp.first);
			}
		}
	}

	addMonitoring(monAdd);
	removeMonitoring(monRem);
}

void ShareManager::reportTaskStatus(uint8_t aTask, const RefreshPathList& directories, bool finished, int64_t aHashSize, const string& displayName, RefreshType aRefreshType) const noexcept {
	string msg;
	switch (aTask) {
		case(REFRESH_ALL):
			msg = finished ? STRING(FILE_LIST_REFRESH_FINISHED) : STRING(FILE_LIST_REFRESH_INITIATED);
			break;
		case(REFRESH_DIRS):
			if (!displayName.empty()) {
				msg = finished ? STRING_F(VIRTUAL_DIRECTORY_REFRESHED, displayName) : STRING_F(FILE_LIST_REFRESH_INITIATED_VPATH, displayName);
			} else if (directories.size() == 1) {
				msg = finished ? STRING_F(DIRECTORY_REFRESHED, *directories.begin()) : STRING_F(FILE_LIST_REFRESH_INITIATED_RPATH, *directories.begin());
			} else {
				msg = finished ? STRING_F(X_DIRECTORIES_REFRESHED, directories.size()) : STRING_F(FILE_LIST_REFRESH_INITIATED_X_PATHS, directories.size());
			}
			break;
		case(ADD_DIR):
			if (directories.size() == 1) {
				msg = finished ? STRING_F(DIRECTORY_ADDED, *directories.begin()) : STRING_F(ADDING_SHARED_DIR, *directories.begin());
			} else {
				msg = finished ? STRING_F(ADDING_X_SHARED_DIRS, directories.size()) : STRING_F(DIRECTORIES_ADDED, directories.size());
			}
			break;
		case(REFRESH_INCOMING):
			msg = finished ? STRING(INCOMING_REFRESHED) : STRING(FILE_LIST_REFRESH_INITIATED_INCOMING);
			break;
		case(ADD_BUNDLE):
			if (finished)
				msg = STRING_F(BUNDLE_X_SHARED, (SETTING(FINISHED_NO_HASH) ? displayName : Util::getLastDir(displayName))); //show the path with no hash so that it can be opened from the system log
			break;
	};

	if (!msg.empty()) {
		if (aHashSize > 0) {
			msg += " " + STRING_F(FILES_ADDED_FOR_HASH, Util::formatBytes(aHashSize));
		} else if (aRefreshType == TYPE_SCHEDULED && !SETTING(LOG_SCHEDULED_REFRESHES)) {
			return;
		}
		LogManager::getInstance()->message(msg, LogMessage::SEV_INFO);
	}
}

int ShareManager::run() {
	runTasks();
	return 0;
}

ShareManager::RefreshInfo::~RefreshInfo() {

}

ShareManager::RefreshInfo::RefreshInfo(const string& aPath, const Directory::Ptr& aOldShareDirectory, uint64_t aLastWrite, ShareBloom& bloom_) : 
	path(aPath), oldShareDirectory(aOldShareDirectory) {

	// Use a different directory for building the tree
	if (aOldShareDirectory && aOldShareDirectory->getProfileDir()) {
		newShareDirectory = Directory::createRoot(Util::getLastDir(aPath), aLastWrite, aOldShareDirectory->getProfileDir(), rootPathsNew, dirNameMapNew, bloom_);
	} else {
		// We'll set the parent later
		newShareDirectory = Directory::createNormal(Util::getLastDir(aPath), nullptr, aLastWrite, dirNameMapNew, bloom_);
	}
}

void ShareManager::runTasks(function<void (float)> progressF /*nullptr*/) noexcept {
	unique_ptr<HashManager::HashPauser> pauser = nullptr;

	for (;;) {
		TaskQueue::TaskPair t;
		if (!tasks.getFront(t))
			break;
		ScopedFunctor([this] { tasks.pop_front(); });

		if (t.first == ASYNC) {
			refreshRunning = false;
			auto task = static_cast<AsyncTask*>(t.second);
			task->f();
			continue;
		}

		auto task = static_cast<ShareTask*>(t.second);
		if (task->type == TYPE_STARTUP_DELAYED)
			Thread::sleep(5000); // let the client start first

		refreshRunning = true;
		if (!pauser) {
			pauser.reset(new HashManager::HashPauser());
		}

		//get unfinished directories and erase exact matches
		bundleDirs.clear();

		auto dirs = task->dirs;
		QueueManager::getInstance()->checkRefreshPaths(bundleDirs, dirs);

		// Handle the removed paths
		for (const auto& d : task->dirs) {
			if (dirs.find(d) == dirs.end()) {
				setRefreshState(d, RefreshState::STATE_NORMAL, true);
			}
		}

		if (dirs.empty()) {
			continue;
		}

		StringList monitoring;
		RefreshInfoSet refreshDirs;

		ShareBloom* refreshBloom = t.first == REFRESH_ALL ? new ShareBloom(1 << 20) : bloom.get();
		Directory::Map newRootPaths;

		// Get refresh infos for each path
		{
			RLock l (cs);
			for(auto& refreshPath: dirs) {
				Directory::Ptr directory = nullptr;

				auto directoryIter = rootPaths.find(refreshPath);
				if (directoryIter != rootPaths.end()) {
					directory = directoryIter->second;
					
					// A monitored dir?
					if (t.first == ADD_DIR && (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_ALL || (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_INCOMING && directory->getProfileDir()->getIncoming())))
						monitoring.push_back(refreshPath);
				} else {
					directory = findDirectory(refreshPath);
				}

				refreshDirs.insert(std::make_shared<RefreshInfo>(refreshPath, directory, File::getLastModified(refreshPath), *refreshBloom));
			}
		}

		reportTaskStatus(t.first, dirs, false, 0, task->displayName, task->type);
		if (t.first == REFRESH_INCOMING) {
			lastIncomingUpdate = GET_TICK();
		} else if (t.first == REFRESH_ALL) {
			lastFullUpdate = GET_TICK();
			lastIncomingUpdate = GET_TICK();
		}

		// Refresh
		atomic<long> progressCounter(0);

		int64_t totalHash = 0;
		ProfileTokenSet dirtyProfiles;

		auto doRefresh = [&](const RefreshInfoPtr& i) {
			auto& ri = *i.get();
			const auto& path = ri.path;

			setRefreshState(ri.path, RefreshState::STATE_RUNNING, false);

			// Build the tree
			bool succeed = false;
			try {
				buildTree(path, Text::toLower(ri.path), ri.newShareDirectory, ri.dirNameMapNew, ri.hashSize, ri.addedSize, ri.tthIndexNew, *refreshBloom);
				succeed = true;
			} catch (const std::bad_alloc&) {
				LogManager::getInstance()->message(STRING_F(DIR_REFRESH_FAILED, path % STRING(OUT_OF_MEMORY)), LogMessage::SEV_ERROR);
			} catch (...) {
				LogManager::getInstance()->message(STRING_F(DIR_REFRESH_FAILED, path % STRING(UNKNOWN_ERROR)), LogMessage::SEV_ERROR);
			}

			// Don't save cache with an incomplete tree
			if (aShutdown)
				return;

			// Apply the changes
			{
				WLock l(cs);
				if (handleRefreshedDirectory(ri)) {
					ri.mergeRefreshChanges(dirNameMap, rootPaths, tthIndex, totalHash, sharedSize, &dirtyProfiles);
				}
			}

			// Finish up
			setRefreshState(ri.path, RefreshState::STATE_NORMAL, succeed);
			if (progressF) {
				progressF(static_cast<float>(progressCounter++) / static_cast<float>(refreshDirs.size()));
			}
		};

		try {
			if (SETTING(REFRESH_THREADING) == SettingsManager::MULTITHREAD_ALWAYS || (SETTING(REFRESH_THREADING) == SettingsManager::MULTITHREAD_MANUAL && (task->type == TYPE_MANUAL || task->type == TYPE_STARTUP_BLOCKING))) {
				TaskScheduler s;
				parallel_for_each(refreshDirs.begin(), refreshDirs.end(), doRefresh);
			} else {
				for_each(refreshDirs, doRefresh);
			}
		} catch (std::exception& e) {
			LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_FAILED) + string(e.what()), LogMessage::SEV_INFO);
			continue;
		}

		if (aShutdown)
			break;

		if(t.first == REFRESH_ALL) {
			// Reset the bloom so that removed files are nulled (which won't happen with partial refreshes)

			WLock l(cs);
			bloom.reset(refreshBloom);
		}

		setProfilesDirty(dirtyProfiles, task->type == TYPE_MANUAL || t.first == REFRESH_ALL || t.first == ADD_BUNDLE);
		reportTaskStatus(t.first, dirs, true, totalHash, task->displayName, task->type);

		addMonitoring(monitoring);

		fire(ShareManagerListener::DirectoriesRefreshed(), t.first, dirs);
	}

	{
		WLock l (dirNames);
		bundleDirs.clear();
	}
	refreshRunning = false;
	refreshing.clear();
}

void ShareManager::RefreshInfo::mergeRefreshChanges(Directory::MultiMap& aDirNameMap, Directory::Map& aRootPaths, HashFileMap& aTTHIndex, int64_t& totalHash, int64_t& totalAdded, ProfileTokenSet* dirtyProfiles) noexcept {
	aDirNameMap.insert(dirNameMapNew.begin(), dirNameMapNew.end());
	aTTHIndex.insert(tthIndexNew.begin(), tthIndexNew.end());

	for (const auto& rp : rootPathsNew) {
		aRootPaths[rp.first] = rp.second;
	}

	totalHash += hashSize;
	totalAdded += addedSize;

	if (dirtyProfiles) {
		newShareDirectory->copyRootProfiles(*dirtyProfiles, true);
	}

	// Save some memory
	dirNameMapNew.clear();
	tthIndexNew.clear();
	oldShareDirectory = nullptr;
	newShareDirectory = nullptr;
}

void ShareManager::setRefreshState(const string& aRefreshPath, RefreshState aState, bool aUpdateRefreshTime) noexcept {
	ProfileDirectory::Ptr pd;

	{
		RLock l(cs);
		auto p = find_if(profileDirs | map_values, [&](const ProfileDirectory::Ptr& aDir) {
			return AirUtil::isParentOrExact(aDir->getPath(), aRefreshPath);
		});

		if (p.base() == profileDirs.end()) {
			return;
		}

		pd = *p;
	}

	// We want to fire a root update also when refreshing subdirectories (as the size/content may have changed)
	// but don't change the refresh state
	if (aRefreshPath == pd->getPath()) {
		pd->setRefreshState(aState);
		if (aUpdateRefreshTime) {
			pd->setLastRefreshTime(GET_TIME());
		}
	}

	fire(ShareManagerListener::RootUpdated(), pd->getPath());
}

bool ShareManager::handleRefreshedDirectory(const RefreshInfo& ri) {
	// Recursively remove the content of this dir from TTHIndex and directory name map
	if (ri.oldShareDirectory) {
		cleanIndices(*ri.oldShareDirectory);
	}

	// Remove this path from root paths
	auto i = rootPaths.find(ri.path);
	if (i != rootPaths.end()) {
		i = rootPaths.erase(i);
	}

	// Set the parent for refreshed subdirectories
	if (!ri.oldShareDirectory || !ri.oldShareDirectory->isRoot()) {

		// All content was removed?
		if (SETTING(SKIP_EMPTY_DIRS_SHARE) && ri.newShareDirectory->directories.empty() && ri.newShareDirectory->files.empty()) {
			if (ri.oldShareDirectory) {
				cleanIndices(*ri.oldShareDirectory);
				ri.oldShareDirectory->directories.erase_key(ri.newShareDirectory->realName.getLower());
			}

			return false;
		}

		Directory::Ptr parent = nullptr;
		if (!ri.oldShareDirectory) {
			// Create the parent
			parent = getDirectory(Util::getParentDir(ri.path), true);
			if (!parent) {
				return false;
			}
		} else {
			parent = ri.oldShareDirectory->getParent();
		}

		// Set the parent
		ri.newShareDirectory->setParent(parent.get());
		parent->directories.erase_key(ri.newShareDirectory->realName.getLower());
		parent->directories.insert_sorted(ri.newShareDirectory);
		parent->updateModifyDate();
	}

	return true;
}

void ShareManager::on(TimerManagerListener::Second, uint64_t /*tick*/) noexcept {
	while (monitor.dispatch()) {
		//...
	}
}

void ShareManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	if(lastSave == 0 || lastSave + 15*60*1000 <= aTick) {
		saveXmlList();
	}

	if(SETTING(AUTO_REFRESH_TIME) > 0 && lastFullUpdate + SETTING(AUTO_REFRESH_TIME) * 60 * 1000 <= aTick) {
		lastIncomingUpdate = aTick;
		lastFullUpdate = aTick;
		refresh(false, TYPE_SCHEDULED);
	} else if(SETTING(INCOMING_REFRESH_TIME) > 0 && lastIncomingUpdate + SETTING(INCOMING_REFRESH_TIME) * 60 * 1000 <= aTick) {
		lastIncomingUpdate = aTick;
		refresh(true, TYPE_SCHEDULED);
	}

	handleChangedFiles(aTick, false);

	restoreFailedMonitoredPaths();
}

void ShareManager::restoreFailedMonitoredPaths() {
	auto restored = monitor.restoreFailedPaths();
	for (const auto& dir : restored) {
		LogManager::getInstance()->message(STRING_F(MONITORING_RESTORED_X, dir), LogMessage::SEV_INFO);
	}
}

ShareDirectoryInfoPtr ShareManager::getRootInfo(const Directory::Ptr& aDir) const noexcept {
	auto& pd = aDir->getProfileDir();

	size_t fileCount = 0, folderCount = 0;
	int64_t size = 0;
	aDir->getContentInfo(size, fileCount, folderCount);

	auto info = std::make_shared<ShareDirectoryInfo>(aDir->getRealPath());
	info->profiles = pd->getRootProfiles();
	info->incoming = pd->getIncoming();
	info->size = size;
	info->fileCount = fileCount;
	info->folderCount = folderCount;
	info->virtualName = pd->getName();
	info->refreshState = static_cast<uint8_t>(pd->getRefreshState());
	info->lastRefreshTime = pd->getLastRefreshTime();
	return info;
}

ShareDirectoryInfoPtr ShareManager::getRootInfo(const string& aPath) const noexcept {
	RLock l(cs);
	auto p = rootPaths.find(aPath);
	if (p != rootPaths.end()) {
		return getRootInfo(p->second);
	}

	return nullptr;
}

ShareDirectoryInfoList ShareManager::getRootInfos() const noexcept {
	ShareDirectoryInfoList ret;

	RLock l (cs);
	for(const auto& d: rootPaths | map_values) {
		ret.push_back(getRootInfo(d));
	}

	return ret;
}
		
void ShareManager::getBloom(HashBloom& bloom_) const noexcept {
	RLock l(cs);
	for(const auto tth: tthIndex | map_keys)
		bloom_.add(*tth);

	for(const auto& tth: tempShares | map_keys)
		bloom_.add(tth);
}

string ShareManager::generateOwnList(ProfileToken aProfile) throw(ShareException) {
	FileList* fl = generateXmlList(aProfile, true);
	return fl->getFileName();
}


//forwards the calls to createFileList for creating the filelist that was reguested.
FileList* ShareManager::generateXmlList(ProfileToken aProfile, bool forced /*false*/) throw(ShareException) {
	FileList* fl = nullptr;

	{
		RLock l(cs);
		const auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
		if(i == shareProfiles.end()) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		fl = (*i)->getProfileList();
	}


	{
		Lock lFl(fl->cs);
		if (fl->allowGenerateNew(forced)) {
			auto tmpName = fl->getFileName().substr(0, fl->getFileName().length() - 4);
			try {
				{
					File f(tmpName, File::RW, File::TRUNCATE | File::CREATE, File::BUFFER_SEQUENTIAL, false);

					toFilelist(f, "/", aProfile, true);

					fl->setXmlListLen(f.getSize());

					File bz(fl->getFileName(), File::WRITE, File::TRUNCATE | File::CREATE, File::BUFFER_SEQUENTIAL, false);
					// We don't care about the leaves...
					CalcOutputStream<TTFilter<1024 * 1024 * 1024>, false> bzTree(&bz);
					FilteredOutputStream<BZFilter, false> bzipper(&bzTree);
					CalcOutputStream<TTFilter<1024 * 1024 * 1024>, false> newXmlFile(&bzipper);

					newXmlFile.write(f.read());
					newXmlFile.flush();

					newXmlFile.getFilter().getTree().finalize();
					bzTree.getFilter().getTree().finalize();

					fl->setXmlRoot(newXmlFile.getFilter().getTree().getRoot());
					fl->setBzXmlRoot(bzTree.getFilter().getTree().getRoot());
				}

				fl->saveList();
				fl->generationFinished(false);
			} catch (const Exception& e) {
				// No new file lists...
				LogManager::getInstance()->message(STRING_F(SAVE_FAILED_X, fl->getFileName() % e.getError()), LogMessage::SEV_ERROR);
				fl->generationFinished(true);

				// do we have anything to send?
				if (fl->getCurrentNumber() == 0) {
					throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
				}
			}

			File::deleteFile(tmpName);
		}
	}
	return fl;
}

MemoryInputStream* ShareManager::generatePartialList(const string& aVirtualPath, bool aRecursive, const OptionalProfileToken& aProfile) const noexcept {
	if(aVirtualPath.front() != '/' || aVirtualPath.back() != '/')
		return 0;

	string xml = Util::emptyString;

	{
		StringOutputStream sos(xml);
		toFilelist(sos, aVirtualPath, aProfile, aRecursive);
	}

	if (xml.empty()) {
		dcdebug("Partial NULL");
		return nullptr;
	} else {
		dcdebug("Partial list generated (%s)\n", aVirtualPath.c_str());
		return new MemoryInputStream(xml);
	}
}

void ShareManager::toFilelist(OutputStream& os_, const string& aVirtualPath, const OptionalProfileToken& aProfile, bool aRecursive) const {
	FileListDir listRoot(Util::emptyString, 0, 0);
	Directory::List childDirectories;

	RLock l(cs);
	dcdebug("Generating filelist for %s \n", aVirtualPath.c_str());

	// Get the directories
	if (aVirtualPath == "/") {
		getRoots(aProfile, childDirectories);
	} else {
		try {
			// We need to save the root directories as well for listing the files directly inside them
			findVirtuals<OptionalProfileToken>(aVirtualPath, aProfile, listRoot.shareDirs);
		} catch (...) {
			return;
		}

		for (const auto& d : listRoot.shareDirs) {
			copy(d->directories, back_inserter(childDirectories));
			listRoot.date = max(listRoot.date, d->getLastWrite());
		}
	}

	// Prepare the data
	for (const auto& d : childDirectories) {
		d->toFileList(listRoot, aRecursive);
		listRoot.date = max(listRoot.date, d->getLastWrite()); // In case the date is not set yet
	}

	// Write the XML
	string tmp, indent = "\t";

	os_.write(SimpleXML::utf8Header);
	os_.write("<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() +
		"\" Base=\"" + SimpleXML::escape(aVirtualPath, tmp, false) +
		"\" BaseDate=\"" + Util::toString(listRoot.date) +
		"\" Generator=\"" + shortVersionString + "\">\r\n");

	for (const auto ld : listRoot.listDirs | map_values) {
		ld->toXml(os_, indent, tmp, aRecursive);
	}
	listRoot.filesToXml(os_, indent, tmp, !aRecursive);

	os_.write("</FileListing>");
}

void ShareManager::Directory::toFileList(FileListDir& aListDir, bool aRecursive) {
	FileListDir* newListDir = nullptr;
	auto pos = aListDir.listDirs.find(const_cast<string*>(&getVirtualNameLower()));
	if (pos != aListDir.listDirs.end()) {
		newListDir = pos->second;
		if (!aRecursive) {
			newListDir->size += getSize();
		}

		newListDir->date = max(newListDir->date, lastWrite);
	} else {
		newListDir = new FileListDir(getVirtualName(), aRecursive ? 0 : getSize(), lastWrite);
		aListDir.listDirs.emplace(const_cast<string*>(&newListDir->name), newListDir);
	}

	newListDir->shareDirs.push_back(this);

	if (aRecursive) {
		for(auto& d: directories) {
			d->toFileList(*newListDir, aRecursive);
		}
	}
}

ShareManager::FileListDir::FileListDir(const string& aName, int64_t aSize, uint64_t aDate) : name(aName), size(aSize), date(aDate) { }

#define LITERAL(n) n, sizeof(n)-1
void ShareManager::FileListDir::toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool aRecursive) const {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(name, tmp2, true));
	if (!aRecursive) {
		xmlFile.write(LITERAL("\" Size=\""));
		xmlFile.write(Util::toString(size));
	}
	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(Util::toString(date));

	if(aRecursive) {
		xmlFile.write(LITERAL("\">\r\n"));

		indent += '\t';
		for(const auto& d: listDirs | map_values) {
			d->toXml(xmlFile, indent, tmp2, aRecursive);
		}

		filesToXml(xmlFile, indent, tmp2, !aRecursive);

		indent.erase(indent.length()-1);
		xmlFile.write(indent);
		xmlFile.write(LITERAL("</Directory>\r\n"));
	} else {
		bool hasDirs = any_of(shareDirs.begin(), shareDirs.end(), [](const Directory::Ptr& d) { return !d->directories.empty(); });
		if(!hasDirs && all_of(shareDirs.begin(), shareDirs.end(), [](const Directory::Ptr& d) { return d->files.empty(); })) {
			xmlFile.write(LITERAL("\" />\r\n"));
		} else {
			xmlFile.write(LITERAL("\" Incomplete=\"1\""));
			if (hasDirs) {
				xmlFile.write(LITERAL(" Children=\"1\""));
			}
			xmlFile.write(LITERAL("/>\r\n"));
		}
	}
}

void ShareManager::FileListDir::filesToXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const {
	bool filesAdded = false;
	int dupeFiles = 0;
	for(auto di = shareDirs.begin(); di != shareDirs.end(); ++di) {
		if (filesAdded) {
			for(const auto& fi: (*di)->files) {
				//go through the dirs that we have added already
				if (none_of(shareDirs.begin(), di, [&fi](const Directory::Ptr& d) { return d->files.find(fi->name.getLower()) != d->files.end(); })) {
					fi->toXml(xmlFile, indent, tmp2, addDate);
				} else {
					dupeFiles++;
				}
			}
		} else if (!(*di)->files.empty()) {
			filesAdded = true;
			for(const auto& f: (*di)->files)
				f->toXml(xmlFile, indent, tmp2, addDate);
		}
	}

	if (dupeFiles > 0 && SETTING(FL_REPORT_FILE_DUPES) && shareDirs.size() > 1) {
		StringList paths;
		for (const auto& d : shareDirs)
			paths.push_back(d->getRealPath());

		LogManager::getInstance()->message(STRING_F(DUPLICATE_FILES_DETECTED, dupeFiles % Util::toString(", ", paths)), LogMessage::SEV_WARNING);
	}
}

void ShareManager::Directory::filesToXmlList(OutputStream& xmlFile, string& indent, string& tmp2) const {
	for(const auto& f: files) {
		xmlFile.write(indent);
		xmlFile.write(LITERAL("<File Name=\""));
		xmlFile.write(SimpleXML::escape(f->name.lowerCaseOnly() ? f->name.getLower() : f->name.getNormal(), tmp2, true));
		xmlFile.write(LITERAL("\"/>\r\n"));
	}
}

ShareManager::Directory::File::File(DualString&& aName, const Directory::Ptr& aParent, const HashedFile& aFileInfo) : 
	size(aFileInfo.getSize()), parent(aParent.get()), tth(aFileInfo.getRoot()), lastWrite(aFileInfo.getTimeStamp()), name(move(aName)) {
	
}

ShareManager::Directory::File::~File() {

}

void ShareManager::Directory::File::toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<File Name=\""));
	xmlFile.write(SimpleXML::escape(name.lowerCaseOnly() ? name.getLower() : name.getNormal(), tmp2, true));
	xmlFile.write(LITERAL("\" Size=\""));
	xmlFile.write(Util::toString(size));
	xmlFile.write(LITERAL("\" TTH=\""));
	tmp2.clear();
	xmlFile.write(getTTH().toBase32(tmp2));

	if (addDate) {
		xmlFile.write(LITERAL("\" Date=\""));
		xmlFile.write(Util::toString(lastWrite));
	}
	xmlFile.write(LITERAL("\"/>\r\n"));
}

ShareManager::FileListDir::~FileListDir() {
	for_each(listDirs | map_values, DeleteFunction());
}

string ShareManager::ProfileDirectory::getCacheXmlPath() const noexcept {
	return Util::getPath(Util::PATH_SHARECACHE) + "ShareCache_" + Util::validateFileName(path) + ".xml";
}

void ShareManager::ProfileDirectory::setName(const string& aName) noexcept {
	virtualName.reset(new DualString(aName));
}

bool ShareManager::ProfileDirectory::useMonitoring() const noexcept {
	return SETTING(MONITORING_MODE) == SettingsManager::MONITORING_ALL || (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_INCOMING && incoming);
}

#define LITERAL(n) n, sizeof(n)-1

void ShareManager::saveXmlList(function<void(float)> progressF /*nullptr*/) noexcept {

	if(xml_saving)
		return;

	xml_saving = true;

	if (progressF)
		progressF(0);

	int cur = 0;
	Directory::List dirtyDirs;

	{
		RLock l(cs);
		boost::algorithm::copy_if(rootPaths | map_values, back_inserter(dirtyDirs), [](const Directory::Ptr& aDir) { return aDir->getProfileDir()->getCacheDirty() && !aDir->getParent(); });

		try {
			parallel_for_each(dirtyDirs.begin(), dirtyDirs.end(), [&](const Directory::Ptr& d) {
				string path = d->getProfileDir()->getCacheXmlPath();
				try {
					string indent, tmp;

					//create a backup first in case we get interrupted on creation.
					File ff(path + ".tmp", File::WRITE, File::TRUNCATE | File::CREATE);
					BufferedOutputStream<false> xmlFile(&ff);

					xmlFile.write(SimpleXML::utf8Header);
					xmlFile.write(LITERAL("<Share Version=\"" SHARE_CACHE_VERSION));
					xmlFile.write(LITERAL("\" Path=\""));
					xmlFile.write(SimpleXML::escape(d->getProfileDir()->getPath(), tmp, true));

					xmlFile.write(LITERAL("\" Date=\""));
					xmlFile.write(SimpleXML::escape(Util::toString(d->getLastWrite()), tmp, true));
					xmlFile.write(LITERAL("\">\r\n"));
					indent += '\t';

					for (const auto& child : d->directories) {
						child->toXmlList(xmlFile, indent, tmp);
					}
					d->filesToXmlList(xmlFile, indent, tmp);

					xmlFile.write(LITERAL("</Share>"));
					xmlFile.flush();
					ff.close();

					File::deleteFile(path);
					File::renameFile(path + ".tmp", path);
				} catch (Exception& e) {
					LogManager::getInstance()->message(STRING_F(SAVE_FAILED_X, path % e.getError()), LogMessage::SEV_WARNING);
				}

				d->getProfileDir()->setCacheDirty(false);
				if (progressF) {
					cur++;
					progressF(static_cast<float>(cur) / static_cast<float>(dirtyDirs.size()));
				}
			});
		} catch (std::exception& e) {
			LogManager::getInstance()->message("Saving the share cache failed: " + string(e.what()), LogMessage::SEV_INFO);
		}
	}

	xml_saving = false;
	lastSave = GET_TICK();
}

void ShareManager::Directory::toXmlList(OutputStream& xmlFile, string& indent, string& tmp) {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(realName.lowerCaseOnly() ? realName.getLower() : realName.getNormal(), tmp, true));

	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(SimpleXML::escape(Util::toString(lastWrite), tmp, true));
	xmlFile.write(LITERAL("\">\r\n"));

	indent += '\t';
	filesToXmlList(xmlFile, indent, tmp);

	for(const auto& d: directories) {
		d->toXmlList(xmlFile, indent, tmp);
	}

	indent.erase(indent.length()-1);
	xmlFile.write(indent);
	xmlFile.write(LITERAL("</Directory>\r\n"));
}

MemoryInputStream* ShareManager::generateTTHList(const string& dir, bool recurse, ProfileToken aProfile) const noexcept {
	
	if(aProfile == SP_HIDDEN)
		return nullptr;
	
	string tths;
	string tmp;
	StringOutputStream sos(tths);
	Directory::List result;

	try{
		RLock l(cs);
		findVirtuals<ProfileToken>(dir, aProfile, result); 
		for(const auto& it: result) {
			//dcdebug("result name %s \n", (*it)->getProfileDir()->getName(aProfile));
			it->toTTHList(sos, tmp, recurse);
		}
	} catch(...) {
		return nullptr;
	}

	if (tths.size() == 0) {
		dcdebug("Partial NULL");
		return nullptr;
	} else {
		return new MemoryInputStream(tths);
	}
}

void ShareManager::Directory::toTTHList(OutputStream& tthList, string& tmp2, bool recursive) const {
	if (recursive) {
		for(const auto& d: directories) {
			d->toTTHList(tthList, tmp2, recursive);
		}
	}

	for(const auto& f: files) {
		tmp2.clear();
		tthList.write(f->getTTH().toBase32(tmp2));
		tthList.write(LITERAL(" "));
	}
}

bool ShareManager::addDirResult(const Directory* aDir, SearchResultList& aResults, const OptionalProfileToken& aProfile, SearchQuery& srch) const noexcept {
	const string path = srch.addParents ? Util::getNmdcParentDir(aDir->getFullName()) : aDir->getFullName();

	// Have we added it already?
	auto p = find_if(aResults, [&path](const SearchResultPtr& sr) { return sr->getPath() == path; });
	if (p != aResults.end())
		return false;

	// Get all directories with this path
	Directory::List result;

	try {
		findVirtuals<OptionalProfileToken>(Util::toAdcFile(path), aProfile, result);
	} catch(...) {
		dcassert(path.empty());
	}

	// Count date and content information
	uint64_t date = 0;
	int64_t size = 0;
	size_t files = 0, folders = 0;
	for(const auto& d: result) {
		d->getContentInfo(size, files, folders);
		date = max(date, d->getLastWrite());
	}

	if (srch.matchesDate(date)) {
		SearchResultPtr sr(new SearchResult(SearchResult::TYPE_DIRECTORY, size, path, TTHValue(), date, files, folders));
		aResults.push_back(sr);
		return true;
	}

	return false;
}

void ShareManager::Directory::File::addSR(SearchResultList& aResults, bool addParent) const noexcept {
	if (addParent) {
		//getInstance()->addDirResult(getFullName(aProfile), aResults, aProfile, true);
		SearchResultPtr sr(new SearchResult(parent->getFullName()));
		aResults.push_back(sr);
	} else {
		SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, 
			size, getFullName(), getTTH(), getLastWrite(), 1));
		aResults.push_back(sr);
	}
}

void ShareManager::nmdcSearch(SearchResultList& l, const string& nmdcString, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults, bool aHideShare) noexcept{
	auto query = SearchQuery(nmdcString, static_cast<Search::SizeModes>(aSearchType), aSize, static_cast<Search::TypeModes>(aFileType), maxResults);
	adcSearch(l, query, aHideShare ? SP_HIDDEN : SETTING(DEFAULT_SP), CID(), "/", false);
}

/**
* Alright, the main point here is that when searching, a search string is most often found in
* the filename, not directory name, so we want to make that case faster. Also, we want to
* avoid changing StringLists unless we absolutely have to --> this should only be done if a string
* has been matched in the directory name. This new stringlist should also be used in all descendants,
* but not the parents...
*/

void ShareManager::Directory::search(SearchResultInfo::Set& results_, SearchQuery& aStrings, int aLevel) const noexcept{
	const auto& dirName = getVirtualNameLower();
	if (aStrings.isExcludedLower(dirName)) {
		return;
	}

	auto old = aStrings.recursion;

	unique_ptr<SearchQuery::Recursion> rec = nullptr;

	// Find any matches in the directory name
	// Subdirectories of fully matched items won't match anything
	if (aStrings.matchesAnyDirectoryLower(dirName)) {
		bool positionsComplete = aStrings.positionsComplete();
		if (aStrings.itemType != SearchQuery::TYPE_FILE && positionsComplete && aStrings.gt == 0 && aStrings.matchesDate(lastWrite)) {
			// Full match
			results_.insert(Directory::SearchResultInfo(this, aStrings, aLevel));
			//if (aStrings.matchType == SearchQuery::MATCH_FULL_PATH) {
			//	return;
			//}
		} 
		
		if (aStrings.matchType == Search::MATCH_PATH_PARTIAL) {
			bool hasValidResult = positionsComplete;
			if (!hasValidResult) {
				// Partial match; ignore if all matches are less than 3 chars in length
				const auto& positions = aStrings.getLastPositions();
				for (size_t j = 0; j < positions.size(); ++j) {
					if (positions[j] != string::npos && aStrings.include.getPatterns()[j].size() > 2) {
						hasValidResult = true;
						break;
					}
				}
			}

			if (hasValidResult) {
				rec.reset(new SearchQuery::Recursion(aStrings, dirName));
				aStrings.recursion = rec.get();
			}
		}
	}

	// Moving up
	aLevel++;
	if (aStrings.recursion) {
		aStrings.recursion->increase(dirName.length());
	}

	// Match files
	if(aStrings.itemType != SearchQuery::TYPE_DIRECTORY) {
		for(const auto& f: files) {
			if (!aStrings.matchesFileLower(f->name.getLower(), f->getSize(), f->getLastWrite())) {
				continue;
			}

			results_.insert(Directory::SearchResultInfo(f, aStrings, aLevel));
			if (aStrings.addParents)
				break;
		}
	}

	// Match directories
	for(const auto& d: directories) {
		d->search(results_, aStrings, aLevel);
	}

	// Moving to a lower level
	if (aStrings.recursion) {
		aStrings.recursion->decrease(dirName.length());
	}

	aStrings.recursion = old;
}

void ShareManager::adcSearch(SearchResultList& results, SearchQuery& srch, const OptionalProfileToken& aProfile, const CID& cid, const string& aDir, bool isAutoSearch) throw(ShareException) {
	totalSearches++;
	if (aProfile == SP_HIDDEN) {
		return;
	}

	RLock l(cs);
	if(srch.root) {
		tthSearches++;
		const auto i = tthIndex.equal_range(const_cast<TTHValue*>(&(*srch.root)));
		for(auto& f: i | map_values) {
			if (f->hasProfile(aProfile) && AirUtil::isParentOrExact(aDir, f->getADCPath())) {
				f->addSR(results, srch.addParents);
				return;
			}
		}

		const auto files = tempShares.equal_range(*srch.root);
		for(const auto& f: files | map_values) {
			if(f.key.empty() || (f.key == cid.toBase32())) { // if no key is set, it means its a hub share.
				//TODO: fix the date?
				SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, f.size, "tmp\\" + Util::getFileName(f.path), *srch.root, 0, 1));
				results.push_back(sr);
			}
		}
		return;
	}

	recursiveSearches++;
	if (isAutoSearch)
		autoSearches++;

	for (const auto& p : srch.include.getPatterns()) {
		if (!bloom->match(p.str())) {
			filteredSearches++;
			return;
		}
	}

	if (srch.itemType == SearchQuery::TYPE_DIRECTORY && srch.matchType == Search::MATCH_NAME_EXACT) {
		if (srch.include.getPatterns().empty()) {
			// Invalid query
			return;
		}

		// Optimized version for exact directory matches
		const auto i = dirNameMap.equal_range(const_cast<string*>(&srch.include.getPatterns().front().str()));
		for(const auto& d: i | map_values) {
			if (!d->hasProfile(aProfile) || !srch.matchesDate(d->getLastWrite())) {
				continue;
			}

			if (!AirUtil::isParentOrExact(aDir, d->getADCPath())) {
				continue;
			}

			addDirResult(d.get(), results, aProfile, srch);
			if (results.size() >= srch.maxResults) {
				break;
			}
		}

		return;
	}

	// Get the search roots
	Directory::List roots;
	if (aDir == "/" || aDir.empty()) {
		getRoots(aProfile, roots);
	} else {
		findVirtuals<OptionalProfileToken>(aDir, aProfile, roots);
	}

	auto start = GET_TICK();

	// go them through recursively
	Directory::SearchResultInfo::Set resultInfos;
	for (const auto& d: roots) {
		d->search(resultInfos, srch, 0);
	}

	// update statistics
	auto end = GET_TICK();
	recursiveSearchTime += end - start;
	searchTokenCount += srch.include.count();
	for (const auto& p : srch.include.getPatterns()) 
		searchTokenLength += p.size();


	// pick the results to return
	for (auto i = resultInfos.begin(); (i != resultInfos.end()) && (results.size() < srch.maxResults); ++i) {
		auto& info = *i;
		if (info.getType() == Directory::SearchResultInfo::DIRECTORY) {
			addDirResult(info.directory, results, aProfile, srch);
		} else {
			info.file->addSR(results, srch.addParents);
		}
	}

	if (!results.empty())
		recursiveSearchesResponded++;
}

void ShareManager::cleanIndices(Directory& dir, const Directory::File* f) noexcept {
	dir.size -= f->getSize();
	sharedSize -= f->getSize();

	auto flst = tthIndex.equal_range(const_cast<TTHValue*>(&f->getTTH()));
	auto p = find(flst | map_values, f);
	if (p.base() != flst.second)
		tthIndex.erase(p.base());
	else
		dcassert(0);
}

void ShareManager::addDirName(const Directory::Ptr& aDir, Directory::MultiMap& aDirNames, ShareBloom& aBloom) noexcept {
	//const auto& name = aDir->getProfileDir() ? aDir->getProfileDir()->getNameLower() : aDir->realName.getLower();
	const auto& name = aDir->realName.getLower();

#ifdef _DEBUG
	auto directories = aDirNames.equal_range(const_cast<string*>(&name));
	auto p = find(directories | map_values, aDir);
	dcassert(p.base() == directories.second);
#endif
	aDirNames.emplace(const_cast<string*>(&name), aDir);
	aBloom.add(aDir->realName.getLower());
}

void ShareManager::removeDirName(const Directory& aDir, Directory::MultiMap& aDirNames) noexcept {
	//const auto& name = aDir.getProfileDir() ? aDir.getProfileDir()->getNameLower() : aDir.realName.getLower();
	const auto& name = aDir.realName.getLower();

	auto directories = aDirNames.equal_range(const_cast<string*>(&name));
	auto p = find_if(directories | map_values, [&aDir](const Directory::Ptr& d) { return d.get() == &aDir; });
	if (p.base() == aDirNames.end()) {
		dcassert(0);
		return;
	}

	aDirNames.erase(p.base());
}

void ShareManager::cleanIndices(Directory& dir) noexcept {
	for(auto& d: dir.directories) {
		cleanIndices(*d);
	}

	//remove from the name map
	removeDirName(dir, dirNameMap);

	//remove all files
	for(auto i = dir.files.begin(); i != dir.files.end(); ++i) {
		cleanIndices(dir, *i);
	}
}

void ShareManager::on(QueueManagerListener::BundleAdded, const BundlePtr& aBundle) noexcept {
	WLock l (dirNames);
	bundleDirs.insert(upper_bound(bundleDirs.begin(), bundleDirs.end(), aBundle->getTarget()), aBundle->getTarget());
}

void ShareManager::shareBundle(const BundlePtr& aBundle) noexcept {
	if (aBundle->isFileBundle())
		return;

	auto path = aBundle->getTarget();
	monitor.callAsync([=] { removeNotifications(path); });

	addRefreshTask(ADD_BUNDLE, { aBundle->getTarget() }, RefreshType::TYPE_BUNDLE, aBundle->getTarget());
}

void ShareManager::removeNotifications(const string& aPath) noexcept {
	auto p = findModifyInfo(aPath);
	if (p != fileModifications.end())
		removeNotifications(p, aPath);
}

bool ShareManager::allowAddDir(const string& aPath) const noexcept {
	{
		RLock l(cs);
		const auto mi = find_if(rootPaths | map_keys, IsParentOrExact(aPath));
		if (mi.base() != rootPaths.end()) {
			string fullPathLower = *mi;
			int pathPos = mi->length();

			StringList sl = StringTokenizer<string>(aPath.substr((*mi).length()), PATH_SEPARATOR).getTokens();

			for(const auto& name: sl) {
				pathPos += name.length()+1;
				fullPathLower += Text::toLower(name) + PATH_SEPARATOR;
				if (!checkSharedName(aPath.substr(0, pathPos), fullPathLower, true, true)) {
					return false;
				}

				auto m = profileDirs.find(fullPathLower);
				if (m != profileDirs.end()) {
					return false;
				}
			}
			return true;
		}
	}
	return false;
}

ShareManager::Directory::Ptr ShareManager::findDirectory(const string& aRealPath, StringList& remainingTokens_) const noexcept {
	auto mi = find_if(rootPaths | map_keys, IsParentOrExact(aRealPath)).base();
	if (mi == rootPaths.end()) {
		return nullptr;
	}

	auto curDir = mi->second;

	remainingTokens_ = StringTokenizer<string>(aRealPath.substr(mi->first.length()), PATH_SEPARATOR).getTokens();

	bool hasMissingToken = false;
	remainingTokens_.erase(std::remove_if(remainingTokens_.begin(), remainingTokens_.end(), [&](const string& currentName) {
		if (!hasMissingToken) {
			auto j = curDir->directories.find(Text::toLower(currentName));
			if (j != curDir->directories.end()) {
				curDir = *j;
				return true;
			}

			hasMissingToken = true;
		}

		return false;
	}), remainingTokens_.end());

	return curDir;
}

ShareManager::Directory::Ptr ShareManager::getDirectory(const string& aRealPath, bool aReportErrors, bool aCheckExcluded) noexcept {
	StringList tokens;
	auto curDir = findDirectory(aRealPath, tokens);
	if (tokens.empty() || !curDir) {
		return curDir;
	}

	auto curPath = curDir->getRealPath();
	for (const auto& currentName : tokens) {
		curPath += currentName + PATH_SEPARATOR;

		auto pathLower = Text::toLower(curPath);
		if (!checkSharedName(curPath, pathLower, true, aReportErrors)) {
			return nullptr;
		} else {
			if (aCheckExcluded) {
				RLock l(dirNames);
				if (excludedPaths.find(pathLower) != excludedPaths.end()) {
					return nullptr;
				}
			}

			curDir->updateModifyDate();
			curDir = Directory::createNormal(DualString(currentName), curDir, File::getLastModified(pathLower), dirNameMap, *bloom.get());
		}
	}

	return curDir;
}

ShareManager::Directory::Ptr ShareManager::findDirectory(const string& aRealPath) const noexcept {
	StringList tokens;
	auto curDir = findDirectory(aRealPath, tokens);
	return tokens.empty() ? curDir : nullptr;
}

void ShareManager::onFileHashed(const string& fname, HashedFile& fileInfo) noexcept {
	ProfileTokenSet dirtyProfiles;
	{
		WLock l(cs);
		auto d = getDirectory(Util::getFilePath(fname), false);
		if (!d) {
			return;
		}

		addFile(Util::getFileName(fname), d, fileInfo, dirtyProfiles);
	}

	setProfilesDirty(dirtyProfiles, false);
}

void ShareManager::addFile(const string& aName, Directory::Ptr& aDir, const HashedFile& fi, ProfileTokenSet& dirtyProfiles_) noexcept {
	DualString dualName(aName);
	auto i = aDir->files.find(dualName.getLower());
	if(i != aDir->files.end()) {
		// Get rid of false constness...
		cleanIndices(*aDir, *i);
		aDir->files.erase(i);
	}

	auto it = aDir->files.insert_sorted(new Directory::File(move(dualName), aDir, fi)).first;
	updateIndices(*aDir, *it, *bloom.get(), sharedSize, tthIndex);

	aDir->copyRootProfiles(dirtyProfiles_, true);
}

StringSet ShareManager::getExcludedPaths() const noexcept {
	RLock l(cs);
	return excludedPaths;
}

ShareProfileList ShareManager::getProfiles() const noexcept {
	RLock l(cs);
	return shareProfiles; 
}

ShareProfileInfo::List ShareManager::getProfileInfos() const noexcept {
	RLock l(cs);
	ShareProfileInfo::List ret;
	for (const auto& sp : shareProfiles) {
		if (sp->getToken() != SP_HIDDEN) {
			auto p = std::make_shared<ShareProfileInfo>(sp->getPlainName(), sp->getToken());
			if (p->token == SETTING(DEFAULT_SP)) {
				p->isDefault = true;
				ret.emplace(ret.begin(), p);
			} else {
				ret.emplace_back(p);
			}
		}
	}
	
	return ret;
}

void ShareManager::setExcludedPaths(const StringSet& aPaths) noexcept {
	WLock l(cs);
	excludedPaths = aPaths;
}

vector<pair<string, StringList>> ShareManager::getGroupedDirectories() const noexcept {
	vector<pair<string, StringList>> ret;
	
	{
		RLock l (cs);
		for(const auto& d: rootPaths | map_values | filtered(Directory::IsParent())) {
			const auto& currentPath = d->getProfileDir()->getPath();

			auto virtualName = d->getProfileDir()->getName();
			auto retVirtualPos = find_if(ret, CompareFirst<string, StringList>(virtualName));
			if (retVirtualPos != ret.end()) {
				//insert under an old virtual node if the real path doesn't exist there already
				if (find(retVirtualPos->second, currentPath) == retVirtualPos->second.end()) {
					retVirtualPos->second.push_back(currentPath); //sorted
				}
			} else {
				ret.emplace_back(virtualName, StringList{ currentPath });
			}
		}
	}

	sort(ret.begin(), ret.end());
	return ret;
}

string lastMessage;
uint64_t messageTick = 0;
bool ShareManager::checkSharedName(const string& aPath, const string& aPathLower, bool isDir, bool aReport /*true*/, int64_t size /*0*/) const noexcept {
	auto report = [&](const string& aMsg) {
		// There may be sequential modification notifications for monitored files so don't spam the same message many times
		if (aReport && (lastMessage != aMsg || messageTick + 3000 < GET_TICK())) {
			LogManager::getInstance()->message(aMsg, LogMessage::SEV_INFO);
			lastMessage = aMsg;
			messageTick = GET_TICK();
		}
	};

	string aNameLower = isDir ? Util::getLastDir(aPathLower) : Util::getFileName(aPathLower);

	if(aNameLower == "." || aNameLower == "..")
		return false;

	if (skipList.match(isDir ? Util::getLastDir(aPath) : Util::getFileName(aPath))) {
		if(SETTING(REPORT_SKIPLIST))
			report(STRING(SKIPLIST_HIT) + aPath);
		return false;
	}

	if (!isDir) {
		//dcassert(File::getSize(aPath) == size);
		string fileExt = Util::getFileExt(aNameLower);
		if( (strcmp(aNameLower.c_str(), "dcplusplus.xml") == 0) || 
			(strcmp(aNameLower.c_str(), "favorites.xml") == 0) ||
			(strcmp(fileExt.c_str(), ".dctmp") == 0) ||
			(strcmp(fileExt.c_str(), ".antifrag") == 0) ) 
		{
			return false;
		}

		//check for forbidden file patterns
		if(SETTING(REMOVE_FORBIDDEN)) {
			string::size_type nameLen = aNameLower.size();
			if ((strcmp(fileExt.c_str(), ".tdc") == 0) ||
				(strcmp(fileExt.c_str(), ".getright") == 0) ||
				(strcmp(fileExt.c_str(), ".temp") == 0) ||
				(strcmp(fileExt.c_str(), ".tmp") == 0) ||
				(strcmp(fileExt.c_str(), ".jc!") == 0) ||	//FlashGet
				(strcmp(fileExt.c_str(), ".dmf") == 0) ||	//Download Master
				(strcmp(fileExt.c_str(), ".!ut") == 0) ||	//uTorrent
				(strcmp(fileExt.c_str(), ".bc!") == 0) ||	//BitComet
				(strcmp(fileExt.c_str(), ".missing") == 0) ||
				(strcmp(fileExt.c_str(), ".bak") == 0) ||
				(strcmp(fileExt.c_str(), ".bad") == 0) ||
				(nameLen > 9 && aNameLower.rfind("part.met") == nameLen - 8) ||				
				(aNameLower.find("__padding_") == 0) ||			//BitComet padding
				(aNameLower.find("__incomplete__") == 0)		//winmx
				) {
					report(STRING(FORBIDDEN_FILE) + aPath);
					return false;
			}
		}

		if(strcmp(aPathLower.c_str(), AirUtil::privKeyFile.c_str()) == 0) {
			return false;
		}

		if(SETTING(NO_ZERO_BYTE) && !(size > 0))
			return false;

		if (SETTING(MAX_FILE_SIZE_SHARED) != 0 && size > Util::convertSize(SETTING(MAX_FILE_SIZE_SHARED), Util::MB)) {
			report(STRING(BIG_FILE_NOT_SHARED) + " " + aPath + " (" + Util::formatBytes(size) + ")");
			return false;
		}
	} else {
#ifdef _WIN32
		// don't share Windows directory
		if(aPathLower.length() >= winDir.length() && strcmp(aPathLower.substr(0, winDir.length()).c_str(), winDir.c_str()) == 0)
			return false;
#endif
	}
	return true;
}

void ShareManager::setSkipList() {
	WLock l (dirNames);
	skipList.pattern = SETTING(SKIPLIST_SHARE);
	skipList.setMethod(SETTING(SHARE_SKIPLIST_USE_REGEXP) ? StringMatch::REGEX : StringMatch::WILDCARD);
	skipList.prepare();
}

void ShareManager::deviceRemoved(const string& aDrive) {
	monitor.deviceRemoved(aDrive);
}

} // namespace dcpp
