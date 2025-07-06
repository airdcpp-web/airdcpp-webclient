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
#include <airdcpp/share/ShareTree.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/util/DupeUtil.h>
#include <airdcpp/core/io/File.h>
#include <airdcpp/core/io/stream/FilteredFile.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/search/SearchResult.h>
#include <airdcpp/search/SearchQuery.h>
#include <airdcpp/share/SharePathValidator.h>
#include <airdcpp/share/profiles/ShareProfile.h>
#include <airdcpp/share/ShareRefreshInfo.h>
#include <airdcpp/core/io/xml/SimpleXML.h>
#include <airdcpp/util/text/StringTokenizer.h>
#include <airdcpp/connection/UserConnection.h>

#include <airdcpp/core/thread/concurrency.h>

namespace dcpp {

using ranges::find_if;
using ranges::copy;


ShareTree::ShareTree() : bloom(make_unique<ShareBloom>(1 << 20)), ShareTreeMaps([this] { return bloom.get(); })
{
#if defined(_DEBUG) && defined(_WIN32)
	testDualString();
#endif
}

void ShareTree::getRealPaths(const TTHValue& aTTH, StringList& paths_) const noexcept {
	RLock l(cs);
	const auto i = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
	for (const auto& f: i | pair_to_range | views::values) {
		paths_.push_back(f->getRealPath());
	}
}

bool ShareTree::isFileShared(const TTHValue& aTTH) const noexcept {
	RLock l(cs);
	return tthIndex.contains(const_cast<TTHValue*>(&aTTH));
}

bool ShareTree::toRealWithSize(const UploadFileQuery& aQuery, string& path_, int64_t& size_, bool& noAccess_) const noexcept {
	if (aQuery.profiles && ranges::all_of(*aQuery.profiles, [](ProfileToken s) { return s == SP_HIDDEN; })) {
		return false;
	}

	RLock l(cs);
	const auto flst = tthIndex.equal_range(const_cast<TTHValue*>(&aQuery.tth));
	for(const auto& file: flst | pair_to_range | views::values) {
		if (!aQuery.profiles || file->getParent()->hasProfile(*aQuery.profiles)) {
			noAccess_ = false;
			path_ = file->getRealPath();
			size_ = file->getSize();
			return true;
		} else {
			noAccess_ = true;
		}
	}

	return false;
}

AdcCommand ShareTree::getFileInfo(const TTHValue& aTTH) const {
	RLock l(cs);
	if (auto i = tthIndex.find(const_cast<TTHValue*>(&aTTH)); i != tthIndex.end()) {
		const ShareDirectory::File* f = i->second;
		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", f->getAdcPath());
		cmd.addParam("SI", Util::toString(f->getSize()));
		cmd.addParam("TR", f->getTTH().toBase32());
		return cmd;
	}

	//not found throw
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

void ShareTree::getRealPaths(const string& aVirtualPath, StringList& realPaths_, const OptionalProfileToken& aProfile) const {
	if (aVirtualPath.empty())
		throw ShareException("empty virtual path");

	if (aVirtualPath == ADC_ROOT_STR) {
		realPaths_ = getRootPathList();
		return;
	}

	ShareDirectory::List dirs;

	RLock l(cs);
	getDirectoriesByVirtualUnsafe<OptionalProfileToken>(aVirtualPath, aProfile, dirs);

	if (aVirtualPath.back() == ADC_SEPARATOR) {
		// Directory
		for (const auto& d : dirs) {
			realPaths_.push_back(d->getRealPathUnsafe());
		}
	} else {
		// File
		auto fileNameLower = Text::toLower(PathUtil::getAdcFileName(aVirtualPath));
		for (const auto& d: dirs) {
			auto file = d->findFileLower(fileNameLower);
			if (file) {
				realPaths_.push_back(file->getRealPath());
				return;
			}
		}
	}
}

string ShareTree::realToVirtualAdc(const string& aPath, const OptionalProfileToken& aToken) const noexcept{
	RLock l(cs);
	auto d = findDirectoryUnsafe(PathUtil::getFilePath(aPath));
	if (!d || !d->hasProfile(aToken)) {
		return Util::emptyString;
	}

	auto vPathAdc = d->getAdcPathUnsafe();
	if (PathUtil::isDirectoryPath(aPath)) {
		// Directory
		return vPathAdc;
	}

	// It's a file
	return vPathAdc + ADC_SEPARATOR_STR + PathUtil::getFileName(aPath);
}

string ShareTree::validateVirtualName(const string& aVirt) const noexcept {
	string tmp = aVirt;
	string::size_type idx = 0;

	while( (idx = tmp.find_first_of("\\/"), idx) != string::npos) {
		tmp[idx] = '_';
	}
	return tmp;
}

void ShareTree::countStats(time_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles_, size_t& uniqueFiles, size_t& lowerCaseFiles_, size_t& totalStrLen_, size_t& roots_) const noexcept{
	unordered_set<decltype(tthIndex)::key_type> uniqueTTHs;

	RLock l(cs);

	for (auto tth : tthIndex | views::keys) {
		uniqueTTHs.insert(tth);
	}

	uniqueFiles = uniqueTTHs.size();

	for (const auto& d : rootPaths | views::values) {
		totalDirs_++;
		roots_++;
		d->countStats(totalAge_, totalDirs_, totalSize_, totalFiles_, lowerCaseFiles_, totalStrLen_);
	}
}

void ShareTree::getRootsUnsafe(const OptionalProfileToken& aProfile, ShareDirectory::List& dirs_) const noexcept {
	ranges::copy(rootPaths | views::values | views::filter(ShareDirectory::HasRootProfile(aProfile)), back_inserter(dirs_));
}

ShareDirectory::Ptr ShareTree::findRootUnsafe(const string& aRootPath) const noexcept {
	auto i = rootPaths.find(aRootPath);
	return i != rootPaths.end() ? i->second : nullptr;
}

string ShareTree::parseRoot(const string& aRealPath) const noexcept {
	RLock l(cs);
	auto root = parseRootUnsafe(aRealPath);
	if (!root) {
		return Util::emptyString;
	}

	return root->getRealPathUnsafe();
}

ShareDirectory::Ptr ShareTree::parseRootUnsafe(const string& aRealPath) const noexcept {
	auto mi = find_if(rootPaths | views::values, ShareDirectory::RootIsParentOrExact(aRealPath)).base();
	return mi == rootPaths.end() ? nullptr : mi->second;
}

ShareDirectory::List ShareTree::getRoots(const OptionalProfileToken& aProfile) const noexcept {
	ShareDirectory::List dirs;
	{
		RLock l(cs);
		getRootsUnsafe(aProfile, dirs);
	}
	return dirs;
}

void ShareTree::getRootsByVirtualUnsafe(const string& aVirtualName, const OptionalProfileToken& aProfile, ShareDirectory::List& dirs_) const noexcept {
	for (const auto& d : rootPaths | views::values | views::filter(ShareDirectory::HasRootProfile(aProfile))) {
		if (Util::stricmp(d->getRoot()->getNameLower(), aVirtualName) == 0) {
			dirs_.push_back(d);
		}
	}
}

void ShareTree::getRootsByVirtualUnsafe(const string& aVirtualName, const ProfileTokenSet& aProfiles, ShareDirectory::List& dirs_) const noexcept {
	for(const auto& d: rootPaths | views::values) {
		// Compare name
		if (Util::stricmp(d->getRoot()->getNameLower(), aVirtualName) != 0) {
			continue;
		}

		// Find any matching profile
		if (ShareProfile::hasCommonProfiles(d->getRoot()->getRootProfiles(), aProfiles)) {
			dirs_.push_back(d);
		}
	}
}

DupeType ShareTree::getAdcDirectoryDupe(const string& aAdcPath, int64_t aSize) const noexcept{
	ShareDirectory::List dirs;

	RLock l(cs);
	getDirectoriesByAdcNameUnsafe(aAdcPath, dirs);

	if (dirs.empty())
		return DUPE_NONE;

	return dirs.front()->getTotalSize() == aSize ? DUPE_SHARE_FULL : DUPE_SHARE_PARTIAL;
}

StringList ShareTree::getAdcDirectoryDupePaths(const string& aAdcPath) const noexcept{
	StringList ret;
	ShareDirectory::List dirs;

	{
		RLock l(cs);
		getDirectoriesByAdcNameUnsafe(aAdcPath, dirs);
		for (const auto& dir : dirs) {
			ret.push_back(dir->getRealPathUnsafe());
		}
	}

	return ret;
}

void ShareTree::getDirectoriesByAdcNameUnsafe(const string& aAdcPath, ShareDirectory::List& dirs_) const noexcept {
	if (aAdcPath.size() < 3)
		return;

	// get the last meaningful directory to look up
	auto [directoryName, subDirStart] = DupeUtil::getAdcDirectoryName(aAdcPath);

	auto nameLower = Text::toLower(directoryName);
	const auto directories = lowerDirNameMap.equal_range(&nameLower);
	if (directories.first == directories.second)
		return;

	for (const auto& directory: directories | pair_to_range | views::values) {
		if (subDirStart != string::npos) {
			// confirm that we have the subdirectory as well
			auto dir = directory->findDirectoryByPath(aAdcPath.substr(subDirStart), ADC_SEPARATOR);
			if (dir) {
				dirs_.push_back(dir);
			}
		} else {
			dirs_.push_back(directory);
		}
	}
}

bool ShareTree::isFileShared(const TTHValue& aTTH, ProfileToken aProfile) const noexcept{
	RLock l (cs);
	const auto files = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
	for(auto f: files | pair_to_range | views::values) {
		if (f->getParent()->hasProfile(aProfile)) {
			return true;
		}
	}

	return false;
}

ShareDirectory::File* ShareTree::findFileUnsafe(const string& aPath) const noexcept {
	auto d = findDirectoryUnsafe(PathUtil::getFilePath(aPath));
	if (d) {
		auto fileNameLower = Text::toLower(PathUtil::getFileName(aPath));
		auto file = d->findFileLower(fileNameLower);
		if (file) {
			return file;
		}
	}

	return nullptr;
}

ShareDirectory::File::ConstSet ShareTree::findFiles(const TTHValue& aTTH) const noexcept {
	ShareDirectory::File::ConstSet ret;

	{
		RLock l(cs);
		auto files = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
		for (auto& f : files | pair_to_range | views::values) {
			ret.insert_sorted(f);
		}
	}

	return ret;
}

StringList ShareTree::getRootPathList() const noexcept {
	StringList paths;

	RLock l(cs);
	ranges::copy(rootPaths | views::keys, back_inserter(paths));
	return paths;
}

ShareRootList ShareTree::getShareRoots() const noexcept {
	ShareRootList roots;

	RLock l(cs);
	ranges::copy(rootPaths | views::values | views::transform(ShareDirectory::ToRoot), back_inserter(roots));
	return roots;
}

ShareDirectory::Map ShareTree::getRootPaths() const noexcept {
	RLock l(cs);
	return rootPaths;
}

ShareRoot::Ptr ShareTree::setRefreshState(const string& aRefreshPath, ShareRootRefreshState aState, bool aUpdateRefreshTime, const optional<ShareRefreshTaskToken>& aRefreshTaskToken) const noexcept {
	ShareRoot::Ptr rootDir;

	{
		RLock l(cs);
		auto p = find_if(rootPaths | views::values, [&](const ShareDirectory::Ptr& aDir) {
			return PathUtil::isParentOrExactLocal(aDir->getRoot()->getPath(), aRefreshPath);
		});

		if (p.base() == rootPaths.end()) {
			return nullptr;
		}

		rootDir = (*p)->getRoot();
	}

	// We want to fire a root update also when refreshing subdirectories (as the size/content may have changed)
	// but don't change the refresh state
	if (aRefreshPath == rootDir->getPath()) {
		rootDir->setRefreshState(aState);
		rootDir->setRefreshTaskToken(aRefreshTaskToken);
		if (aUpdateRefreshTime) {
			rootDir->setLastRefreshTime(GET_TIME());
		}
	}

	return rootDir;
}


ShareRoot::Ptr ShareTree::addShareRoot(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept {
	auto lastModified = File::getLastModified(aDirectoryInfo->path);
	auto root = addShareRoot(aDirectoryInfo->path, aDirectoryInfo->virtualName, aDirectoryInfo->profiles, aDirectoryInfo->incoming, lastModified, 0);

#ifdef _DEBUG
	validateDirectoryTreeDebug();
#endif

	return root;
}

ShareRoot::Ptr ShareTree::addShareRoot(const string& aPath, const string& aVirtualName, const ProfileTokenSet& aProfiles, bool aIncoming, time_t aLastModified, time_t aLastRefreshed) noexcept {
	// dcassert(!aDirectoryInfo->profiles.empty());
	// const auto& path = aDirectoryInfo->path;

	WLock l(cs);
	if (rootPaths.contains(aPath)) {
		return nullptr;
	}

	dcassert(find_if(rootPaths | views::keys, IsParentOrExact(aPath, PATH_SEPARATOR)).base() == rootPaths.end());

	// It's a new parent, will be handled in the task thread
	auto root = ShareDirectory::createRoot(aPath, aVirtualName, aProfiles, aIncoming, aLastModified, *this, aLastRefreshed);
	return root->getRoot();
}

ShareRoot::Ptr ShareTree::removeShareRoot(const string& aPath) noexcept {
	ShareDirectory::Ptr directory = nullptr;

	{
		WLock l(cs);
		auto k = rootPaths.find(aPath);
		if (k == rootPaths.end()) {
			return nullptr;
		}

		directory = k->second;

		rootPaths.erase(k);

		// Remove the root
		ShareDirectory::cleanIndices(*directory, sharedSize, tthIndex, lowerDirNameMap);
	}

	File::deleteFile(directory->getRoot()->getCacheXmlPath());

#ifdef _DEBUG
	validateDirectoryTreeDebug();
#endif

	return directory->getRoot();
}

void ShareTree::removeProfile(ProfileToken aProfile, StringList& rootsToRemove_) noexcept {
	WLock l(cs);
	for (auto const& [path, root] : rootPaths) {
		if (root->getRoot()->removeRootProfile(aProfile)) {
			rootsToRemove_.push_back(path);
		}
	}
}

ShareRoot::Ptr ShareTree::updateShareRoot(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept {
	ShareRoot::Ptr rootDirectory = nullptr;

	auto vName = validateVirtualName(aDirectoryInfo->virtualName);
	{
		WLock l(cs);
		auto directory = findRootUnsafe(aDirectoryInfo->path);
		if (!directory) {
			return nullptr;
		}

		rootDirectory = directory->getRoot();

		ShareDirectory::removeDirName(*directory, lowerDirNameMap);
		rootDirectory->setName(vName);
		ShareDirectory::addDirName(directory, lowerDirNameMap, *bloom.get());
	}

	rootDirectory->setIncoming(aDirectoryInfo->incoming);
	rootDirectory->setRootProfiles(aDirectoryInfo->profiles);

#ifdef _DEBUG
	validateDirectoryTreeDebug();
#endif

	return rootDirectory;
}

bool ShareTree::applyRefreshChanges(ShareRefreshInfo& ri, ProfileTokenSet* aDirtyProfiles) {
	ShareDirectory::Ptr parent = nullptr;

	WLock l(cs);

	// Recursively remove the content of this dir from TTHIndex and directory name map
	if (ri.optionalOldDirectory) {
		// Root removed while refreshing?
		if (ri.optionalOldDirectory->isRoot() && !findRootUnsafe(ri.path)) {
			return false;
		}

		parent = ri.optionalOldDirectory->getParent();

		// Remove the old directory
		ShareDirectory::cleanIndices(*ri.optionalOldDirectory, sharedSize, tthIndex, lowerDirNameMap);
	}

	// Set the parent for refreshed subdirectories
	// (previous directory should always be available for roots)
	if (!ri.optionalOldDirectory || !ri.optionalOldDirectory->isRoot()) {
		// All content was removed?
		if (!ri.checkContent(ri.newDirectory)) {
			return false;
		}

		if (!parent) {
			// Create new parent
			parent = ensureDirectoryUnsafe(PathUtil::getParentDir(ri.path));
			if (!parent) {
				return false;
			}
		}

		// Set the parent
		if (!ShareDirectory::setParent(ri.newDirectory, parent)) {
			return false;
		}
	}

	ri.applyRefreshChanges(lowerDirNameMap, rootPaths, tthIndex, sharedSize, aDirtyProfiles);
	dcdebug("Share changes applied for the directory %s\n", ri.path.c_str());
	return true;
}

ShareDirectoryInfoPtr ShareTree::getRootInfoUnsafe(const ShareDirectory::Ptr& aDir) const noexcept {
	auto& rootDir = aDir->getRoot();

	auto contentInfo(DirectoryContentInfo::empty());
	int64_t size = 0;
	aDir->getContentInfo(size, contentInfo);

	auto info = std::make_shared<ShareDirectoryInfo>(aDir->getRealPathUnsafe());
	info->profiles = rootDir->getRootProfiles();
	info->incoming = rootDir->getIncoming();
	info->size = size;
	info->contentInfo = contentInfo;
	info->virtualName = rootDir->getName();
	info->refreshState = static_cast<uint8_t>(rootDir->getRefreshState());
	info->lastRefreshTime = rootDir->getLastRefreshTime();
	info->refreshTaskToken = rootDir->getRefreshTaskToken();
	return info;
}

ShareDirectoryInfoPtr ShareTree::getRootInfo(const string& aPath) const noexcept {
	RLock l(cs);
	if (auto directory = findRootUnsafe(aPath); directory) {
		return getRootInfoUnsafe(directory);
	}

	return nullptr;
}

ShareDirectoryInfoList ShareTree::getRootInfos() const noexcept {
	ShareDirectoryInfoList ret;

	RLock l (cs);
	for(const auto& d: rootPaths | views::values) {
		ret.push_back(getRootInfoUnsafe(d));
	}

	return ret;
}
		
void ShareTree::getBloom(ProfileToken aToken, HashBloom& bloom_) const noexcept {
	RLock l(cs);
	for (const auto& [tth, file] : tthIndex) {
		if (file->hasProfile(aToken)) {
			bloom_.add(*tth);
		}
	}
}

void ShareTree::getBloomFileCount(ProfileToken aToken, size_t& fileCount_) const noexcept {
	int64_t totalSize = 0;
	getProfileInfo(aToken, totalSize, fileCount_);
}

void ShareTree::setBloom(ShareBloom* aBloom) noexcept {
	WLock l(cs);
	bloom.reset(aBloom);
}

#define LITERAL(n) n, sizeof(n)-1
void ShareTree::toCache(OutputStream& os_, const ShareDirectory::Ptr& aDirectory) const {
	string indent, tmp;

	os_.write(SimpleXML::utf8Header);
	os_.write(LITERAL("<Share Version=\"" SHARE_CACHE_VERSION));
	os_.write(LITERAL("\" Path=\""));
	os_.write(SimpleXML::escape(aDirectory->getRoot()->getPath(), tmp, true));

	os_.write(LITERAL("\" Date=\""));
	os_.write(SimpleXML::escape(Util::toString(aDirectory->getLastWrite()), tmp, true));
	os_.write(LITERAL("\">\r\n"));
	indent += '\t';

	{
		RLock l(cs);
		for (const auto& child : aDirectory->getDirectories()) {
			child->toCacheXmlList(os_, indent, tmp);
		}
		aDirectory->filesToCacheXmlList(os_, indent, tmp);
	}

	os_.write(LITERAL("</Share>"));
}

void ShareTree::toFilelist(OutputStream& os_, const string& aVirtualPath, const OptionalProfileToken& aProfile, bool aRecursive, const FilelistDirectory::DuplicateFileHandler& aDuplicateFileHandler) const {
	ShareDirectory::List currentDirectory, children;

	dcdebug("Generating filelist for %s \n", aVirtualPath.c_str());

	RLock l(cs);

	// Get the directories
	if (aVirtualPath == ADC_ROOT_STR) {
		// We are getting the children of the root (we don't have an actual share directory for root)
		getRootsUnsafe(aProfile, children);
	} else {
		try {
			// We need to save the current directory content as well for listing the files
			getDirectoriesByVirtualUnsafe<OptionalProfileToken>(aVirtualPath, aProfile, currentDirectory);
		} catch (...) {
			return;
		}

		for (const auto& d : currentDirectory) {
			copy(d->getDirectories(), back_inserter(children));
		}
	}

	auto listRoot = FilelistDirectory::generateRoot(currentDirectory, children, aRecursive);
	{
		// Write the XML
		string tmp, indent = "\t";

		os_.write(SimpleXML::utf8Header);
		os_.write(R"(<FileListing Version="1" CID=")" + ClientManager::getInstance()->getMyCID().toBase32() +
			"\" Base=\"" + SimpleXML::escape(aVirtualPath, tmp, false) +
			"\" BaseDate=\"" + Util::toString(listRoot->getDate()) +
			"\" Generator=\"" + shortVersionString + "\">\r\n");

		for (const auto& ld : listRoot->getListDirectories() | views::values) {
			ld->toXml(os_, indent, tmp, aRecursive, aDuplicateFileHandler);
		}
		listRoot->filesToXml(os_, indent, tmp, !aRecursive, aDuplicateFileHandler);
	}

	os_.write("</FileListing>");
}

void ShareTree::toTTHList(OutputStream& os_, const string& aVirtualPath, bool aRecursive, ProfileToken aProfile) const noexcept {
	ShareDirectory::List directories;
	string tmp;

	RLock l(cs);
	try {
		getDirectoriesByVirtualUnsafe<ProfileToken>(aVirtualPath, aProfile, directories);
	} catch(...) {
	}

	for (const auto& it : directories) {
		//dcdebug("result name %s \n", (*it)->getRoot()->getName(aProfile));
		it->toTTHList(os_, tmp, aRecursive);
	}
}

bool ShareTree::addDirectoryResultUnsafe(const ShareDirectory* aDir, SearchResultList& aResults, const OptionalProfileToken& aProfile, const SearchQuery& srch) const noexcept {
	const string path = srch.addParents ? PathUtil::getAdcParentDir(aDir->getAdcPathUnsafe()) : aDir->getAdcPathUnsafe();

	// Have we added it already?
	if (ranges::any_of(aResults, [&path](const SearchResultPtr& sr) { return Util::stricmp(sr->getAdcPath(), path) == 0; }))
		return false;

	// Get all directories with this path
	ShareDirectory::List result;

	try {
		getDirectoriesByVirtualUnsafe<OptionalProfileToken>(path, aProfile, result);
	} catch(...) {
		dcassert(path.empty());
	}

	// Count date and content information
	time_t date = 0;
	int64_t size = 0;
	auto contentInfo(DirectoryContentInfo::empty());
	for(const auto& d: result) {
		d->getContentInfo(size, contentInfo);
		date = max(date, d->getLastWrite());
	}

	if (srch.matchesDate(date)) {
		auto sr = make_shared<SearchResult>(SearchResult::Type::DIRECTORY, size, path, TTHValue(), date, contentInfo);
		aResults.push_back(sr);
		return true;
	}

	return false;
}

void ShareTree::search(SearchResultList& results, const TTHValue& aTTH, const ShareSearch& aSearchInfo) const noexcept {
	RLock l(cs);
	const auto i = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
	for (auto& f : i | pair_to_range | views::values) {
		if (f->hasProfile(aSearchInfo.profile) && PathUtil::isParentOrExactAdc(aSearchInfo.virtualPath, f->getAdcPath())) {
			f->addSR(results, aSearchInfo.search.addParents);
			return;
		}
	}
}

void ShareTree::getProfileInfo(ProfileToken aProfile, int64_t& totalSize_, size_t& filesCount_) const noexcept {
	ShareDirectory::List roots;

	RLock l(cs);
	getRootsUnsafe(aProfile, roots);
	for (const auto& d : roots) {
		d->getProfileInfo(aProfile, totalSize_, filesCount_);
	}
}


void ShareTree::searchText(SearchResultList& results_, ShareSearch& aSearchInfo, ShareSearchCounters& counters_) const {
	dcassert(!aSearchInfo.virtualPath.empty());

	counters_.totalSearches++;
	if (aSearchInfo.profile == SP_HIDDEN) {
		return;
	}

	auto& srch = aSearchInfo.search;
	dcassert(!srch.root);

	counters_.recursiveSearches++;
	if (aSearchInfo.isAutoSearch) {
		counters_.autoSearches++;
	}

	if (!matchBloom(srch)) {
		counters_.filteredSearches++;
		return;
	}

	ShareDirectory::SearchResultInfo::Set resultInfos;

	RLock l(cs);
	{
		auto endF = counters_.onMatchingRecursiveSearch(srch);

		// Get the search roots
		ShareDirectory::List roots;
		if (aSearchInfo.virtualPath == ADC_ROOT_STR) {
			getRootsUnsafe(aSearchInfo.profile, roots);
		} else {
			getDirectoriesByVirtualUnsafe<OptionalProfileToken>(aSearchInfo.virtualPath, aSearchInfo.profile, roots);
		}

		// go them through recursively
		for (const auto& d: roots) {
			d->search(resultInfos, srch, 0);
		}

		endF();
	}


	// pick the results to return
	for (auto i = resultInfos.begin(); (i != resultInfos.end()) && (results_.size() < srch.maxResults); ++i) {
		auto& info = *i;
		if (info.getType() == ShareDirectory::SearchResultInfo::DIRECTORY) {
			addDirectoryResultUnsafe(info.directory, results_, aSearchInfo.profile, srch);
		} else {
			info.file->addSR(results_, srch.addParents);
		}
	}

	if (!results_.empty()) {
		counters_.recursiveSearchesResponded++;
	}
}

bool ShareTree::matchBloom(const SearchQuery& aSearch) const noexcept {
	RLock l(cs);
	auto matches = ranges::all_of(aSearch.include.getPatterns(), [this](auto& p) { return bloom->match(p.str()); });
	return matches;
}

Callback ShareSearchCounters::onMatchingRecursiveSearch(const SearchQuery& aSearch) noexcept {
	auto start = GET_TICK();
	return [start, &aSearch, this] {
		auto end = GET_TICK();
		recursiveSearchTime += end - start;
		searchTokenCount += aSearch.include.count();
		for (const auto& p : aSearch.include.getPatterns())
			searchTokenLength += p.size();
	};
}

ShareSearchStats ShareSearchCounters::toStats() const noexcept {
	auto upseconds = static_cast<double>(GET_TICK()) / 1000.00;

	ShareSearchStats stats;

	stats.totalSearches = totalSearches;
	stats.totalSearchesPerSecond = Util::countAverage(totalSearches, upseconds);
	stats.recursiveSearches = recursiveSearches;
	stats.recursiveSearchesResponded = recursiveSearchesResponded;
	stats.filteredSearches = filteredSearches;
	stats.unfilteredRecursiveSearchesPerSecond = static_cast<double>(recursiveSearches - filteredSearches) / upseconds;

	stats.averageSearchMatchMs = static_cast<uint64_t>(Util::countAverage(recursiveSearchTime, recursiveSearches - filteredSearches));
	stats.averageSearchTokenCount = Util::countAverage(searchTokenCount, recursiveSearches - filteredSearches);
	stats.averageSearchTokenLength = Util::countAverage(searchTokenLength, searchTokenCount);

	stats.autoSearches = autoSearches;
	stats.tthSearches = tthSearches;

	return stats;
}

ShareDirectory::Ptr ShareTree::findDirectoryUnsafe(const string& aRealPath, StringList& remainingTokens_) const noexcept {
	auto curDir = parseRootUnsafe(aRealPath);
	if (!curDir) {
		return nullptr;
	}

	remainingTokens_ = StringTokenizer<string>(aRealPath.substr(curDir->getRealPathUnsafe().length()), PATH_SEPARATOR).getTokens();

	bool hasMissingToken = false;
	std::erase_if(remainingTokens_, [&](const string& currentName) {
		if (!hasMissingToken) {
			auto d = curDir->findDirectoryLower(Text::toLower(currentName));
			if (d) {
				curDir = d;
				return true;
			}

			hasMissingToken = true;
		}

		return false;
	});

	return curDir;
}

ShareDirectory::Ptr ShareTree::ensureDirectoryUnsafe(const string& aRealPath) noexcept {
	StringList tokens;

	// Find the existing directories
	auto curDir = findDirectoryUnsafe(aRealPath, tokens);
	if (!curDir) {
		return nullptr;
	}

	// Create missing directories
	// Tokens should have been validated earlier
	for (const auto& curName: tokens) {
		curDir->updateModifyDate();
		curDir = ShareDirectory::createNormal(DualString(curName), curDir, File::getLastModified(curDir->getRealPathUnsafe()), *this);
	}

	return curDir;
}

void ShareTree::validateRootPath(const string& aRealPath, const ProfileFormatter& aProfileFormatter) const {
	RLock l(cs);
	for (const auto& [rootPath, rootDirectory] : rootPaths) {
		if (PathUtil::isParentOrExactLocal(rootPath, aRealPath)) {
			if (Util::stricmp(rootPath, aRealPath) != 0) {
				// Subdirectory of an existing directory is not allowed
				throw ShareException(STRING_F(DIRECTORY_PARENT_SHARED, aProfileFormatter(rootDirectory->getRoot()->getRootProfiles())));
			}

			throw ShareException(STRING(DIRECTORY_SHARED));
		}

		if (PathUtil::isSubLocal(rootPath, aRealPath)) {
			throw ShareException(STRING_F(DIRECTORY_SUBDIRS_SHARED, aProfileFormatter(rootDirectory->getRoot()->getRootProfiles())));
		}
	}
}

ShareDirectory::Ptr ShareTree::findDirectoryUnsafe(const string& aRealPath) const noexcept {
	StringList tokens;
	auto curDir = findDirectoryUnsafe(aRealPath, tokens);
	return tokens.empty() ? curDir : nullptr;
}

bool ShareTree::findDirectoryByRealPath(const string& aPath, const ShareDirectoryCallback& aCallback) const noexcept {
	RLock l(cs);
	auto directory = findDirectoryUnsafe(aPath);
	if (!directory) {
		return false;
	}

	if (aCallback) {
		aCallback(directory);
	}

	return true;
}

bool ShareTree::findFileByRealPath(const string& aPath, const ShareFileCallback& aCallback) const noexcept {
	RLock l(cs);
	auto file = findFileUnsafe(aPath);
	if (!file) {
		return false;
	}

	if (aCallback) {
		aCallback(*file);
	}

	return true;
}


void ShareTree::addHashedFile(const string& aRealPath, const HashedFile& aFileInfo, ProfileTokenSet* dirtyProfiles) noexcept {
	WLock l(cs);
	auto d = ensureDirectoryUnsafe(PathUtil::getFilePath(aRealPath));
	if (!d) {
		return;
	}

	d->addFile(PathUtil::getFileName(aRealPath), aFileInfo, *this, sharedSize, dirtyProfiles);
}


// DEBUG CODE
#ifdef _DEBUG

#ifdef _WIN32 // Text::wideToUtf8 is available only on Windows
void ShareTree::testDualString() {
	{
		auto emoji = Text::wideToUtf8(L"\U0001F30D");

		DualString d1(emoji);
		dcassert(d1.getNormal() == emoji);
		dcassert(d1.getLower() == emoji);
	}

	{
		auto character = _T("\u00D6"); // Ö
		DualString d2(Text::wideToUtf8(character));
		dcassert(d2.getNormal() != d2.getLower());
	}

	{
		auto character = Text::wideToUtf8(L"I\u0307"); // Capital i with a dot
		DualString d2(character);
		dcassert(d2.getNormal() != d2.getLower());
	}
}
#endif

void ShareTree::validateDirectoryTreeDebug() const noexcept {
	RLock l(cs);
	OrderedStringSet directories, files;

	auto start = GET_TICK();
	{
		for (const auto& d : rootPaths | views::values) {
			validateDirectoryRecursiveDebugUnsafe(d, directories, files);
		}
	}
	auto end = GET_TICK();
	dcdebug("Share tree checked in " U64_FMT " ms\n", end - start);

	StringList filesDiff, directoriesDiff;
	if (files.size() != tthIndex.size()) {
		OrderedStringSet indexed;
		for (const auto& f : tthIndex | views::values) {
			indexed.insert(f->getRealPath());
		}

		set_symmetric_difference(files.begin(), files.end(), indexed.begin(), indexed.end(), back_inserter(filesDiff));
	}

	if (directories.size() != lowerDirNameMap.size()) {
		OrderedStringSet indexed;
		for (const auto& d : lowerDirNameMap | views::values) {
			indexed.insert(d->getRealPathUnsafe());
		}

		set_symmetric_difference(directories.begin(), directories.end(), indexed.begin(), indexed.end(), back_inserter(directoriesDiff));
	}

	dcassert(directoriesDiff.empty() && filesDiff.empty());
}

void ShareTree::validateDirectoryRecursiveDebugUnsafe(const ShareDirectory::Ptr& aDir, OrderedStringSet& directoryPaths_, OrderedStringSet& filePaths_) const noexcept {
	{
		auto [_, isUnique] = directoryPaths_.insert(aDir->getRealPathUnsafe());
		dcassert(isUnique);
	}

	{
		ShareDirectory::List dirs;
		getDirectoriesByAdcNameUnsafe(aDir->getAdcPathUnsafe(), dirs);

		dcassert(ranges::count_if(dirs, [&](const ShareDirectory::Ptr& d) {
			return d->getRealPathUnsafe() == aDir->getRealPathUnsafe();
		}) == 1);

		dcassert(bloom->match(aDir->getVirtualNameLower()));
	}

	int64_t realDirectorySize = 0;
	for (const auto& f : aDir->getFiles()) {
		auto flst = tthIndex.equal_range(const_cast<TTHValue*>(&f->getTTH()));
		dcassert(ranges::count_if(flst | pair_to_range | views::values, [&](const ShareDirectory::File* aFile) {
			return aFile->getRealPath() == f->getRealPath();
		}) == 1);

		dcassert(bloom->match(f->getName().getLower()));
		auto [_, isUnique] = filePaths_.insert(f->getRealPath());
		dcassert(isUnique);
		realDirectorySize += f->getSize();
	}

	auto cachedDirectorySize = aDir->getLevelSize();
	dcassert(cachedDirectorySize == realDirectorySize);

	for (const auto& d : aDir->getDirectories()) {
		validateDirectoryRecursiveDebugUnsafe(d, directoryPaths_, filePaths_);
	}
}

#endif


} // namespace dcpp
