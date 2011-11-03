/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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

#include "Util.h"

#include "SettingsManager.h"
#include "ResourceManager.h"
#include "FavoriteManager.h"

#include "StringSearch.h"
#include "StringTokenizer.h"
#include "Singleton.h"
#include "DirectoryListing.h"
#include "pme.h"
#include "LogManager.h"

namespace dcpp {

class AdlSearchManager;

///	Class that represent an ADL search
class ADLSearch
{
public:

	ADLSearch();


	// The search string
	string searchString;									 

	// Active search
	bool isActive;
	
	// Forbidden file
	bool isForbidden;
	// Regexp
	bool isRegexp;
	// Casesensitive
	bool isCaseSensitive;
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
		TTHash,
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

private:
	friend class ADLSearchManager;

	// Search for file match 
	bool checkSize(int64_t size);
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
		DestDir() : name(""), dir(NULL), subdir(NULL) {}
	};
	typedef vector<DestDir> DestDirList;

	ADLSearchManager();
	~ADLSearchManager();

	// Search collections
	typedef vector<ADLSearch> SearchCollection;

	typedef pair<PME, ADLSearch> regexPair;
	typedef vector<regexPair> RegexSearchCollection;

	typedef pair<StringSearch::List, ADLSearch> ssPair;
	typedef vector<ssPair> StringSearchCollection;

	typedef unordered_map<TTHValue, ADLSearch> TTHSearchCollection;

	SearchCollection collection;

	//for files
	RegexSearchCollection regexCol;
	TTHSearchCollection tthCol;
	StringSearchCollection ssCol;

	//for dirs
	RegexSearchCollection regexColDir;
	TTHSearchCollection tthColDir;
	StringSearchCollection ssColDir;


	// Load/save search collection to XML file
	void Load();
	void Save();

	// Settings
	GETSET(bool, breakOnFirst, BreakOnFirst)
	GETSET(HintedUser, user, User)
	GETSET(bool, compareTTH, CompareTTH)
	GETSET(bool, compareSS, CompareSS)
	GETSET(bool, compareRE, CompareRE)

	// @remarks Used to add ADLSearch directories to an existing DirectoryListing
	void matchListing(DirectoryListing& /*aDirList*/) noexcept;

private:
	// @internal
	void matchRecurse(DestDirList& /*aDestList*/, DirectoryListing::Directory* /*aDir*/, string& /*aPath*/);
	// Search for file match
	void MatchesFile(DestDirList& destDirVector, DirectoryListing::File *currentFile, string& fullPath);
	// Search for directory match
	void MatchesDirectory(DestDirList& destDirVector, DirectoryListing::Directory* currentDir, string& fullPath);
	// Step up directory
	void stepUpDirectory(DestDirList& destDirVector);

	bool matchSS(const string& s, const StringSearch::List stringSearchList);
	bool matchTTH(const TTHValue fileTTH) { return tthCol.find(fileTTH) != tthCol.end(); }

	// Prepare destination directory indexing
	void PrepareDestinationDirectories(DestDirList& destDirVector, DirectoryListing::Directory* root, StringMap& params);
	// Finalize destination directories
	void FinalizeDestinationDirectories(DestDirList& destDirVector, DirectoryListing::Directory* root);

	static string getConfigFile();
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_A_D_L_SEARCH_H)


/**
 * @file
 * $Id: ADLSearch.h 466 2009-11-13 18:47:25Z BigMuscle $
  */
