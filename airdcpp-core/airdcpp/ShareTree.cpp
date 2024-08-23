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
#include "ShareTree.h"

#include "ClientManager.h"
#include "DupeUtil.h"
#include "File.h"
#include "FilteredFile.h"
#include "PathUtil.h"
#include "ResourceManager.h"
#include "SearchResult.h"
#include "SearchQuery.h"
#include "SharePathValidator.h"
#include "ShareProfile.h"
#include "ShareRefreshInfo.h"
#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "UserConnection.h"

#include "concurrency.h"

namespace dcpp {

using ranges::find_if;
using ranges::copy;


ShareTree::ShareTree() : bloom(new ShareBloom(1 << 20))
{ 

#ifdef _DEBUG
#ifdef _WIN32
	{
		auto emoji = Text::wideToUtf8(L"\U0001F30D");

		DualString d1(emoji);
		dcassert(d1.getNormal() == emoji);
		dcassert(d1.getLower() == emoji);
	}

	{
		auto character = _T("\u00D6"); // �
		DualString d2(Text::wideToUtf8(character));
		dcassert(d2.getNormal() != d2.getLower());
	}
#endif
#endif
}

StringList ShareTree::getRealPaths(const TTHValue& aTTH) const noexcept {
	StringList ret;

	// RLock l(cs);
	const auto i = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
	for (auto f = i.first; f != i.second; ++f) {
		ret.push_back((*f).second->getRealPath());
	}

	for (const auto& item : tempShare.getTempShares(aTTH)) {
		ret.push_back(item.path);
	}

	return ret;
}

bool ShareTree::isTTHShared(const TTHValue& aTTH) const noexcept {
	// RLock l(cs);
	return tthIndex.find(const_cast<TTHValue*>(&aTTH)) != tthIndex.end();
}

string ShareTree::toVirtual(const TTHValue& aTTH) const {
	// RLock l(cs);
	auto i = tthIndex.find(const_cast<TTHValue*>(&aTTH));
	if (i != tthIndex.end()) {
		return i->second->getAdcPath();
	}

	//nothing found throw;
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

void ShareTree::toRealWithSize(const string& aVirtualFile, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_) {
	if(aVirtualFile.compare(0, 4, "TTH/") == 0) {
		TTHValue tth(aVirtualFile.substr(4));

		// RLock l(cs);
		if (ranges::any_of(aProfiles, [](ProfileToken s) { return s != SP_HIDDEN; })) {
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

		for (const auto& item: tempShare.getTempShares(tth)) {
			noAccess_ = false;
			if (!item.hasAccess(aUser)) {
				noAccess_ = true;
			} else {
				path_ = item.path;
				size_ = item.size;
				return;
			}
		}
	} else {
		ShareDirectory::List dirs;

		// RLock l (cs);
		findVirtuals<ProfileTokenSet>(aVirtualFile, aProfiles, dirs);

		auto fileNameLower = Text::toLower(PathUtil::getAdcFileName(aVirtualFile));
		for(const auto& d: dirs) {
			auto file = d->findFileLower(fileNameLower);
			if (file) {
				path_ = file->getRealPath();
				size_ = file->getSize();
				return;
			}
		}
	}

	throw ShareException(noAccess_ ? "You don't have access to this file" : UserConnection::FILE_NOT_AVAILABLE);
}

AdcCommand ShareTree::getFileInfo(const TTHValue& aTTH) const {
	// RLock l(cs);
	auto i = tthIndex.find(const_cast<TTHValue*>(&aTTH));
	if(i != tthIndex.end()) {
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
		realPaths_ = getRootPaths();
		return;
	}

	ShareDirectory::List dirs;

	// RLock l(cs);
	findVirtuals<OptionalProfileToken>(aVirtualPath, aProfile, dirs);

	if (aVirtualPath.back() == ADC_SEPARATOR) {
		// Directory
		for (const auto& d : dirs) {
			realPaths_.push_back(d->getRealPath());
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
	// RLock l(cs);
	auto d = findDirectory(PathUtil::getFilePath(aPath));
	if (!d || !d->hasProfile(aToken)) {
		return Util::emptyString;
	}

	auto vPathAdc = d->getAdcPath();
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

void ShareTree::countStats(time_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles_, size_t uniqueFiles, size_t& lowerCaseFiles_, size_t& totalStrLen_, size_t& roots_) const noexcept{
	unordered_set<decltype(tthIndex)::key_type> uniqueTTHs;

	// RLock l(cs);

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

ShareSearchStats ShareTree::getSearchMatchingStats() const noexcept {
	auto upseconds = static_cast<double>(GET_TICK()) / 1000.00;

	ShareSearchStats stats;

	stats.totalSearches = totalSearches;
	stats.totalSearchesPerSecond = Util::countAverage(totalSearches, upseconds);
	stats.recursiveSearches = recursiveSearches;
	stats.recursiveSearchesResponded = recursiveSearchesResponded;
	stats.filteredSearches = filteredSearches;
	stats.unfilteredRecursiveSearchesPerSecond = (recursiveSearches - filteredSearches) / upseconds;

	stats.averageSearchMatchMs = static_cast<uint64_t>(Util::countAverage(recursiveSearchTime, recursiveSearches - filteredSearches));
	stats.averageSearchTokenCount = Util::countAverage(searchTokenCount, recursiveSearches - filteredSearches);
	stats.averageSearchTokenLength = Util::countAverage(searchTokenLength, searchTokenCount);

	stats.autoSearches = autoSearches;
	stats.tthSearches = tthSearches;

	return stats;
}

void ShareTree::getRoots(const OptionalProfileToken& aProfile, ShareDirectory::List& dirs_) const noexcept {
	ranges::copy(rootPaths | views::values | views::filter(ShareDirectory::HasRootProfile(aProfile)), back_inserter(dirs_));
}

void ShareTree::getRootsByVirtual(const string& aVirtualName, const OptionalProfileToken& aProfile, ShareDirectory::List& dirs_) const noexcept {
	for(const auto& d: rootPaths | views::values | views::filter(ShareDirectory::HasRootProfile(aProfile))) {
		if(Util::stricmp(d->getRoot()->getName(), aVirtualName) == 0) {
			dirs_.push_back(d);
		}
	}
}

void ShareTree::getRootsByVirtual(const string& aVirtualName, const ProfileTokenSet& aProfiles, ShareDirectory::List& dirs_) const noexcept {
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

int64_t ShareTree::getTotalShareSize(ProfileToken aProfile) const noexcept {
	int64_t ret = 0;

	{
		// RLock l(cs);
		for (const auto& d : rootPaths | views::values) {
			if (d->getRoot()->hasRootProfile(aProfile)) {
				ret += d->getTotalSize();
			}
		}
	}

	return ret;
}

DupeType ShareTree::getAdcDirectoryDupe(const string& aAdcPath, int64_t aSize) const noexcept{
	ShareDirectory::List dirs;

	// RLock l(cs);
	getDirectoriesByAdcName(aAdcPath, dirs);

	if (dirs.empty())
		return DUPE_NONE;

	return dirs.front()->getTotalSize() == aSize ? DUPE_SHARE_FULL : DUPE_SHARE_PARTIAL;
}

StringList ShareTree::getAdcDirectoryDupePaths(const string& aAdcPath) const noexcept{
	StringList ret;
	ShareDirectory::List dirs;

	{
		// RLock l(cs);
		getDirectoriesByAdcName(aAdcPath, dirs);
	}

	for (const auto& dir : dirs) {
		ret.push_back(dir->getRealPath());
	}

	return ret;
}

void ShareTree::getDirectoriesByAdcName(const string& aAdcPath, ShareDirectory::List& dirs_) const noexcept {
	if (aAdcPath.size() < 3)
		return;

	// get the last meaningful directory to look up
	auto nameInfo = DupeUtil::getAdcDirectoryName(aAdcPath);

	auto nameLower = Text::toLower(nameInfo.first);
	const auto directories = lowerDirNameMap.equal_range(&nameLower);
	if (directories.first == directories.second)
		return;

	for (auto s = directories.first; s != directories.second; ++s) {
		if (nameInfo.second != string::npos) {
			// confirm that we have the subdirectory as well
			auto dir = s->second->findDirectoryByPath(aAdcPath.substr(nameInfo.second), ADC_SEPARATOR);
			if (dir) {
				dirs_.push_back(dir);
			}
		} else {
			dirs_.push_back(s->second);
		}
	}
}

bool ShareTree::isFileShared(const TTHValue& aTTH) const noexcept{
	// RLock l (cs);
	return tthIndex.find(const_cast<TTHValue*>(&aTTH)) != tthIndex.end();
}

bool ShareTree::isFileShared(const TTHValue& aTTH, ProfileToken aProfile) const noexcept{
	// RLock l (cs);
	const auto files = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
	for(auto i = files.first; i != files.second; ++i) {
		if(i->second->getParent()->hasProfile(aProfile)) {
			return true;
		}
	}

	return false;
}

ShareDirectory::File* ShareTree::findFile(const string& aPath) const noexcept {
	auto d = findDirectory(PathUtil::getFilePath(aPath));
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
	auto files = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));

	ShareDirectory::File::ConstSet ret;
	for (auto& f: files | pair_to_range | views::values) {
		ret.insert_sorted(f);
	}

	return ret;
}

#ifdef _DEBUG

void ShareTree::validateDirectoryTreeDebug() const noexcept {
	OrderedStringSet directories, files;

	auto start = GET_TICK();
	{
		// RLock l(cs);
		for (const auto& d : rootPaths | views::values) {
			validateDirectoryRecursiveDebug(d, directories, files);
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
			indexed.insert(d->getRealPath());
		}

		set_symmetric_difference(directories.begin(), directories.end(), indexed.begin(), indexed.end(), back_inserter(directoriesDiff));
	}

	dcassert(directoriesDiff.empty() && filesDiff.empty());
}

void ShareTree::validateDirectoryRecursiveDebug(const ShareDirectory::Ptr& aDir, OrderedStringSet& directoryPaths_, OrderedStringSet& filePaths_) const noexcept {
	{
		auto res = directoryPaths_.insert(aDir->getRealPath());
		dcassert(res.second);
	}

	{
		ShareDirectory::List dirs;
		getDirectoriesByAdcName(aDir->getAdcPath(), dirs);

		dcassert(ranges::count_if(dirs, [&](const ShareDirectory::Ptr& d) {
			return d->getRealPath() == aDir->getRealPath();
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
		auto res = filePaths_.insert(f->getRealPath());
		dcassert(res.second);
		realDirectorySize += f->getSize();
	}

	auto cachedDirectorySize = aDir->getLevelSize();
	dcassert(cachedDirectorySize == realDirectorySize);

	for (const auto& d : aDir->getDirectories()) {
		validateDirectoryRecursiveDebug(d, directoryPaths_, filePaths_);
	}
}

#endif

StringList ShareTree::getRootPaths() const noexcept {
	StringList paths;

	// RLock l(cs);
	ranges::copy(rootPaths | views::keys, back_inserter(paths));
	return paths;
}

ShareRoot::Ptr ShareTree::setRefreshState(const string& aRefreshPath, ShareRootRefreshState aState, bool aUpdateRefreshTime, const optional<ShareRefreshTaskToken>& aRefreshTaskToken) const noexcept {
	ShareRoot::Ptr rootDir;

	{
		// RLock l(cs);
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
	return addShareRoot(aDirectoryInfo->path, aDirectoryInfo->virtualName, aDirectoryInfo->profiles, aDirectoryInfo->incoming, lastModified, 0);
}

ShareRoot::Ptr ShareTree::addShareRoot(const string& aPath, const string& aVirtualName, const ProfileTokenSet& aProfiles, bool aIncoming, time_t aLastModified, time_t aLastRefreshed) noexcept {
	// dcassert(!aDirectoryInfo->profiles.empty());
	// const auto& path = aDirectoryInfo->path;

	// WLock l(cs);
	auto i = rootPaths.find(aPath);
	if (i != rootPaths.end()) {
		return nullptr;
	}

	dcassert(find_if(rootPaths | views::keys, IsParentOrExact(aPath, PATH_SEPARATOR)).base() == rootPaths.end());

	// It's a new parent, will be handled in the task thread
	auto root = ShareDirectory::createRoot(aPath, aVirtualName, aProfiles, aIncoming, aLastModified, rootPaths, lowerDirNameMap, *bloom.get(), aLastRefreshed);
	return root->getRoot();
}

ShareRoot::Ptr ShareTree::removeShareRoot(const string& aPath) noexcept {
	// WLock l(cs);
	auto k = rootPaths.find(aPath);
	if (k == rootPaths.end()) {
		return nullptr;
	}

	auto directory = k->second;

	// dirtyProfiles = k->second->getRoot()->getRootProfiles();

	rootPaths.erase(k);

	// Remove the root
	ShareDirectory::cleanIndices(*directory, sharedSize, tthIndex, lowerDirNameMap);
	File::deleteFile(directory->getRoot()->getCacheXmlPath());
	return directory->getRoot();
}

ShareRoot::Ptr ShareTree::updateShareRoot(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept {
	// dcassert(!aDirectoryInfo->profiles.empty());

	// WLock l(cs);
	auto p = rootPaths.find(aDirectoryInfo->path);
	if (p != rootPaths.end()) {
		auto vName = validateVirtualName(aDirectoryInfo->virtualName);
		auto rootDirectory = p->second->getRoot();

		ShareDirectory::removeDirName(*p->second, lowerDirNameMap);
		rootDirectory->setName(vName);
		ShareDirectory::addDirName(p->second, lowerDirNameMap, *bloom.get());

		rootDirectory->setIncoming(aDirectoryInfo->incoming);
		rootDirectory->setRootProfiles(aDirectoryInfo->profiles);
		return rootDirectory;
	} else {
		return nullptr;
	}
}

bool ShareTree::applyRefreshChanges(ShareRefreshInfo& ri, ProfileTokenSet* aDirtyProfiles) {
	ShareDirectory::Ptr parent = nullptr;

	// Recursively remove the content of this dir from TTHIndex and directory name map
	if (ri.oldShareDirectory) {
		// Root removed while refreshing?
		if (ri.oldShareDirectory->isRoot() && rootPaths.find(ri.path) == rootPaths.end()) {
			return false;
		}

		parent = ri.oldShareDirectory->getParent();

		// Remove the old directory
		ShareDirectory::cleanIndices(*ri.oldShareDirectory, sharedSize, tthIndex, lowerDirNameMap);
	}

	// Set the parent for refreshed subdirectories
	// (previous directory should always be available for roots)
	if (!ri.oldShareDirectory || !ri.oldShareDirectory->isRoot()) {
		// All content was removed?
		if (!ri.checkContent(ri.newShareDirectory)) {
			return false;
		}

		if (!parent) {
			// Create new parent
			parent = getDirectory(PathUtil::getParentDir(ri.path));
			if (!parent) {
				return false;
			}
		}

		// Set the parent
		if (!ShareDirectory::setParent(ri.newShareDirectory, parent)) {
			return false;
		}
	}

	ri.applyRefreshChanges(lowerDirNameMap, rootPaths, tthIndex, sharedSize, aDirtyProfiles);
	dcdebug("Share changes applied for the directory %s\n", ri.path.c_str());
	return true;
}

ShareDirectoryInfoPtr ShareTree::getRootInfo(const ShareDirectory::Ptr& aDir) const noexcept {
	auto& rootDir = aDir->getRoot();

	auto contentInfo(DirectoryContentInfo::empty());
	int64_t size = 0;
	aDir->getContentInfo(size, contentInfo);

	auto info = std::make_shared<ShareDirectoryInfo>(aDir->getRealPath());
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
	// RLock l(cs);
	auto p = rootPaths.find(aPath);
	if (p != rootPaths.end()) {
		return getRootInfo(p->second);
	}

	return nullptr;
}

ShareDirectoryInfoList ShareTree::getRootInfos() const noexcept {
	ShareDirectoryInfoList ret;

	// RLock l (cs);
	for(const auto& d: rootPaths | views::values) {
		ret.push_back(getRootInfo(d));
	}

	return ret;
}
		
void ShareTree::getBloom(HashBloom& bloom_) const noexcept {
	// RLock l(cs);
	for(const auto tth: tthIndex | views::keys)
		bloom_.add(*tth);

	for(const auto& item: tempShare.getTempShares())
		bloom_.add(item.tth);
}

MemoryInputStream* ShareTree::generatePartialList(const string& aVirtualPath, bool aRecursive, const OptionalProfileToken& aProfile, const FilelistDirectory::DuplicateFileHandler& aDuplicateFileHandler) const noexcept {
	if (aVirtualPath.front() != ADC_SEPARATOR || aVirtualPath.back() != ADC_SEPARATOR) {
		return 0;
	}

	string xml = Util::emptyString;

	{
		StringOutputStream sos(xml);
		toFilelist(sos, aVirtualPath, aProfile, aRecursive, aDuplicateFileHandler);
	}

	if (xml.empty()) {
		dcdebug("Partial NULL");
		return nullptr;
	} else {
		dcdebug("Partial list generated (%s)\n", aVirtualPath.c_str());
		return new MemoryInputStream(xml);
	}
}

void ShareTree::toFilelist(OutputStream& os_, const string& aVirtualPath, const OptionalProfileToken& aProfile, bool aRecursive, const FilelistDirectory::DuplicateFileHandler& aDuplicateFileHandler) const {
	FilelistDirectory listRoot(Util::emptyString, 0);
	ShareDirectory::List childDirectories;

	// RLock l(cs);
	dcdebug("Generating filelist for %s \n", aVirtualPath.c_str());

	// Get the directories
	if (aVirtualPath == ADC_ROOT_STR) {
		getRoots(aProfile, childDirectories);
	} else {
		try {
			// We need to save the root directories as well for listing the files directly inside them
			findVirtuals<OptionalProfileToken>(aVirtualPath, aProfile, listRoot.shareDirs);
		} catch (...) {
			return;
		}

		for (const auto& d : listRoot.shareDirs) {
			copy(d->getDirectories(), back_inserter(childDirectories));
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

	for (const auto ld : listRoot.listDirs | views::values) {
		ld->toXml(os_, indent, tmp, aRecursive, aDuplicateFileHandler);
	}
	listRoot.filesToXml(os_, indent, tmp, !aRecursive, aDuplicateFileHandler);

	os_.write("</FileListing>");
}

MemoryInputStream* ShareTree::generateTTHList(const string& aVirtualPath, bool recurse, ProfileToken aProfile) const noexcept {
	
	if(aProfile == SP_HIDDEN)
		return nullptr;
	
	string tths;
	string tmp;
	StringOutputStream sos(tths);
	ShareDirectory::List result;

	try{
		// RLock l(cs);
		findVirtuals<ProfileToken>(aVirtualPath, aProfile, result);
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

bool ShareTree::addDirectoryResult(const ShareDirectory* aDir, SearchResultList& aResults, const OptionalProfileToken& aProfile, SearchQuery& srch) const noexcept {
	const string path = srch.addParents ? PathUtil::getAdcParentDir(aDir->getAdcPath()) : aDir->getAdcPath();

	// Have we added it already?
	auto p = find_if(aResults, [&path](const SearchResultPtr& sr) { return sr->getAdcPath() == path; });
	if (p != aResults.end())
		return false;

	// Get all directories with this path
	ShareDirectory::List result;

	try {
		findVirtuals<OptionalProfileToken>(path, aProfile, result);
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
		auto sr = make_shared<SearchResult>(SearchResult::TYPE_DIRECTORY, size, path, TTHValue(), date, contentInfo);
		aResults.push_back(sr);
		return true;
	}

	return false;
}

void ShareTree::search(SearchResultList& results, SearchQuery& srch, const OptionalProfileToken& aProfile, const UserPtr& aUser, const string& aDir, bool aIsAutoSearch) {
	dcassert(!aDir.empty());

	totalSearches++;
	if (aProfile == SP_HIDDEN) {
		return;
	}

	// RLock l(cs);
	if (srch.root) {
		tthSearches++;
		const auto i = tthIndex.equal_range(const_cast<TTHValue*>(&(*srch.root)));
		for(auto& f: i | pair_to_range | views::values) {
			if (f->hasProfile(aProfile) && PathUtil::isParentOrExactAdc(aDir, f->getAdcPath())) {
				f->addSR(results, srch.addParents);
				return;
			}
		}
		
		const auto items = tempShare.getTempShares(*srch.root);
		for(const auto& item: items) {
			if (item.hasAccess(aUser)) {
				//TODO: fix the date?
				auto sr = make_shared<SearchResult>(SearchResult::TYPE_FILE, item.size, "/tmp/" + item.name, *srch.root, item.timeAdded, DirectoryContentInfo::uninitialized());
				results.push_back(sr);
			}
		}
		return;
	}

	recursiveSearches++;
	if (aIsAutoSearch)
		autoSearches++;

	for (const auto& p : srch.include.getPatterns()) {
		if (!bloom->match(p.str())) {
			filteredSearches++;
			return;
		}
	}

	// Get the search roots
	ShareDirectory::List roots;
	if (aDir == ADC_ROOT_STR) {
		getRoots(aProfile, roots);
	} else {
		findVirtuals<OptionalProfileToken>(aDir, aProfile, roots);
	}

	auto start = GET_TICK();

	// go them through recursively
	ShareDirectory::SearchResultInfo::Set resultInfos;
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
		if (info.getType() == ShareDirectory::SearchResultInfo::DIRECTORY) {
			addDirectoryResult(info.directory, results, aProfile, srch);
		} else {
			info.file->addSR(results, srch.addParents);
		}
	}

	if (!results.empty())
		recursiveSearchesResponded++;
}

ShareDirectory::Ptr ShareTree::findDirectory(const string& aRealPath, StringList& remainingTokens_) const noexcept {
	auto mi = find_if(rootPaths | views::values, ShareDirectory::RootIsParentOrExact(aRealPath)).base();
	if (mi == rootPaths.end()) {
		return nullptr;
	}

	auto curDir = mi->second;

	remainingTokens_ = StringTokenizer<string>(aRealPath.substr(mi->first.length()), PATH_SEPARATOR).getTokens();

	bool hasMissingToken = false;
	remainingTokens_.erase(std::remove_if(remainingTokens_.begin(), remainingTokens_.end(), [&](const string& currentName) {
		if (!hasMissingToken) {
			auto d = curDir->findDirectoryLower(Text::toLower(currentName));
			if (d) {
				curDir = d;
				return true;
			}

			hasMissingToken = true;
		}

		return false;
	}), remainingTokens_.end());

	return curDir;
}

ShareDirectory::Ptr ShareTree::getDirectory(const string& aRealPath) noexcept {
	StringList tokens;

	// Find the existing directories
	auto curDir = findDirectory(aRealPath, tokens);
	if (!curDir) {
		return nullptr;
	}

	// Create missing directories
	// Tokens should have been validated earlier
	for (const auto& curName: tokens) {
		curDir->updateModifyDate();
		curDir = ShareDirectory::createNormal(DualString(curName), curDir, File::getLastModified(curDir->getRealPath()), lowerDirNameMap, *bloom.get());
	}

	return curDir;
}

ShareDirectory::Ptr ShareTree::findDirectory(const string& aRealPath) const noexcept {
	StringList tokens;
	auto curDir = findDirectory(aRealPath, tokens);
	return tokens.empty() ? curDir : nullptr;
}

void ShareTree::addHashedFile(const string& aRealPath, const HashedFile& aFileInfo, ProfileTokenSet* dirtyProfiles) noexcept {
	auto d = getDirectory(PathUtil::getFilePath(aRealPath));
	if (!d) {
		return;
	}

	d->addFile(PathUtil::getFileName(aRealPath), aFileInfo, tthIndex, *bloom.get(), sharedSize, dirtyProfiles);
}

GroupedDirectoryMap ShareTree::getGroupedDirectories() const noexcept {
	GroupedDirectoryMap ret;
	
	{
		// RLock l (cs);
		for (const auto& d: rootPaths | views::values) {
			const auto& currentPath = d->getRoot()->getPath();
			auto virtualName = d->getRoot()->getName();

			ret[virtualName].insert(currentPath);
		}
	}

	return ret;
}

} // namespace dcpp