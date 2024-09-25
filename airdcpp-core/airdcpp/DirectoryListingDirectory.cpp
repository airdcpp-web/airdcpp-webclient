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

#include <airdcpp/DirectoryListingDirectory.h>

#include <airdcpp/concurrency.h>

#include <airdcpp/DupeUtil.h>
#include <airdcpp/Exception.h>
#include <airdcpp/PathUtil.h>
#include <airdcpp/SearchQuery.h>
#include <airdcpp/SettingsManager.h>
#include <airdcpp/ShareManager.h>
#include <airdcpp/StringTokenizer.h>


namespace dcpp {

using ranges::for_each;
using ranges::find_if;

atomic<DirectoryListingItemToken> itemIdCounter{ 1 };

bool DirectoryListing::Directory::Sort::operator()(const Ptr& a, const Ptr& b) const {
	return compare(a->getName(), b->getName()) < 0;
}

bool DirectoryListing::File::Sort::operator()(const Ptr& a, const Ptr& b) const {
	return compare(a->getName(), b->getName()) < 0;
}

DirectoryListing::File::File(Directory* aDir, const string& aName, int64_t aSize, const TTHValue& aTTH, time_t aRemoteDate) noexcept :
	name(aName), size(aSize), parent(aDir), tthRoot(aTTH), remoteDate(aRemoteDate), token(itemIdCounter++) {

	if (size > 0) {
		dupe = DupeUtil::checkFileDupe(tthRoot);
	}

	//dcdebug("DirectoryListing::File (copy) %s was created\n", aName.c_str());
}

string DirectoryListing::File::getAdcPathUnsafe() const noexcept {
	return parent->getAdcPathUnsafe() + name;
}

DirectoryListing::File::File(const File& rhs, File::Owner aOwner) noexcept : 
	name(rhs.name), size(rhs.size), parent(rhs.parent), tthRoot(rhs.tthRoot), 
	dupe(rhs.dupe), remoteDate(rhs.remoteDate), owner(aOwner), token(itemIdCounter++)
{
	dcdebug("DirectoryListing::File (copy) %s was created\n", rhs.getName().c_str());
}

DirectoryListing::Directory::Ptr DirectoryListing::Directory::create(Directory* aParent, const string& aName, DirType aType, time_t aUpdateDate, const DirectoryContentInfo& aContentInfo, const string& aSize, time_t aRemoteDate) {
	dcassert(aType != TYPE_VIRTUAL);
	auto dir = Ptr(new Directory(aParent, aName, aType, aUpdateDate, aContentInfo, aSize, aRemoteDate));
	if (aParent) {
		dcassert(!aParent->directories.contains(&dir->getName()));
		auto [dp, inserted] = aParent->directories.try_emplace(&dir->getName(), dir);
		if (!inserted) {
			throw AbortException("The directory " + dir->getAdcPathUnsafe() + " contains items with duplicate names (" + dir->getName() + ", " + *(*dp).first + ")");
		}
	}

	return dir;
}

DirectoryListing::VirtualDirectory::Ptr DirectoryListing::VirtualDirectory::create(const string& aFullPath, Directory* aParent, const string& aName, bool aAddToParent) {
	dcassert(aParent);

	auto name = aName;
	if (aParent->directories.contains(&name)) {
		// No duplicate file names
		int num = 0;
		for (;;) {
			num++;
			name = aName + " (" + Util::toString(num) + ")";
			if (!aParent->directories.contains(&name)) {
				break;
			}
		}
	}

	auto dir = Ptr(new VirtualDirectory(aFullPath, aParent, name));

	if (aAddToParent) {
		dcassert(!aParent->directories.contains(&dir->getName()));
		aParent->directories.try_emplace(&dir->getName(), dir);
	}

	return dir;
}

DirectoryListing::VirtualDirectory::VirtualDirectory(const string& aFullAdcPath, DirectoryListing::Directory* aParent, const string& aName) :
	Directory(aParent, aName, Directory::TYPE_VIRTUAL, GET_TIME(), DirectoryContentInfo::uninitialized(), Util::emptyString, 0), fullAdcPath(aFullAdcPath) {

}

DirectoryListing::Directory::Directory(Directory* aParent, const string& aName, Directory::DirType aType, time_t aUpdateDate, const DirectoryContentInfo& aContentInfo, const string& aSize, time_t aRemoteDate /*0*/)
	: parent(aParent), type(aType), remoteDate(aRemoteDate), lastUpdateDate(aUpdateDate), contentInfo(aContentInfo), name(aName), token(itemIdCounter++) {

	if (!aSize.empty()) {
		partialSize = Util::toInt64(aSize);
	}

	//dcdebug("DirectoryListing::Directory %s was created\n", aName.c_str());
}

bool DirectoryListing::Directory::findIncomplete() const noexcept {
	/* Recursive check for incomplete dirs */
	if (!isComplete()) {
		return true;
	}

	return find_if(directories | views::values, [](const Directory::Ptr& dir) { 
		return dir->findIncomplete(); 
	}).base() != directories.end();
}

bool DirectoryListing::Directory::findCompleteChildren() const noexcept {
	return find_if(directories | views::values, [](const Directory::Ptr& dir) {
		return dir->isComplete();
	}).base() != directories.end();
}

DirectoryContentInfo DirectoryListing::Directory::getContentInfoRecursive(bool aCountVirtual) const noexcept {
	if (isComplete()) {
		size_t directoryCount = 0, fileCount = 0;
		getContentInfo(directoryCount, fileCount, aCountVirtual);
		return DirectoryContentInfo(static_cast<int>(directoryCount), static_cast<int>(fileCount));
	}

	return contentInfo;
}

void DirectoryListing::Directory::getContentInfo(size_t& directories_, size_t& files_, bool aCountVirtual) const noexcept {
	if (!aCountVirtual && isVirtual()) {
		return;
	}

	if (isComplete()) {
		directories_ += directories.size();
		files_ += files.size();

		for (const auto& d : directories | views::values) {
			d->getContentInfo(directories_, files_, aCountVirtual);
		}
	} else if (contentInfo.isInitialized()) {
		directories_ += contentInfo.directories;
		files_ += contentInfo.files;
	}
}

BundleFileAddData::List DirectoryListing::Directory::toBundleInfoList() const noexcept {
	BundleFileAddData::List bundleFiles;
	toBundleInfoList(Util::emptyString, bundleFiles);
	return bundleFiles;
}

void DirectoryListing::Directory::toBundleInfoList(const string& aTarget, BundleFileAddData::List& aFiles) const noexcept {
	// First, recurse over the directories
	for (const auto& d: directories | views::values) {
		d->toBundleInfoList(PathUtil::joinDirectory(aTarget, d->getName()), aFiles);
	}

	// Then add the files
	for (const auto& f: files) {
		aFiles.emplace_back(aTarget + f->getName(), f->getTTH(), f->getSize(), Priority::DEFAULT, f->getRemoteDate());
	}
}

struct HashContained {
	explicit HashContained(const DirectoryListing::Directory::TTHSet& l) : tl(l) { }
	const DirectoryListing::Directory::TTHSet& tl;
	bool operator()(const DirectoryListing::File::Ptr& i) const {
		return tl.contains(i->getTTH());
	}
};

struct DirectoryEmpty {
	bool operator()(const DirectoryListing::Directory::Ptr& aDir) const {
		return aDir->getContentInfo().isEmpty();
	}
};

struct SizeLess {
	bool operator()(const DirectoryListing::File::Ptr& f) const {
		return f->getSize() < Util::convertSize(SETTING(SKIP_SUBTRACT), Util::KB);
	}
};

DirectoryListing::Directory::~Directory() {
	//dcdebug("DirectoryListing::Directory %s deleted\n", name.c_str());
}

void DirectoryListing::Directory::clearAll() noexcept {
	directories.clear();
	files.clear();
}

void DirectoryListing::Directory::filterList(DirectoryListing::Directory::TTHSet& l) noexcept {
	std::erase_if(directories, [&l](auto& dp) {
		auto d = dp.second.get();
		d->filterList(l);
		return d->directories.empty() && d->files.empty();
	});

	std::erase_if(files, HashContained(l));

	if (SETTING(SKIP_SUBTRACT) > 0 && files.size() < 2) {   //setting for only skip if folder filecount under x ?
		std::erase_if(files, SizeLess());
	}
}

void DirectoryListing::Directory::getHashList(DirectoryListing::Directory::TTHSet& l) const noexcept {
	for(const auto& d: directories | views::values)  
		d->getHashList(l);

	for(const auto& f: files) 
		l.insert(f->getTTH());
}

void DirectoryListing::File::getLocalPathsUnsafe(StringList& ret, const OptionalProfileToken& aShareProfileToken) const {
	if (aShareProfileToken) {
		string path;
		if (parent->isVirtual()) {
			auto virtualDir = static_cast<VirtualDirectory*>(parent);
			path = virtualDir->getFullAdcPath();
		} else {
			path = parent->getAdcPathUnsafe();
		}

		ShareManager::getInstance()->getRealPaths(path + name, ret, aShareProfileToken);
	} else {
		ret = DupeUtil::getFileDupePaths(dupe, tthRoot);
	}
}

void DirectoryListing::Directory::getLocalPathsUnsafe(StringList& ret, const OptionalProfileToken& aShareProfileToken) const {
	if (isRoot() || (isVirtual() && parent->isRoot()) /* || !isOwnList)*/)
		return;

	string path;
	if (isVirtual()) {
		auto virtualDir = static_cast<const VirtualDirectory*>(this);
		path = virtualDir->getFullAdcPath() + name;
	} else {
		path = getAdcPathUnsafe();
	}

	if (aShareProfileToken) {
		ShareManager::getInstance()->getRealPaths(path, ret, aShareProfileToken);
	} else {
		ret = DupeUtil::getAdcDirectoryDupePaths(dupe, path);
	}
}

int64_t DirectoryListing::Directory::getTotalSize(bool aCountVirtual) const noexcept {
	if (!isComplete())
		return partialSize;

	int64_t x = 0;
	for (const auto& f : files) {
		x += f->getSize();
	}

	for (const auto& d: directories | views::values) {
		if (!aCountVirtual && d->isVirtual()) {
			continue;
		}

		x += d->getTotalSize(d->isVirtual());
	}
	return x;
}

void DirectoryListing::Directory::clearVirtualDirectories() noexcept {
	std::erase_if(directories, [](auto& dp) {
		return dp.second->isVirtual();
	});
}

string DirectoryListing::Directory::getAdcPathUnsafe() const noexcept {
	//make sure to not try and get the name of the root dir
	if (parent) {
		return PathUtil::joinAdcDirectory(parent->getAdcPathUnsafe(), name);
	}

	// root
	return ADC_ROOT_STR;
}

bool DirectoryListing::File::isInQueue() const noexcept {
	return DupeUtil::isQueueDupe(dupe) || DupeUtil::isFinishedDupe(dupe);
}

DupeType DirectoryListing::Directory::checkDupesRecursive() noexcept {
	// Go through the files even if the directory is incomplete 
	// (some of the children may still be available)
	DupeUtil::DupeSet dupeSet;

	// Children
	for (const auto& d : directories | views::values) {
		dupeSet.emplace(d->checkDupesRecursive());
	}

	// Files
	for (const auto& f : files) {
		auto fileDupe = DupeUtil::checkFileDupe(f->getTTH());
		f->setDupe(fileDupe);
		dupeSet.emplace(fileDupe);
	}

	setDupe(DupeUtil::parseDirectoryContentDupe(dupeSet));

	if (dupe == DUPE_NONE && !isComplete()) {
		// Content unknown
		setDupe(DupeUtil::checkAdcDirectoryDupe(getAdcPathUnsafe(), partialSize));
	}

	return dupe;
}

} // namespace dcpp
