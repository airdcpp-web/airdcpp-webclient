/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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
#include "Download.h"
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

ShareDirInfo::ShareDirInfo(const ShareDirInfoPtr& aInfo, ProfileToken aNewProfile) : vname(aInfo->vname), profile(aNewProfile), path(aInfo->path), incoming(aInfo->incoming),
	found(false), diffState(aInfo->diffState), state(STATE_NORMAL), size(aInfo->size) {

}

ShareDirInfo::ShareDirInfo(const string& aVname, ProfileToken aProfile, const string& aPath, bool aIncoming /*false*/, State aState /*STATE_NORMAL*/) : vname(aVname), profile(aProfile), path(aPath), incoming(aIncoming),
	found(false), diffState(DIFF_NORMAL), state(aState), size(0) {}

ShareManager::ShareManager() : lastFullUpdate(GET_TICK()), lastIncomingUpdate(GET_TICK()), sharedSize(0),
	xml_saving(false), lastSave(0), aShutdown(false), refreshRunning(false), totalSearches(0), bloom(new ShareBloom(1<<20)), monitorDebug(false)
{ 
	SettingsManager::getInstance()->addListener(this);
	QueueManager::getInstance()->addListener(this);
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
	TimerManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);

	join();
}

void ShareManager::startup(function<void(const string&)> splashF, function<void(float)> progressF) noexcept {
	AirUtil::updateCachedSettings();
	if (!getShareProfile(SETTING(DEFAULT_SP))) {
		if (shareProfiles.empty()) {
			auto sp = ShareProfilePtr(new ShareProfile(STRING(DEFAULT), 0));
			shareProfiles.push_back(sp);
		} else {
			SettingsManager::getInstance()->set(SettingsManager::DEFAULT_SP, shareProfiles.front()->getToken());
		}
	}

	ShareProfilePtr hidden = ShareProfilePtr(new ShareProfile("Hidden", SP_HIDDEN));
	shareProfiles.push_back(hidden);

	setSkipList();

	if(!loadCache(progressF)) {
		if (splashF)
			splashF(STRING(REFRESHING_SHARE));
		refresh(false, TYPE_STARTUP_BLOCKING, progressF);
	}

	addAsyncTask([this] {
		rebuildTotalExcludes();

		monitor.reset(new DirectoryMonitor(1, false));
		monitor->addListener(this);

		//this requires disk access
		StringList monitorPaths;

		{
			RLock l(cs);
			for(const auto& d: rootPaths | map_values | filtered(Directory::IsParent())) {
				if (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_ALL || (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_INCOMING && d->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING)))
					monitorPaths.push_back(d->getProfileDir()->getPath());
			}
		}

		//monitor->addDirectory(R"(C:\)");
		addMonitoring(monitorPaths);
		TimerManager::getInstance()->addListener(this);

		if (SETTING(STARTUP_REFRESH))
			refresh(false, TYPE_STARTUP_DELAYED);
	});
}

void ShareManager::addMonitoring(const StringList& aPaths) noexcept {
	int added = 0;
	for(const auto& p: aPaths) {
		try {
			if (monitor->addDirectory(p))
				added++;
		} catch (MonitorException& e) {
			LogManager::getInstance()->message(STRING_F(FAILED_ADD_MONITORING, p % e.getError()), LogManager::LOG_ERROR);
		}
	}

	if (added > 0)
		LogManager::getInstance()->message(STRING_F(X_MONITORING_ADDED, added), LogManager::LOG_INFO);
}

void ShareManager::removeMonitoring(const StringList& aPaths) noexcept {
	int removed = 0;
	for(const auto& p: aPaths) {
		try {
			if (monitor->removeDirectory(p))
				removed++;
		} catch (MonitorException& e) {
			LogManager::getInstance()->message("Error occurred when trying to remove the foldrer " + p + " from monitoring: " + e.getError(), LogManager::LOG_ERROR);
		}
	}

	if (removed > 0)
		LogManager::getInstance()->message(STRING_F(X_MONITORING_REMOVED, removed), LogManager::LOG_INFO);
}

optional<pair<string, bool>> ShareManager::checkModifiedPath(const string& aPath) const noexcept {
	// TODO: FIX LINUX
	FileFindIter f(aPath);
	if (f != FileFindIter()) {
		if (!SETTING(SHARE_HIDDEN) && f->isHidden())
			return nullptr;

		if (!SETTING(SHARE_FOLLOW_SYMLINKS) && f->isLink())
			return nullptr;

		bool isDir = f->isDirectory();
		auto path = isDir ? aPath + PATH_SEPARATOR : aPath;
		if (!checkSharedName(path, Text::toLower(path), isDir, true, f->getSize()))
			return nullptr;

		return make_pair(path, isDir);
	}

	return nullptr;
}

void ShareManager::addModifyInfo(const string& aPath, bool isDirectory, DirModifyInfo::ActionType aAction) noexcept {
	auto filePath = isDirectory ? aPath : Util::getFilePath(aPath);

	auto p = findModifyInfo(filePath);
	if (p == fileModifications.end()) {
		//add a new modify info
		fileModifications.emplace_front(aPath, isDirectory, aAction);
	} else {
		if (!isDirectory) {
			//add the file
			if (AirUtil::isSub((*p).path, filePath))
				(*p).setPath(filePath);

			p->addFile(aPath, aAction);
		} else if (filePath == (*p).path) {
			//update the dir action
			p->dirAction = aAction;
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
	monitor->callAsync([this] { handleChangedFiles(GET_TICK(), true); });
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
		LogManager::getInstance()->message(STRING_F(SHARED_DIR_REMOVED, info.path), LogManager::LOG_INFO);
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
				LogManager::getInstance()->message((removedPath.back() == PATH_SEPARATOR ? STRING_F(SHARED_DIR_REMOVED, removedPath) : STRING_F(SHARED_FILE_DELETED, removedPath)), LogManager::LOG_INFO);
			} else {
				LogManager::getInstance()->message(STRING_F(X_SHARED_FILES_REMOVED, removed % info.path), LogManager::LOG_INFO);
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
		dir = findDirectory(info.path, false, false, true);
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
	if (find_if(*bundlePaths_, IsParentOrExactOrSub<false>(Text::toLower(info.path))) != (*bundlePaths_).end()) {
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
		LogManager::getInstance()->message(STRING_F(X_SHARED_FILES_ADDED, hashedFiles % info.path), LogManager::LOG_INFO);
	}  
	
	if (hashSize > 0) {
		if (filesToHash == 1)
			LogManager::getInstance()->message(STRING_F(FILE_X_ADDED_FOR_HASH, hashFile % Util::formatBytes(hashSize)), LogManager::LOG_INFO);
		else
			LogManager::getInstance()->message(STRING_F(X_FILES_ADDED_FOR_HASH, filesToHash % Util::formatBytes(hashSize) % info.path), LogManager::LOG_INFO);
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
		addRefreshTask(REFRESH_DIRS, refresh, TYPE_MONITORING, Util::emptyString);
	}

	setProfilesDirty(dirtyProfiles);
}

void ShareManager::on(DirectoryMonitorListener::DirectoryFailed, const string& aPath, const string& aError) noexcept {
	LogManager::getInstance()->message(STRING_F(MONITOR_DIR_FAILED, aPath % aError), LogManager::LOG_ERROR);
}

void ShareManager::on(DirectoryMonitorListener::FileCreated, const string& aPath) noexcept {
	if (monitorDebug)
		LogManager::getInstance()->message("File added: " + aPath, LogManager::LOG_INFO);

	auto ret = checkModifiedPath(aPath);
	if (ret) {
		addModifyInfo((*ret).first, (*ret).second, DirModifyInfo::ACTION_CREATED);
	}
}

void ShareManager::on(DirectoryMonitorListener::FileModified, const string& aPath) noexcept {
	if (monitorDebug)
		LogManager::getInstance()->message("File modified: " + aPath, LogManager::LOG_INFO);

	auto ret = checkModifiedPath(aPath);
	if (ret) {
		// modified directories won't matter at the moment
		if ((*ret).second)
			return;

		addModifyInfo((*ret).first, false, DirModifyInfo::ACTION_MODIFIED);
	}
}

void ShareManager::Directory::getRenameInfoList(const string& aPath, RenameList& aRename) noexcept {
	string path = aPath + name.getNormal() + PATH_SEPARATOR;
	for (const auto& f: files) {
		aRename.emplace_back(path + f->name.getNormal(), HashedFile(f->getTTH(), f->getLastWrite(), f->getSize()));
	}

	for (const auto& d: directories) {
		d->getRenameInfoList(path + name.getNormal() + PATH_SEPARATOR, aRename);
	}
}

void ShareManager::on(DirectoryMonitorListener::FileRenamed, const string& aOldPath, const string& aNewPath) noexcept {
	if (monitorDebug)
		LogManager::getInstance()->message("File renamed, old: " + aOldPath + " new: " + aNewPath, LogManager::LOG_INFO);

	ProfileTokenSet dirtyProfiles;
	RenameList toRename;
	bool found = true;
	bool noSharing = false;

	{
		WLock l(cs);
		auto parent = findDirectory(Util::getFilePath(aOldPath), false, false, false);
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
					removeDirName(*d);

					//rename
					parent->directories.erase(p);
					d->name = DualString(Util::getFileName(aNewPath));
					parent->directories.insert_sorted(d);
					parent->updateModifyDate();

					//add in bloom and dir name map
					d->addBloom(*bloom.get());
					addDirName(d);

					//get files to convert in the hash database (recursive)
					d->getRenameInfoList(Util::emptyString, toRename);

					LogManager::getInstance()->message(STRING_F(SHARED_DIR_RENAMED, (aOldPath + PATH_SEPARATOR) % (aNewPath + PATH_SEPARATOR)), LogManager::LOG_INFO);
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

						LogManager::getInstance()->message(STRING_F(SHARED_FILE_RENAMED, aOldPath % aNewPath), LogManager::LOG_INFO);
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

	setProfilesDirty(dirtyProfiles);
}

void ShareManager::on(DirectoryMonitorListener::FileDeleted, const string& aPath) noexcept {
	if (monitorDebug)
		LogManager::getInstance()->message("File deleted: " + aPath, LogManager::LOG_INFO);

	onFileDeleted(aPath);
}

void ShareManager::onFileDeleted(const string& aPath) {
	{
		RLock l(cs);
		auto parent = findDirectory(Util::getFilePath(aPath), false, false, false);
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
			if (AirUtil::isParentOrExactCase(aPath, i->first)) {
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
	auto parent = findDirectory(isDirectory ? Util::getParentDir(aPath) : Util::getFilePath(aPath), false, false, false);
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
			parent->getParent()->directories.erase_key(parent->name.getLower());
		}
	}

	return deleted;
}

void ShareManager::on(DirectoryMonitorListener::Overflow, const string& aRootPath) noexcept {
	if (monitorDebug)
		LogManager::getInstance()->message("Monitoring overflow: " + aRootPath, LogManager::LOG_INFO);

	// refresh the dir
	refresh(aRootPath);
}

void ShareManager::abortRefresh() noexcept {
	//abort buildtree and refresh, we are shutting down.
	aShutdown = true;
}

void ShareManager::shutdown(function<void(float)> progressF) noexcept {
	monitor->removeListener(this);
	saveXmlList(false, progressF);

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
}

void ShareManager::setProfilesDirty(ProfileTokenSet aProfiles, bool forceXmlRefresh /*false*/) noexcept {
	if (!aProfiles.empty()) {
		RLock l(cs);
		for(const auto aProfile: aProfiles) {
			auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
			if(i != shareProfiles.end()) {
				if (forceXmlRefresh)
					(*i)->getProfileList()->setForceXmlRefresh(true);
				(*i)->getProfileList()->setXmlDirty(true);
				(*i)->setProfileInfoDirty(true);
			}
		}
	}
}

ShareManager::Directory::Directory(DualString&& aRealName, const ShareManager::Directory::Ptr& aParent, uint64_t aLastWrite, ProfileDirectory::Ptr aProfileDir) :
	size(0),
	parent(aParent.get()),
	profileDir(aProfileDir),
	lastWrite(aLastWrite),
	name(move(aRealName))
{
}

ShareManager::Directory::~Directory() { 
	for_each(files, DeleteFunction());
}

void ShareManager::Directory::updateModifyDate() {
	lastWrite = dcpp::File::getLastModified(getRealPath(false));
}

void ShareManager::Directory::getResultInfo(ProfileToken aProfile, int64_t& size_, size_t& files_, size_t& folders_) const noexcept {
	for(const auto& d: directories) {
		if (d->isLevelExcluded(aProfile))
			continue;
		
		d->getResultInfo(aProfile, size_, files_, folders_);
	}

	folders_ += directories.size();
	size_ += size;
	files_ += files.size();
}

int64_t ShareManager::Directory::getSize(ProfileToken aProfile) const noexcept {
	int64_t tmp = size;
	for(const auto& d: directories) {
		if (d->isLevelExcluded(aProfile))
			continue;
		tmp += d->getSize(aProfile);
	}
	return tmp;
}

int64_t ShareManager::Directory::getTotalSize() const noexcept {
	int64_t tmp = size;
	for(const auto& d: directories)
		tmp += d->getTotalSize();

	return tmp;
}

string ShareManager::Directory::getADCPath(ProfileToken aProfile) const noexcept {
	if(profileDir && profileDir->hasRootProfile(aProfile))
		return '/' + profileDir->getName(aProfile) + '/';
	return parent->getADCPath(aProfile) + name.getNormal() + '/';
}

string ShareManager::Directory::getVirtualName(ProfileToken aProfile) const noexcept {
	if(profileDir && profileDir->hasRootProfile(aProfile))
		return profileDir->getName(aProfile);
	return name.getNormal();
}

string ShareManager::Directory::getFullName(ProfileToken aProfile) const noexcept {
	if(profileDir && profileDir->hasRootProfile(aProfile))
		return profileDir->getName(aProfile) + '\\';
	dcassert(parent);
	return parent->getFullName(aProfile) + name.getNormal() + '\\';
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
		return getParent()->getRealPath(name.getNormal() + PATH_SEPARATOR_STR + path);
	}

	return getProfileDir()->getPath() + path;
}

bool ShareManager::Directory::isRootLevel(ProfileToken aProfile) const noexcept {
	return profileDir && profileDir->hasRootProfile(aProfile) ? true : false;
}

bool ShareManager::Directory::hasProfile(ProfileTokenSet& aProfiles) const noexcept {
	if (profileDir) {
		if (profileDir->hasRootProfile(aProfiles))
			return true;
		if (profileDir->isExcluded(aProfiles))
			return false;
	}

	if (parent)
		return parent->hasProfile(aProfiles);
	return false;
}


void ShareManager::Directory::copyRootProfiles(ProfileTokenSet& aProfiles, bool setCacheDirty) const noexcept {
	if (profileDir) {
		boost::copy(profileDir->getRootProfiles() | map_keys, inserter(aProfiles, aProfiles.begin()));
		if (setCacheDirty)
			profileDir->setCacheDirty(true);
	}

	if (parent)
		parent->copyRootProfiles(aProfiles, setCacheDirty);
}

bool ShareManager::ProfileDirectory::hasRootProfile(const ProfileTokenSet& aProfiles) const noexcept {
	for(const auto ap: aProfiles) {
		if (rootProfiles.find(ap) != rootProfiles.end())
			return true;
	}
	return false;
}

bool ShareManager::Directory::hasProfile(ProfileToken aProfile) const noexcept {
	if(profileDir) {
		if (isLevelExcluded(aProfile))
			return false;
		if (profileDir->hasRootProfile(aProfile))
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

bool ShareManager::ProfileDirectory::isExcluded(ProfileTokenSet& aProfiles) const noexcept {
	for (auto t : excludedProfiles) {
		aProfiles.erase(t);
	}

	return aProfiles.empty();
}

bool ShareManager::Directory::isLevelExcluded(ProfileToken aProfile) const noexcept {
	return profileDir && profileDir->isExcluded(aProfile);
}

bool ShareManager::Directory::isLevelExcluded(ProfileTokenSet& aProfiles) const noexcept {
	return profileDir && profileDir->isExcluded(aProfiles);
}

bool ShareManager::ProfileDirectory::isExcluded(ProfileToken aProfile) const noexcept {
	return !excludedProfiles.empty() && excludedProfiles.find(aProfile) != excludedProfiles.end();
}

ShareManager::ProfileDirectory::ProfileDirectory(const string& aRootPath, const string& aVname, ProfileToken aProfile, bool incoming /*false*/) : path(aRootPath), cacheDirty(false) { 
	rootProfiles[aProfile] = aVname;
	setFlag(FLAG_ROOT);
	if (incoming)
		setFlag(FLAG_INCOMING);
}

ShareManager::ProfileDirectory::ProfileDirectory(const string& aRootPath, ProfileToken aProfile) : path(aRootPath), cacheDirty(false) {
	excludedProfiles.insert(aProfile);
	setFlag(FLAG_EXCLUDE_PROFILE);
}

void ShareManager::ProfileDirectory::addRootProfile(const string& aName, ProfileToken aProfile) noexcept {
	rootProfiles[aProfile] = aName;
	setFlag(FLAG_ROOT);
}

void ShareManager::ProfileDirectory::addExclude(ProfileToken aProfile) noexcept {
	setFlag(FLAG_EXCLUDE_PROFILE);
	excludedProfiles.insert(aProfile);
}

bool ShareManager::ProfileDirectory::removeRootProfile(ProfileToken aProfile) noexcept {
	rootProfiles.erase(aProfile);
	return rootProfiles.empty();
}

bool ShareManager::ProfileDirectory::removeExcludedProfile(ProfileToken aProfile) noexcept {
	excludedProfiles.erase(aProfile);
	return excludedProfiles.empty();
}

string ShareManager::ProfileDirectory::getName(ProfileToken aProfile) const noexcept {
	auto p = rootProfiles.find(aProfile);
	return p == rootProfiles.end() ? Util::emptyString : p->second; 
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
	if(i != tthIndex.end()) 
		return i->second->getADCPath(aProfile);

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

ShareProfilePtr ShareManager::getProfile(ProfileToken aProfile) const noexcept {
	RLock l(cs);
	const auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
	if(i != shareProfiles.end()) {
		return *i;
	}

	return nullptr;
}

pair<int64_t, string> ShareManager::getFileListInfo(const string& virtualFile, ProfileToken aProfile) throw(ShareException) {
	if(virtualFile == "MyList.DcLst") 
		throw ShareException("NMDC-style lists no longer supported, please upgrade your client");

	if(virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
		FileList* fl = generateXmlList(aProfile);
		return make_pair(fl->getBzXmlListLen(), fl->getFileName());
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

void ShareManager::toRealWithSize(const string& virtualFile, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_) throw(ShareException) {
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		TTHValue tth(virtualFile.substr(4));

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
		findVirtuals<ProfileTokenSet>(virtualFile, aProfiles, dirs);

		auto fileName = Text::toLower(Util::getAdcFileName(virtualFile));
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
		cmd.addParam("FN", f->getADCPath(aProfile));
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
void ShareManager::clearTempShares() {
	WLock l(cs);
	tempShares.clear();
}

void ShareManager::getRealPaths(const string& aPath, StringList& ret, ProfileToken aProfile) const throw(ShareException) {
	if (aPath.empty())
		throw ShareException("empty virtual path");

	Directory::List dirs;

	RLock l (cs);
	findVirtuals<ProfileToken>(aPath, aProfile, dirs);

	if (aPath.back() == '/') {
		for(const auto& d: dirs)
			ret.push_back(d->getRealPath());
	} else { //its a file
		auto fileName = Text::toLower(Util::getAdcFileName(aPath));
		for(const auto& d: dirs) {
			auto it = d->files.find(fileName);
			if(it != d->files.end()) {
				ret.push_back((*it)->getRealPath());
				return;
			}
		}
	}
}

bool ShareManager::isRealPathShared(const string& aPath) noexcept {
	RLock l (cs);
	auto d = findDirectory(Util::getFilePath(aPath), false, false, true);
	if (d) {
		auto it = d->files.find(Text::toLower(Util::getFileName(aPath)));
		if(it != d->files.end()) {
			return true;
		}
	}

	return false;
}

string ShareManager::validateVirtual(const string& aVirt) const noexcept {
	string tmp = aVirt;
	string::size_type idx = 0;

	while( (idx = tmp.find_first_of("\\/"), idx) != string::npos) {
		tmp[idx] = '_';
	}
	return tmp;
}

void ShareManager::addRoot(const string& aPath, Directory::Ptr& aDir) noexcept {
	rootPaths[Text::toLower(aPath)] = aDir;
}

ShareManager::DirMap::const_iterator ShareManager::findRoot(const string& aPath) const noexcept {
	return rootPaths.find(Text::toLower(aPath));
}

void ShareManager::loadProfile(SimpleXML& aXml, const string& aName, ProfileToken aToken) {
	ShareProfilePtr sp = ShareProfilePtr(new ShareProfile(aName, aToken));
	shareProfiles.push_back(sp);

	aXml.stepIn();
	while(aXml.findChild("Directory")) {
		string realPath = aXml.getChildData();
		if(realPath.empty()) {
			continue;
		}
		// make sure realPath ends with PATH_SEPARATOR
		if(realPath[realPath.size() - 1] != PATH_SEPARATOR) {
			realPath += PATH_SEPARATOR;
		}

		const string& virtualName = aXml.getChildAttrib("Virtual");
		string vName = validateVirtual(virtualName.empty() ? Util::getLastDir(realPath) : virtualName);

		ProfileDirectory::Ptr pd = nullptr;
		auto p = profileDirs.find(realPath);
		if (p != profileDirs.end()) {
			pd = p->second;
			pd->addRootProfile(virtualName, aToken);
		} else {
			pd = ProfileDirectory::Ptr(new ProfileDirectory(realPath, virtualName, aToken, aXml.getBoolChildAttrib("Incoming")));
			profileDirs[realPath] = pd;
		}

		auto j = findRoot(realPath);
		if (j == rootPaths.end()) {
			auto dir = Directory::create(virtualName, nullptr, 0, pd);
			addRoot(realPath, dir);
		}
	}

	aXml.resetCurrentChild();
	if(aXml.findChild("NoShare")) {
		aXml.stepIn();
		while(aXml.findChild("Directory")) {
			auto path = aXml.getChildData();

			const auto p = profileDirs.find(path);
			if (p != profileDirs.end()) {
				auto pd = p->second;
				pd->addExclude(aToken);
			} else {
				auto pd = ProfileDirectory::Ptr(new ProfileDirectory(path, aToken));
				profileDirs[path] = pd;
			}
		}
		aXml.stepOut();
	}
	aXml.stepOut();
}

void ShareManager::load(SimpleXML& aXml) {
	//WLock l(cs);
	aXml.resetCurrentChild();

	if(aXml.findChild("Share")) {
		string name = aXml.getChildAttrib("Name");
		loadProfile(aXml, !name.empty() ? name : STRING(DEFAULT), aXml.getIntChildAttrib("Token"));
	}

	aXml.resetCurrentChild();
	while(aXml.findChild("ShareProfile")) {
		auto token = aXml.getIntChildAttrib("Token");
		string name = aXml.getChildAttrib("Name");
		if (token != SP_HIDDEN && !name.empty()) //reserve a few numbers for predefined profiles
			loadProfile(aXml, name, token);
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

optional<ProfileToken> ShareManager::getProfileByName(const string& aName) const noexcept {
	if (aName.empty()) {
		return SETTING(DEFAULT_SP);
	}

	auto p = find_if(shareProfiles, [&](const ShareProfilePtr& aProfile) { return Util::stricmp(aProfile->getPlainName(), aName) == 0; });
	if (p == shareProfiles.end())
		return nullptr;
	return (*p)->getToken();
}

ShareManager::Directory::Ptr ShareManager::Directory::create(DualString&& aName, const Ptr& aParent, uint64_t aLastWrite, ProfileDirectory::Ptr aRoot /*nullptr*/) {
	auto dir = Ptr(new Directory(move(aName), aParent, aLastWrite, aRoot));
	if (aParent)
		aParent->directories.insert_sorted(dir);
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
	ShareLoader(const string& aPath, const ShareManager::Directory::Ptr& aOldRoot, ShareManager::ShareBloom* aBloom) : 
		ShareManager::RefreshInfo(aPath, aOldRoot, 0),
		ThreadedCallBack(aOldRoot->getProfileDir()->getCacheXmlPath()),
		curDirPath(aOldRoot->getProfileDir()->getPath()),
		curDirPathLower(Text::toLower(aOldRoot->getProfileDir()->getPath())),
		bloom(aBloom)
	{ 
		cur = root;
	}


	void startTag(const string& name, StringPairList& attribs, bool simple) {
		if(compare(name, SDIRECTORY) == 0) {
			const string& name = getAttrib(attribs, SNAME, 0);
			const string& date = getAttrib(attribs, DATE, 1);

			if(!name.empty()) {
				curDirPath += name + PATH_SEPARATOR;
				//lastPos += cur->name.size()+1;

				ShareManager::ProfileDirectory::Ptr pd = nullptr;
				if (!subProfiles.empty()) {
					auto i = subProfiles.find(curDirPath);
					if(i != subProfiles.end()) {
						pd = i->second;
					}
				}

				cur = ShareManager::Directory::create(name, cur, Util::toUInt32(date), pd);
				curDirPathLower += cur->name.getLower() + PATH_SEPARATOR;
				if (pd && pd->isSet(ShareManager::ProfileDirectory::FLAG_ROOT)) {
					rootPathsNew[curDirPathLower] = cur;
				}

				cur->addBloom(*bloom);
				dirNameMapNew.emplace(const_cast<string*>(&cur->name.getLower()), cur);
			}

			if(simple) {
				if(cur) {
					cur = cur->getParent();
				}
			}
		} else if (cur && compare(name, SFILE) == 0) {
			const string& fname = getAttrib(attribs, SNAME, 0);
			if(fname.empty()) {
				dcdebug("Invalid file found: %s\n", fname.c_str());
				return;
			}

			try {
				DualString name(fname);
				HashedFile fi;
				HashManager::getInstance()->getFileInfo(curDirPathLower + name.getLower(), curDirPath, fi);
				auto pos = cur->files.insert_sorted(new ShareManager::Directory::File(move(name), cur, fi));
				ShareManager::updateIndices(*cur, *pos.first, *bloom, addedSize, tthIndexNew);
			}catch(Exception& e) {
				hashSize += File::getSize(curDirPath + fname);
				dcdebug("Error loading file list %s \n", e.getError().c_str());
			}
		} else if (compare(name, SHARE) == 0) {
			int version = Util::toInt(getAttrib(attribs, SVERSION, 0));
			if (version > Util::toInt(SHARE_CACHE_VERSION))
				throw("Newer cache version"); //don't load those...

			cur->addBloom(*bloom);
			dirNameMapNew.emplace(const_cast<string*>(&cur->name.getLower()), cur);
			cur->setLastWrite(Util::toUInt32(getAttrib(attribs, DATE, 2)));
		}
	}
	void endTag(const string& name) {
		if(compare(name, SDIRECTORY) == 0) {
			if(cur) {
				//auto len = curDirPath.size()-lastPos;
				//curDirPath.erase(lastPos, len);
				//curDirPathLower.erase(lastPos, len);
				//lastPos -= len;
				curDirPath = Util::getParentDir(curDirPath);
				curDirPathLower = Util::getParentDir(curDirPathLower);
				cur = cur->getParent();
			}
		}
	}

private:
	friend struct SizeSort;

	ShareManager::Directory::Ptr cur;
	//ShareManager::RefreshInfo& ri;

	string curDirPathLower;
	string curDirPath;
	ShareManager::ShareBloom* bloom;
	//int lastPos;
};

typedef shared_ptr<ShareManager::ShareLoader> ShareLoaderPtr;
typedef vector<ShareLoaderPtr> LoaderList;

bool ShareManager::loadCache(function<void(float)> progressF) noexcept{
	HashManager::HashPauser pauser;

	Util::migrate(Util::getPath(Util::PATH_SHARECACHE), "ShareCache_*");

	StringList fileList = File::findFiles(Util::getPath(Util::PATH_SHARECACHE), "ShareCache_*", File::TYPE_FILE);

	if (fileList.empty()) {
		if (Util::fileExists(Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml")) {
			//delete the old cache
			File::deleteFile(Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml");
		}
		return rootPaths.empty();
	}

	DirMap parents;

	//get the parent dirs
	for (const auto& p : rootPaths) {
		if (find_if(rootPaths | map_keys, [&p](const string& path) { return AirUtil::isSub(p.second->getProfileDir()->getPath(), path); }).base() == rootPaths.end())
			parents.insert(p);
	}

	LoaderList ll;

	//create the info dirs
	for (const auto& p : fileList) {
		if (Util::getFileExt(p) == ".xml") {
			auto rp = find_if(parents | map_values, [&p](const Directory::Ptr& aDir) { return Util::stricmp(aDir->getProfileDir()->getCacheXmlPath(), p) == 0; });
			if (rp.base() != parents.end()) { //make sure that subdirs are never listed here...
				try {
					auto loader = new ShareLoader(rp.base()->first, *rp, bloom.get());
					ll.emplace_back(loader);
					continue;
				} catch (...) {}
			}
		}

		//no use for extra files
		File::deleteFile(p);
	}

	const auto dirCount = ll.size();

	//ll.sort(SimpleXMLReader::ThreadedCallBack::SizeSort());

	//load the XML files
	atomic<long> loaded(0);
	bool hasFailed = false;

	try {
		parallel_for_each(ll.begin(), ll.end(), [&](ShareLoaderPtr& i) {
			//LogManager::getInstance()->message("Thread: " + Util::toString(::GetCurrentThreadId()) + "Size " + Util::toString(loader.size), LogManager::LOG_INFO);
			auto& loader = *i;
			try {
				SimpleXMLReader(&loader).parse(*loader.file);
			} catch (SimpleXMLException& e) {
				LogManager::getInstance()->message("Error loading " + loader.xmlPath + ": " + e.getError(), LogManager::LOG_ERROR);
				hasFailed = true;
				File::deleteFile(loader.xmlPath);
			} catch (...) {
				hasFailed = true;
				File::deleteFile(loader.xmlPath);
			}

			if (progressF) {
				progressF(static_cast<float>(loaded++) / static_cast<float>(dirCount));
			}
		});
	} catch (std::exception& e) {
		hasFailed = true;
		LogManager::getInstance()->message("Loading the share cache failed: " + string(e.what()), LogManager::LOG_INFO);
	}

	if (hasFailed)
		return false;

	//apply the changes
	int64_t hashSize = 0;

	DirMap newRoots;

	mergeRefreshChanges(ll, dirNameMap, newRoots, tthIndex, hashSize, sharedSize, nullptr);

	//make sure that the subprofiles are added too
	for (auto& p: newRoots)
		rootPaths[p.first] = p.second;

	//were all parents loaded?
	StringList refreshPaths;
	for (auto& i: parents) {
		auto p = newRoots.find(i.first);
		if (p == newRoots.end()) {
			//add for refresh
			refreshPaths.push_back(i.second->getProfileDir()->getPath());
		}
	}

	addRefreshTask(REFRESH_DIRS, refreshPaths, TYPE_MANUAL, Util::emptyString);

	if (hashSize > 0) {
		LogManager::getInstance()->message(STRING_F(FILES_ADDED_FOR_HASH_STARTUP, Util::formatBytes(hashSize)), LogManager::LOG_INFO);
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
			aXml.addChildAttrib("Virtual", d->getProfileDir()->getName(sp->getToken()));
			//if (p->second->getRoot()->hasFlag(ProfileDirectory::FLAG_INCOMING))
			aXml.addChildAttrib("Incoming", d->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING));
		}

		aXml.addTag("NoShare");
		aXml.stepIn();
		for(const auto& pd: profileDirs | map_values) {
			if (pd->isExcluded(sp->getToken()))
				aXml.addTag("Directory", pd->getPath());
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
		} /*else {
			totalStrLen_ += f.getNameLower().length(); //the len is the same, right?
		}*/
	}

	totalStrLen_ += name.getLower().length();
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


string ShareManager::printStats() const noexcept {
	uint64_t totalAge=0;
	size_t totalFiles=0, lowerCaseFiles=0, totalDirs=0, totalStrLen=0, roots=0;
	int64_t totalSize=0;

	countStats(totalAge, totalDirs, totalSize, totalFiles, lowerCaseFiles, totalStrLen, roots);

	unordered_set<TTHValue*> uniqueTTHs;
	for(auto tth: tthIndex | map_keys) {
		uniqueTTHs.insert(tth);
	}

	/*auto dirSize1 = sizeof(Directory);
	auto dirSize2 = sizeof(Directory::Ptr);
	auto fileSize = sizeof(File);
	auto bloomSize = 1<<20;*/

	size_t memUsage = totalStrLen; // total length of all names
	memUsage += sizeof(Directory)*totalDirs; //directories
	memUsage += sizeof(File)*totalFiles; //files
	memUsage += roots*(1<<20); //root blooms
	memUsage += sizeof(Directory::Ptr)*totalDirs; //pointers stored in the vector for each directory

	auto upMinutes = static_cast<double>(GET_TICK()) / 1000.00 / 60.00;

	string ret = boost::str(boost::format(
"\r\n\r\n-=[ Share statistics ]=-\r\n\r\n\
Share profiles: %d\r\n\
Shared root paths: %d (of which %d%% have no parent)\r\n\
Total share size: %s\r\n\
Total incoming searches: %d (%d per minute)\r\n\
Total shared files: %d (of which %d%% are lowercase)\r\n\
Unique TTHs: %d (%d%%)\r\n\
Total shared directories: %d (%d files per directory)\r\n\
Estimated memory usage for the share: %d (this doesn't include the hash store)\r\n\
Average age of a file: %s")

		% shareProfiles.size()
		% roots % ((static_cast<double>(roots == 0 ? 0 : rootPaths.size()) / roots)*100.00)
		% Util::formatBytes(totalSize)
		% totalSearches % (totalSearches / upMinutes)
		% totalFiles % ((static_cast<double>(lowerCaseFiles) / static_cast<double>(totalFiles))*100.00)
		% uniqueTTHs.size() % ((static_cast<double>(uniqueTTHs.size()) / static_cast<double>(totalFiles))*100.00)
		% totalDirs % (static_cast<double>(totalFiles) / static_cast<double>(totalDirs))
		% Util::formatBytes(memUsage)
		% Util::formatTime(GET_TIME() - (totalFiles > 0 ? (totalAge / totalFiles) : 0), false, true));

	ret += "\r\n\r\n\r\n-=[ Monitoring statistics ]=-\r\n\r\n";
	if (monitor->hasDirectories()) {
		ret += "Debug mode: ";
		ret += (monitorDebug ? "Enabled" : "Disabled");
		ret += " \r\n\r\nMonitored paths:\r\n";
		ret += monitor->getStats();
	} else {
		ret += "No folders are being monitored\r\n";
	}
	return ret;
}

void ShareManager::validatePath(const string& realPath, const string& virtualName) const throw(ShareException) {
	if(realPath.empty() || virtualName.empty()) {
		throw ShareException(STRING(NO_DIRECTORY_SPECIFIED));
	}

	if (!SETTING(SHARE_HIDDEN) && File::isHidden(realPath)) {
		throw ShareException(STRING(DIRECTORY_IS_HIDDEN));
	}

	if(Util::stricmp(SETTING(TEMP_DOWNLOAD_DIRECTORY), realPath) == 0) {
		throw ShareException(STRING(DONT_SHARE_TEMP_DIRECTORY));
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

	if (realPath == Util::getFilePath(Util::getAppName()) || realPath == Util::getPath(Util::PATH_USER_CONFIG) || realPath == Util::getPath(Util::PATH_USER_LOCAL)) {
		throw ShareException(STRING(DONT_SHARE_APP_DIRECTORY));
	}
}

void ShareManager::getByVirtual(const string& virtualName, ProfileToken aProfile, Directory::List& dirs) const noexcept {
	for(const auto& d: rootPaths | map_values) {
		if(d->getProfileDir()->hasRootProfile(aProfile) && Util::stricmp(d->getProfileDir()->getName(aProfile), virtualName) == 0) {
			dirs.push_back(d);
		}
	}
}

void ShareManager::getByVirtual(const string& virtualName, const ProfileTokenSet& aProfiles, Directory::List& dirs) const noexcept {
	for(const auto& d: rootPaths | map_values) {
		for(auto& k: d->getProfileDir()->getRootProfiles()) {
			if(aProfiles.find(k.first) != aProfiles.end() && Util::stricmp(k.second, virtualName) == 0) {
				dirs.push_back(d);
			}
		}
	}
}

int64_t ShareManager::getShareSize(const string& realPath, ProfileToken aProfile) const noexcept {
	RLock l(cs);
	const auto j = findRoot(realPath);
	if(j != rootPaths.end()) {
		return j->second->getSize(aProfile);
	}
	return -1;

}

void ShareManager::Directory::getProfileInfo(ProfileToken aProfile, int64_t& totalSize, size_t& filesCount) const noexcept {
	totalSize += size;
	filesCount += files.size();

	for(const auto& d: directories) {
		if (d->isLevelExcluded(aProfile))
			continue;
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
			ret += d->getSize(aProfile);
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

uint8_t ShareManager::isDirShared(const string& aDir, int64_t aSize) const noexcept{
	Directory::List dirs;

	RLock l (cs);
	getDirsByName(aDir, dirs);
	if (dirs.empty())
		return 0;

	return dirs.front()->getTotalSize() == aSize ? 2 : 1;
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
	const auto directories = dirNameMap.equal_range(&p.first);
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

void ShareManager::buildTree(string& aPath, string& aPathLower, const Directory::Ptr& aDir, const ProfileDirMap& aSubRoots, DirMultiMap& aDirs, DirMap& newShares, 
	int64_t& hashSize, int64_t& addedSize, HashFileMap& tthIndexNew, ShareBloom& aBloom) noexcept {

	FileFindIter end;
	for(FileFindIter i(aPath, "*"); i != end && !aShutdown; ++i) {
		string name = i->getFileName();
		if(name.empty()) {
			LogManager::getInstance()->message("Invalid file name found while hashing folder " + aPath + ".", LogManager::LOG_WARNING);
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
			}

			ProfileDirectory::Ptr profileDir = nullptr;
			if (!aSubRoots.empty()) {
				//add excluded dirs and sub roots in our new maps
				auto p = aSubRoots.find(curPathLower);
				if (p != aSubRoots.end()) {
					if (p->second->isSet(ProfileDirectory::FLAG_ROOT) || p->second->isSet(ProfileDirectory::FLAG_EXCLUDE_PROFILE))
						profileDir = p->second;
					if (p->second->isSet(ProfileDirectory::FLAG_EXCLUDE_TOTAL))
						continue;
				}
			}

			auto dir = Directory::create(move(dualName), aDir, i->getLastWriteTime(), profileDir);

			buildTree(curPath, curPathLower, dir, aSubRoots, aDirs, newShares, hashSize, addedSize, tthIndexNew, aBloom);

			//roots will always be added
			if (profileDir && profileDir->isSet(ProfileDirectory::FLAG_ROOT)) {
				newShares[curPathLower] = dir;
			} else if (SETTING(SKIP_EMPTY_DIRS_SHARE) && dir->directories.empty() && dir->files.empty()) {
				//remove it
				aDir->directories.erase_key(dir->name.getLower());
				continue;
			}

			aDirs.emplace(const_cast<string*>(&dir->name.getLower()), dir);
			dir->addBloom(aBloom);
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
					updateIndices(*aDir, *pos.first, aBloom, addedSize, tthIndexNew);
				} else {
					hashSize += size;
				}
			} catch(const HashException&) {
			}
		}
	}
}

void ShareManager::Directory::addBloom(ShareBloom& aBloom) const noexcept {
	if (profileDir && profileDir->hasRoots()) {
		for(const auto& vName: profileDir->getRootProfiles() | map_values) {
			aBloom.add(Text::toLower(vName));
		}
	} else {
		aBloom.add(name.getLower());
	}
}

void ShareManager::updateIndices(Directory::Ptr& dir, ShareBloom& aBloom, int64_t& sharedSize, HashFileMap& tthIndex, DirMultiMap& aDirNames) noexcept {
	// add to bloom
	dir->addBloom(aBloom);
	aDirNames.emplace(const_cast<string*>(&dir->name.getLower()), dir);

	// update all sub items
	for(auto d: dir->directories) {
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

int ShareManager::refresh(const string& aDir) noexcept {
	string path = aDir;

	if(path[ path.length() -1 ] != PATH_SEPARATOR)
		path += PATH_SEPARATOR;

	StringList refreshPaths;
	string displayName;

	{
		RLock l(cs);
		const auto i = findRoot(path);
		if(i == rootPaths.end()) {
			//check if it's a virtual path

			OrderedStringSet vNames;
			for(const auto& d: rootPaths | map_values) {
				// compare all virtual names for real paths
				for(const auto& vName: d->getProfileDir()->getRootProfiles() | map_values) {
					if(Util::stricmp(vName, aDir ) == 0) {
						refreshPaths.push_back(d->getRealPath());
						vNames.insert(vName);
					}
				}
			}

			if (!refreshPaths.empty()) {
				sort(refreshPaths.begin(), refreshPaths.end());
				refreshPaths.erase(unique(refreshPaths.begin(), refreshPaths.end()), refreshPaths.end());

				if (!vNames.empty())
					displayName = Util::listToString(vNames);
			} else {
				auto d = findDirectory(aDir, false, false, false);
				if (d) {
					refreshPaths.push_back(path);
				}
			}
		} else {
			refreshPaths.push_back(path);
		}
	}

	return addRefreshTask(REFRESH_DIRS, refreshPaths, TYPE_MANUAL, displayName);
}


int ShareManager::refresh(bool incoming, RefreshType aType, function<void(float)> progressF /*nullptr*/) noexcept {
	StringList dirs;

	{
		RLock l (cs);
		for(const auto& d: rootPaths | map_values | filtered(Directory::IsParent())) {
			if (incoming && !d->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING))
				continue;
			dirs.push_back(d->getProfileDir()->getPath());
		}
	}

	return addRefreshTask(incoming ? REFRESH_INCOMING : REFRESH_ALL, dirs, aType, Util::emptyString, progressF);
}

struct ShareTask : public Task {
	ShareTask(const StringList& aDirs, const string& aDisplayName, ShareManager::RefreshType aRefreshType) : dirs(aDirs), displayName(aDisplayName), type(aRefreshType) { }
	StringList dirs;
	string displayName;
	ShareManager::RefreshType type;
};

void ShareManager::addAsyncTask(AsyncF aF) noexcept {
	tasks.add(ASYNC, unique_ptr<Task>(new AsyncTask(aF)));
	if (!refreshing.test_and_set()) {
		start();
	}
}

int ShareManager::addRefreshTask(uint8_t aTask, StringList& dirs, RefreshType aRefreshType, const string& displayName /*Util::emptyString*/, function<void (float)> progressF /*nullptr*/) noexcept {
	if (dirs.empty()) {
		return REFRESH_PATH_NOT_FOUND;
	}

	{
		Lock l(tasks.cs);
		auto& tq = tasks.getTasks();
		if (aTask == REFRESH_ALL) {
			//don't queue multiple full refreshes
			const auto p = find_if(tq, [](const TaskQueue::UniqueTaskPair& tp) { return tp.first == REFRESH_ALL; });
			if (p != tq.end())
				return REFRESH_ALREADY_QUEUED;
		} else {
			//remove directories that have already been queued for refreshing
			for(const auto& i: tq) {
				if (i.first != ASYNC) {
					auto t = static_cast<ShareTask*>(i.second.get());
					dirs.erase(boost::remove_if(dirs, [t](const string& s) { return boost::find(t->dirs, s) != t->dirs.end(); }), dirs.end());
				}
			}
		}
	}

	if (dirs.empty()) {
		return REFRESH_ALREADY_QUEUED;
	}

	tasks.add(aTask, unique_ptr<Task>(new ShareTask(dirs, displayName, aRefreshType)));

	if(refreshing.test_and_set()) {
		if (aRefreshType != TYPE_STARTUP_DELAYED) {
			//this is always called from the task thread...
			string msg;
			switch (aTask) {
			case(REFRESH_ALL) :
				msg = STRING(REFRESH_QUEUED);
				break;
			case(REFRESH_DIRS) :
				if (!displayName.empty()) {
					msg = STRING_F(VIRTUAL_REFRESH_QUEUED, displayName);
				}
				else if (dirs.size() == 1) {
					msg = STRING_F(DIRECTORY_REFRESH_QUEUED, *dirs.begin());
				}
				break;
			case(ADD_DIR) :
				if (dirs.size() == 1) {
					msg = STRING_F(ADD_DIRECTORY_QUEUED, *dirs.begin());
				}
				else {
					msg = STRING_F(ADD_DIRECTORIES_QUEUED, dirs.size());
				}
				break;
			case(REFRESH_INCOMING) :
				msg = STRING(INCOMING_REFRESH_QUEUED);
				break;
			};

			if (!msg.empty()) {
				LogManager::getInstance()->message(msg, LogManager::LOG_INFO);
			}
		}
		return REFRESH_IN_PROGRESS;
	}

	if(aRefreshType == TYPE_STARTUP_BLOCKING && aTask == REFRESH_ALL) { 
		runTasks(progressF);
	} else {
		try {
			start();
			setThreadPriority(aRefreshType == TYPE_MANUAL ? Thread::NORMAL : Thread::IDLE);
		} catch(const ThreadException& e) {
			LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_FAILED) + " " + e.getError(), LogManager::LOG_WARNING);
			refreshing.clear();
		}
	}

	return REFRESH_STARTED;
}

void ShareManager::getParentPaths(StringList& aDirs) const noexcept {
	//removes subroots from shared dirs
	RLock l (cs);
	for(const auto& dp: rootPaths) {
		if (find_if(rootPaths | map_keys, [&dp](const string& aPath) { return AirUtil::isSub(dp.first, aPath); }).base() == rootPaths.end())
			aDirs.push_back(dp.second->getProfileDir()->getPath());
	}
}

ShareManager::ProfileDirMap ShareManager::getSubProfileDirs(const string& aPath) const noexcept{
	ProfileDirMap aRoots;
	for(const auto& i: profileDirs) {
		if (AirUtil::isSub(i.first, aPath)) {
			aRoots[i.second->getPath()] = i.second;
		}
	}

	return aRoots;
}

void ShareManager::addProfiles(const ShareProfileInfo::List& aProfiles) noexcept{
	WLock l(cs);
	for (auto& sp : aProfiles) {
		shareProfiles.emplace(shareProfiles.end()-1, new ShareProfile(sp->name, sp->token));
	}
}

void ShareManager::removeProfiles(const ShareProfileInfo::List& aProfiles) noexcept{
	WLock l(cs);
	for (auto& sp : aProfiles) {
		shareProfiles.erase(remove(shareProfiles.begin(), shareProfiles.end(), sp->token), shareProfiles.end());
	}
}

void ShareManager::renameProfiles(const ShareProfileInfo::List& aProfiles) noexcept {
	WLock l(cs);
	for (auto& sp : aProfiles) {
		auto p = find(shareProfiles.begin(), shareProfiles.end(), sp->token);
		if (p != shareProfiles.end()) {
			(*p)->setPlainName(sp->name);
		}
	}

	// sort the profiles
	/*ShareProfileList newProfiles;
	for (auto& sp : aProfiles) {
		auto p = find(shareProfiles.begin(), shareProfiles.end(), sp->token);
		if (p != shareProfiles.end()) {
			newProfiles.push_back(*p);
		}
	}

	shareProfiles = newProfiles;*/
}

void ShareManager::addDirectories(const ShareDirInfo::List& aNewDirs) noexcept {
	StringList add, refresh;
	ProfileTokenSet dirtyProfiles;

	{
		WLock l (cs);
		for(const auto& d: aNewDirs) {
			const auto& sdiPath = d->path;
			auto i = findRoot(sdiPath);
			if (i != rootPaths.end()) {
				// Trying to share an already shared root
				i->second->getProfileDir()->addRootProfile(d->vname, d->profile);
				dirtyProfiles.insert(d->profile);
			} else {
				auto p = find_if(rootPaths | map_keys, [&sdiPath](const string& path) { return AirUtil::isSub(sdiPath, path); });
				if (p.base() != rootPaths.end()) {
					// It's a subdir
					auto dir = findDirectory(sdiPath, false, false);
					if (dir) {
						if (dir->getProfileDir()) {
							//an existing subroot exists
							dcassert(dir->getProfileDir()->hasExcludes());
							dir->getProfileDir()->addRootProfile(d->vname, d->profile);
						} else {
							auto root = ProfileDirectory::Ptr(new ProfileDirectory(sdiPath, d->vname, d->profile, d->incoming));
							dir->setProfileDir(root);
							profileDirs[sdiPath] = root;
						}
						addRoot(sdiPath, dir);
						dirtyProfiles.insert(d->profile);
					} else {
						//this is probably in an excluded dirs of an existing root, add it
						dir = findDirectory(sdiPath, true, true, false);
						if (dir) {
							auto root = ProfileDirectory::Ptr(new ProfileDirectory(sdiPath, d->vname, d->profile, d->incoming));
							dir->setProfileDir(root);
							profileDirs[sdiPath] = root;
							addRoot(sdiPath, dir);
							if (find_if(aNewDirs, [p](const ShareDirInfoPtr& aSDI) { return Util::stricmp(aSDI->path, *p) == 0; }) == aNewDirs.end())
								refresh.push_back(*p); //refresh the top directory unless it's also added now.....
						}
					}
				} else {
					// It's a new parent, will be handled in the task thread
					auto root = ProfileDirectory::Ptr(new ProfileDirectory(sdiPath, d->vname, d->profile, d->incoming));
					//root->setFlag(ProfileDirectory::FLAG_ADD);
					Directory::Ptr dp = Directory::create(Util::getLastDir(sdiPath), nullptr, File::getLastModified(sdiPath), root);
					addRoot(sdiPath, dp);
					profileDirs[sdiPath] = root;
					addDirName(dp);
					add.push_back(sdiPath);
				}
			}
		}
	}

	rebuildTotalExcludes();
	if (!refresh.empty())
		addRefreshTask(REFRESH_DIRS, refresh, TYPE_MANUAL);

	if (add.empty()) {
		//we are only modifying existing trees
		setProfilesDirty(dirtyProfiles, true);
		return;
	}

	addRefreshTask(ADD_DIR, add, TYPE_MANUAL);
}

void ShareManager::removeDirectories(const ShareDirInfo::List& aRemoveDirs) noexcept{
	ProfileTokenSet dirtyProfiles;
	StringList stopHashing;
	StringList removeMonitors;

	{
		WLock l (cs);
		for(const auto& rd: aRemoveDirs) {
			auto k = findRoot(rd->path);
			if (k != rootPaths.end()) {
				dirtyProfiles.insert(rd->profile);

				auto sd = k->second;
				if (sd->getProfileDir()->removeRootProfile(rd->profile)) {
					//can we remove the profile dir?
					if (!sd->getProfileDir()->hasExcludes()) {
						profileDirs.erase(rd->path);
					}

					stopHashing.push_back(rd->path);
					rootPaths.erase(k);
					if (sd->getParent()) {
						//the content still stays shared.. just null the profile
						sd->setProfileDir(nullptr);
						continue;
					}

					if (sd->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING)) {
						removeMonitors.push_back(rd->path);
					}

					cleanIndices(*sd);
					File::deleteFile(sd->getProfileDir()->getCacheXmlPath());

					//no parent directories, get all child roots for this
					Directory::List subDirs;
					for(auto& sdp: rootPaths) {
						if(AirUtil::isSub(sdp.first, rd->path)) {
							subDirs.push_back(sdp.second);
						}
					}

					//check the folder levels
					size_t minLen = std::numeric_limits<size_t>::max();
					for (auto& d: subDirs) {
						minLen = min(Util::getParentDir(d->getProfileDir()->getPath()).length(), minLen);
					}

					//update our new parents
					for (auto& d: subDirs) {
						if (Util::getParentDir(d->getProfileDir()->getPath()).length() == minLen) {
							d->setParent(nullptr);
							d->getProfileDir()->setCacheDirty(true);
							updateIndices(d, *bloom.get(), sharedSize, tthIndex, dirNameMap);
						}
					}
				}
			}
		}
	}

	if (stopHashing.size() == 1)
		LogManager::getInstance()->message(STRING_F(SHARED_DIR_REMOVED, stopHashing.front()), LogManager::LOG_INFO);
	else if (!stopHashing.empty())
		LogManager::getInstance()->message(STRING_F(X_SHARED_DIRS_REMOVED, stopHashing.size()), LogManager::LOG_INFO);

	for(const auto& p: stopHashing) {
		HashManager::getInstance()->stopHashing(p);
	}

	removeMonitoring(removeMonitors);

	rebuildTotalExcludes();
	setProfilesDirty(dirtyProfiles, true);
}

void ShareManager::changeDirectories(const ShareDirInfo::List& changedDirs) noexcept {
	//updates the incoming status and the virtual name (called from GUI)

	ProfileTokenSet dirtyProfiles;
	StringList monAdd;
	StringList monRem;

	{
		WLock l(cs);
		for(const auto& cd: changedDirs) {
			string vName = validateVirtual(cd->vname);
			dirtyProfiles.insert(cd->profile);

			auto p = findRoot(cd->path);
			if (p != rootPaths.end()) {
				p->second->getProfileDir()->addRootProfile(vName, cd->profile); //renames it really

				// change the incoming state
				if (!cd->incoming) {
					if (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_INCOMING && p->second->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING))
						monRem.push_back(cd->path);
					p->second->getProfileDir()->unsetFlag(ProfileDirectory::FLAG_INCOMING);
				} else {
					if (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_INCOMING && !p->second->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING))
						monAdd.push_back(cd->path);
					p->second->getProfileDir()->setFlag(ProfileDirectory::FLAG_INCOMING);
				}
			}
		}
	}

	addMonitoring(monAdd);
	removeMonitoring(monRem);
	setProfilesDirty(dirtyProfiles);
}

void ShareManager::rebuildMonitoring() noexcept {
	StringList monAdd;
	StringList monRem;

	{
		RLock l(cs);
		for(auto& dp: rootPaths) {
			if (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_DISABLED || (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_INCOMING && !dp.second->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING))) {
				monRem.push_back(dp.first);
			} else if (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_ALL || (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_INCOMING && dp.second->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING))) {
				monAdd.push_back(dp.first);
			}
		}
	}

	addMonitoring(monAdd);
	removeMonitoring(monRem);
}

void ShareManager::reportTaskStatus(uint8_t aTask, const StringList& directories, bool finished, int64_t aHashSize, const string& displayName, RefreshType aRefreshType) const noexcept {
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
		LogManager::getInstance()->message(msg, LogManager::LOG_INFO);
	}
}

int ShareManager::run() {
	runTasks();
	return 0;
}

ShareManager::RefreshInfo::~RefreshInfo() {

}

ShareManager::RefreshInfo::RefreshInfo(const string& aPath, const Directory::Ptr& aOldRoot, uint64_t aLastWrite) : path(aPath), oldRoot(aOldRoot), addedSize(0), hashSize(0) {
	subProfiles = getInstance()->getSubProfileDirs(aPath);

	//create the new root
	root = Directory::create(Util::getLastDir(aPath), nullptr, aLastWrite, aOldRoot ? aOldRoot->getProfileDir() : nullptr);
	if (aOldRoot && aOldRoot->getProfileDir() && aOldRoot->getProfileDir()->isSet(ProfileDirectory::FLAG_ROOT)) {
		rootPathsNew[Text::toLower(aPath)] = root;
	}

	dirNameMapNew.emplace(const_cast<string*>(&root->name.getLower()), root);
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
		QueueManager::getInstance()->checkRefreshPaths(bundleDirs, task->dirs);
		if (task->dirs.empty()) {
			continue;
		}

		StringList monitoring;
		vector<shared_ptr<RefreshInfo>> refreshDirs;

		//find excluded dirs and sub-roots for each directory being refreshed (they will be passed on to buildTree for matching)
		{
			RLock l (cs);
			for(auto& i: task->dirs) {
				auto d = findRoot(i);
				if (d != rootPaths.end()) {
					refreshDirs.emplace_back(new RefreshInfo(i, d->second, File::getLastModified(i)));
					
					//a monitored dir?
					if (t.first == ADD_DIR && (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_ALL || (SETTING(MONITORING_MODE) == SettingsManager::MONITORING_INCOMING && d->second->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING))))
						monitoring.push_back(i);
				} else {
					auto curDir = findDirectory(i, false, false, false);

					//curDir may also be nullptr
					refreshDirs.emplace_back(new RefreshInfo(i, curDir, File::getLastModified(i)));
				}
			}
		}

		reportTaskStatus(t.first, task->dirs, false, 0, task->displayName, task->type);
		if (t.first == REFRESH_INCOMING) {
			lastIncomingUpdate = GET_TICK();
		} else if (t.first == REFRESH_ALL) {
			lastFullUpdate = GET_TICK();
			lastIncomingUpdate = GET_TICK();
		}

		//build the new tree
		atomic<long> progressCounter(0);
		const size_t dirCount = refreshDirs.size();


		//bloom
		ShareBloom* refreshBloom = t.first == REFRESH_ALL ? new ShareBloom(1<<20) : bloom.get();

		auto doRefresh = [&](RefreshInfoPtr& i) {
			auto& ri = *i;
			//if (checkHidden(ri.path)) {
				auto pathLower = Text::toLower(ri.path);
				auto path = ri.path;
				ri.root->addBloom(*refreshBloom);
				buildTree(path, pathLower, ri.root, ri.subProfiles, ri.dirNameMapNew, ri.rootPathsNew, ri.hashSize, ri.addedSize, ri.tthIndexNew, *refreshBloom);
				dcassert(ri.path == path);
			//}

			if(progressF) {
				progressF(static_cast<float>(progressCounter++) / static_cast<float>(dirCount));
			}
		};

		try {
			if (SETTING(REFRESH_THREADING) == SettingsManager::MULTITHREAD_ALWAYS || (SETTING(REFRESH_THREADING) == SettingsManager::MULTITHREAD_MANUAL && (task->type == TYPE_MANUAL || task->type == TYPE_STARTUP_BLOCKING))) {
				parallel_for_each(refreshDirs.begin(), refreshDirs.end(), doRefresh);
			} else {
				for_each(refreshDirs, doRefresh);
			}
		} catch (std::exception& e) {
			LogManager::getInstance()->message("Refresh failed: " + string(e.what()), LogManager::LOG_INFO);
			continue;
		}

		if (aShutdown)
			break;

		int64_t totalHash=0;
		ProfileTokenSet dirtyProfiles;

		//append the changes
		{		
			WLock l(cs);
			if(t.first != REFRESH_ALL) {
				for(auto p = refreshDirs.begin(); p != refreshDirs.end(); ) {
					auto& ri = **p;

					//recursively remove the content of this dir from TTHIndex and dir name list
					if (ri.oldRoot)
						cleanIndices(*ri.oldRoot);

					//clear this path and its children from root paths
					for(auto i = rootPaths.begin(); i != rootPaths.end(); ) {
						if (AirUtil::isParentOrExact(ri.path, i->first)) {
							if (t.first == ADD_DIR && AirUtil::isSub(i->first, ri.root->getProfileDir()->getPath()) && !i->second->getParent()) {
								//in case we are adding a new parent
								File::deleteFile(i->second->getProfileDir()->getCacheXmlPath());
								cleanIndices(*i->second);
							}

							i = rootPaths.erase(i);
						} else {
							i++;
						}
					}

					//set the parent for refreshed subdirectories
					if (!ri.oldRoot || ri.oldRoot->getParent()) {
						Directory::Ptr parent = nullptr;
						if (!ri.oldRoot) {
							//get the parent
							parent = findDirectory(Util::getParentDir(ri.path), true, true, true);
							if (!parent) {
								p = refreshDirs.erase(p);
								continue;
							}
						} else {
							parent = ri.oldRoot->getParent();
						}

						//set the parent
						ri.root->setParent(parent.get());
						parent->directories.erase_key(ri.root->name.getLower());
						parent->directories.insert_sorted(ri.root);
						parent->updateModifyDate();
					}

					p++;
				}

				bloom->merge(*refreshBloom);
				mergeRefreshChanges(refreshDirs, dirNameMap, rootPaths, tthIndex, totalHash, sharedSize, &dirtyProfiles);
			} else {
				int64_t totalAdded=0;
				DirMultiMap newDirNames;
				DirMap newRoots;
				HashFileMap newTTHs;

				mergeRefreshChanges(refreshDirs, newDirNames, newRoots, newTTHs, totalHash, totalAdded, &dirtyProfiles);

				rootPaths.swap(newRoots);
				dirNameMap.swap(newDirNames);
				tthIndex.swap(newTTHs);

				sharedSize = totalAdded;
				bloom.reset(refreshBloom);
			}
		}

		setProfilesDirty(dirtyProfiles, task->type == TYPE_MANUAL || t.first == REFRESH_ALL);
			
		if (task->type == TYPE_MANUAL)
			ClientManager::getInstance()->infoUpdated();

		reportTaskStatus(t.first, task->dirs, true, totalHash, task->displayName, task->type);

		addMonitoring(monitoring);
	}

	{
		WLock l (dirNames);
		bundleDirs.clear();
	}
	refreshRunning = false;
	refreshing.clear();
}

void ShareManager::on(TimerManagerListener::Second, uint64_t /*tick*/) noexcept {
	while (monitor->dispatch()) {
		//...
	}
}

void ShareManager::on(TimerManagerListener::Minute, uint64_t tick) noexcept {
	if(lastSave == 0 || lastSave + 15*60*1000 <= tick) {
		saveXmlList();
	}

	if(SETTING(AUTO_REFRESH_TIME) > 0 && lastFullUpdate + SETTING(AUTO_REFRESH_TIME) * 60 * 1000 <= tick) {
		lastIncomingUpdate = tick;
		lastFullUpdate = tick;
		refresh(false, TYPE_SCHEDULED);
	} else if(SETTING(INCOMING_REFRESH_TIME) > 0 && lastIncomingUpdate + SETTING(INCOMING_REFRESH_TIME) * 60 * 1000 <= tick) {
		lastIncomingUpdate = tick;
		refresh(true, TYPE_SCHEDULED);
	}

	handleChangedFiles(tick, false);
}

void ShareManager::getShares(ShareDirInfo::Map& aDirs) const noexcept {
	RLock l (cs);
	for(const auto& d: rootPaths | map_values) {
		const auto& profiles = d->getProfileDir()->getRootProfiles();
		for(const auto& pd: profiles) {
			auto sdi = new ShareDirInfo(pd.second, pd.first, d->getRealPath(), d->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING));
			sdi->size = d->getSize(pd.first);
			aDirs[pd.first].push_back(sdi);
		}
	}

}

/*size_t ShareManager::getSharedFiles(ProfileToken aProfile) const noexcept {
	return boost::count_if(tthIndex | map_values, [aProfile](Directory::File::Set::const_iterator f) { return f->getParent()->hasProfile(aProfile); });
}*/
		
void ShareManager::getBloom(HashBloom& bloom) const noexcept {
	RLock l(cs);
	for(const auto tth: tthIndex | map_keys)
		bloom.add(*tth);

	for(const auto& tth: tempShares | map_keys)
		bloom.add(tth);
}

string ShareManager::generateOwnList(ProfileToken aProfile) throw(ShareException) {
	FileList* fl = generateXmlList(aProfile, true);
	return fl->getFileName();
}


//forwards the calls to createFileList for creating the filelist that was reguested.
FileList* ShareManager::generateXmlList(ProfileToken aProfile, bool forced /*false*/) throw(ShareException) {
	FileList* fl = nullptr;

	{
		WLock l(cs);
		const auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
		if(i == shareProfiles.end()) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		fl = (*i)->getProfileList() ? (*i)->getProfileList() : (*i)->generateProfileList();
	}


	{
		Lock l(fl->cs);
		if (fl->allowGenerateNew(forced)) {
			auto tmpName = fl->getFileName().substr(0, fl->getFileName().length() - 4);
			try {
				{
					File f(tmpName, File::RW, File::TRUNCATE | File::CREATE, File::BUFFER_SEQUENTIAL, false);

					f.write(SimpleXML::utf8Header);
					f.write("<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"/\" Generator=\"DC++ " DCVERSIONSTRING "\">\r\n");

					string tmp;
					string indent = "\t";

					auto root = new FileListDir(Util::emptyString, 0, 0);

					{
						RLock l(cs);
						for (const auto& d : rootPaths | map_values | filtered(Directory::HasRootProfile(aProfile))) {
							d->toFileList(root, aProfile, true);
						}

						for (const auto it2 : root->listDirs | map_values) {
							it2->toXml(f, indent, tmp, true);
						}
					}

					delete root;

					f.write("</FileListing>");
					f.flush();

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
			} catch (const Exception&) {
				// No new file lists...
				fl->generationFinished(true);
			}

			File::deleteFile(tmpName);
		}
	}
	return fl;
}

MemoryInputStream* ShareManager::generatePartialList(const string& dir, bool recurse, ProfileToken aProfile) const noexcept {
	if(dir.front() != '/' || dir.back() != '/')
		return 0;

	string xml = SimpleXML::utf8Header;

	{
		unique_ptr<FileListDir> root(new FileListDir(Util::emptyString, 0, 0));
		string tmp;

		RLock l(cs);

		if(dir == "/") {
			dcdebug("Generating partial from root folders");
			for(const auto& sd: rootPaths | map_values) {
				if(sd->getProfileDir()->hasRootProfile(aProfile)) {
					sd->toFileList(root.get(), aProfile, recurse);
				}
			}
		} else {
			//dcdebug("wanted %s \n", dir);
			try {
				Directory::List result;
				findVirtuals<ProfileToken>(dir, aProfile, result); 
				if (!result.empty()) {
					root->shareDirs = result;

					//add the subdirs
					for(const auto& it: result) {
						for(const auto& d: it->directories) { 
							if (!d->isLevelExcluded(aProfile))
								d->toFileList(root.get(), aProfile, recurse); 
						}
						root->date = max(root->date, it->getLastWrite());
					}
				}
			} catch(...) {
				return nullptr;
			}
		}

		xml += "<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + 
			"\" Base=\"" + SimpleXML::escape(dir, tmp, false) + 
			"\" BaseDate=\"" + Util::toString(root->date) +
			"\" Generator=\"DC++ " DCVERSIONSTRING "\">\r\n";

		StringOutputStream sos(xml);
		string indent = "\t";

		for(const auto ld: root->listDirs | map_values)
			ld->toXml(sos, indent, tmp, recurse);

		root->filesToXml(sos, indent, tmp, !recurse);
		sos.write("</FileListing>");
	}

	if (xml.empty()) {
		dcdebug("Partial NULL");
		return nullptr;
	} else {
		dcdebug("Partial list Generated.");
		return new MemoryInputStream(xml);
	}
}

void ShareManager::Directory::toFileList(FileListDir* aListDir, ProfileToken aProfile, bool isFullList) {
	auto n = getVirtualName(aProfile);

	FileListDir* newListDir = nullptr;
	auto pos = aListDir->listDirs.find(n);
	if (pos != aListDir->listDirs.end()) {
		newListDir = pos->second;
		if (!isFullList)
			newListDir->size += getSize(aProfile);
		newListDir->date = max(newListDir->date, lastWrite);
	} else {
		newListDir = new FileListDir(n, isFullList ? 0 : getSize(aProfile), lastWrite);
		aListDir->listDirs.emplace(n, newListDir);
	}

	newListDir->shareDirs.push_back(this);

	if (isFullList) {
		for(auto& d: directories) {
			if (!d->isLevelExcluded(aProfile)) 
				d->toFileList(newListDir, aProfile, isFullList);
		}
	}
}

ShareManager::FileListDir::FileListDir(const string& aName, int64_t aSize, int aDate) : name(aName), size(aSize), date(aDate) { }

#define LITERAL(n) n, sizeof(n)-1
void ShareManager::FileListDir::toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool fullList) const {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(name, tmp2, true));
	if (!fullList) {
		xmlFile.write(LITERAL("\" Size=\""));
		xmlFile.write(Util::toString(size));
	}
	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(Util::toString(date));

	if(fullList) {
		xmlFile.write(LITERAL("\">\r\n"));

		indent += '\t';
		for(const auto& d: listDirs | map_values) {
			d->toXml(xmlFile, indent, tmp2, fullList);
		}

		filesToXml(xmlFile, indent, tmp2, !fullList);

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

		LogManager::getInstance()->message(STRING_F(DUPLICATE_FILES_DETECTED, dupeFiles % Util::toString(", ", paths)), LogManager::LOG_WARNING);
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
	//if (name)
	//	delete name;
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

#define LITERAL(n) n, sizeof(n)-1

void ShareManager::saveXmlList(bool verbose /*false*/, function<void(float)> progressF /*nullptr*/) noexcept {

	if(xml_saving)
		return;

	xml_saving = true;

	if (progressF)
		progressF(0);

	int cur = 0;
	Directory::List dirtyDirs;

	{
		RLock l(cs);
		//boost::algorithm::copy_if(
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
						child->toXmlList(xmlFile, d->getProfileDir()->getPath() + child->name.getNormal() + PATH_SEPARATOR, indent, tmp);
					}
					d->filesToXmlList(xmlFile, indent, tmp);

					xmlFile.write(LITERAL("</Share>"));
					xmlFile.flush();
					ff.close();

					File::deleteFile(path);
					File::renameFile(path + ".tmp", path);
				} catch (Exception& e) {
					LogManager::getInstance()->message("Error saving " + path + ": " + e.getError(), LogManager::LOG_WARNING);
				}

				d->getProfileDir()->setCacheDirty(false);
				if (progressF) {
					cur++;
					progressF(static_cast<float>(cur) / static_cast<float>(dirtyDirs.size()));
				}
			});
		} catch (std::exception& e) {
			LogManager::getInstance()->message("Saving the share cache failed: " + string(e.what()), LogManager::LOG_INFO);
		}
	}

	xml_saving = false;
	lastSave = GET_TICK();
	if (verbose)
		LogManager::getInstance()->message("Share cache saved.", LogManager::LOG_INFO);
}

void ShareManager::Directory::toXmlList(OutputStream& xmlFile, string&& path, string& indent, string& tmp) {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(name.lowerCaseOnly() ? name.getLower() : name.getNormal(), tmp, true));

	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(SimpleXML::escape(Util::toString(lastWrite), tmp, true));
	xmlFile.write(LITERAL("\">\r\n"));

	indent += '\t';
	filesToXmlList(xmlFile, indent, tmp);

	for(const auto& d: directories) {
		d->toXmlList(xmlFile, path + d->name.getLower() + PATH_SEPARATOR, indent, tmp);
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

bool ShareManager::addDirResult(const string& aPath, SearchResultList& aResults, ProfileToken aProfile, SearchQuery& srch) const noexcept {
	const string path = srch.addParents ? (Util::getNmdcParentDir(aPath)) : aPath;

	//have we added it already?
	auto p = find_if(aResults, [&path](const SearchResultPtr& sr) { return sr->getPath() == path; });
	if (p != aResults.end())
		return false;

	//get all dirs with this path
	Directory::List result;

	try {
		findVirtuals<ProfileToken>(Util::toAdcFile(path), aProfile, result);
	} catch(...) {
		dcassert(0);
	}

	uint64_t date = 0;
	int64_t size = 0;
	size_t files = 0, folders = 0;
	for(const auto& d: result) {
		if (!d->isLevelExcluded(aProfile)) {
			d->getResultInfo(aProfile, size, files, folders);
			date = max(date, d->getLastWrite());
		}
	}

	if (srch.matchesDate(date)) {
		SearchResultPtr sr(new SearchResult(SearchResult::TYPE_DIRECTORY, size, path, TTHValue(), date, files, folders));
		aResults.push_back(sr);
		return true;
	}

	return false;
}

void ShareManager::Directory::File::addSR(SearchResultList& aResults, ProfileToken aProfile, bool addParent) const noexcept {
	if (addParent) {
		//getInstance()->addDirResult(getFullName(aProfile), aResults, aProfile, true);
		SearchResultPtr sr(new SearchResult(parent->getFullName(aProfile)));
		aResults.push_back(sr);
	} else {
		SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, 
			size, getFullName(aProfile), getTTH(), getLastWrite(), 1));
		aResults.push_back(sr);
	}
}

void ShareManager::nmdcSearch(SearchResultList& l, const string& nmdcString, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults, bool aHideShare) noexcept{
	auto query = SearchQuery(nmdcString, aSearchType, aSize, aFileType);
	search(l, query, maxResults, aHideShare ? SP_HIDDEN : SETTING(DEFAULT_SP), CID(), Util::emptyString);
}

/**
* Alright, the main point here is that when searching, a search string is most often found in
* the filename, not directory name, so we want to make that case faster. Also, we want to
* avoid changing StringLists unless we absolutely have to --> this should only be done if a string
* has been matched in the directory name. This new stringlist should also be used in all descendants,
* but not the parents...
*/
void ShareManager::Directory::search(SearchResultList& aResults, SearchQuery& aStrings, StringList::size_type maxResults, ProfileToken aProfile) const noexcept {
	for(const auto& i: aStrings.exclude) {
		if(i.matchLower(profileDir ? Text::toLower(profileDir->getName(aProfile)) : name.getLower()))
			return;
	}

	StringSearch::List* old = aStrings.include;

	// Find any matches in the directory name
	unique_ptr<StringSearch::List> newStr(aStrings.matchesDirectoryReLower(profileDir ? Text::toLower(profileDir->getName(aProfile)) : name.getLower()));
	if(newStr.get() != 0 && aStrings.matchType == SearchQuery::MATCH_FULL_PATH) {
		aStrings.include = newStr.get();
	}

	bool sizeOk = (aStrings.gt == 0) && aStrings.matchesDate(lastWrite);
	if((aStrings.include->empty() || (newStr.get() && newStr.get()->empty())) && aStrings.ext.empty() && sizeOk && aStrings.itemType != SearchQuery::TYPE_FILE) {
		// We satisfied all the search words! Add the directory...
		getInstance()->addDirResult(getFullName(aProfile), aResults, aProfile, aStrings);
	}

	if(aStrings.itemType != SearchQuery::TYPE_DIRECTORY) {
		for(const auto& f: files) {
			if (!aStrings.matchesFileLower(f->name.getLower(), f->getSize(), f->getLastWrite())) {
				continue;
			}


			f->addSR(aResults, aProfile, aStrings.addParents);

			if(aResults.size() >= maxResults) {
				return;
			}

			if (aStrings.addParents)
				break;
		}
	}

	for(auto l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
		if ((*l)->isLevelExcluded(aProfile))
			continue;
		(*l)->search(aResults, aStrings, maxResults, aProfile);
	}

	aStrings.include = old;
}

void ShareManager::search(SearchResultList& results, SearchQuery& srch, StringList::size_type maxResults, ProfileToken aProfile, const CID& cid, const string& aDir) throw(ShareException) {
	totalSearches++;
	if (aProfile == SP_HIDDEN) {
		return;
	}

	RLock l(cs);
	if(srch.root) {
		const auto i = tthIndex.equal_range(const_cast<TTHValue*>(&(*srch.root)));
		for(auto& f: i | map_values) {
			if (f->hasProfile(aProfile) && AirUtil::isParentOrExact(aDir, f->getADCPath(aProfile))) {
				f->addSR(results, aProfile, srch.addParents);
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

	if (!bloom->match(srch.includeInit))
		return;

	if (srch.itemType == SearchQuery::TYPE_DIRECTORY && srch.matchType == SearchQuery::MATCH_EXACT) {
		const auto i = dirNameMap.equal_range(const_cast<string*>(&srch.includeInit.front().getPattern()));
		for(const auto& d: i | map_values) {
			auto path = d->getADCPath(aProfile);
			if (d->hasProfile(aProfile) && AirUtil::isParentOrExact(aDir, path) && srch.matchesDate(d->getLastWrite()) && addDirResult(path, results, aProfile, srch)) {
				return;
			}
		}

		return;
	}

	// get the roots
	Directory::List roots;
	if (aDir.empty() || aDir == "/") {
		copy(rootPaths | map_values | filtered(Directory::HasRootProfile(aProfile)), back_inserter(roots));
	} else {
		findVirtuals<ProfileToken>(aDir, aProfile, roots);
	}

	// go them through recursively
	for (auto d = roots.begin(); (d != roots.end()) && (results.size() < maxResults); ++d) {
		(*d)->search(results, srch, maxResults, aProfile);
	}
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

void ShareManager::addDirName(Directory::Ptr& dir) noexcept {
#ifdef _DEBUG
	auto directories = dirNameMap.equal_range(const_cast<string*>(&dir->name.getLower()));
	auto p = find(directories | map_values, dir);
	dcassert(p.base() == directories.second);
#endif
	dirNameMap.emplace(const_cast<string*>(&dir->name.getLower()), dir);
}

void ShareManager::removeDirName(Directory& dir) noexcept {
	auto directories = dirNameMap.equal_range(const_cast<string*>(&dir.name.getLower()));
	auto p = find_if(directories | map_values, [&dir](const Directory::Ptr& d) { return d.get() == &dir; });
	if (p.base() != dirNameMap.end())
		dirNameMap.erase(p.base());
	else
		dcassert(0);
}

void ShareManager::cleanIndices(Directory& dir) noexcept {
	for(auto& d: dir.directories) {
		cleanIndices(*d);
	}

	//remove from the name map
	removeDirName(dir);

	//remove all files
	for(auto i = dir.files.begin(); i != dir.files.end(); ++i) {
		cleanIndices(dir, *i);
	}
}

void ShareManager::on(QueueManagerListener::BundleAdded, const BundlePtr& aBundle) noexcept {
	WLock l (dirNames);
	bundleDirs.insert(upper_bound(bundleDirs.begin(), bundleDirs.end(), aBundle->getTarget()), aBundle->getTarget());
}

void ShareManager::on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept {
	if (aBundle->getStatus() == Bundle::STATUS_MOVED) {
		//we don't want any monitoring actions for this folder...
		string path = aBundle->getTarget();
		monitor->callAsync([=] { removeNotifications(path); });
	} else if (aBundle->getStatus() == Bundle::STATUS_HASHED) {
		StringList dirs;
		dirs.push_back(aBundle->getTarget());
		addRefreshTask(ADD_BUNDLE, dirs, TYPE_BUNDLE, aBundle->getTarget());
	}
}

void ShareManager::removeNotifications(const string& aPath) noexcept {
	auto p = findModifyInfo(aPath);
	if (p != fileModifications.end())
		removeNotifications(p, aPath);
}

bool ShareManager::allowAddDir(const string& aPath) const noexcept {
	{
		RLock l(cs);
		const auto mi = find_if(rootPaths | map_keys, IsParentOrExact<true>(aPath));
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
				if (m != profileDirs.end() && m->second->isSet(ProfileDirectory::FLAG_EXCLUDE_TOTAL)) {
					return false;
				}
			}
			return true;
		}
	}
	return false;
}

ShareManager::Directory::Ptr ShareManager::findDirectory(const string& fname, bool allowAdd, bool report, bool checkExcludes /*true*/) noexcept {
	auto mi = find_if(rootPaths | map_keys, IsParentOrExact<true>(fname)).base();
	if (mi != rootPaths.end()) {
		auto curDir = mi->second;
		StringList sl = StringTokenizer<string>(fname.substr(mi->first.length()), PATH_SEPARATOR).getTokens();
		string fullPathLower = mi->first;
		int pathPos = mi->first.length();

		for(const auto& name: sl) {
			DualString dualName(name);
			pathPos += name.length()+1;
			fullPathLower += dualName.getLower() + PATH_SEPARATOR;
			auto j = curDir->directories.find(dualName.getLower());
			if (j != curDir->directories.end()) {
				curDir = *j;
			} else if (!allowAdd || !checkSharedName(fname.substr(0, pathPos), fullPathLower, true, report)) {
				return nullptr;
			} else {
				auto m = profileDirs.find(fullPathLower);
				if (checkExcludes && m != profileDirs.end() && m->second->isSet(ProfileDirectory::FLAG_EXCLUDE_TOTAL)) {
					return nullptr;
				}

				curDir->updateModifyDate();
				curDir = Directory::create(move(dualName), curDir, File::getLastModified(fullPathLower), m != profileDirs.end() ? m->second : nullptr);
				addDirName(curDir);
				curDir->addBloom(*bloom.get());
			}
		}
		return curDir;
	}
	return nullptr;
}

void ShareManager::onFileHashed(const string& fname, HashedFile& fileInfo) noexcept {
	ProfileTokenSet dirtyProfiles;
	{
		WLock l(cs);
		Directory::Ptr d = findDirectory(Util::getFilePath(fname), true, false);
		if (!d) {
			return;
		}

		addFile(Util::getFileName(fname), d, fileInfo, dirtyProfiles);
	}

	setProfilesDirty(dirtyProfiles);
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

void ShareManager::getExcludes(ProfileToken aProfile, StringList& excludes) const noexcept {
	for(const auto& i: profileDirs) {
		if (i.second->isExcluded(aProfile))
			excludes.push_back(i.first);
	}
}

ShareProfileInfo::List ShareManager::getProfileInfos() const noexcept {
	ShareProfileInfo::List ret;
	for (const auto& sp : shareProfiles) {
		if (sp->getToken() != SP_HIDDEN) {
			auto p = new ShareProfileInfo(sp->getPlainName(), sp->getToken());
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

void ShareManager::changeExcludedDirs(const ProfileTokenStringList& aAdd, const ProfileTokenStringList& aRemove) noexcept {
	ProfileTokenSet dirtyProfiles;

	{
		WLock l (cs);

		//add new exludes
		for(const auto i: aAdd) {
			auto dir = findDirectory(i.second, false, false);
			if (dir) {
				dirtyProfiles.insert(i.first);
				if (dir->getProfileDir()) {
					dir->getProfileDir()->addExclude(i.first);
					continue;
				}
			}

			auto pd = ProfileDirectory::Ptr(new ProfileDirectory(i.second, i.first));
			if (dir)
				dir->setProfileDir(pd);
			profileDirs[i.second] = pd;
		}

		//remove existing excludes
		for(const auto i: aRemove) {
			dirtyProfiles.insert(i.first);
			auto pdPos = profileDirs.find(i.second);
			if (pdPos != profileDirs.end() && pdPos->second->removeExcludedProfile(i.first) && !pdPos->second->hasRoots()) {
				profileDirs.erase(pdPos);
			}
		}
	}

	setProfilesDirty(dirtyProfiles, true);
	rebuildTotalExcludes();
}

void ShareManager::rebuildTotalExcludes() noexcept {
	RLock l (cs);
	for(auto& pdPos: profileDirs) {
		auto pd = pdPos.second;

		//profileDirs also include all shared roots...
		if (!pd->hasExcludes() || pd->hasRoots())
			continue;

		pd->unsetFlag(ProfileDirectory::FLAG_EXCLUDE_TOTAL);

		ProfileTokenSet sharedProfiles;

		//List all profiles where this dir is shared in
		for(const auto& s: rootPaths) {
			if (AirUtil::isParentOrExact(s.first, pdPos.first)) {
				s.second->copyRootProfiles(sharedProfiles, false);
			}
		}


		//Is the directory excluded in all profiles?
		if (any_of(sharedProfiles.begin(), sharedProfiles.end(), [&pd](const ProfileToken aToken) { return pd->getExcludedProfiles().find(aToken) == pd->getExcludedProfiles().end(); }))
			continue;


		//Are there shared roots in subdirs?
		auto subDirs = find_if(profileDirs | map_values, [pdPos](const ProfileDirectory::Ptr& spd) { 
			return spd->hasRoots() && AirUtil::isSub(spd->getPath(), pdPos.first); 
		});

		if (subDirs.base() == profileDirs.end()) {
			//LogManager::getInstance()->message(pdPos->first + " is a total exclude", LogManager::LOG_INFO);
			pdPos.second->setFlag(ProfileDirectory::FLAG_EXCLUDE_TOTAL);
		}
	}
}

vector<pair<string, StringList>> ShareManager::getGroupedDirectories() const noexcept {
	vector<pair<string, StringList>> ret;
	
	{
		RLock l (cs);
		for(const auto& d: rootPaths | map_values | filtered(Directory::IsParent())) {
			for(const auto& vName: d->getProfileDir()->getRootProfiles() | map_values) {
				auto retVirtual = find_if(ret, CompareFirst<string, StringList>(vName));

				const auto& rp = d->getRealPath();
				if (retVirtual != ret.end()) {
					//insert under an old virtual node if the real path doesn't exist there already
					if (find(retVirtual->second, rp) == retVirtual->second.end()) {
						retVirtual->second.push_back(rp); //sorted
					}
				} else {
					ret.emplace_back(vName, StringList { rp });
				}
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
			LogManager::getInstance()->message(aMsg, LogManager::LOG_INFO);
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
		dcassert(File::getSize(aPath) == size);
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
		if((strcmp(aPathLower.c_str(), AirUtil::tempDLDir.c_str()) == 0)) {
			return false;
		}
	}
	return true;
}

void ShareManager::setSkipList() {
	WLock l (dirNames);
	skipList.pattern = SETTING(SKIPLIST_SHARE);
	skipList.setMethod(SETTING(SHARE_SKIPLIST_USE_REGEXP) ? StringMatch::REGEX : StringMatch::WILDCARD);
	skipList.prepare();
}

} // namespace dcpp
