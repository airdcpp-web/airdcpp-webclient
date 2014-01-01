/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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

/*
 * Automatic Directory Listing Search
 * Henrik Engstr�m, henrikengstrom at home se
 */

#ifndef DCPLUSPLUS_DCPP_A_D_L_SEARCH_H
#define DCPLUSPLUS_DCPP_A_D_L_SEARCH_H

#include "StringSearch.h"
#include "Singleton.h"
#include "DirectoryListing.h"
#include "StringMatch.h"

namespace dcpp {

class AdlSearchManager;

///	Class that represent an ADL search
class ADLSearch
{
public:

	ADLSearch();								 

	// Active search
	bool isActive;

	// Some Comment
	string adlsComment;

	// Auto Queue Results
	bool isAutoQueue;

	// Search source type
	enum SourceType {
		TypeFirst = 0,
		OnlyFile = TypeFirst,
		OnlyDirectory,
		FullPath,
		TypeLast
	} sourceType;

	SourceType StringToSourceType(const string& s);

	string SourceTypeToString(SourceType t);

	tstring SourceTypeToDisplayString(SourceType t);

	// Maximum & minimum file sizes (in bytes). 
	// Negative values means do not check.
	int64_t minFileSize;
	int64_t maxFileSize;

	enum SizeType {
		SizeBytes     = TypeFirst,
		SizeKiloBytes,
		SizeMegaBytes,
		SizeGigaBytes
	};

	SizeType typeFileSize;

	SizeType StringToSizeType(const string& s);
	string SizeTypeToString(SizeType t);
	tstring SizeTypeToDisplayString(SizeType t);
	int64_t GetSizeBase();

	// Name of the destination directory (empty = 'ADLSearch') and its index
	string destDir;
	unsigned long ddIndex;

	bool isRegEx() const;
	void setRegEx(bool b);
	string getPattern();
	void setPattern(const string& aPattern);
private:
	friend class ADLSearchManager;

	StringMatch match;

	/// Prepare search
	void prepare();

	/// Search for file match
	bool matchesFile(const string& f, const string& fp, int64_t size);
	/// Search for directory match
	bool matchesDirectory(const string& d);

	bool searchAll(const string& s);
};


class ADLSearchManager : public Singleton<ADLSearchManager>
{
public:
	// Destination directory indexing
	struct DestDir {
		string name;
		DirectoryListing::Directory* dir;
		DirectoryListing::Directory* subdir;
		bool fileAdded;
	};
	typedef vector<DestDir> DestDirList;

	ADLSearchManager();
	~ADLSearchManager();

	// Search collections
	//typedef vector<ADLSearch*> SearchCollection;
	typedef vector<ADLSearch> SearchCollection;
	SearchCollection collection;


	// Load/save search collection to XML file
	void load();
	void save(bool forced = false);

	// Settings
	GETSET(bool, breakOnFirst, BreakOnFirst)
	GETSET(HintedUser, user, User)

	// @remarks Used to add ADLSearch directories to an existing DirectoryListing
	void matchListing(DirectoryListing& /*aDirList*/) noexcept;
	bool addCollection(ADLSearch& search, int index);
	bool removeCollection(int index);
	bool changeState(int index, bool enabled);
	bool updateCollection(ADLSearch& search, int index);
	int8_t getRunning() { return running; }
private:
	ADLSearch::SourceType StringToSourceType(const string& s);
	bool dirty;

	// @internal
	void matchRecurse(DestDirList& /*aDestList*/, const DirectoryListing::Directory::Ptr& /*aDir*/, string& /*aPath*/, DirectoryListing& /*aDirList*/);
	// Search for file match
	void MatchesFile(DestDirList& destDirVector, const DirectoryListing::File *currentFile, string& fullPath);
	// Search for directory match
	void MatchesDirectory(DestDirList& destDirVector, const DirectoryListing::Directory::Ptr& currentDir, string& fullPath);
	// Step up directory
	void stepUpDirectory(DestDirList& destDirVector);

	// Prepare destination directory indexing
	void PrepareDestinationDirectories(DestDirList& destDirVector, DirectoryListing::Directory::Ptr& root);
	// Finalize destination directories
	void FinalizeDestinationDirectories(DestDirList& destDirVector, DirectoryListing::Directory::Ptr& root);

	int8_t running;
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_A_D_L_SEARCH_H)