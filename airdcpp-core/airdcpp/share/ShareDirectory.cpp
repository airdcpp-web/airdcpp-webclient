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
#include <airdcpp/share/ShareDirectory.h>

#include <airdcpp/util/AppUtil.h>
#include <airdcpp/core/io/File.h>
#include <airdcpp/hash/HashedFile.h>
#include <airdcpp/message/Message.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/search/SearchQuery.h>
#include <airdcpp/search/SearchResult.h>
#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/core/io/xml/SimpleXML.h>

namespace dcpp {

bool ShareDirectory::RootIsParentOrExact::operator()(const ShareDirectory::Ptr& aDirectory) const noexcept {
	return PathUtil::isParentOrExactLower(aDirectory->getRoot()->getPathLower(), compareToLower, separator);
}

ShareDirectory::ShareDirectory(DualString&& aRealName, ShareDirectory* aParent, time_t aLastWrite, const ShareRoot::Ptr& aRoot) :
	lastWrite(aLastWrite),
	parent(aParent),
	root(aRoot),
	realName(std::move(aRealName))
{
}

ShareDirectory::~ShareDirectory() {
	ranges::for_each(files, std::default_delete<File>());
}

ShareDirectory::File::File(DualString&& aName, ShareDirectory* aParent, const HashedFile& aFileInfo) :
	size(aFileInfo.getSize()), parent(aParent), lastWrite(aFileInfo.getTimeStamp()), tth(aFileInfo.getRoot()), name(std::move(aName)) {

}

ShareDirectory::File::~File() {

}



ShareDirectory::Ptr ShareDirectory::createNormal(DualString&& aRealName, ShareDirectory* aParent, time_t aLastWrite, ShareTreeMaps& maps_) noexcept {
	auto dir = std::make_shared<ShareDirectory>(std::move(aRealName), aParent, aLastWrite, nullptr);

	if (aParent) {
		auto added = aParent->directories.insert_sorted(dir).second;
		if (!added) {
			return nullptr;
		}
	}

	addDirName(dir, maps_.lowerDirNameMap, maps_.getBloom());
	return dir;
}

ShareDirectory::Ptr ShareDirectory::createRoot(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming,
	time_t aLastWrite, ShareTreeMaps& maps_, time_t aLastRefreshTime) noexcept
{
	auto dir = Ptr(new ShareDirectory(PathUtil::getLastDir(aRootPath), nullptr, aLastWrite, ShareRoot::create(aRootPath, aVname, aProfiles, aIncoming, aLastRefreshTime)));

	dcassert(maps_.rootPaths.find(dir->getRealPathUnsafe()) == maps_.rootPaths.end());
	maps_.rootPaths[dir->getRealPathUnsafe()] = dir;

	addDirName(dir, maps_.lowerDirNameMap, maps_.getBloom());
	return dir;
}

ShareDirectory::Ptr ShareDirectory::cloneRoot(const Ptr& aOldRoot, time_t aLastWrite, ShareTreeMaps& maps_) noexcept {
	auto root = aOldRoot->getRoot();
	return ShareDirectory::createRoot(
		root->getPath(), root->getName(),
		root->getRootProfiles(), root->getIncoming(),
		aLastWrite, maps_, root->getLastRefreshTime()
	);
}

bool ShareDirectory::setParent(const ShareDirectory::Ptr& aDirectory, ShareDirectory* aParent) noexcept {
	aDirectory->parent = aParent;
	if (aParent) {
		if (auto inserted = aParent->directories.insert_sorted(aDirectory).second; !inserted) {
			dcassert(0);
			return false;
		}

		aParent->updateModifyDate();
	}

	return true;
}

void ShareDirectory::updateModifyDate() {
	lastWrite = dcpp::File::getLastModified(getRealPathUnsafe());
}

int64_t ShareDirectory::getLevelSize() const noexcept {
	return size;
}

int64_t ShareDirectory::getTotalSize() const noexcept {
	int64_t tmp = size;
	for (const auto& d : directories) {
		tmp += d->getTotalSize();
	}

	return tmp;
}

string ShareDirectory::getAdcPathUnsafe() const noexcept {
	if (parent) {
		return PathUtil::joinAdcDirectory(parent->getAdcPathUnsafe(), realName.getNormal());
	}

	if (!root) {
		// Root may not be available for subdirectories that are being refreshed
		return ADC_SEPARATOR_STR;
	}

	return ADC_SEPARATOR + root->getName() + ADC_SEPARATOR;
}

string ShareDirectory::getVirtualName() const noexcept {
	if (root) {
		return root->getName();
	}

	return realName.getNormal();
}

const string& ShareDirectory::getVirtualNameLower() const noexcept {
	if (root) {
		return root->getNameLower();
	}

	return realName.getLower();
}

void ShareDirectory::addFile(DualString&& aName, const HashedFile& aFileInfo, ShareTreeMaps& maps_, int64_t& sharedSize_, ProfileTokenSet* dirtyProfiles_) noexcept {
	{
		auto i = files.find(aName.getLower());
		if (i != files.end()) {
			// Get rid of false constness...
			(*i)->cleanIndices(sharedSize_, maps_.tthIndex);
			delete* i;
			files.erase(i);
		}
	}

	auto it = files.insert_sorted(new ShareDirectory::File(std::move(aName), this, aFileInfo)).first;
	(*it)->updateIndices(maps_.getBloom(), sharedSize_, maps_.tthIndex);

	if (dirtyProfiles_) {
		copyRootProfiles(*dirtyProfiles_, true);
	}
}

const ShareRoot::Ptr& ShareDirectory::getRoot() const noexcept {
	dcassert(isRoot());
	return root; 
}

void ShareDirectory::increaseSize(int64_t aSize, int64_t& totalSize_) noexcept {
	size += aSize;
	totalSize_ += aSize;
	//dcassert(accumulate(files.begin(), files.end(), (int64_t)0, [](int64_t aTotal, const File* aFile) { return aTotal + aFile->getSize(); }) == size);
}

void ShareDirectory::decreaseSize(int64_t aSize, int64_t& totalSize_) noexcept {
	size -= aSize;
	totalSize_ -= aSize;
	dcassert(size >= 0 && totalSize_ >= 0);
}

string ShareDirectory::getRealPath(const string& aPath) const noexcept {
	if (parent) {
		return parent->getRealPath(realName.getNormal() + PATH_SEPARATOR_STR + aPath);
	}

	if (!root) {
		// Root may not be available for subdirectories that are being refreshed
		return aPath;
	}

	return root->getPath() + aPath;
}

bool ShareDirectory::isRoot() const noexcept {
	return root ? true : false;
}


void ShareDirectory::countStats(time_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles_, size_t& lowerCaseFiles_, size_t& totalStrLen_) const noexcept {
	for (auto& d : directories) {
		d->countStats(totalAge_, totalDirs_, totalSize_, totalFiles_, lowerCaseFiles_, totalStrLen_);
	}

	for (const auto& f : files) {
		totalSize_ += f->getSize();
		totalAge_ += f->getLastWrite();
		totalStrLen_ += f->getName().length();
		if (f->getName().lowerCaseOnly()) {
			lowerCaseFiles_++;
		}
	}

	totalStrLen_ += realName.length();
	totalDirs_ += directories.size();
	totalFiles_ += files.size();
}


ShareDirectory::Ptr ShareDirectory::findDirectoryByPath(const string& aPath, char aSeparator) const noexcept {
	dcassert(!aPath.empty());

	auto p = aPath.find(aSeparator);
	if (auto d = directories.find(Text::toLower(p != string::npos ? aPath.substr(0, p) : aPath)); d != directories.end()) {
		if (p == aPath.size() || p == aPath.size() - 1)
			return *d;

		return (*d)->findDirectoryByPath(aPath.substr(p + 1), aSeparator);
	}

	return nullptr;
}

ShareDirectory::Ptr ShareDirectory::findDirectoryLower(const string& aNameLower) const noexcept {
	dcassert(Text::isLower(aNameLower));
	auto p = directories.find(aNameLower);
	return p != directories.end() ? *p : nullptr;
}

ShareDirectory::File* ShareDirectory::findFileLower(const string& aNameLower) const noexcept {
	dcassert(Text::isLower(aNameLower));
	auto fileIter = files.find(aNameLower);
	return fileIter != files.end() ? *fileIter : nullptr;
}

void ShareDirectory::getContentInfo(int64_t& size_, DirectoryContentInfo& contentInfo_) const noexcept {
	for (const auto& d : directories) {
		d->getContentInfo(size_, contentInfo_);
	}

	contentInfo_.directories += static_cast<int>(directories.size());
	size_ += size;
	contentInfo_.files += static_cast<int>(files.size());
}



// PROFILES
void ShareDirectory::getProfileInfo(ProfileToken aProfile, int64_t& totalSize_, size_t& filesCount_) const noexcept {
	totalSize_ += size;
	filesCount_ += files.size();

	for (const auto& d : directories) {
		d->getProfileInfo(aProfile, totalSize_, filesCount_);
	}
}

bool ShareDirectory::hasProfile(const ProfileTokenSet& aProfiles) const noexcept {
	if (root && root->hasRootProfile(aProfiles)) {
		return true;
	}

	if (parent) {
		return parent->hasProfile(aProfiles);
	}

	return false;
}


void ShareDirectory::copyRootProfiles(ProfileTokenSet& profiles_, bool aSetCacheDirty) const noexcept {
	if (root) {
		ranges::copy(root->getRootProfiles(), inserter(profiles_, profiles_.begin()));
		if (aSetCacheDirty)
			root->setCacheDirty(true);
	}

	if (parent)
		parent->copyRootProfiles(profiles_, aSetCacheDirty);
}

ProfileTokenSet ShareDirectory::getRootProfiles() const noexcept {
	ProfileTokenSet profiles;
	copyRootProfiles(profiles, false);
	return profiles;
}

bool ShareRoot::hasRootProfile(const ProfileTokenSet& aProfiles) const noexcept {
	auto found = ranges::any_of(aProfiles, [this](auto profile) {
		return rootProfiles.contains(profile);
	});
	return found;
}

bool ShareDirectory::hasProfile(const OptionalProfileToken& aProfile) const noexcept {
	if (!aProfile || (root && root->hasRootProfile(*aProfile))) {
		return true;
	}

	if (parent) {
		return parent->hasProfile(aProfile);
	}

	return false;
}


// INDEXES
void ShareDirectory::cleanIndices(ShareDirectory& aDirectory, int64_t& sharedSize_, File::TTHMap& tthIndex_, ShareDirectory::MultiMap& dirNames_) noexcept {
	aDirectory.cleanIndices(sharedSize_, tthIndex_, dirNames_);

	if (aDirectory.parent) {
		aDirectory.parent->directories.erase_key(aDirectory.realName.getLower());
		aDirectory.parent = nullptr;
	}
}

void ShareDirectory::cleanIndices(int64_t& sharedSize_, ShareDirectory::File::TTHMap& tthIndex_, ShareDirectory::MultiMap& dirNames_) const noexcept {
	for (const auto& d : directories) {
		d->cleanIndices(sharedSize_, tthIndex_, dirNames_);
	}

	//remove from the name map
	removeDirName(*this, dirNames_);

	//remove all files
	for (const auto& f : files) {
		f->cleanIndices(sharedSize_, tthIndex_);
	}
}

void ShareDirectory::File::updateIndices(ShareBloom& bloom_, int64_t& sharedSize_, TTHMap& tthIndex_) noexcept {
	parent->increaseSize(size, sharedSize_);

#ifdef _DEBUG
	checkAddedTTHDebug(this, tthIndex_);
#endif
	tthIndex_.emplace(&tth, this);
	bloom_.add(name.getLower());
}

void ShareDirectory::File::cleanIndices(int64_t& sharedSize_, File::TTHMap& tthIndex_) noexcept {
	parent->decreaseSize(size, sharedSize_);

	auto flst = tthIndex_.equal_range(&tth);
	auto p = ranges::find(flst | pair_to_range | views::values, this);
	if (p.base() != flst.second)
		tthIndex_.erase(p.base());
	else
		dcassert(0);
}

void ShareDirectory::addDirName(const ShareDirectory::Ptr& aDir, ShareDirectory::MultiMap& aDirNames, ShareBloom& aBloom) noexcept {
	const auto& nameLower = aDir->getVirtualNameLower();

#ifdef _DEBUG
	checkAddedDirNameDebug(aDir, aDirNames);
#endif
	aDirNames.emplace(const_cast<string*>(&nameLower), aDir);
	aBloom.add(nameLower);
}

void ShareDirectory::removeDirName(const ShareDirectory& aDir, ShareDirectory::MultiMap& aDirNames) noexcept {
	auto directories = aDirNames.equal_range(const_cast<string*>(&aDir.getVirtualNameLower()));
	auto p = ranges::find_if(directories | pair_to_range | views::values, [&aDir](const ShareDirectory::Ptr& d) { return d.get() == &aDir; });
	if (p.base() == aDirNames.end()) {
		dcassert(0);
		return;
	}

	aDirNames.erase(p.base());
}

#ifdef _DEBUG
void ShareDirectory::checkAddedDirNameDebug(const ShareDirectory::Ptr& aDir, ShareDirectory::MultiMap& aDirNames) noexcept {
	auto directories = aDirNames.equal_range(const_cast<string*>(&aDir->getVirtualNameLower()));
	auto findByPtr = ranges::find(directories | pair_to_range | views::values, aDir);
	auto findByPath = ranges::find_if(directories | pair_to_range | views::values, [&](const ShareDirectory::Ptr& d) {
		return d->getRealPathUnsafe() == aDir->getRealPathUnsafe();
	});

	dcassert(findByPtr.base() == directories.second);
	dcassert(findByPath.base() == directories.second);
}

void ShareDirectory::File::checkAddedTTHDebug(const ShareDirectory::File* aFile, ShareDirectory::File::TTHMap& aTTHIndex) noexcept {
	auto flst = aTTHIndex.equal_range(const_cast<TTHValue*>(&aFile->getTTH()));
	auto p = ranges::find(flst | pair_to_range | views::values, aFile);
	dcassert(p.base() == flst.second);
}

#endif


// SEARCH

ShareDirectory::SearchResultInfo::SearchResultInfo(const File* f, const SearchQuery& aSearch, int aLevel) :
	file(f), type(FILE), scores(SearchQuery::getRelevanceScore(aSearch, aLevel, false, f->getName().getLower())) {

}

ShareDirectory::SearchResultInfo::SearchResultInfo(const ShareDirectory* d, const SearchQuery& aSearch, int aLevel) :
	directory(d), type(DIRECTORY), scores(SearchQuery::getRelevanceScore(aSearch, aLevel, true, d->realName.getLower())) {

}


/**
* Alright, the main point here is that when searching, a search string is most often found in
* the filename, not directory name, so we want to make that case faster. Also, we want to
* avoid changing StringLists unless we absolutely have to --> this should only be done if a string
* has been matched in the directory name. This new stringlist should also be used in all descendants,
* but not the parents...
*/

void ShareDirectory::search(SearchResultInfo::Set& results_, SearchQuery& aStrings, int aLevel) const noexcept {
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
		if (aStrings.itemType != SearchQuery::ItemType::FILE && positionsComplete && aStrings.gt == 0 && aStrings.matchesDate(lastWrite)) {
			// Full match
			results_.emplace(this, aStrings, aLevel);
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
	if (aStrings.itemType != SearchQuery::ItemType::DIRECTORY) {
		for (const auto& f : files) {
			if (!aStrings.matchesFileLower(f->getName().getLower(), f->getSize(), f->getLastWrite())) {
				continue;
			}

			results_.emplace(f, aStrings, aLevel);
			if (aStrings.addParents)
				break;
		}
	}

	// Match directories
	for (const auto& d : directories) {
		d->search(results_, aStrings, aLevel);
	}

	// Moving to a lower level
	if (aStrings.recursion) {
		aStrings.recursion->decrease(dirName.length());
	}

	aStrings.recursion = old;
}

void ShareDirectory::File::addSR(SearchResultList& aResults, bool aAddParent) const noexcept {
	auto path = aAddParent ? parent->getAdcPathUnsafe() : getAdcPath();

	// Have we added it already?
	if (ranges::any_of(aResults, [&path](const SearchResultPtr& sr) { return Util::stricmp(sr->getAdcPath(), path) == 0; }))
		return;

	if (aAddParent) {
		auto sr = make_shared<SearchResult>(path);
		aResults.push_back(sr);
	} else {
		auto sr = make_shared<SearchResult>(SearchResult::Type::FILE,
			size, path, getTTH(), getLastWrite(), DirectoryContentInfo::uninitialized());

		aResults.push_back(sr);
	}
}


// CACHE
#define LITERAL(n) n, sizeof(n)-1
void ShareDirectory::toCacheXmlList(OutputStream& xmlFile, string& indent, string& tmp) const {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(realName.lowerCaseOnly() ? realName.getLower() : realName.getNormal(), tmp, true));

	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(SimpleXML::escape(Util::toString(lastWrite), tmp, true));
	xmlFile.write(LITERAL("\">\r\n"));

	indent += '\t';
	filesToCacheXmlList(xmlFile, indent, tmp);

	for (const auto& d : directories) {
		d->toCacheXmlList(xmlFile, indent, tmp);
	}

	indent.erase(indent.length() - 1);
	xmlFile.write(indent);
	xmlFile.write(LITERAL("</Directory>\r\n"));
}

void ShareDirectory::filesToCacheXmlList(OutputStream& xmlFile, string& indent, string& tmp2) const {
	for (const auto& f : files) {
		xmlFile.write(indent);
		xmlFile.write(LITERAL("<File Name=\""));
		xmlFile.write(SimpleXML::escape(f->getName().lowerCaseOnly() ? f->getName().getLower() : f->getName().getNormal(), tmp2, true));
		xmlFile.write(LITERAL("\"/>\r\n"));
	}
}


// FILELISTS

FilelistDirectory::FilelistDirectory(const string& aName, time_t aDate) : date(aDate), name(aName) { }

unique_ptr<FilelistDirectory> FilelistDirectory::generateRoot(const ShareDirectory::List& aRootDirectory, const ShareDirectory::List& aChildren, bool aRecursive) {
	auto listRoot = make_unique<FilelistDirectory>(Util::emptyString, 0);

	listRoot->shareDirs = aRootDirectory;
	for (const auto& dir: aRootDirectory) {
		listRoot->date = max(listRoot->date, dir->getLastWrite());
	}

	for (const auto& child : aChildren) {
		listRoot->toFileList(child, aRecursive);
	}

	return std::move(listRoot);
}

void FilelistDirectory::toFileList(const ShareDirectory::Ptr& aShareDirectory, bool aRecursive) {
	FilelistDirectory* listDirectory = nullptr;
	if (auto pos = listDirectories.find(const_cast<string*>(&aShareDirectory->getVirtualNameLower())); pos != listDirectories.end()) {
		listDirectory = pos->second.get();
	} else {
		auto newListDir = std::make_unique<FilelistDirectory>(aShareDirectory->getVirtualName(), aShareDirectory->getLastWrite());
		listDirectory = newListDir.get();
		listDirectories.emplace(const_cast<string*>(&newListDir->name), std::move(newListDir));
	}

	listDirectory->shareDirs.push_back(aShareDirectory);

	if (aRecursive) {
		for (const auto& childDirectory: aShareDirectory->getDirectories()) {
			listDirectory->toFileList(childDirectory, true);
		}
	}

	date = max(date, aShareDirectory->getLastWrite());
}

void ShareDirectory::File::toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const {
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

void ShareDirectory::toTTHList(OutputStream& tthList, string& tmp2, bool recursive) const {
	if (recursive) {
		for (const auto& d : directories) {
			d->toTTHList(tthList, tmp2, recursive);
		}
	}

	for (const auto& f : files) {
		tmp2.clear();
		tthList.write(f->getTTH().toBase32(tmp2));
		tthList.write(LITERAL(" "));
	}
}

void FilelistDirectory::toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool aRecursive, const DuplicateFileHandler& aDuplicateFileHandler) const {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(name, tmp2, true));
	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(Util::toString(date));

	if (aRecursive) {
		xmlFile.write(LITERAL("\">\r\n"));

		indent += '\t';
		for (const auto& d : listDirectories | views::values) {
			d->toXml(xmlFile, indent, tmp2, aRecursive, aDuplicateFileHandler);
		}

		filesToXml(xmlFile, indent, tmp2, !aRecursive, aDuplicateFileHandler);

		indent.erase(indent.length() - 1);
		xmlFile.write(indent);
		xmlFile.write(LITERAL("</Directory>\r\n"));
	} else {
		auto contentInfo(DirectoryContentInfo::empty());
		int64_t totalSize = 0;
		for (const auto& d : shareDirs) {
			d->getContentInfo(totalSize, contentInfo);
		}

		xmlFile.write(LITERAL("\" Size=\""));
		xmlFile.write(Util::toString(totalSize));

		if (contentInfo.files == 0 && contentInfo.directories == 0) {
			xmlFile.write(LITERAL("\" />\r\n"));
		} else {
			xmlFile.write(LITERAL("\" Incomplete=\"1"));

			if (contentInfo.directories > 0) {
				xmlFile.write(LITERAL("\" Directories=\""));
				xmlFile.write(Util::toString(contentInfo.directories));
			}

			if (contentInfo.files > 0) {
				xmlFile.write(LITERAL("\" Files=\""));
				xmlFile.write(Util::toString(contentInfo.files));
			}

			xmlFile.write(LITERAL("\"/>\r\n"));
		}
	}
}

void FilelistDirectory::filesToXml(OutputStream& xmlFile, string& indent, string& tmp2, bool aAddDate, const DuplicateFileHandler& aDuplicateFileHandler) const {
	bool filesAdded = false;
	int dupeFileCount = 0;
	for (auto di = shareDirs.begin(); di != shareDirs.end(); ++di) {
		if (filesAdded) {
			for (const auto& fi : (*di)->getFiles()) {
				//go through the dirs that we have added already
				if (none_of(shareDirs.begin(), di, [&fi](const ShareDirectory::Ptr& d) { return d->findFileLower(fi->getName().getLower()); })) {
					fi->toXml(xmlFile, indent, tmp2, aAddDate);
				} else {
					dupeFileCount++;
				}
			}
		} else if (!(*di)->getFiles().empty()) {
			filesAdded = true;
			for (const auto& f : (*di)->getFiles())
				f->toXml(xmlFile, indent, tmp2, aAddDate);
		}
	}

	if (dupeFileCount > 0 && SETTING(FL_REPORT_FILE_DUPES) && shareDirs.size() > 1) {
		StringList paths;
		for (const auto& d : shareDirs)
			paths.push_back(d->getRealPathUnsafe());

		aDuplicateFileHandler(paths, dupeFileCount);
	}
}




// ROOTS

bool ShareRoot::hasRootProfile(ProfileToken aProfile) const noexcept {
	return rootProfiles.contains(aProfile);
}

ShareRoot::ShareRoot(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming, time_t aLastRefreshTime) noexcept :
	rootProfiles(aProfiles), incoming(aIncoming), lastRefreshTime(aLastRefreshTime),
	virtualName(make_unique<DualString>(aVname)), path(aRootPath), pathLower(Text::toLower(aRootPath)) {

}

ShareRoot::Ptr ShareRoot::create(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming, time_t aLastRefreshTime) noexcept {
	return shared_ptr<ShareRoot>(new ShareRoot(aRootPath, aVname, aProfiles, aIncoming, aLastRefreshTime));
}

void ShareRoot::addRootProfile(ProfileToken aProfile) noexcept {
	rootProfiles.emplace(aProfile);
}

bool ShareRoot::removeRootProfile(ProfileToken aProfile) noexcept {
	rootProfiles.erase(aProfile);
	return rootProfiles.empty();
}

string ShareRoot::getCacheXmlPath() const noexcept {
	return AppUtil::getPath(AppUtil::PATH_SHARECACHE) + "ShareCache_" + PathUtil::validateFileName(path) + ".xml";
}

void ShareRoot::setName(const string& aName) noexcept {
	virtualName = make_unique<DualString>(aName);
}

}