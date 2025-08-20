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
#include <airdcpp/share/ShareManager.h>

#include <airdcpp/queue/Bundle.h>
#include <airdcpp/core/io/compress/BZUtils.h>
#include <airdcpp/DCPlusPlus.h>
#include <airdcpp/core/classes/ErrorCollector.h>
#include <airdcpp/core/io/File.h>
#include <airdcpp/core/io/stream/FilteredFile.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/hash/HashManager.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/search/SearchQuery.h>
#include <airdcpp/search/SearchResult.h>
#include <airdcpp/share/SharePathValidator.h>
#include <airdcpp/share/profiles/ShareProfileManager.h>
#include <airdcpp/share/ShareTasks.h>
#include <airdcpp/share/ShareTree.h>
#include <airdcpp/core/io/xml/SimpleXML.h>
#include <airdcpp/core/io/stream/Streams.h>
#include <airdcpp/transfer/Transfer.h>
#include <airdcpp/connection/UserConnection.h>

#include <airdcpp/core/version.h>

#include <airdcpp/core/thread/concurrency.h>

namespace dcpp {

using ranges::find_if;
using ranges::for_each;

ShareManager::ShareManager() : 
	profiles(make_unique<ShareProfileManager>([this](const ShareProfilePtr& p) { removeRootProfile(p); })), 
	tree(make_unique<ShareTree>()),
	validator(make_unique<SharePathValidator>([this](const string& aRealPath) { return tree->parseRoot(aRealPath); })),
	tasks(make_unique<ShareTasks>(this))
{ 
	SettingsManager::getInstance()->addListener(this);
	HashManager::getInstance()->addListener(this);

	File::ensureDirectory(AppUtil::getPath(AppUtil::PATH_SHARECACHE));

	SettingsManager::getInstance()->registerChangeHandler({ 
		SettingsManager::SKIPLIST_SHARE, SettingsManager::SHARE_SKIPLIST_USE_REGEXP 
	}, [this](auto ...) {
		reloadSkiplist();
	});

	registerUploadFileProvider(tree.get());
}

ShareManager::~ShareManager() {
	HashManager::getInstance()->removeListener(this);
	SettingsManager::getInstance()->removeListener(this);
}

void ShareManager::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(SHARE));
}

void ShareManager::duplicateFilelistFileLogger(const StringList& aDirectoryPaths, int aDupeFileCount) noexcept {
	log(STRING_F(DUPLICATE_FILES_DETECTED, aDupeFileCount % Util::toString(", ", aDirectoryPaths)), LogMessage::SEV_WARNING);
}


void ShareManager::registerUploadFileProvider(const UploadFileProvider* aProvider) noexcept {
	hashedFileProviders.push_back(aProvider);
}

// Note that settings are loaded before this function is called
// This function shouldn't initialize anything that is needed by the startup wizard
void ShareManager::startup(StartupLoader& aLoader) noexcept {
	// Refresh involves hooks, run only after everything has been loaded and the extensions are running

	bool refreshScheduled = false;
	if (!loadCache(aLoader.progressF)) {
		aLoader.addPostLoadTask([this, &aLoader] {
			aLoader.stepF(STRING(REFRESHING_SHARE));
			refresh(ShareRefreshType::STARTUP, ShareRefreshPriority::BLOCKING, aLoader.progressF);
		});

		refreshScheduled = true;
	}

	aLoader.addPostLoadTask([refreshScheduled, this] {
		TimerManager::getInstance()->addListener(this);

		if (!refreshScheduled && SETTING(STARTUP_REFRESH)) {
			refresh(ShareRefreshType::STARTUP, ShareRefreshPriority::NORMAL);
		}
	});
}

void ShareManager::shutdown(const ProgressFunction& progressF) noexcept {
	saveShareCache(progressF);
	profiles->removeCachedFilelists();

	TimerManager::getInstance()->removeListener(this);
	tasks->shutdown();
}


// LOADING
void ShareManager::on(SettingsManagerListener::LoadCompleted, bool) noexcept {
	validator->reloadSkiplist();
	profiles->ensureDefaultProfiles();

	{
		// Validate loaded paths
		auto rootPathsCopy = tree->getRootPathsUnsafe();
		for (const auto& [path, directory] : rootPathsCopy) {
			if (find_if(rootPathsCopy | views::keys, [&path](const string& aOtherPath) {
				return PathUtil::isSubLocal(path, aOtherPath);
			}).base() != rootPathsCopy.end()) {
				tree->removeShareRoot(path);

				log("The directory " + path + " was not loaded: parent of this directory is shared in another profile, which is not supported in this client version.", LogMessage::SEV_WARNING);
			}
		}
	}
}

static const string SDIRECTORY = "Directory";
static const string SFILE = "File";
static const string SNAME = "Name";
static const string SSIZE = "Size";
static const string DATE = "Date";
static const string SHARE = "Share";
static const string SVERSION = "Version";

struct ShareManager::ShareLoader : public SimpleXMLReader::ThreadedCallBack, public ShareRefreshInfo {
	ShareLoader(const string& aPath, const ShareDirectory::Ptr& aOldRoot, ShareBloom& aBloom) :
		ThreadedCallBack(aOldRoot->getRoot()->getCacheXmlPath()),
		ShareRefreshInfo(aPath, aOldRoot, 0, aBloom),
		curDirPathLower(aOldRoot->getRoot()->getPathLower()),
		curDirPath(aOldRoot->getRoot()->getPath())
	{ 
		cur = newDirectory.get();
	}


	void startTag(const string& aName, StringPairList& aAttribs, bool aSimple) override {
		if(compare(aName, SDIRECTORY) == 0) {
			const string& name = getAttrib(aAttribs, SNAME, 0);
			const string& date = getAttrib(aAttribs, DATE, 1);

			if (!name.empty()) {
				curDirPath += name + PATH_SEPARATOR;

				cur = ShareDirectory::createNormal(name, cur, Util::toTimeT(date), *this).get();
				if (!cur) {
					throw Exception("Duplicate directory name");
				}

				curDirPathLower += cur->getRealName().getLower() + PATH_SEPARATOR;
			}

			if (aSimple) {
				if (cur) {
					cur = cur->getParent();
				}
			}
		} else if (cur && compare(aName, SFILE) == 0) {
			const auto& fname = getAttrib(aAttribs, SNAME, 0);
			if (fname.empty()) {
				dcdebug("Invalid file found: %s\n", fname.c_str());
				return;
			}

			try {
				DualString name(fname);
				HashedFile fi;
				HashManager::getInstance()->getFileInfo(curDirPathLower + name.getLower(), curDirPath + fname, fi);
				cur->addFile(std::move(name), fi, *this, stats.addedSize);
			} catch(Exception& e) {
				stats.hashSize += File::getSize(curDirPath + fname);
				dcdebug("Error loading shared file %s \n", e.getError().c_str());
			}
		} else if (compare(aName, SHARE) == 0) {
			auto version = Util::toInt(getAttrib(aAttribs, SVERSION, 0));
			if (version > Util::toInt(SHARE_CACHE_VERSION))
				throw Exception("Newer cache version"); //don't load those...

			cur->setLastWrite(Util::toTimeT(getAttrib(aAttribs, DATE, 2)));
		}
	}
	void endTag(const string& name) override {
		if (compare(name, SDIRECTORY) == 0) {
			if (cur) {
				curDirPath = PathUtil::getParentDir(curDirPath);
				curDirPathLower = PathUtil::getParentDir(curDirPathLower);
				cur = cur->getParent();
			}
		}
	}

private:
	friend struct SizeSort;

	ShareDirectory* cur;

	string curDirPathLower;
	string curDirPath;
};

using ShareLoaderPtr = shared_ptr<ShareManager::ShareLoader>;
using LoaderList = vector<ShareLoaderPtr>;

bool ShareManager::loadCache(const ProgressFunction& progressF) noexcept {
	HashManager::HashPauser pauser;

	AppUtil::migrate(AppUtil::getPath(AppUtil::PATH_SHARECACHE), "ShareCache_*");

	LoaderList cacheLoaders;

	// Create loaders
	for (const auto& [rootPath, rootDir] : tree->getRootPathsUnsafe()) {
		try {
			auto loader = std::make_shared<ShareLoader>(rootPath, rootDir, *tree->getBloom());
			cacheLoaders.emplace_back(loader);
		} catch (const FileException&) {
			log(STRING_F(SHARE_CACHE_FILE_MISSING, rootPath), LogMessage::SEV_ERROR);
			return false;
		}
	}

	{
		// Remove obsolete cache files
		auto fileList = File::findFiles(AppUtil::getPath(AppUtil::PATH_SHARECACHE), "ShareCache_*", File::TYPE_FILE);
		for (const auto& p: fileList) {
			auto rp = find_if(cacheLoaders, [&p](const ShareLoaderPtr& aLoader) {
				return p == aLoader->xmlPath;
			});

			if (rp == cacheLoaders.end()) {
				File::deleteFile(p);
			}
		}
	}

	if (cacheLoaders.empty()) {
		return true;
	}

	{
		const auto dirCount = cacheLoaders.size();

		//ll.sort(SimpleXMLReader::ThreadedCallBack::SizeSort());

		// Parse the actual cache files
		atomic<long> loaded(0);
		bool hasFailedCaches = false;

		try {
			parallel_for_each(cacheLoaders.begin(), cacheLoaders.end(), [&](const ShareLoaderPtr& i) {
				//log("Thread: " + Util::toString(::GetCurrentThreadId()) + "Size " + Util::toString(loader.size), LogMessage::SEV_INFO);
				auto& loader = *i;
				try {
					SimpleXMLReader(&loader).parse(*loader.file);
				} catch (SimpleXMLException& e) {
					log(STRING_F(LOAD_FAILED_X, loader.xmlPath % e.getError()), LogMessage::SEV_ERROR);
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
			log("Loading the share cache failed: " + string(e.what()), LogMessage::SEV_INFO);
		}

		if (hasFailedCaches) {
			// Refresh all
			return false;
		}
	}

	// Apply the changes
	ShareRefreshStats stats;
	for (const auto& l : cacheLoaders) {
		tree->applyRefreshChanges(*l, nullptr);
		stats.merge(l->stats);
	}

#ifdef _DEBUG
	tree->validateDirectoryTreeDebug();
#endif

	if (stats.hashSize > 0) {
		log(STRING_F(FILES_ADDED_FOR_HASH_STARTUP, Util::formatBytes(stats.hashSize)), LogMessage::SEV_INFO);
	}

	return true;
}


// PROFILES
void ShareManager::loadProfile(SimpleXML& aXml, bool aIsDefault) {
	auto shareProfile = profiles->loadProfile(aXml, aIsDefault);
	if (!shareProfile) {
		return;
	}

	aXml.stepIn();
	while(aXml.findChild("Directory")) {
		auto realPath = PathUtil::validateDirectoryPath(aXml.getChildData());
		if(realPath.empty()) {
			continue;
		}

		const auto& loadedVirtualName = aXml.getChildAttrib("Virtual");

		const auto& rootPaths = tree->getRootPathsUnsafe();
		auto p = rootPaths.find(realPath);
		if (p != rootPaths.end()) {
			p->second->getRoot()->addRootProfile(shareProfile->getToken());
		} else {
			auto incoming = aXml.getBoolChildAttrib("Incoming");
			auto lastRefreshTime = aXml.getTimeChildAttrib("LastRefreshTime");

			// Validate in case we have changed the rules
			auto vName = validateVirtualName(loadedVirtualName.empty() ? PathUtil::getLastDir(realPath) : loadedVirtualName);
			tree->addShareRoot(realPath, vName, { shareProfile->getToken()}, incoming, 0, lastRefreshTime);
		}
	}

	aXml.resetCurrentChild();

	if (shareProfile->isDefault()) {
		validator->loadExcludes(aXml);
	}

	aXml.stepOut();
}

void ShareManager::loadProfiles(SimpleXML& aXml) {
	aXml.resetCurrentChild();
	if(aXml.findChild("Share")) {
		loadProfile(aXml, true);
	}

	aXml.resetCurrentChild();
	while (aXml.findChild("ShareProfile")) {
		loadProfile(aXml, false);
	}
}

void ShareManager::saveProfiles(SimpleXML& aXml) const {
	for(const auto& sp: profiles->getProfiles() | views::filter(ShareProfile::NotHidden())) {
		auto isDefault = sp->getToken() == SETTING(DEFAULT_SP);

		aXml.addTag(isDefault ? "Share" : "ShareProfile"); // Keep the old Share tag around for compatibility with other clients
		aXml.addChildAttrib("Token", sp->getToken());
		aXml.addChildAttrib("Name", sp->getPlainName());
		aXml.stepIn();

		{
			for (const auto& rootDirectory : tree->getRoots(sp->getToken()) | views::transform(ShareDirectory::ToRoot)) {
				aXml.addTag("Directory", rootDirectory->getPath());
				aXml.addChildAttrib("Virtual", rootDirectory->getName());
				aXml.addChildAttrib("Incoming", rootDirectory->getIncoming());
				aXml.addChildAttrib("LastRefreshTime", rootDirectory->getLastRefreshTime());
			}
		}

		if (isDefault) {
			// Excludes are global so they need to be saved only once
			validator->saveExcludes(aXml);
		}

		aXml.stepOut();
	}
}

void ShareManager::removeRootProfile(const ShareProfilePtr& aProfile) noexcept {
	StringList removedPaths;
	tree->removeProfile(aProfile->getToken(), removedPaths);

	for (const auto& path : removedPaths) {
		removeRootDirectory(path);
	}
}

ShareProfilePtr ShareManager::getShareProfile(ProfileToken aProfile, bool allowFallback) const noexcept {
	return profiles->getShareProfile(aProfile, allowFallback);
}

ShareProfileList ShareManager::getProfiles() const noexcept {
	return profiles->getProfiles();
}

void ShareManager::getProfileInfo(ProfileToken aProfile, int64_t& totalSize_, size_t& filesCount_) const noexcept {
	if (aProfile == SP_HIDDEN) {
		return;
	}

	auto sp = getShareProfile(aProfile);
	if (!sp)
		return;

	if (sp->getProfileContentInfoDirty()) {
		tree->getProfileInfo(aProfile, totalSize_, filesCount_);

		sp->setSharedFiles(filesCount_);
		sp->setShareSize(totalSize_);
		sp->setProfileContentInfoDirty(false);
	}

	totalSize_ = sp->getShareSize();
	filesCount_ = sp->getSharedFiles();
}


// TREE

void ShareManager::getBloom(ProfileToken aProfile, ByteVector& v, size_t k, size_t m, size_t h) const noexcept {
	HashBloom bloom;
	bloom.reset(k, m, h);

	for (const auto& p : hashedFileProviders) {
		p->getBloom(aProfile, bloom);
	}

	bloom.copy_to(v);
}

size_t ShareManager::getBloomFileCount(ProfileToken aProfile) const noexcept {
	size_t fileCount = 0;
	for (const auto& p : hashedFileProviders) {
		p->getBloomFileCount(aProfile, fileCount);
	}
	return fileCount;
}

int64_t ShareManager::getSharedSize() const noexcept {
	return tree->getSharedSize();
}

ShareManager::HashedFileInfo ShareManager::toRealWithSize(const string& aVirtualFile, const ProfileTokenSet& aProfiles, const UserPtr& aOptionalUser, const Segment& aSegment) const noexcept {
	if (aVirtualFile.compare(0, 4, "TTH/") != 0) {
		return HashedFileInfo();
	}

	TTHValue tth(aVirtualFile.substr(4));

	UploadFileQuery query(tth, aOptionalUser, &aProfiles, &aSegment);
	return toRealWithSize(query);
}

ShareManager::HashedFileInfo ShareManager::toRealWithSize(const UploadFileQuery& aQuery) const noexcept {
	HashedFileInfo result;
	for (const auto& p : hashedFileProviders) {
		if (p->toRealWithSize(aQuery, result.path, result.size, result.noAccess)) {
			result.found = true;
			result.provider = p;
			return result;
		}
	}

	return result;
}

StringList ShareManager::getRealPaths(const TTHValue& aTTH) const noexcept {
	StringList ret;
	for (const auto& p : hashedFileProviders) {
		p->getRealPaths(aTTH, ret);
	}

	return ret;
}

void ShareManager::getRealPaths(const string& aVirtualPath, StringList& realPaths_, const OptionalProfileToken& aProfile) const {
	return tree->getRealPaths(aVirtualPath, realPaths_, aProfile);
}

void ShareManager::search(SearchResultList& results_, ShareSearch& aSearch) {
	if (aSearch.search.root) {
		searchCounters.tthSearches++;
		for (const auto& p : hashedFileProviders) {
			p->search(results_, *aSearch.search.root, aSearch);
		}

		return;
	}

	tree->searchText(results_, aSearch, searchCounters);
}

MemoryInputStream* ShareManager::getTree(const string& aVirtualFile, ProfileToken aProfile) const noexcept {
	TigerTree tigerTree;
	if (aVirtualFile.compare(0, 4, "TTH/") == 0) {
		if (!HashManager::getInstance()->getTree(TTHValue(aVirtualFile.substr(4)), tigerTree))
			return nullptr;
	} else {
		try {
			TTHValue tth = profiles->getListTTH(aVirtualFile, aProfile);
			HashManager::getInstance()->getTree(tth, tigerTree);
		} catch (const Exception&) {
			return nullptr;
		}
	}

	ByteVector buf = tigerTree.getLeafData();
	return new MemoryInputStream(&buf[0], buf.size());
}

bool ShareManager::isRealPathShared(const string& aPath) const noexcept {
	return PathUtil::isDirectoryPath(aPath) ? findDirectoryByRealPath(aPath) : findFileByRealPath(aPath);
}

string ShareManager::realToVirtualAdc(const string& aPath, const OptionalProfileToken& aToken) const noexcept {
	return tree->realToVirtualAdc(aPath, aToken);
}

int64_t ShareManager::getTotalShareSize(ProfileToken aProfile) const noexcept {
	int64_t totalSize = 0;
	size_t fileCount = 0;
	getProfileInfo(aProfile, totalSize, fileCount);
	return totalSize;
}

DupeType ShareManager::getAdcDirectoryDupe(const string& aAdcPath, int64_t aSize) const noexcept {
	return tree->getAdcDirectoryDupe(aAdcPath, aSize);
}

StringList ShareManager::getAdcDirectoryDupePaths(const string& aAdcPath) const noexcept {
	return tree->getAdcDirectoryDupePaths(aAdcPath);
}

bool ShareManager::isFileShared(const TTHValue& aTTH) const noexcept{
	return tree->isFileShared(aTTH);
}

bool ShareManager::isFileShared(const TTHValue& aTTH, ProfileToken aProfile) const noexcept{
	return tree->isFileShared(aTTH, aProfile);
}

bool ShareManager::findDirectoryByRealPath(const string& aPath, const ShareDirectoryCallback& aCallback) const noexcept {
	return tree->findDirectoryByRealPath(aPath, aCallback);
}

bool ShareManager::findFileByRealPath(const string& aPath, const ShareFileCallback& aCallback) const noexcept {
	return tree->findFileByRealPath(aPath, aCallback);
}

ShareDirectory::File::ConstSet ShareManager::findFiles(const TTHValue& aTTH) const noexcept {
	return tree->findFiles(aTTH);
}


// ADC
AdcCommand ShareManager::getFileInfo(const string& aFile, ProfileToken aProfile) {
	if(aFile == Transfer::USER_LIST_NAME_EXTRACTED) {
		auto fl = generateXmlList(aProfile);
		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(fl->getXmlListLen()));
		cmd.addParam("TR", fl->getXmlRoot().toBase32());
		return cmd;
	} else if(aFile == Transfer::USER_LIST_NAME_BZ) {
		auto fl = generateXmlList(aProfile);
		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(fl->getBzXmlListLen()));
		cmd.addParam("TR", fl->getBzXmlRoot().toBase32());
		return cmd;
	}

	if (aFile.compare(0, 4, "TTH/") != 0) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	TTHValue val(aFile.substr(4));
	return tree->getFileInfo(val);
}


// STATS
optional<ShareItemStats> ShareManager::getShareItemStats() const noexcept {
	ShareItemStats stats;
	stats.profileCount = profiles->getProfiles().size() - 1; // remove hidden

	time_t totalAge = 0;
	tree->countStats(totalAge, stats.totalDirectoryCount, stats.totalSize, stats.totalFileCount, stats.uniqueFileCount, stats.lowerCaseFiles, stats.totalNameSize, stats.rootDirectoryCount);

	if (stats.uniqueFileCount == 0 || stats.totalDirectoryCount == 0) {
		return nullopt;
	}

	stats.averageFileAge = GET_TIME() - static_cast<time_t>(Util::countAverage(totalAge, stats.totalFileCount));
	stats.averageNameLength = Util::countAverage(stats.totalNameSize, stats.totalFileCount + stats.totalDirectoryCount);
	return stats;
}

ShareSearchStats ShareManager::getSearchMatchingStats() const noexcept {
	return searchCounters.toStats();
}


// REFRESH
ShareManager::RefreshTaskHandler::ShareBuilder::ShareBuilder(const string& aPath, const ShareDirectory::Ptr& aOldRoot, time_t aLastWrite, ShareBloom& bloom_, ShareManager* aSm) :
	sm(*aSm), ShareRefreshInfo(aPath, aOldRoot, aLastWrite, bloom_) {

}

bool ShareManager::RefreshTaskHandler::ShareBuilder::buildTree(const bool& aStopping) noexcept {
	try {
		buildTree(path, Text::toLower(path), newDirectory, optionalOldDirectory, aStopping);
	} catch (const std::bad_alloc&) {
		log(STRING_F(DIR_REFRESH_FAILED, path % STRING(OUT_OF_MEMORY)), LogMessage::SEV_ERROR);
		return false;
	} catch (...) {
		log(STRING_F(DIR_REFRESH_FAILED, path % STRING(UNKNOWN_ERROR)), LogMessage::SEV_ERROR);
		return false;
	}

	return !aStopping;
}

bool ShareManager::RefreshTaskHandler::ShareBuilder::validateFileItem(const FileItemInfoBase& aFileItem, const string& aPath, bool aIsNew, bool aNewParent, ErrorCollector& aErrorCollector) noexcept {
	try {
		sm.validator->validateHooked(aFileItem, aPath, false, &sm, aIsNew, aNewParent);
	} catch (const ShareValidatorException& e) {
		if (SETTING(REPORT_BLOCKED_SHARE) && ShareValidatorException::isReportableError(e.getType())) {
			if (aFileItem.isDirectory()) {
				log(STRING_F(SHARE_DIRECTORY_BLOCKED, aPath % e.getError()), LogMessage::SEV_INFO);
			} else {
				aErrorCollector.add(e.getError(), PathUtil::getFileName(aPath), false);
			}
		}

		dcdebug("Item %s won't be shared: %s\n", aPath.c_str(), e.what());
		return false;
	} catch (...) {
		return false;
	}

	return true;
}

void ShareManager::RefreshTaskHandler::ShareBuilder::buildTree(const string& aPath, const string& aPathLower, const ShareDirectory::Ptr& aParent, const ShareDirectory::Ptr& aOldParent, const bool& aStopping) {
	ErrorCollector errors;
	FileFindIter end;
	for(FileFindIter i(aPath, "*"); i != end && !aStopping; ++i) {
		const auto name = i->getFileName();
		if(name.empty()) {
			return;
		}

		const auto isDirectory = i->isDirectory();
		if (!isDirectory) {
			errors.increaseTotal();
		}

		DualString dualName(name);
		auto curPath = aPath + name + (isDirectory ? PATH_SEPARATOR_STR : Util::emptyString);
		auto curPathLower = aPathLower + dualName.getLower() + (isDirectory ? PATH_SEPARATOR_STR : Util::emptyString);

		if (isDirectory) {
			// Check whether it's shared already
			ShareDirectory::Ptr oldDir = nullptr;
			if (aOldParent) {
				RLock l(sm.tree->getCS());
				oldDir = aOldParent->findDirectoryLower(dualName.getLower());
			}

			auto isNew = !oldDir;

			// Validations
			{
				auto newParent = !aOldParent;
				if (!validateFileItem(*i, curPath, isNew, newParent, errors)) {
					stats.skippedDirectoryCount++;
					continue;
				}

			}

			// Add it
			auto curDir = ShareDirectory::createNormal(std::move(dualName), aParent.get(), i->getLastWriteTime(), *this);
			if (curDir) {
				buildTree(curPath, curPathLower, curDir, oldDir, aStopping);
				if (checkContent(curDir)) {
					if (isNew) {
						stats.newDirectoryCount++;
					} else {
						stats.existingDirectoryCount++;
					}
				}
			}
		} else {
			// Not a directory, assume it's a file...

			{
				// Check whether it's shared already
				auto isNew = !aOldParent;
				if (aOldParent) {
					RLock l(sm.tree->getCS());
					isNew = !aOldParent->findFileLower(dualName.getLower());
				}


				// Validations
				auto newParent = !aOldParent;
				if (!validateFileItem(*i, curPath, isNew, newParent, errors)) {
					stats.skippedFileCount++;
					continue;
				}

				if (isNew) {
					stats.newFileCount++;
				} else {
					stats.existingFileCount++;
				}
			}

			// Add it
			auto size = i->getSize();
			try {
				HashedFile fi(i->getLastWriteTime(), size);
				if(HashManager::getInstance()->checkTTH(aPathLower + dualName.getLower(), aPath + name, fi)) {
					aParent->addFile(std::move(dualName), fi, *this, stats.addedSize);
				} else {
					stats.hashSize += size;
				}
			} catch(const HashException&) {
			}
		}
	}

	auto msg = errors.getMessage();
	if (!msg.empty()) {
		log(STRING_F(SHARE_FILES_BLOCKED, aPath % msg), LogMessage::SEV_INFO);
	}
}

optional<RefreshTaskQueueInfo> ShareManager::refreshVirtualName(const string& aVirtualName, ShareRefreshPriority aPriority) noexcept {
	StringList refreshDirs;

	{
		for (const auto& rootDirectory: tree->getShareRoots()) {
			if (Util::stricmp(rootDirectory->getNameLower(), aVirtualName) == 0) {
				refreshDirs.push_back(rootDirectory->getPath());
			}
		}
	}

	if (refreshDirs.empty()) {
		return nullopt;
	}

	return tasks->addRefreshTask(aPriority, refreshDirs, ShareRefreshType::REFRESH_DIRS, aVirtualName);
}


RefreshTaskQueueInfo ShareManager::refresh(ShareRefreshType aType, ShareRefreshPriority aPriority, const ProgressFunction& progressF /*nullptr*/) noexcept {
	StringList dirs;

	{
		for (const auto& rootDirectory: tree->getShareRoots()) {
			if (aType == ShareRefreshType::REFRESH_INCOMING && !rootDirectory->getIncoming())
				continue;

			dirs.push_back(rootDirectory->getPath());
		}
	}

	return tasks->addRefreshTask(aPriority, dirs, aType, Util::emptyString, progressF);
}

optional<RefreshTaskQueueInfo> ShareManager::refreshPathsHooked(ShareRefreshPriority aPriority, const StringList& aPaths, CallerPtr aCaller, const string& aDisplayName /*Util::emptyString*/, const ProgressFunction& aProgressF /*nullptr*/) noexcept {
	try {
		return refreshPathsHookedThrow(aPriority, aPaths, aCaller, aDisplayName, aProgressF);
	} catch (const Exception&) {
		// ...
	}

	return nullopt;
}


RefreshTaskQueueInfo ShareManager::refreshPathsHookedThrow(ShareRefreshPriority aPriority, const StringList& aPaths, CallerPtr aCaller, const string& aDisplayName, const ProgressFunction& aProgressF) {
	for (const auto& path : aPaths) {
		// Ensure that the path exists in share (or it can be added)
		validatePathHooked(path, false, aCaller);
	}

	return tasks->addRefreshTask(aPriority, aPaths, ShareRefreshType::REFRESH_DIRS, aDisplayName, aProgressF);
}

bool ShareManager::handleRefreshPath(const string& aRefreshPath, const ShareRefreshTask& aTask, ShareRefreshStats& totalStats, ShareBloom* bloom_, ProfileTokenSet& dirtyProfiles_) noexcept {
	ShareDirectory::Ptr optionalOldDirectory = nullptr;

	{
		RLock l(tree->getCS());
		optionalOldDirectory = tree->findDirectoryUnsafe(aRefreshPath);
	}

	auto ri = RefreshTaskHandler::ShareBuilder(aRefreshPath, optionalOldDirectory, File::getLastModified(aRefreshPath), *bloom_, this);
	setRefreshState(ri.path, ShareRootRefreshState::STATE_RUNNING, false, aTask.token);

	// Build the tree
	auto completed = ri.buildTree(aTask.canceled);

	// Apply the changes
	if (completed) {
		tree->applyRefreshChanges(ri, &dirtyProfiles_);
		totalStats.merge(ri.stats);
	}

	// Finish up
	setRefreshState(ri.path, ShareRootRefreshState::STATE_NORMAL, completed, nullopt);

	return completed;
}


void ShareManager::onRefreshTaskCompleted(bool aCompleted, const ShareRefreshTask& aTask, const ShareRefreshStats& aTotalStats, ShareBloom* bloom_, ProfileTokenSet& dirtyProfiles_) noexcept {
	if (aTask.type == ShareRefreshType::REFRESH_ALL) {
		if (aCompleted) {
			// Reset the bloom so that removed files are nulled (which won't happen with partial refreshes)
			tree->setBloom(bloom_);
		} else {
			delete bloom_;
		}
	}

	profiles->setProfilesDirty(dirtyProfiles_, aTask.priority == ShareRefreshPriority::MANUAL || aTask.type == ShareRefreshType::REFRESH_ALL || aTask.type == ShareRefreshType::BUNDLE);

	fire(ShareManagerListener::RefreshCompleted(), aTask, aCompleted, aTotalStats);

#ifdef _DEBUG
	tree->validateDirectoryTreeDebug();
#endif
}

ShareManager::RefreshTaskHandler::RefreshTaskHandler(ShareBloom* aBloom, PathRefreshF&& aPathRefreshF, CompletionF&& aCompletionF) : 
	pathRefreshF(std::move(aPathRefreshF)), completionF(std::move(aCompletionF)), bloom(std::move(aBloom)) 
{}

void ShareManager::RefreshTaskHandler::refreshCompleted(bool aCompleted, const ShareRefreshTask& aTask, const ShareRefreshStats& aTotalStats) {
	return completionF(aCompleted, aTask, aTotalStats, bloom, dirtyProfiles);
}

bool ShareManager::RefreshTaskHandler::refreshPath(const string& aRefreshPath, const ShareRefreshTask& aTask, ShareRefreshStats& totalStats) {
	return pathRefreshF(aRefreshPath, aTask, totalStats, bloom, dirtyProfiles);
}

shared_ptr<ShareTasksManager::RefreshTaskHandler> ShareManager::startRefresh(const ShareRefreshTask& aTask) noexcept {
	auto refreshBloom = aTask.type == ShareRefreshType::REFRESH_ALL ? new ShareBloom(1 << 20) : tree->getBloom();
	ProfileTokenSet dirtyProfiles;

	if (aTask.type == ShareRefreshType::REFRESH_INCOMING) {
		lastIncomingUpdate = GET_TICK();
	}
	else if (aTask.type == ShareRefreshType::REFRESH_ALL) {
		lastFullUpdate = GET_TICK();
		lastIncomingUpdate = GET_TICK();
	}

	fire(ShareManagerListener::RefreshStarted(), aTask);
	return make_shared<ShareManager::RefreshTaskHandler>(
		refreshBloom,
		std::bind_front(&ShareManager::handleRefreshPath, this),
		std::bind_front(&ShareManager::onRefreshTaskCompleted, this)
	);
}

void ShareManager::onRefreshQueued(const ShareRefreshTask& aTask) noexcept {
	for (auto& path : aTask.dirs) {
		setRefreshState(path, ShareRootRefreshState::STATE_PENDING, false, aTask.token);
	}

	fire(ShareManagerListener::RefreshQueued(), aTask);

}

void ShareManager::setRefreshState(const string& aRefreshPath, ShareRootRefreshState aState, bool aUpdateRefreshTime, const optional<ShareRefreshTaskToken>& aRefreshTaskToken) noexcept {
	auto rootDir = tree->setRefreshState(aRefreshPath, aState, aUpdateRefreshTime, aRefreshTaskToken);
	if (rootDir) {
		fire(ShareManagerListener::RootRefreshState(), rootDir->getPath());
	}
}

ShareRefreshTaskList ShareManager::getRefreshTasks() const noexcept {
	return tasks->getRefreshTasks();
}

bool ShareManager::isRefreshing() const noexcept {
	return tasks->isRefreshing();
}

bool ShareManager::abortRefresh(optional<ShareRefreshTaskToken> aToken) noexcept {
	auto paths = tasks->abortRefresh(aToken);
	for (const auto& d : paths) {
		setRefreshState(d, ShareRootRefreshState::STATE_NORMAL, false, nullopt);
	}

	return !paths.empty();
}


// TIMER
void ShareManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	if (lastSave == 0 || lastSave + 15 * 60 * 1000 <= aTick) {
		saveShareCache();
	}

	if (SETTING(AUTO_REFRESH_TIME) > 0 && lastFullUpdate + SETTING(AUTO_REFRESH_TIME) * 60 * 1000 <= aTick) {
		lastIncomingUpdate = aTick;
		lastFullUpdate = aTick;
		refresh(ShareRefreshType::REFRESH_ALL, ShareRefreshPriority::SCHEDULED);
	} else if (SETTING(INCOMING_REFRESH_TIME) > 0 && lastIncomingUpdate + SETTING(INCOMING_REFRESH_TIME) * 60 * 1000 <= aTick) {
		lastIncomingUpdate = aTick;
		refresh(ShareRefreshType::REFRESH_INCOMING, ShareRefreshPriority::SCHEDULED);
	}
}

// ROOTS
ShareDirectoryInfoList ShareManager::getRootInfos() const noexcept {
	return tree->getRootInfos();
}

ShareDirectoryInfoPtr ShareManager::getRootInfo(const string& aPath) const noexcept {
	return tree->getRootInfo(aPath);
}

bool ShareManager::addRootDirectory(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept {
	dcassert(!aDirectoryInfo->profiles.empty());
	if (!tree->addShareRoot(aDirectoryInfo)) {
		return false;
	}

	const auto& path = aDirectoryInfo->path;
	fire(ShareManagerListener::RootCreated(), path);
	tasks->addRefreshTask(ShareRefreshPriority::MANUAL, { path }, ShareRefreshType::ADD_DIR);

	profiles->setProfilesDirty(aDirectoryInfo->profiles, true);
	return true;
}

bool ShareManager::removeRootDirectory(const string& aPath) noexcept {
	auto root = tree->removeShareRoot(aPath);
	if (!root) {
		return false;
	}

	HashManager::getInstance()->stopHashing(aPath);

	// Safe, the directory isn't in use
	decltype(auto) dirtyProfiles = root->getRootProfiles();

	log(STRING_F(SHARED_DIR_REMOVED, aPath), LogMessage::SEV_INFO);

	fire(ShareManagerListener::RootRemoved(), aPath);
	profiles->setProfilesDirty(dirtyProfiles, true);
	return true;
}

bool ShareManager::updateRootDirectory(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept {
	dcassert(!aDirectoryInfo->profiles.empty());

	if (!tree->updateShareRoot(aDirectoryInfo)) {
		return false;
	}

	profiles->setProfilesDirty(aDirectoryInfo->profiles, true);

	fire(ShareManagerListener::RootUpdated(), aDirectoryInfo->path);
	return true;
}

GroupedDirectoryMap ShareManager::getGroupedDirectories() const noexcept {
	GroupedDirectoryMap ret;
	for (const auto& rootDirectory: tree->getShareRoots()) {
		auto currentPath = rootDirectory->getPath();
		auto virtualName = rootDirectory->getName();

		ret[virtualName].insert(currentPath);
	}

	return ret;
}


// FILELISTS
pair<int64_t, string> ShareManager::getFileListInfo(const string_view& aVirtualFile, ProfileToken aProfile) {
	if (aVirtualFile == "MyList.DcLst")
		throw ShareException("NMDC-style lists no longer supported, please upgrade your client");

	if (aVirtualFile == Transfer::USER_LIST_NAME_BZ || aVirtualFile == Transfer::USER_LIST_NAME_EXTRACTED) {
		auto filelist = generateXmlList(aProfile);
		return { filelist->getBzXmlListLen(), filelist->getFileName() };
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}


string ShareManager::generateOwnList(ProfileToken aProfile) {
	auto filelist = generateXmlList(aProfile, true);
	return filelist->getFileName();
}


//forwards the calls to createFileList for creating the filelist that was requested.
FileList* ShareManager::generateXmlList(ProfileToken aProfile, bool forced /*false*/) {
	auto shareProfile = profiles->getShareProfile(aProfile);
	if (!shareProfile) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	// The filelist generation code currently causes the filelist to get corrupted if the size is over 1 gigabytes, which has happened with a share of over 30 million files
	// Uploading filelists of that size would get problematic, as loading them would most likely crash all 32 bit clients
	// Limit the maximum file count to 20 million, to be somewhat safe
	if (shareProfile->getSharedFiles() > 20000000) {
		throw ShareException("The size of the filelist exceeds the maximum limit of 1 GB / 20 million files; please use a partial list instead");
	}

	auto fl = shareProfile->getProfileList();

	{
		Lock lFl(fl->cs);
		if (fl->allowGenerateNew(forced)) {
			auto tmpName = fl->getFileName().substr(0, fl->getFileName().length() - 4);
			try {
				{
					File f(tmpName, File::RW, File::TRUNCATE | File::CREATE, File::BUFFER_SEQUENTIAL, false);

					tree->toFilelist(f, ADC_ROOT_STR, aProfile, true, duplicateFilelistFileLogger);

					fl->setXmlListLen(f.getSize());

					File bz(fl->getFileName(), File::WRITE, File::TRUNCATE | File::CREATE, File::BUFFER_SEQUENTIAL, false);
					// We don't care about the leaves...
					CalcOutputStream<TTFilter<1024 * 1024 * 1024>, false> bzTree(&bz);
					FilteredOutputStream<BZFilter, false> bzipper(&bzTree);
					CalcOutputStream<TTFilter<1024 * 1024 * 1024>, false> newXmlFile(&bzipper);

					newXmlFile.write(f.read());
					newXmlFile.flushBuffers(false);

					newXmlFile.getFilter().getTree().finalize();
					bzTree.getFilter().getTree().finalize();

					fl->setXmlRoot(newXmlFile.getFilter().getTree().getRoot());
					fl->setBzXmlRoot(bzTree.getFilter().getTree().getRoot());
				}

				fl->saveList();
				fl->generationFinished(false);
			} catch (const Exception& e) {
				// No new file lists...
				log(STRING_F(SAVE_FAILED_X, fl->getFileName() % e.getError()), LogMessage::SEV_ERROR);
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
	string xml = Util::emptyString;

	{
		StringOutputStream sos(xml);
		tree->toFilelist(sos, aVirtualPath, aProfile, aRecursive, duplicateFilelistFileLogger);
	}

	if (xml.empty()) {
		dcdebug("Partial NULL");
		return nullptr;
	}

	dcdebug("Partial list generated (%s)\n", aVirtualPath.c_str());
	return new MemoryInputStream(xml);
}

MemoryInputStream* ShareManager::generateTTHList(const string& aVirtualPath, bool aRecursive, ProfileToken aProfile) const noexcept {
	string tths;
	{
		StringOutputStream sos(tths);
		tree->toTTHList(sos, aVirtualPath, aRecursive, aProfile);
	}

	if (tths.size() == 0) {
		dcdebug("TTH list NULL");
		return nullptr;
	}

	dcdebug("TTH list generated (%s)\n", aVirtualPath.c_str());
	return new MemoryInputStream(tths);
}


// CACHE
void ShareManager::saveShareCache(const ProgressFunction& progressF /*nullptr*/) noexcept {
	if (shareCacheSaving)
		return;

	shareCacheSaving = true;

	if (progressF)
		progressF(0);

	int cur = 0;
	ShareDirectory::List dirtyDirs;

	{
		ranges::copy_if(tree->getRootPaths() | views::values, back_inserter(dirtyDirs), [](const ShareDirectory::Ptr& aDir) { 
			return aDir->getRoot()->getCacheDirty(); 
		});

		try {
			parallel_for_each(dirtyDirs.begin(), dirtyDirs.end(), [&](const ShareDirectory::Ptr& d) {
				string path = d->getRoot()->getCacheXmlPath();
				try {
					{
						string indent, tmp;

						//create a backup first in case we get interrupted on creation.
						File ff(path + ".tmp", File::WRITE, File::TRUNCATE | File::CREATE);
						BufferedOutputStream<false> xmlFile(&ff);
						tree->toCache(xmlFile, d);
					}

					File::deleteFile(path);
					File::renameFile(path + ".tmp", path);
				} catch (Exception& e) {
					log(STRING_F(SAVE_FAILED_X, path % e.getError()), LogMessage::SEV_WARNING);
				}

				d->getRoot()->setCacheDirty(false);
				if (progressF) {
					cur++;
					progressF(static_cast<float>(cur) / static_cast<float>(dirtyDirs.size()));
				}
			});
		} catch (std::exception& e) {
			log("Saving the share cache failed: " + string(e.what()), LogMessage::SEV_INFO);
		}
	}

	shareCacheSaving = false;
	lastSave = GET_TICK();
}


// SHARING
void ShareManager::shareBundle(const BundlePtr& aBundle) noexcept {
	if (aBundle->isFileBundle()) {
		try {
			HashedFile fi;
			HashManager::getInstance()->getFileInfo(Text::toLower(aBundle->getTarget()), aBundle->getTarget(), fi);
			onFileHashed(aBundle->getTarget(), fi);

			log(STRING_F(SHARED_FILE_ADDED, aBundle->getTarget()), LogMessage::SEV_INFO);
		} catch (...) { dcassert(0); }

		return;
	}

	tasks->addRefreshTask(ShareRefreshPriority::NORMAL, { aBundle->getTarget() }, ShareRefreshType::BUNDLE, aBundle->getTarget());
}

void ShareManager::onFileHashed(const string& aRealPath, const HashedFile& aFileInfo) noexcept {
	ProfileTokenSet dirtyProfiles;
	tree->addHashedFile(aRealPath, aFileInfo, &dirtyProfiles);
	profiles->setProfilesDirty(dirtyProfiles, false);
}


// VALIDATION
bool ShareManager::allowShareDirectoryHooked(const string& aRealPath, CallerPtr aCaller) const noexcept {
	try {
		validatePathHooked(aRealPath, false, aCaller);
		return true;
	} catch (const Exception&) { }

	return false;
}

void ShareManager::validatePathHooked(const string& aRealPath, bool aSkipQueueCheck, CallerPtr aCaller) const {
	StringList tokens;
	ShareDirectory::Ptr baseDirectory = nullptr;
	string basePath;

	auto isDirectoryPath = PathUtil::isDirectoryPath(aRealPath);
	auto isFileShared = false;

	{
		RLock l(tree->getCS());
		baseDirectory = tree->findDirectoryUnsafe(!isDirectoryPath ? PathUtil::getFilePath(aRealPath) : aRealPath, tokens);
		if (!baseDirectory) {
			throw ShareException(STRING(DIRECTORY_NOT_FOUND));
		}

		if (!isDirectoryPath && tokens.empty()) {
			auto fileNameLower = Text::toLower(PathUtil::getFileName(aRealPath));
			isFileShared = baseDirectory->findFileLower(fileNameLower);
		}

		basePath = baseDirectory->getRealPathUnsafe();
	}

	// Validate missing directory path tokens
	validator->validateNewDirectoryPathTokensHooked(basePath, tokens, aSkipQueueCheck, aCaller);

	if (!isDirectoryPath && !isFileShared) {
		// Validate the file
		validator->validateNewPathHooked(aRealPath, aSkipQueueCheck, !tokens.empty(), aCaller);
	}
}

string ShareManager::validateVirtualName(const string& aVirt) const noexcept {
	string tmp = aVirt;
	string::size_type idx = 0;

	while ((idx = tmp.find_first_of("\\/"), idx) != string::npos) {
		tmp[idx] = '_';
	}
	return tmp;
}

void ShareManager::validateRootPath(const string& aRealPath, bool aMatchCurrentRoots) const {
	validator->validateRootPath(aRealPath);

	if (aMatchCurrentRoots) {
		auto shareProfiles = profiles->getProfiles();
		auto formatProfiles = [&shareProfiles](const ProfileTokenSet& aProfiles) {
			auto rootProfileNames = ShareProfile::getProfileNames(aProfiles, shareProfiles);
			return Util::listToString(rootProfileNames);
		};

		tree->validateRootPath(aRealPath, formatProfiles);
	}
}


// EXCLUDES
StringSet ShareManager::getExcludedPaths() const noexcept {
	return validator->getExcludedPaths();
}

void ShareManager::addExcludedPath(const string& aPath) {
	validator->addExcludedPath(aPath);
	fire(ShareManagerListener::ExcludeAdded(), aPath);
}

bool ShareManager::removeExcludedPath(const string& aPath) noexcept {
	if (validator->removeExcludedPath(aPath)) {
		fire(ShareManagerListener::ExcludeRemoved(), aPath);
		return true;
	}

	return false;
}


// SKIPLIST
void ShareManager::reloadSkiplist() {
	validator->reloadSkiplist();
}

void ShareManager::setExcludedPaths(const StringSet& aPaths) noexcept {
	validator->setExcludedPaths(aPaths);
}

} // namespace dcpp
