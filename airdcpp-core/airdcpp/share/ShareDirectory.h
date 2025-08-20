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

#ifndef DCPLUSPLUS_DCPP_SHAREDIRECTORY_H
#define DCPLUSPLUS_DCPP_SHAREDIRECTORY_H

#include <airdcpp/core/classes/BloomFilter.h>
#include <airdcpp/util/text/DualString.h>
#include <airdcpp/core/types/GetSet.h>
#include <airdcpp/hash/value/HashBloom.h>
#include <airdcpp/hash/value/MerkleTree.h>
#include <airdcpp/core/classes/SortedVector.h>
#include <airdcpp/util/Util.h>

#include <airdcpp/core/header/typedefs.h>

namespace dcpp {

typedef BloomFilter<5> ShareBloom;

enum class ShareRootRefreshState : uint8_t {
	STATE_NORMAL,
	STATE_PENDING,
	STATE_RUNNING,
};

class ShareRoot {
public:
	typedef shared_ptr<ShareRoot> Ptr;
	typedef unordered_map<TTHValue, Ptr> Map;

	static Ptr create(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming, time_t aLastRefreshTime) noexcept;

	GETSET(ProfileTokenSet, rootProfiles, RootProfiles);
	IGETSET(bool, cacheDirty, CacheDirty, false);
	IGETSET(bool, incoming, Incoming, false);
	IGETSET(ShareRootRefreshState, refreshState, RefreshState, ShareRootRefreshState::STATE_NORMAL);
	IGETSET(optional<ShareRefreshTaskToken>, refreshTaskToken, RefreshTaskToken, nullopt);
	IGETSET(time_t, lastRefreshTime, LastRefreshTime, 0);

	bool hasRootProfile(ProfileToken aProfile) const noexcept;
	bool hasRootProfile(const ProfileTokenSet& aProfiles) const noexcept;
	void addRootProfile(ProfileToken aProfile) noexcept;
	bool removeRootProfile(ProfileToken aProfile) noexcept;

	string getName() const noexcept {
		return virtualName->getNormal();
	}

	const string& getNameLower() const noexcept {
		return virtualName->getLower();
	}

	const string& getPath() const noexcept {
		return path;
	}

	const string& getPathLower() const noexcept {
		return pathLower;
	}

	void setName(const string& aName) noexcept;
	string getCacheXmlPath() const noexcept;

	ShareRoot(ShareRoot&) = delete;
	ShareRoot& operator=(ShareRoot&) = delete;
private:
	ShareRoot(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming, time_t aLastRefreshTime) noexcept;

	unique_ptr<DualString> virtualName;
	const string path;
	const string pathLower;
};

class ShareTreeMaps;
class FilelistDirectory;
class ShareDirectory {
public:
	typedef std::shared_ptr<ShareDirectory> Ptr;
	typedef unordered_map<string, Ptr, noCaseStringHash, noCaseStringEq> Map;
	typedef unordered_multimap<string*, ShareDirectory::Ptr, StringPtrHash, StringPtrEq> MultiMap;
	typedef Map::iterator MapIter;
	typedef std::vector<ShareDirectory::Ptr> List;

	struct NameLower {
		const string& operator()(const Ptr& a) const noexcept { return a->realName.getLower(); }
	};

	class File {
	public:
		struct NameLower {
			const string& operator()(const File* a) const noexcept { return a->name.getLower(); }
		};

		typedef SortedVector<File*, std::vector, string, Compare, NameLower> Set;
		typedef SortedVector<const File*, std::vector, string, Compare, NameLower> ConstSet;
		typedef unordered_multimap<TTHValue*, const ShareDirectory::File*> TTHMap;

		File(DualString&& aName, ShareDirectory* aParent, const HashedFile& aFileInfo);
		~File();

		inline string getAdcPath() const noexcept { return parent->getAdcPathUnsafe() + name.getNormal(); }
		inline string getRealPath() const noexcept { return parent->getRealPath(name.getNormal()); }
		inline bool hasProfile(const OptionalProfileToken& aProfile) const noexcept { return parent->hasProfile(aProfile); }

		void toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const;
		void addSR(SearchResultList& aResults, bool addParent) const noexcept;

		GETSET(int64_t, size, Size);
		GETSET(ShareDirectory*, parent, Parent);
		GETSET(time_t, lastWrite, LastWrite);
		GETSET(TTHValue, tth, TTH);

		void updateIndices(ShareBloom& aBloom_, int64_t& sharedSize_, File::TTHMap& tthIndex_) noexcept;
		void cleanIndices(int64_t& sharedSize_, TTHMap& tthIndex_) noexcept;

#ifdef _DEBUG
		// Checks that duplicate/incorrect files won't get through
		// Called before the file has been added in the index
		static void checkAddedTTHDebug(const ShareDirectory::File* f, TTHMap& aTTHIndex) noexcept;
#endif
		const DualString& getName() const noexcept {
			return name;
		}
	private:
		DualString name;
	};

	class SearchResultInfo {
	public:
		struct Sort {
			bool operator()(const SearchResultInfo& left, const SearchResultInfo& right) const noexcept { return left.scores > right.scores; }
		};

		SearchResultInfo(const File* f, const SearchQuery& aSearch, int aLevel);
		SearchResultInfo(const ShareDirectory* d, const SearchQuery& aSearch, int aLevel);

		typedef multiset<SearchResultInfo, Sort> Set;
		enum Type : uint8_t {
			FILE,
			DIRECTORY
		};

		union {
			const ShareDirectory* directory;
			const ShareDirectory::File* file;
		};

		Type getType() const noexcept { return type; }
	private:
		const Type type;
		double scores;
	};

	typedef SortedVector<Ptr, std::vector, string, Compare, NameLower> Set;

	static Ptr createNormal(DualString&& aRealName, ShareDirectory* aParent, time_t aLastWrite, ShareTreeMaps& maps_) noexcept;
	static Ptr createRoot(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming, time_t aLastWrite, ShareTreeMaps& maps_, time_t aLastRefreshTime) noexcept;
	static Ptr cloneRoot(const Ptr& aOldRoot, time_t aLastWrite, ShareTreeMaps& maps_) noexcept;

	// Set a new parent for the directory
	// Possible directories with the same name must be removed from the parent first
	static bool setParent(const ShareDirectory::Ptr& aDirectory, ShareDirectory* aParent) noexcept;

	// Remove directory from possible parent and all shared containers
	static void cleanIndices(ShareDirectory& aDirectory, int64_t& sharedSize_, File::TTHMap& tthIndex_, ShareDirectory::MultiMap& aDirNames_) noexcept;

	struct HasRootProfile {
		HasRootProfile(const OptionalProfileToken& aProfile) : profile(aProfile) { }
		bool operator()(const Ptr& d) const noexcept {
			return d->hasProfile(profile);
		}
		OptionalProfileToken profile; // Never use a reference here as this predicate may also be initilized with ProfileToken without warnings

		HasRootProfile& operator=(const HasRootProfile&) = delete;
	};

	static ShareRoot::Ptr ToRoot(const ShareDirectory::Ptr& aDirectory) noexcept {
		dcassert(aDirectory->getRoot());
		return aDirectory->getRoot();
	}


	string getAdcPathUnsafe() const noexcept;
	string getVirtualName() const noexcept;
	const string& getVirtualNameLower() const noexcept;

	// Parents may be deleted
	inline string getRealPathUnsafe() const noexcept { return getRealPath(Util::emptyString); };

	bool hasProfile(const ProfileTokenSet& aProfiles) const noexcept;
	bool hasProfile(const OptionalProfileToken& aProfile) const noexcept;

	void getContentInfo(int64_t& size_, DirectoryContentInfo& contentInfo_) const noexcept;

	// Return cached size for files directly inside this directory
	int64_t getLevelSize() const noexcept;

	// Count the recursive total size for the directory
	int64_t getTotalSize() const noexcept;

	void getProfileInfo(ProfileToken aProfile, int64_t& totalSize_, size_t& filesCount_) const noexcept;

	void search(SearchResultInfo::Set& aResults, SearchQuery& aStrings, int aLevel) const noexcept;

	void toTTHList(OutputStream& tthList, string& tmp2, bool aRecursive) const;

	//for file list caching
	void toCacheXmlList(OutputStream& xmlFile, string& indent, string& tmp) const;
	void filesToCacheXmlList(OutputStream& xmlFile, string& indent, string& tmp2) const;

	GETSET(time_t, lastWrite, LastWrite);

	~ShareDirectory();

	ProfileTokenSet getRootProfiles() const noexcept;
	void copyRootProfiles(ProfileTokenSet& profiles_, bool aSetCacheDirty) const noexcept;
	bool isRoot() const noexcept;

	void countStats(time_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles_, size_t& lowerCaseFiles_, size_t& totalStrLen_) const noexcept;

	// check for an updated modify date from filesystem
	void updateModifyDate();

	ShareDirectory(ShareDirectory&) = delete;
	ShareDirectory& operator=(ShareDirectory&) = delete;

	const ShareRoot::Ptr& getRoot() const noexcept;
	void increaseSize(int64_t aSize, int64_t& totalSize_) noexcept;
	void decreaseSize(int64_t aSize, int64_t& totalSize_) noexcept;

	const Set& getDirectories() const noexcept {
		return directories;
	}

	ShareDirectory* getParent() const noexcept {
		return parent;
	}

	// Find child directory by path
	// Returning of the initial directory (aPath empty) is not supported
	ShareDirectory::Ptr findDirectoryByPath(const string& aPath, char aSeparator) const noexcept;

	ShareDirectory::Ptr findDirectoryLower(const string& aName) const noexcept;
	File* findFileLower(const string& aNameLower) const noexcept;


	class RootIsParentOrExact {
	public:
		// Returns true for items matching the predicate that are parent directories of compareTo (or exact matches)
		RootIsParentOrExact(const string& aCompareTo) : compareToLower(Text::toLower(aCompareTo)), separator(PATH_SEPARATOR) {}
		bool operator()(const ShareDirectory::Ptr& aDirectory) const noexcept;

		RootIsParentOrExact& operator=(const RootIsParentOrExact&) = delete;
	private:
		const string compareToLower;
		const char separator;
	};

	static void addDirName(const ShareDirectory::Ptr& aDir, ShareDirectory::MultiMap& aDirNames, ShareBloom& aBloom) noexcept;
	static void removeDirName(const ShareDirectory& aDir, ShareDirectory::MultiMap& aDirNames) noexcept;

#ifdef _DEBUG
	// Checks that duplicate/incorrect directories won't get through
	// Called before the directory has been added in the index
	static void checkAddedDirNameDebug(const ShareDirectory::Ptr& aDir, ShareDirectory::MultiMap& aDirNames) noexcept;
#endif

	void addFile(DualString&& aName, const HashedFile& fi, ShareTreeMaps& maps_, int64_t& sharedSize_, ProfileTokenSet* dirtyProfiles_ = nullptr) noexcept;

	File::Set getFiles() const noexcept {
		return files;
	}

	const DualString& getRealName() const noexcept {
		return realName;
	}

	// Shoild not be used directly, use createNormal or createRoot instead
	ShareDirectory(DualString&& aRealName, ShareDirectory* aParent, time_t aLastWrite, const ShareRoot::Ptr& aRoot = nullptr);
private:
	File::Set files;
	void cleanIndices(int64_t& sharedSize_, File::TTHMap& tthIndex_, ShareDirectory::MultiMap& dirNames_) const noexcept;

	ShareDirectory* parent;
	Set directories;

	// Size for files directly inside this directory
	int64_t size = 0;
	ShareRoot::Ptr root;

	string getRealPath(const string& path) const noexcept;
	DualString realName;
};

class ShareTreeMaps {
public:
	typedef std::function<ShareBloom*()> GetBloomF;
	ShareTreeMaps(GetBloomF&& aGetBloomF) : getBloomF(aGetBloomF) {}

	// Map real name to virtual name - multiple real names may be mapped to a single virtual one
	ShareDirectory::Map rootPaths;

	// All directory names cached for easy lookups (mostly for directory dupe checks)
	ShareDirectory::MultiMap lowerDirNameMap;

	ShareDirectory::File::TTHMap tthIndex;
	ShareBloom& getBloom() noexcept {
		return *getBloomF();
	}
private:
	GetBloomF getBloomF;
};

class FilelistDirectory {
public:
	typedef unordered_map<string*, unique_ptr<FilelistDirectory>, noCaseStringHash, noCaseStringEq> Map;

	static unique_ptr<FilelistDirectory> generateRoot(const ShareDirectory::List& aRootDirectory, const ShareDirectory::List& aChildren, bool aRecursive);

	typedef std::function<void(const StringList& /*directoryPaths*/, int /*dupeFileCount*/)> DuplicateFileHandler;
	void toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool aFullList, const DuplicateFileHandler& aDuplicateFileHandler) const;
	void filesToXml(OutputStream& xmlFile, string& indent, string& tmp2, bool aAddDate, const DuplicateFileHandler& aDuplicateFileHandler) const;

	FilelistDirectory(FilelistDirectory&) = delete;
	FilelistDirectory& operator=(FilelistDirectory&) = delete;

	GETPROP(time_t, date, Date);
	GETPROP(Map, listDirectories, ListDirectories);

	FilelistDirectory(const string& aName, time_t aDate);
private:
	void toFileList(const ShareDirectory::Ptr& aShareDirectory, bool aRecursive);

	ShareDirectory::List shareDirs;
	const string name;
};

typedef function<void(const ShareDirectory::Ptr&)> ShareDirectoryCallback;
typedef function<void(const ShareDirectory::File&)> ShareFileCallback;

typedef vector<ShareRoot::Ptr> ShareRootList;

} // namespace dcpp

#endif // !defined(SHARE_MANAGER_H)
