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
#include "ShareManager.h"

#include "Bundle.h"
#include "BZUtils.h"
#include "DCPlusPlus.h"
#include "ErrorCollector.h"
#include "File.h"
#include "FilteredFile.h"
#include "LogManager.h"
#include "HashManager.h"
#include "PathUtil.h"
#include "ResourceManager.h"
#include "ScopedFunctor.h"
#include "SearchQuery.h"
#include "SearchResult.h"
#include "SharePathValidator.h"
#include "ShareTasks.h"
#include "ShareTree.h"
#include "SimpleXML.h"
#include "Transfer.h"
#include "UserConnection.h"

#include "version.h"

#include "concurrency.h"

namespace dcpp {

using ranges::find_if;
using ranges::for_each;

#define SHARE_CACHE_VERSION "3"

ShareManager::ShareManager() : validator(make_unique<SharePathValidator>()), tree(make_unique<ShareTree>()), tasks(make_unique<ShareTasks>(this)) { 
	SettingsManager::getInstance()->addListener(this);
	HashManager::getInstance()->addListener(this);

	File::ensureDirectory(AppUtil::getPath(AppUtil::PATH_SHARECACHE));

	SettingsManager::getInstance()->registerChangeHandler({ 
		SettingsManager::SKIPLIST_SHARE, SettingsManager::SHARE_SKIPLIST_USE_REGEXP 
	}, [this](auto ...) {
		reloadSkiplist();
	});
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

	aLoader.addPostLoadTask([=, this] {
		TimerManager::getInstance()->addListener(this);

		if (!refreshScheduled && SETTING(STARTUP_REFRESH)) {
			refresh(ShareRefreshType::STARTUP, ShareRefreshPriority::NORMAL);
		}
	});
}

void ShareManager::shutdown(function<void(float)> progressF) noexcept {
	saveShareCache(progressF);
	removeCachedFilelists();

	TimerManager::getInstance()->removeListener(this);
	tasks->shutdown();
}

void ShareManager::removeCachedFilelists() noexcept {
	try {
		RLock l(cs);
		//clear refs so we can delete filelists.
		auto lists = File::findFiles(AppUtil::getPath(AppUtil::PATH_USER_CONFIG), "files?*.xml.bz2", File::TYPE_FILE);
		for (auto& profile: shareProfiles) {
			if (profile->getProfileList() && profile->getProfileList()->bzXmlRef.get()) {
				profile->getProfileList()->bzXmlRef.reset();
			}
		}

		for_each(lists, File::deleteFile);
	}
	catch (...) {}
}

StringList ShareManager::getRealPaths(const TTHValue& aTTH) const noexcept {
	RLock l(cs);
	return tree->getRealPaths(aTTH);
}

int64_t ShareManager::getSharedSize() const noexcept {
	return tree->getSharedSize();
}

bool ShareManager::isTTHShared(const TTHValue& aTTH) const noexcept {
	RLock l(cs);
	return tree->isTTHShared(aTTH);
}

string ShareManager::toVirtual(const TTHValue& aTTH, ProfileToken aProfile) const {
	
	RLock l(cs);

	auto fl = getFileList(aProfile);
	if (aTTH == fl->getBzXmlRoot()) {
		return Transfer::USER_LIST_NAME_BZ;
	} else if(aTTH == fl->getXmlRoot()) {
		return Transfer::USER_LIST_NAME;
	}

	return tree->toVirtual(aTTH);
}

FileList* ShareManager::getFileList(ProfileToken aProfile) const {
	const auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
	if (i != shareProfiles.end()) {
		dcassert((*i)->getProfileList());
		return (*i)->getProfileList();
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

pair<int64_t, string> ShareManager::getFileListInfo(const string& aVirtualFile, ProfileToken aProfile) {
	if (aVirtualFile == "MyList.DcLst")
		throw ShareException("NMDC-style lists no longer supported, please upgrade your client");

	if (aVirtualFile == Transfer::USER_LIST_NAME_BZ || aVirtualFile == Transfer::USER_LIST_NAME) {
		FileList* fl = generateXmlList(aProfile);
		return { fl->getBzXmlListLen(), fl->getFileName() };
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

void ShareManager::toRealWithSize(const string& aVirtualFile, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_) {
	RLock l(cs);
	return tree->toRealWithSize(aVirtualFile, aProfiles, aUser, path_, size_, noAccess_);
}

TTHValue ShareManager::getListTTH(const string& aVirtualFile, ProfileToken aProfile) const {
	RLock l(cs);
	if (aVirtualFile == Transfer::USER_LIST_NAME_BZ) {
		return getFileList(aProfile)->getBzXmlRoot();
	} else if (aVirtualFile == Transfer::USER_LIST_NAME) {
		return getFileList(aProfile)->getXmlRoot();
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

MemoryInputStream* ShareManager::getTree(const string& aVirtualFile, ProfileToken aProfile) const noexcept {
	TigerTree tigerTree;
	if (aVirtualFile.compare(0, 4, "TTH/") == 0) {
		if (!HashManager::getInstance()->getTree(TTHValue(aVirtualFile.substr(4)), tigerTree))
			return nullptr;
	} else {
		try {
			TTHValue tth = getListTTH(aVirtualFile, aProfile);
			HashManager::getInstance()->getTree(tth, tigerTree);
		} catch (const Exception&) {
			return nullptr;
		}
	}

	ByteVector buf = tigerTree.getLeafData();
	return new MemoryInputStream(&buf[0], buf.size());
}

AdcCommand ShareManager::getFileInfo(const string& aFile, ProfileToken aProfile) {
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
	return tree->getFileInfo(val);
}

optional<TempShareToken> ShareManager::isTempShared(const UserPtr& aUser, const TTHValue& aTTH) const noexcept {
	RLock l(cs);
	return tree->getTempShareManager().isTempShared(aUser, aTTH);
}

TempShareInfoList ShareManager::getTempShares() const noexcept {
	RLock l(cs);
	return tree->getTempShareManager().getTempShares();
}

optional<TempShareInfo> ShareManager::addTempShare(const TTHValue& aTTH, const string& aName, const string& aFilePath, int64_t aSize, ProfileToken aProfile, const UserPtr& aUser) noexcept {
	// Regular shared file?
	if (isFileShared(aTTH, aProfile)) {
		return nullopt;
	}

	optional<pair<TempShareInfo, bool>> addInfo;

	{
		WLock l(cs);
		addInfo.emplace(tree->getTempShareManager().addTempShare(aTTH, aName, aFilePath, aSize, aUser));
	}

	if (addInfo->second) {
		fire(ShareManagerListener::TempFileAdded(), addInfo->first);
	}

	return addInfo->first;
}

TempShareInfoList ShareManager::getTempShares(const TTHValue& aTTH) const noexcept {
	RLock l(cs);
	return tree->getTempShareManager().getTempShares(aTTH);
}

bool ShareManager::removeTempShare(TempShareToken aId) noexcept {
	optional<TempShareInfo> removedItem;

	{
		WLock l(cs);
		auto removed = tree->getTempShareManager().removeTempShare(aId);
		if (!removed) {
			return false;
		}

		removedItem.emplace(*removed);
	}

	fire(ShareManagerListener::TempFileRemoved(), *removedItem);
	return true;
}

void ShareManager::getRealPaths(const string& aVirtualPath, StringList& realPaths_, const OptionalProfileToken& aProfile) const {
	RLock l(cs);
	return tree->getRealPaths(aVirtualPath, realPaths_, aProfile);
}

bool ShareManager::isRealPathShared(const string& aPath) const noexcept {
	return PathUtil::isDirectoryPath(aPath) ? findDirectoryByRealPath(aPath) : findFileByRealPath(aPath);
}

string ShareManager::realToVirtualAdc(const string& aPath, const OptionalProfileToken& aToken) const noexcept {
	RLock l(cs);
	return tree->realToVirtualAdc(aPath, aToken);
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
		auto realPath = PathUtil::validatePath(aXml.getChildData(), true);
		if(realPath.empty()) {
			continue;
		}

		const auto& loadedVirtualName = aXml.getChildAttrib("Virtual");

		const auto& rootPaths = tree->getRoots();
		auto p = rootPaths.find(realPath);
		if (p != rootPaths.end()) {
			p->second->getRoot()->addRootProfile(aToken);
		} else {
			auto incoming = aXml.getBoolChildAttrib("Incoming");
			auto lastRefreshTime = aXml.getLongLongChildAttrib("LastRefreshTime");

			// Validate in case we have changed the rules
			auto vName = validateVirtualName(loadedVirtualName.empty() ? PathUtil::getLastDir(realPath) : loadedVirtualName);
			tree->addShareRoot(realPath, vName, { aToken }, incoming, 0, lastRefreshTime);
		}
	}

	aXml.resetCurrentChild();

	if (sp->isDefault()) {
		validator->loadExcludes(aXml);
	}

	aXml.stepOut();
}

void ShareManager::load(SimpleXML& aXml) {
	aXml.resetCurrentChild();
	if(aXml.findChild("Share")) {
		const auto& name = aXml.getChildAttrib("Name");
		loadProfile(aXml, !name.empty() ? name : STRING(DEFAULT), aXml.getIntChildAttrib("Token"));
	}

	aXml.resetCurrentChild();
	while (aXml.findChild("ShareProfile")) {
		const auto& token = aXml.getIntChildAttrib("Token");
		const auto& name = aXml.getChildAttrib("Name");
		if (token != SP_HIDDEN && !name.empty()) {
			loadProfile(aXml, name, token);
		}
	}
}

void ShareManager::on(SettingsManagerListener::LoadCompleted, bool) noexcept {
	validator->reloadSkiplist();

	{
		// Check share profiles
		if (!getShareProfile(SETTING(DEFAULT_SP))) {
			if (shareProfiles.empty()) {
				auto sp = std::make_shared<ShareProfile>(STRING(DEFAULT), SETTING(DEFAULT_SP));
				shareProfiles.push_back(sp);
			} else {
				SettingsManager::getInstance()->set(SettingsManager::DEFAULT_SP, shareProfiles.front()->getToken());
			}
		}

		auto hiddenProfile = std::make_shared<ShareProfile>(STRING(SHARE_HIDDEN), SP_HIDDEN);
		shareProfiles.push_back(hiddenProfile);
	}

	{
		// Validate loaded paths
		auto rootPathsCopy = tree->getRoots();
		for (const auto& dp : rootPathsCopy) {
			if (find_if(rootPathsCopy | views::keys, [&dp](const string& aPath) {
				return PathUtil::isSubLocal(dp.first, aPath);
			}).base() != rootPathsCopy.end()) {
				tree->removeShareRoot(dp.first);

				log("The directory " + dp.first + " was not loaded: parent of this directory is shared in another profile, which is not supported in this client version.", LogMessage::SEV_WARNING);
			}
		}
	}
}

ShareProfilePtr ShareManager::getShareProfile(ProfileToken aProfile, bool aAllowFallback /*false*/) const noexcept {
	RLock l (cs);
	const auto p = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
	if (p != shareProfiles.end()) {
		return *p;
	} else if (aAllowFallback) {
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
		return nullopt;
	return (*p)->getToken();
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
		ShareRefreshInfo(aPath, aOldRoot, 0, aBloom),
		ThreadedCallBack(aOldRoot->getRoot()->getCacheXmlPath()),
		curDirPath(aOldRoot->getRoot()->getPath()),
		curDirPathLower(aOldRoot->getRoot()->getPathLower())
	{ 
		cur = newShareDirectory;
	}


	void startTag(const string& aName, StringPairList& aAttribs, bool aSimple) {
		if(compare(aName, SDIRECTORY) == 0) {
			const string& name = getAttrib(aAttribs, SNAME, 0);
			const string& date = getAttrib(aAttribs, DATE, 1);

			if (!name.empty()) {
				curDirPath += name + PATH_SEPARATOR;

				cur = ShareDirectory::createNormal(name, cur, Util::toTimeT(date), lowerDirNameMapNew, bloom);
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
				cur->addFile(std::move(name), fi, tthIndexNew, bloom, stats.addedSize);
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
	void endTag(const string& name) {
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

	ShareDirectory::Ptr cur;

	string curDirPathLower;
	string curDirPath;
};

typedef shared_ptr<ShareManager::ShareLoader> ShareLoaderPtr;
typedef vector<ShareLoaderPtr> LoaderList;

bool ShareManager::loadCache(function<void(float)> progressF) noexcept {
	HashManager::HashPauser pauser;

	AppUtil::migrate(AppUtil::getPath(AppUtil::PATH_SHARECACHE), "ShareCache_*");

	LoaderList cacheLoaders;

	// Create loaders
	for (const auto& rp: tree->getRoots()) {
		try {
			auto loader = std::make_shared<ShareLoader>(rp.first, rp.second, *tree->getBloom());
			cacheLoaders.emplace_back(loader);
		} catch (const FileException&) {
			log(STRING_F(SHARE_CACHE_FILE_MISSING, rp.first), LogMessage::SEV_ERROR);
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
			parallel_for_each(cacheLoaders.begin(), cacheLoaders.end(), [&](ShareLoaderPtr& i) {
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
	//validateDirectoryTreeDebug();
#endif

	if (stats.hashSize > 0) {
		log(STRING_F(FILES_ADDED_FOR_HASH_STARTUP, Util::formatBytes(stats.hashSize)), LogMessage::SEV_INFO);
	}

	return true;
}

void ShareManager::save(SimpleXML& aXml) {
	RLock l(cs);
	for(const auto& sp: shareProfiles | views::filter(ShareProfile::NotHidden())) {
		auto isDefault = sp->getToken() == SETTING(DEFAULT_SP);

		aXml.addTag(isDefault ? "Share" : "ShareProfile"); // Keep the old Share tag around for compatibility with other clients
		aXml.addChildAttrib("Token", sp->getToken());
		aXml.addChildAttrib("Name", sp->getPlainName());
		aXml.stepIn();

		for(const auto& d: tree->getRoots() | views::values | views::filter(ShareDirectory::HasRootProfile(sp->getToken()))) {
			aXml.addTag("Directory", d->getRealPath());
			aXml.addChildAttrib("Virtual", d->getRoot()->getName());
			aXml.addChildAttrib("Incoming", d->getRoot()->getIncoming());
			aXml.addChildAttrib("LastRefreshTime", d->getRoot()->getLastRefreshTime());
		}

		if (isDefault) {
			// Excludes are global so they need to be saved only once
			validator->saveExcludes(aXml);
		}

		aXml.stepOut();
	}
}

optional<ShareItemStats> ShareManager::getShareItemStats() const noexcept {
	ShareItemStats stats;
	stats.profileCount = shareProfiles.size() - 1; // remove hidden

	time_t totalAge = 0;

	{
		RLock l(cs);
		tree->countStats(totalAge, stats.totalDirectoryCount, stats.totalSize, stats.totalFileCount, stats.uniqueFileCount, stats.lowerCaseFiles, stats.totalNameSize, stats.rootDirectoryCount);
	}

	if (stats.uniqueFileCount == 0 || stats.totalDirectoryCount == 0) {
		return nullopt;
	}

	stats.averageFileAge = GET_TIME() - static_cast<time_t>(Util::countAverage(totalAge, stats.totalFileCount));
	stats.averageNameLength = Util::countAverage(stats.totalNameSize, stats.totalFileCount + stats.totalDirectoryCount);
	return stats;
}

ShareSearchStats ShareManager::getSearchMatchingStats() const noexcept {
	return tree->getSearchMatchingStats();
}

void ShareManager::validateRootPath(const string& aRealPath, bool aMatchCurrentRoots) const {
	validator->validateRootPath(aRealPath);

	if (aMatchCurrentRoots) {
		RLock l(cs);
		for (const auto& p: tree->getRoots()) {
			auto rootProfileNames = ShareProfile::getProfileNames(p.second->getRoot()->getRootProfiles(), shareProfiles);
			if (PathUtil::isParentOrExactLocal(p.first, aRealPath)) {
				if (Util::stricmp(p.first, aRealPath) != 0) {
					// Subdirectory of an existing directory is not allowed
					throw ShareException(STRING_F(DIRECTORY_PARENT_SHARED, Util::listToString(rootProfileNames)));
				}

				throw ShareException(STRING(DIRECTORY_SHARED));
			}

			if (PathUtil::isSubLocal(p.first, aRealPath)) {
				throw ShareException(STRING_F(DIRECTORY_SUBDIRS_SHARED, Util::listToString(rootProfileNames)));
			}
		}
	}
}

void ShareManager::getProfileInfo(ProfileToken aProfile, int64_t& totalSize_, size_t& filesCount_) const noexcept {
	auto sp = getShareProfile(aProfile);
	if (!sp)
		return;

	if (sp->getProfileInfoDirty()) {
		{
			RLock l(cs);
			for (const auto& d : tree->getRoots() | views::values) {
				if (d->getRoot()->hasRootProfile(aProfile)) {
					d->getProfileInfo(aProfile, totalSize_, filesCount_);
				}
			}
		}

		sp->setSharedFiles(filesCount_);
		sp->setShareSize(totalSize_);
		sp->setProfileInfoDirty(false);
	}

	totalSize_ = sp->getShareSize();
	filesCount_ = sp->getSharedFiles();
}

int64_t ShareManager::getTotalShareSize(ProfileToken aProfile) const noexcept {
	RLock l(cs);
	return tree->getTotalShareSize(aProfile);
}

DupeType ShareManager::getAdcDirectoryDupe(const string& aAdcPath, int64_t aSize) const noexcept{
	RLock l(cs);
	return tree->getAdcDirectoryDupe(aAdcPath, aSize);
}

StringList ShareManager::getAdcDirectoryDupePaths(const string& aAdcPath) const noexcept {
	RLock l(cs);
	return tree->getAdcDirectoryDupePaths(aAdcPath);
}

bool ShareManager::isFileShared(const TTHValue& aTTH) const noexcept{
	RLock l (cs);
	return tree->isFileShared(aTTH);
}

bool ShareManager::isFileShared(const TTHValue& aTTH, ProfileToken aProfile) const noexcept{
	RLock l (cs);
	return tree->isFileShared(aTTH, aProfile);
}

bool ShareManager::findDirectoryByRealPath(const string& aPath, const DirectoryCallback& aCallback) const noexcept {
	RLock l(cs);
	auto directory = tree->findDirectory(aPath);
	if (!directory) {
		return false;
	}

	if (aCallback) {
		aCallback(directory);
	}

	return true;
}

bool ShareManager::findFileByRealPath(const string& aPath, const FileCallback& aCallback) const noexcept {
	RLock l(cs);
	auto file = tree->findFile(aPath);
	if (!file) {
		return false;
	}

	if (aCallback) {
		aCallback(*file);
	}

	return true;
}

ShareDirectory::File::ConstSet ShareManager::findFiles(const TTHValue& aTTH) const noexcept {
	RLock l(cs);
	return tree->findFiles(aTTH);
}

ShareManager::RefreshTaskHandler::ShareBuilder::ShareBuilder(const string& aPath, const ShareDirectory::Ptr& aOldRoot, time_t aLastWrite, ShareBloom& bloom_, ShareManager* aSm) :
	sm(*aSm), ShareRefreshInfo(aPath, aOldRoot, aLastWrite, bloom_) {

}

bool ShareManager::RefreshTaskHandler::ShareBuilder::buildTree(const bool& aStopping) noexcept {
	try {
		buildTree(path, Text::toLower(path), newShareDirectory, oldShareDirectory, aStopping);
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
				RLock l(sm.cs);
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
			auto curDir = ShareDirectory::createNormal(std::move(dualName), aParent, i->getLastWriteTime(), lowerDirNameMapNew, bloom);
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
					RLock l(sm.cs);
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
					aParent->addFile(std::move(dualName), fi, tthIndexNew, bloom, stats.addedSize);
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
		RLock l(cs);
		for(const auto& d: tree->getRoots() | views::values) {
			if (Util::stricmp(d->getRoot()->getNameLower(), aVirtualName) == 0) {
				refreshDirs.push_back(d->getRealPath());
			}
		}
	}

	if (refreshDirs.empty()) {
		return nullopt;
	}

	return tasks->addRefreshTask(aPriority, refreshDirs, ShareRefreshType::REFRESH_DIRS, aVirtualName);
}


RefreshTaskQueueInfo ShareManager::refresh(ShareRefreshType aType, ShareRefreshPriority aPriority, function<void(float)> progressF /*nullptr*/) noexcept {
	StringList dirs;

	{
		RLock l (cs);
		for (const auto& d: tree->getRoots() | views::values) {
			if (aType == ShareRefreshType::REFRESH_INCOMING && !d->getRoot()->getIncoming())
				continue;

			dirs.push_back(d->getRoot()->getPath());
		}
	}

	return tasks->addRefreshTask(aPriority, dirs, aType, Util::emptyString, progressF);
}

optional<RefreshTaskQueueInfo> ShareManager::refreshPathsHooked(ShareRefreshPriority aPriority, const StringList& aPaths, const void* aCaller, const string& aDisplayName /*Util::emptyString*/, function<void(float)> aProgressF /*nullptr*/) noexcept {
	try {
		return refreshPathsHookedThrow(aPriority, aPaths, aCaller, aDisplayName, aProgressF);
	} catch (const Exception&) {
		// ...
	}

	return nullopt;
}


RefreshTaskQueueInfo ShareManager::refreshPathsHookedThrow(ShareRefreshPriority aPriority, const StringList& aPaths, const void* aCaller, const string& aDisplayName, function<void(float)> aProgressF) {
	for (const auto& path : aPaths) {
		// Ensure that the path exists in share (or it can be added)
		validatePathHooked(path, false, aCaller);
	}

	return tasks->addRefreshTask(aPriority, aPaths, ShareRefreshType::REFRESH_DIRS, aDisplayName, aProgressF);
}

bool ShareManager::handleRefreshPath(const string& aRefreshPath, const ShareRefreshTask& aTask, ShareRefreshStats& totalStats, ShareBloom* bloom_, ProfileTokenSet& dirtyProfiles_) noexcept {
	ShareDirectory::Ptr directory = nullptr;

	{
		RLock l(cs);
		directory = tree->findDirectory(aRefreshPath);
	}

	auto ri = RefreshTaskHandler::ShareBuilder(aRefreshPath, directory, File::getLastModified(aRefreshPath), *bloom_, this);
	setRefreshState(ri.path, ShareRootRefreshState::STATE_RUNNING, false, aTask.token);

	// Build the tree
	auto completed = ri.buildTree(aTask.canceled);

	// Apply the changes
	if (completed) {
		{
			WLock l(cs);
			tree->applyRefreshChanges(ri, &dirtyProfiles_);
		}

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

			WLock l(cs);
			tree->setBloom(bloom_);
		}
		else {
			delete bloom_;
		}
	}

	setProfilesDirty(dirtyProfiles_, aTask.priority == ShareRefreshPriority::MANUAL || aTask.type == ShareRefreshType::REFRESH_ALL || aTask.type == ShareRefreshType::BUNDLE);

	fire(ShareManagerListener::RefreshCompleted(), aTask, aCompleted, aTotalStats);

#ifdef _DEBUG
	RLock l(cs);
	tree->validateDirectoryTreeDebug();
#endif
}

ShareManager::RefreshTaskHandler::RefreshTaskHandler(ShareBloom* aBloom, PathRefreshF aPathRefreshF, CompletionF aCompletionF) : bloom(std::move(aBloom)), pathRefreshF(aPathRefreshF), completionF(aCompletionF) {

}

void ShareManager::RefreshTaskHandler::refreshCompleted(bool aCompleted, const ShareRefreshTask& aTask, const ShareRefreshStats& aTotalStats) {
	return completionF(aCompleted, aTask, aTotalStats, bloom, dirtyProfiles);
}

bool ShareManager::RefreshTaskHandler::refreshPath(const string& aRefreshPath, const ShareRefreshTask& aTask, ShareRefreshStats& totalStats) {
	return pathRefreshF(aRefreshPath, aTask, totalStats, bloom, dirtyProfiles);
}

unique_ptr<ShareTasksManager::RefreshTaskHandler> ShareManager::startRefresh(const ShareRefreshTask& aTask) noexcept {
	auto refreshBloom = aTask.type == ShareRefreshType::REFRESH_ALL ? new ShareBloom(1 << 20) : tree->getBloom();
	ProfileTokenSet dirtyProfiles;

	if (aTask.type == ShareRefreshType::REFRESH_INCOMING) {
		lastIncomingUpdate = GET_TICK();
	}
	else if (aTask.type == ShareRefreshType::REFRESH_ALL) {
		lastFullUpdate = GET_TICK();
		lastIncomingUpdate = GET_TICK();
	}

	return make_unique<ShareManager::RefreshTaskHandler>(
		refreshBloom,
		std::bind(&ShareManager::handleRefreshPath, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5),
		std::bind(&ShareManager::onRefreshTaskCompleted, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5)
	);
}

void ShareManager::onRefreshQueued(const ShareRefreshTask& aTask) noexcept {
	for (auto& path : aTask.dirs) {
		setRefreshState(path, ShareRootRefreshState::STATE_PENDING, false, aTask.token);
	}

	fire(ShareManagerListener::RefreshQueued(), aTask);

}

void ShareManager::setRefreshState(const string& aRefreshPath, ShareRootRefreshState aState, bool aUpdateRefreshTime, const optional<ShareRefreshTaskToken>& aRefreshTaskToken) noexcept {
	ShareRoot::Ptr rootDir;

	{
		RLock l(cs);
		rootDir = tree->setRefreshState(aRefreshPath, aState, aUpdateRefreshTime, aRefreshTaskToken);
	}

	fire(ShareManagerListener::RootRefreshState(), rootDir->getPath());
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



// PROFILES
void ShareManager::setDefaultProfile(ProfileToken aNewDefault) noexcept {
	auto oldDefault = SETTING(DEFAULT_SP);

	{
		WLock l(cs);
		// Put the default profile on top
		auto p = ranges::find(shareProfiles, aNewDefault, &ShareProfile::getToken);
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
		for (auto& root : tree->getRoots()) {
			auto profiles = root.second->getRoot()->getRootProfiles();
			profiles.erase(aToken);
			root.second->getRoot()->setRootProfiles(profiles);

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
	
	fire(ShareManagerListener::ProfileRemoved(), aToken); //removeRootDirectories() might take a while so fire listener first.
	removeRootDirectories(removedPaths);
	return true;
}

void ShareManager::setProfilesDirty(const ProfileTokenSet& aProfiles, bool aIsMajorChange /*false*/) noexcept {
	if (!aProfiles.empty()) {
		RLock l(cs);
		for (const auto token : aProfiles) {
			auto i = find(shareProfiles.begin(), shareProfiles.end(), token);
			if (i != shareProfiles.end()) {
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

ShareProfileList ShareManager::getProfiles() const noexcept {
	RLock l(cs);
	return shareProfiles;
}

ShareProfileInfo::List ShareManager::getProfileInfos() const noexcept {
	ShareProfileInfo::List ret;

	RLock l(cs);
	for (const auto& sp : shareProfiles | views::filter(ShareProfile::NotHidden())) {
		auto p = std::make_shared<ShareProfileInfo>(sp->getPlainName(), sp->getToken());
		if (p->token == SETTING(DEFAULT_SP)) {
			p->isDefault = true;
			ret.emplace(ret.begin(), p);
		} else {
			ret.emplace_back(p);
		}
	}

	return ret;
}


// ROOTS
ShareDirectoryInfoList ShareManager::getRootInfos() const noexcept {
	RLock l(cs);
	return tree->getRootInfos();
}

ShareDirectoryInfoPtr ShareManager::getRootInfo(const string& aPath) const noexcept {
	RLock l(cs);
	return tree->getRootInfo(aPath);
}

bool ShareManager::addRootDirectory(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept {
	dcassert(!aDirectoryInfo->profiles.empty());
	const auto& path = aDirectoryInfo->path;

	{
		WLock l(cs);
		if (!tree->addShareRoot(aDirectoryInfo)) {
			return false;
		}
	}

	fire(ShareManagerListener::RootCreated(), path);
	tasks->addRefreshTask(ShareRefreshPriority::MANUAL, { path }, ShareRefreshType::ADD_DIR);

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
		if (!tree->removeShareRoot(aPath)) {
			return false;
		}
	}

	HashManager::getInstance()->stopHashing(aPath);

	log(STRING_F(SHARED_DIR_REMOVED, aPath), LogMessage::SEV_INFO);

	fire(ShareManagerListener::RootRemoved(), aPath);
	setProfilesDirty(dirtyProfiles, true);
	return true;
}

void ShareManager::removeRootDirectories(const StringList& aRemoveDirs) noexcept{

	for(const auto& path: aRemoveDirs) {
		removeRootDirectory(path);
	}


#ifdef _DEBUG
	RLock l(cs);
	tree->validateDirectoryTreeDebug();
#endif
}

bool ShareManager::updateRootDirectory(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept {
	dcassert(!aDirectoryInfo->profiles.empty());
	ShareRoot::Ptr rootDirectory;
	ProfileTokenSet dirtyProfiles = aDirectoryInfo->profiles;

	{
		WLock l(cs);
		if (!tree->updateShareRoot(aDirectoryInfo)) {
			return false;
		}
	}

	setProfilesDirty(dirtyProfiles, true);

	fire(ShareManagerListener::RootUpdated(), aDirectoryInfo->path);

	return true;
}

void ShareManager::updateRootDirectories(const ShareDirectoryInfoList& changedDirs) noexcept {
	for(const auto& dirInfo: changedDirs) {
		updateRootDirectory(dirInfo);
	}

#ifdef _DEBUG
	RLock l(cs);
	tree->validateDirectoryTreeDebug();
#endif
}
	
void ShareManager::getBloom(HashBloom& bloom_) const noexcept {
	RLock l(cs);
	tree->getBloom(bloom_);
}

string ShareManager::generateOwnList(ProfileToken aProfile) {
	FileList* fl = generateXmlList(aProfile, true);
	return fl->getFileName();
}


//forwards the calls to createFileList for creating the filelist that was reguested.
FileList* ShareManager::generateXmlList(ProfileToken aProfile, bool forced /*false*/) {
	ShareProfilePtr shareProfile = nullptr;

	{
		RLock l(cs);
		const auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
		if(i == shareProfiles.end()) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		shareProfile = *i;
	}

	// The filelist generation code currently causes the filelist to get corrupted if the size is over 1 gigabytes, which has happened with a share of over 30 million files
	// Uploading filelists of that size would get problematic, as loading them would most likely crash all 32 bit clients
	// Limit the maximum file count to 20 million, to be somewhat safe
	if (shareProfile->getSharedFiles() > 20000000) {
		throw ShareException("The size of the filelist exceeds the maximum limit of 1 GB / 20 million files; please use a partial list instead");
	}

	FileList* fl = shareProfile->getProfileList();

	{
		Lock lFl(fl->cs);
		if (fl->allowGenerateNew(forced)) {
			auto tmpName = fl->getFileName().substr(0, fl->getFileName().length() - 4);
			try {
				{
					File f(tmpName, File::RW, File::TRUNCATE | File::CREATE, File::BUFFER_SEQUENTIAL, false);

					{
						RLock l(cs);
						tree->toFilelist(f, ADC_ROOT_STR, aProfile, true, duplicateFilelistFileLogger);
					}

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
	if (aVirtualPath.front() != ADC_SEPARATOR || aVirtualPath.back() != ADC_SEPARATOR) {
		return 0;
	}

	string xml = Util::emptyString;

	{
		StringOutputStream sos(xml);

		RLock l(cs);
		tree->toFilelist(sos, aVirtualPath, aProfile, aRecursive, duplicateFilelistFileLogger);
	}

	if (xml.empty()) {
		dcdebug("Partial NULL");
		return nullptr;
	} else {
		dcdebug("Partial list generated (%s)\n", aVirtualPath.c_str());
		return new MemoryInputStream(xml);
	}
}

#define LITERAL(n) n, sizeof(n)-1

void ShareManager::saveShareCache(function<void(float)> progressF /*nullptr*/) noexcept {

	if (shareCacheSaving)
		return;

	shareCacheSaving = true;

	if (progressF)
		progressF(0);

	int cur = 0;
	ShareDirectory::List dirtyDirs;

	{
		RLock l(cs);
		ranges::copy_if(tree->getRoots() | views::values, back_inserter(dirtyDirs), [](const ShareDirectory::Ptr& aDir) { 
			return aDir->getRoot()->getCacheDirty() && !aDir->getParent(); 
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

						xmlFile.write(SimpleXML::utf8Header);
						xmlFile.write(LITERAL("<Share Version=\"" SHARE_CACHE_VERSION));
						xmlFile.write(LITERAL("\" Path=\""));
						xmlFile.write(SimpleXML::escape(d->getRoot()->getPath(), tmp, true));

						xmlFile.write(LITERAL("\" Date=\""));
						xmlFile.write(SimpleXML::escape(Util::toString(d->getLastWrite()), tmp, true));
						xmlFile.write(LITERAL("\">\r\n"));
						indent += '\t';

						for (const auto& child : d->getDirectories()) {
							child->toCacheXmlList(xmlFile, indent, tmp);
						}
						d->filesToCacheXmlList(xmlFile, indent, tmp);

						xmlFile.write(LITERAL("</Share>"));
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

MemoryInputStream* ShareManager::generateTTHList(const string& aVirtualPath, bool recurse, ProfileToken aProfile) const noexcept {
	
	if(aProfile == SP_HIDDEN)
		return nullptr;
	
	string tths;
	string tmp;
	StringOutputStream sos(tths);
	ShareDirectory::List result;

	try{
		RLock l(cs);
		tree->findVirtuals<ProfileToken>(aVirtualPath, aProfile, result);
		for(const auto& it: result) {
			//dcdebug("result name %s \n", (*it)->getRoot()->getName(aProfile));
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

void ShareManager::search(SearchResultList& results_, SearchQuery& srch, const OptionalProfileToken& aProfile, const UserPtr& aUser, const string& aDir, bool aIsAutoSearch) {
	RLock l(cs);
	tree->search(results_, srch, aProfile, aUser, aDir, aIsAutoSearch);
}

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

	auto path = aBundle->getTarget();
	tasks->addRefreshTask(ShareRefreshPriority::NORMAL, { aBundle->getTarget() }, ShareRefreshType::BUNDLE, aBundle->getTarget());
}

void ShareManager::onFileHashed(const string& aRealPath, const HashedFile& aFileInfo) noexcept {
	ProfileTokenSet dirtyProfiles;

	{
		WLock l(cs);
		tree->addHashedFile(aRealPath, aFileInfo, &dirtyProfiles);
	}

	setProfilesDirty(dirtyProfiles, false);
}

bool ShareManager::allowShareDirectoryHooked(const string& aRealPath, const void* aCaller) const noexcept {
	try {
		validatePathHooked(aRealPath, false, aCaller);
		return true;
	} catch (const Exception&) { }

	return false;
}

void ShareManager::validatePathHooked(const string& aRealPath, bool aSkipQueueCheck, const void* aCaller) const {
	StringList tokens;
	ShareDirectory::Ptr baseDirectory = nullptr;

	auto isDirectoryPath = PathUtil::isDirectoryPath(aRealPath);
	auto isFileShared = false;

	{
		RLock l(cs);
		baseDirectory = tree->findDirectory(!isDirectoryPath ? PathUtil::getFilePath(aRealPath) : aRealPath, tokens);
		if (!baseDirectory) {
			throw ShareException(STRING(DIRECTORY_NOT_FOUND));
		}

		if (!isDirectoryPath && tokens.empty()) {
			auto fileNameLower = Text::toLower(PathUtil::getFileName(aRealPath));
			isFileShared = baseDirectory->findFileLower(fileNameLower);
		}
	}


	// Validate missing directory path tokens
	validator->validateNewDirectoryPathTokensHooked(baseDirectory->getRealPath(), tokens, aSkipQueueCheck, aCaller);

	if (!isDirectoryPath && !isFileShared) {
		// Validate the file
		validator->validateNewPathHooked(aRealPath, aSkipQueueCheck, !tokens.empty(), aCaller);
	}
}

GroupedDirectoryMap ShareManager::getGroupedDirectories() const noexcept {
	GroupedDirectoryMap ret;
	
	{
		RLock l (cs);
		for (const auto& d: tree->getRoots() | views::values) {
			const auto& currentPath = d->getRoot()->getPath();
			auto virtualName = d->getRoot()->getName();

			ret[virtualName].insert(currentPath);
		}
	}

	return ret;
}

StringSet ShareManager::getExcludedPaths() const noexcept {
	return validator->getExcludedPaths();
}

void ShareManager::addExcludedPath(const string& aPath) {
	StringList rootPaths;

	{
		RLock l(cs);
		rootPaths = tree->getRootPaths();
	}

	validator->addExcludedPath(aPath, rootPaths);
	fire(ShareManagerListener::ExcludeAdded(), aPath);
}

bool ShareManager::removeExcludedPath(const string& aPath) noexcept {
	if (validator->removeExcludedPath(aPath)) {
		fire(ShareManagerListener::ExcludeRemoved(), aPath);
		return true;
	}

	return false;
}

void ShareManager::reloadSkiplist() {
	validator->reloadSkiplist();
}

void ShareManager::setExcludedPaths(const StringSet& aPaths) noexcept {
	validator->setExcludedPaths(aPaths);
}

} // namespace dcpp
