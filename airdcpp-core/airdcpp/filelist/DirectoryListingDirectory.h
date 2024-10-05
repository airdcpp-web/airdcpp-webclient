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

#ifndef DCPLUSPLUS_DCPP_DIRECTORY_LISTING_DIRECTORY_H
#define DCPLUSPLUS_DCPP_DIRECTORY_LISTING_DIRECTORY_H

#include <airdcpp/forward.h>
#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/filelist/DirectoryListing.h>
#include <airdcpp/core/types/DirectoryContentInfo.h>
#include <airdcpp/core/types/DupeType.h>
#include <airdcpp/core/types/GetSet.h>
#include <airdcpp/util/Util.h>
#include <airdcpp/queue/QueueAddInfo.h>

namespace dcpp {

class SearchQuery;

class DirectoryListing::File: public boost::noncopyable {

public:
	using Owner = const void*;
	using Ptr = std::shared_ptr<File>;

	struct Sort { bool operator()(const Ptr& a, const Ptr& b) const; };

	using List = std::vector<Ptr>;
	using Iter = List::const_iterator;
		
	File(Directory* aDir, const string& aName, int64_t aSize, const TTHValue& aTTH, time_t aRemoteDate) noexcept;
	File(const File& rhs, Owner aOwner) noexcept;

	~File() = default;

	string getAdcPathUnsafe() const noexcept;

	GETSET(string, name, Name);
	GETSET(int64_t, size, Size);
	GETSET(Directory*, parent, Parent);
	GETSET(TTHValue, tthRoot, TTH);
	IGETSET(DupeType, dupe, Dupe, DUPE_NONE);
	IGETSET(time_t, remoteDate, RemoteDate, 0);

	bool isInQueue() const noexcept;

	Owner getOwner() const noexcept {
		return owner;
	}
	DirectoryListingItemToken getToken() const noexcept {
		return token;
	}
	void getLocalPathsUnsafe(StringList& ret, const OptionalProfileToken& aShareProfileToken) const;
private:
	Owner owner = nullptr;
	const DirectoryListingItemToken token;
};

enum class DirectoryListing::DirectoryLoadType {
	CHANGE_NORMAL,
	CHANGE_RELOAD,
	LOAD_CONTENT,
	NONE,
};

class DirectoryListing::Directory : public boost::noncopyable {
public:
	enum DirType {
		TYPE_NORMAL,
		TYPE_INCOMPLETE_CHILD,
		TYPE_INCOMPLETE_NOCHILD,
		TYPE_VIRTUAL,
	};

	using Ptr = std::shared_ptr<Directory>;

	struct Sort { bool operator()(const Ptr& a, const Ptr& b) const; };

	using List = std::vector<Ptr>;
	using TTHSet = unordered_set<TTHValue>;
	using Map = map<const string *, Ptr, noCaseStringLess>;
		
	Map directories;
	File::List files;

	static Ptr create(Directory* aParent, const string& aName, DirType aType, time_t aUpdateDate, 
		const DirectoryContentInfo& aContentInfo = DirectoryContentInfo::uninitialized(),
		const string& aSize = Util::emptyString, time_t aRemoteDate = 0);

	virtual ~Directory();

	int64_t getTotalSize(bool aCountVirtual) const noexcept;
	void filterList(TTHSet& l) noexcept;
	void getHashList(TTHSet& l) const noexcept;
	void clearVirtualDirectories() noexcept;
	void clearAll() noexcept;
	void getLocalPathsUnsafe(StringList& ret, const OptionalProfileToken& aShareProfileToken) const;

	bool findIncomplete() const noexcept;
	bool findCompleteChildren() const noexcept;

	string getAdcPathUnsafe() const noexcept;
	DupeType checkDupesRecursive() noexcept;
		
	IGETSET(int64_t, partialSize, PartialSize, 0);
	GETSET(Directory*, parent, Parent);
	GETSET(DirType, type, Type);
	IGETSET(DupeType, dupe, Dupe, DUPE_NONE);
	IGETSET(time_t, remoteDate, RemoteDate, 0);
	IGETSET(time_t, lastUpdateDate, LastUpdateDate, 0);
	IGETSET(DirectoryLoadType, loading, Loading, DirectoryLoadType::NONE);

	bool isComplete() const noexcept { return type == TYPE_VIRTUAL || type == TYPE_NORMAL; }
	void setComplete() noexcept { type = TYPE_NORMAL; }
	bool isVirtual() const noexcept { return type == TYPE_VIRTUAL; }
	bool isRoot() const noexcept { return !parent; }

	// Create recursive bundle file info listing with relative paths
	BundleFileAddData::List toBundleInfoList() const noexcept;

	const string& getName() const noexcept {
		return name;
	}

	// This function not thread safe as it will go through all complete directories
	DirectoryContentInfo getContentInfoRecursive(bool aCountVirtual) const noexcept;

	// Partial list content info only
	const DirectoryContentInfo& getContentInfo() const noexcept {
		return contentInfo;
	}

	void setContentInfo(const DirectoryContentInfo& aContentInfo) {
		contentInfo.files = aContentInfo.files;
		contentInfo.directories = aContentInfo.directories;
	}

	static bool NotVirtual(const Directory::Ptr& aDirectory) noexcept {
		return !aDirectory->isVirtual();
	}

	DirectoryListingItemToken getToken() const noexcept {
		return token;
	}
protected:
	void toBundleInfoList(const string& aTarget, BundleFileAddData::List& aFiles) const noexcept;

	Directory(Directory* aParent, const string& aName, DirType aType, time_t aUpdateDate, const DirectoryContentInfo& aContentInfo, const string& aSize, time_t aRemoteDate);

	void getContentInfo(size_t& directories_, size_t& files_, bool aCountVirtual) const noexcept;

	DirectoryContentInfo contentInfo = DirectoryContentInfo::uninitialized();
	const string name;
	const DirectoryListingItemToken token;
};

class DirectoryListing::VirtualDirectory : public DirectoryListing::Directory {
public:
	using Ptr = shared_ptr<VirtualDirectory>;
	GETSET(string, fullAdcPath, FullAdcPath);
	static Ptr create(const string& aFullAdcPath, Directory* aParent, const string& aName, bool aAddToParent = true);
private:
	VirtualDirectory(const string& aFullPath, Directory* aParent, const string& aName);
};

inline bool operator==(const DirectoryListing::Directory::Ptr& a, const string& b) { return Util::stricmp(a->getName(), b) == 0; }
inline bool operator==(const DirectoryListing::File::Ptr& a, const string& b) { return Util::stricmp(a->getName(), b) == 0; }

} // namespace dcpp

#endif // !defined(DIRECTORY_LISTING_H)
