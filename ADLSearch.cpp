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
 * Henrik Engström, henrikengstrom at home se
 */

#include "stdinc.h"
#include "ADLSearch.h"

#include "ClientManager.h"
#include "File.h"
#include "QueueManager.h"
#include "SimpleXML.h"

namespace dcpp {
	
	// Constructor
ADLSearch::ADLSearch() : searchString("<Enter string>"), isActive(true), isAutoQueue(false), sourceType(OnlyFile), 
		minFileSize(-1), maxFileSize(-1), typeFileSize(SizeBytes), destDir("ADLSearch"), ddIndex(0),
		isForbidden(false), isRegexp(false), isCaseSensitive(true), adlsComment("none") {}
ADLSearch::SourceType ADLSearch::StringToSourceType(const string& s) {
		if(stricmp(s.c_str(), "Filename") == 0) {
			return OnlyFile;
		} else if(stricmp(s.c_str(), "Directory") == 0) {
			return OnlyDirectory;
		} else if(stricmp(s.c_str(), "Full Path") == 0) {
			return FullPath;
		} else if(stricmp(s.c_str(), "TTH") == 0) {
			return TTHash;
		} else {
			return OnlyFile;
		}
	}

string ADLSearch::SourceTypeToString(SourceType t) {
		switch(t) {
		default:
		case OnlyFile:		return "Filename";
		case OnlyDirectory:	return "Directory";
		case FullPath:		return "Full Path";
		case TTHash:		return "TTH";
		}
	}

tstring ADLSearch::SourceTypeToDisplayString(SourceType t) {
		switch(t) {
		default:
		case OnlyFile:		return TSTRING(FILENAME);
		case OnlyDirectory:	return TSTRING(DIRECTORY);
		case FullPath:		return TSTRING(ADLS_FULL_PATH);
		case TTHash:		return _T("TTH");
		}
	}

ADLSearch::SizeType ADLSearch::StringToSizeType(const string& s) {
		if(stricmp(s.c_str(), "B") == 0) {
			return SizeBytes;
		} else if(stricmp(s.c_str(), "KiB") == 0) {
			return SizeKiloBytes;
		} else if(stricmp(s.c_str(), "MiB") == 0) {
			return SizeMegaBytes;
		} else if(stricmp(s.c_str(), "GiB") == 0) {
			return SizeGigaBytes;
		} else {
			return SizeBytes;
		}
	}

string ADLSearch::SizeTypeToString(SizeType t) {
		switch(t) {
		default:
		case SizeBytes:		return "B";
		case SizeKiloBytes:	return "KiB";
		case SizeMegaBytes:	return "MiB";
		case SizeGigaBytes:	return "GiB";
		
		}
	}
	

tstring ADLSearch::SizeTypeToDisplayString(ADLSearch::SizeType t) {
		switch(t) {
		default:
		case SizeBytes:		return CTSTRING(B);
		case SizeKiloBytes:	return CTSTRING(KiB);
		case SizeMegaBytes:	return CTSTRING(MiB);
		case SizeGigaBytes:	return CTSTRING(GiB);
		}
	}

int64_t ADLSearch::GetSizeBase() {
		switch(typeFileSize) {
		default:
		case SizeBytes:		return (int64_t)1;
		case SizeKiloBytes:	return (int64_t)1024;
		case SizeMegaBytes:	return (int64_t)1024 * (int64_t)1024;
		case SizeGigaBytes:	return (int64_t)1024 * (int64_t)1024 * (int64_t)1024;
		
	}
}

// Constructor/destructor
ADLSearchManager::ADLSearchManager() : user(UserPtr(), Util::emptyString), running(0) {
	Load(); 
}

ADLSearchManager::~ADLSearchManager() {
	Save(); 
}

ADLSearch::SourceType ADLSearchManager::StringToSourceType(const string& s) {
	if(stricmp(s.c_str(), "Filename") == 0) {
		return ADLSearch::OnlyFile;
	} else if(stricmp(s.c_str(), "Directory") == 0) {
		return ADLSearch::OnlyDirectory;
	} else if(stricmp(s.c_str(), "Full Path") == 0) {
		return ADLSearch::FullPath;
	} else if(stricmp(s.c_str(), "TTH") == 0) {
		return ADLSearch::TTHash;
	} else {
		return ADLSearch::OnlyFile;
	}
}

void ADLSearchManager::Load()
{
	if (running > 0) {
		LogManager::getInstance()->message(CSTRING(ADLSEARCH_IN_PROGRESS));
		return;
	}

	// Clear current
	collection.clear();
	ssCol.clear();
	ssColDir.clear();
	regexCol.clear();
	regexColDir.clear();
	tthCol.clear();

	// Load file as a string
	try {
		SimpleXML xml;
		Util::migrate(getConfigFile());
		xml.fromXML(File(getConfigFile(), File::READ, File::OPEN).read());

		if(xml.findChild("ADLSearch")) {
			xml.stepIn();

			// Predicted several groups of searches to be differentiated
			// in multiple categories. Not implemented yet.
			if(xml.findChild("SearchGroup")) {
				xml.stepIn();

				// Loop until no more searches found
				while(xml.findChild("Search")) {
					xml.stepIn();

					// Found another search, load it
					ADLSearch* search = new ADLSearch();

					if(xml.findChild("SearchString")) {
						search->searchString = xml.getChildData();
						xml.resetCurrentChild();
					}
					if(xml.findChild("SourceType")) {
						search->sourceType = search->StringToSourceType(xml.getChildData());
						xml.resetCurrentChild();
					}
					if(xml.findChild("DestDirectory")) {
						search->destDir = xml.getChildData();
						xml.resetCurrentChild();
					}
					if(xml.findChild("AdlsComment")) {
						search->adlsComment = xml.getChildData();
						xml.resetCurrentChild();
					} else {
						search->adlsComment = "none";
						xml.resetCurrentChild();
					}
					if(xml.findChild("IsActive")) {
						search->isActive = (Util::toInt(xml.getChildData()) != 0);
						xml.resetCurrentChild();
					}
					
					if(xml.findChild("IsForbidden")) {
						search->isForbidden = (Util::toInt(xml.getChildData()) != 0);
						xml.resetCurrentChild();
					} else {
						search->isForbidden = 0;
						xml.resetCurrentChild();
					}
					if(xml.findChild("IsRegExp")) {
						search->isRegexp = (Util::toInt(xml.getChildData()) != 0);
						xml.resetCurrentChild();
					}
					if(xml.findChild("IsCaseSensitive")) {
						search->isCaseSensitive = (Util::toInt(xml.getChildData()) != 0);
						xml.resetCurrentChild();
					}
					if(xml.findChild("MaxSize")) {
						search->maxFileSize = Util::toInt64(xml.getChildData());
						xml.resetCurrentChild();
					}
					if(xml.findChild("MinSize")) {
						search->minFileSize = Util::toInt64(xml.getChildData());
						xml.resetCurrentChild();
					}
					if(xml.findChild("SizeType")) {
						search->typeFileSize = search->StringToSizeType(xml.getChildData());
						xml.resetCurrentChild();
					}
					if(xml.findChild("IsAutoQueue")) {
						search->isAutoQueue = (Util::toInt(xml.getChildData()) != 0);
						xml.resetCurrentChild();
					}

					// Add search to collection
					addCollection(search, true, true);

					// Go to next search
					xml.stepOut();
				}
			}
		}
	} 
	catch(const SimpleXMLException&) { } 
	catch(const FileException&) { }
}

bool ADLSearchManager::addCollection(ADLSearch* search, bool addMain, bool addSub, bool useIndex, int index) {
	if (running > 0) {
		LogManager::getInstance()->message(CSTRING(ADLSEARCH_IN_PROGRESS));
		return false;
	}

	if (search->searchString.size() == 0) {
		return false;
	}
	
	if (addMain) {
		if (!useIndex) {
			collection.push_back(search);
		//} else if (index <= collection.size()) {
		} else {
			collection.insert(collection.begin() + index, search);
		}
	}


	//check validity
	if(!search->isActive || !addSub) {
		return true;
	}

	bool dirOnly = search->sourceType == 1;

	//add into correct collection
	if (search->isRegexp) {
		try {
			PME reg(search->searchString, search->isCaseSensitive ? "" : "i");
			if(reg.IsValid()) {
				reg.study();
				if (dirOnly) {
					regexColDir.push_back(make_pair(reg, search));
				} else {
					regexCol.push_back(make_pair(reg, search));
				}
			}
		} catch(...) {
			//ignore
		}
	} else if (search->sourceType == 3) {
		if (search->searchString.length() == 39) {
			//tthCol[TTHValue(search.searchString)] = *search;
		}
	} else {
		StringSearch::List stringSearchList;
		// Split into substrings
		StringTokenizer<string> st(search->searchString, ' ');
		for(StringList::iterator i = st.getTokens().begin(); i != st.getTokens().end(); ++i) {
			if(i->size() > 0) {
				// Add substring search
				stringSearchList.push_back(StringSearch(*i));
			}
		}

		if (stringSearchList.size() > 0) {
			if (dirOnly) {
				ssColDir.push_back(make_pair(stringSearchList, search));
			} else {
				ssCol.push_back(make_pair(stringSearchList, search));
			}
		}
	}

	//cache these
	setCompareRE(!regexCol.empty() || !regexColDir.empty());
	setCompareTTH(!tthCol.empty());
	setCompareSS(!ssCol.empty() || !ssColDir.empty());
	return true;
}

bool ADLSearchManager::removeCollection(int index, bool move) {
	if (running > 0) {
		LogManager::getInstance()->message(CSTRING(ADLSEARCH_IN_PROGRESS));
		return false;
	}

	collection.erase(collection.begin() + index);

	if (!move) {
		//rebuild subcollections
		ssCol.clear();
		ssColDir.clear();
		regexCol.clear();
		regexColDir.clear();
		tthCol.clear();
		for (auto i = collection.begin(); i != collection.end(); ++i) {
			addCollection(*i, false, true);
		}
	}
	return true;
}

void ADLSearchManager::Save() {
	// Prepare xml string for saving
	try {
		SimpleXML xml;

		xml.addTag("ADLSearch");
		xml.stepIn();

		// Predicted several groups of searches to be differentiated
		// in multiple categories. Not implemented yet.
		xml.addTag("SearchGroup");
		xml.stepIn();

		// Save all	searches
		for(SearchCollection::iterator i = collection.begin(); i != collection.end(); ++i) {
			ADLSearch* search = *i;
			if(search->searchString.size() == 0) {
				continue;
			}
			string type = "type";
			xml.addTag("Search");
			xml.stepIn();

			xml.addTag("SearchString", search->searchString);
			xml.addChildAttrib(type, string("string"));

			xml.addTag("SourceType", search->SourceTypeToString(search->sourceType));
			xml.addChildAttrib(type, string("string"));

			xml.addTag("DestDirectory", search->destDir);
			xml.addChildAttrib(type, string("string"));

			xml.addTag("AdlsComment", search->adlsComment);
			xml.addChildAttrib(type, string("string"));

			xml.addTag("IsActive", search->isActive);
			xml.addChildAttrib(type, string("int"));

			xml.addTag("IsForbidden", search->isForbidden);
			xml.addChildAttrib(type, string("int"));

			xml.addTag("IsRegExp", search->isRegexp);
			xml.addChildAttrib(type, string("int"));

			xml.addTag("IsCaseSensitive", search->isCaseSensitive);
			xml.addChildAttrib(type, string("int"));

			xml.addTag("MaxSize", search->maxFileSize);
			xml.addChildAttrib(type, string("int64"));

			xml.addTag("MinSize", search->minFileSize);
			xml.addChildAttrib(type, string("int64"));

			xml.addTag("SizeType", search->SizeTypeToString(search->typeFileSize));
			xml.addChildAttrib(type, string("string"));

			xml.addTag("IsAutoQueue", search->isAutoQueue);
			xml.addChildAttrib(type, string("int"));

			xml.stepOut();
		}

		xml.stepOut();

		xml.stepOut();

		// Save string to file			
		try {
			File fout(getConfigFile(), File::WRITE, File::CREATE | File::TRUNCATE);
			fout.write(SimpleXML::utf8Header);
			fout.write(xml.toXML());
			fout.close();
		} catch(const FileException&) {	}
	} catch(const SimpleXMLException&) { }
}


bool ADLSearch::checkSize(int64_t size) {
	// Check size for files
	if(size >= 0 && (sourceType == OnlyFile || sourceType == FullPath)) {
		if(minFileSize >= 0 && size < minFileSize * GetSizeBase()) {
			// Too small
			return false;
		}
		if(maxFileSize >= 0 && size > maxFileSize * GetSizeBase()) {
			// Too large
			return false;
		}
	}
	return true;
}

void ADLSearchManager::MatchesFile(DestDirList& destDirVector, DirectoryListing::File *currentFile, string& fullPath) {
	// Add to any substructure being stored
	for(DestDirList::iterator id = destDirVector.begin(); id != destDirVector.end(); ++id) {
		if(id->subdir != NULL) {
			DirectoryListing::File *copyFile = new DirectoryListing::File(*currentFile, true);
			dcassert(id->subdir->getAdls());
			
			id->subdir->files.push_back(copyFile);
		}
		id->fileAdded = false;	// Prepare for next stage
	}

	// Prepare to match searches
	if(currentFile->getName().size() < 1) {
		return;
	}

	string filePath = fullPath + "\\" + currentFile->getName();
	ADLSearch* match = NULL;
	bool hasMatch = false;

	//Match tth
	if (compareTTH) {
		auto i = tthCol.find(currentFile->getTTH());
		if (i != tthCol.end()) {
			if(destDirVector[i->second->ddIndex].subdir == NULL) {
				match = i->second;
				hasMatch = true;
			}
		}
	}

	//Match regexes
	if (compareRE && (!hasMatch || !breakOnFirst)) {
		for (auto i = regexCol.begin(); i != regexCol.end(); ++i) {
			if(destDirVector[i->second->ddIndex].subdir != NULL || !(i->second->checkSize(currentFile->getSize()))) {
				continue;
			}
			if (i->first.match(i->second->sourceType == 0 ? currentFile->getName() : filePath) > 0) {
				match = i->second;
				hasMatch = true;
				if(breakOnFirst) {
					break;
				}
			}
		}
	}

	//Match StringSearch
	if (compareSS && (!hasMatch || !breakOnFirst)) {
		for (auto i = ssCol.begin(); i != ssCol.end(); ++i) {
			if(destDirVector[i->second->ddIndex].subdir != NULL || !(i->second->checkSize(currentFile->getSize()))) {
				continue;
			}
			if (matchSS(i->second->sourceType == 0 ? currentFile->getName() : filePath, i->first)) {
				match = i->second;
				hasMatch = true;
				if(breakOnFirst) {
					break;
				}
			}
		}
	}

	if (hasMatch) {
		DirectoryListing::File *copyFile = new DirectoryListing::File(*currentFile, true);
		if(match->isForbidden) {

			string comment, name;
			comment = match->adlsComment.c_str();
			name = currentFile->getName().c_str();

			StringMap params;
			params["AC"] = comment;
			params["AI"] = name;
		}
		//LogManager::getInstance()->message("Add item " + currentFile->getName() +  " to adldir " + Util::toString(match->ddIndex) + " (item: " + match->getSearchString() + ")"); 
		destDirVector[match->ddIndex].dir->files.push_back(copyFile);
		destDirVector[match->ddIndex].fileAdded = true;

		if(match->isAutoQueue && !match->isForbidden) {
			try {
				QueueManager::getInstance()->add(SETTING(DOWNLOAD_DIRECTORY) + currentFile->getName(),
					currentFile->getSize(), currentFile->getTTH(), getUser());
			} catch(const Exception&) {	}
		}
	}
}

bool ADLSearchManager::matchSS(const string& s, const StringSearch::List stringSearchList) {
	// Match all substrings
	for(StringSearch::List::const_iterator i = stringSearchList.begin(); i != stringSearchList.end(); ++i) {
		if(!i->match(s)) {
			return false;
		}
	}
	return true;
}

void ADLSearchManager::MatchesDirectory(DestDirList& destDirVector, DirectoryListing::Directory* currentDir, string& fullPath) {
	// Add to any substructure being stored
	for(DestDirList::iterator id = destDirVector.begin(); id != destDirVector.end(); ++id) {
		if(id->subdir != NULL) {
			DirectoryListing::Directory* newDir = new DirectoryListing::AdlDirectory(fullPath, id->subdir, currentDir->getName());
			id->subdir->directories.push_back(newDir);
			id->subdir = newDir;
		}
	}

	// Prepare to match searches
	if(currentDir->getName().size() < 1) {
		return;
	}

	ADLSearch* match = NULL;
	bool hasMatch = false;

	//Match regexes
	if (compareRE) {
		for (auto i = regexColDir.begin(); i != regexColDir.end(); ++i) {
			if(destDirVector[i->second->ddIndex].subdir != NULL) {
				continue;
			}
			if (i->first.match(currentDir->getName()) > 0) {
				match = i->second;
				hasMatch = true;
				if(breakOnFirst) {
					break;
				}
			}
		}
	}

	//Match StringSearch
	if (compareSS && (!hasMatch || !breakOnFirst)) {
		for (auto i = ssColDir.begin(); i != ssColDir.end(); ++i) {
			if(destDirVector[i->second->ddIndex].subdir != NULL) {
				continue;
			}
			if (matchSS(currentDir->getName(), i->first)) {
				match = i->second;
				hasMatch = true;
				if(breakOnFirst) {
					break;
				}
			}
		}
	}

	
	if (hasMatch) {
		//LogManager::getInstance()->message("Add dir " + currentDir->getName() +  " to adldir " + Util::toString(match->ddIndex) + " (item: " + match->getSearchString() + ")"); 
		destDirVector[match->ddIndex].subdir = new DirectoryListing::AdlDirectory(fullPath, destDirVector[match->ddIndex].dir, currentDir->getName());
		destDirVector[match->ddIndex].dir->directories.push_back(destDirVector[match->ddIndex].subdir);
	}
}

void ADLSearchManager::stepUpDirectory(DestDirList& destDirVector) {
		for(DestDirList::iterator id = destDirVector.begin(); id != destDirVector.end(); ++id) {
			if(id->subdir != NULL) {
				id->subdir = id->subdir->getParent();
				if(id->subdir == id->dir) {
					id->subdir = NULL;
				}
			}
		}
	}

void ADLSearchManager::PrepareDestinationDirectories(DestDirList& destDirVector, DirectoryListing::Directory* root) {
	// Load default destination directory (index = 0)
	destDirVector.clear();
	vector<DestDir>::iterator id = destDirVector.insert(destDirVector.end(), DestDir());
	id->name = "ADLSearch";
	id->dir  = new DirectoryListing::Directory(root, "<<<" + id->name + ">>>", true, true);

	// Scan all loaded searches
	for(SearchCollection::iterator is = collection.begin(); is != collection.end(); ++is) {
		// Check empty destination directory
		if((*is)->destDir.size() == 0) {
			// Set to default
			(*is)->ddIndex = 0;
			continue;
		}

		// Check if exists
		bool isNew = true;
		long ddIndex = 0;
		for(id = destDirVector.begin(); id != destDirVector.end(); ++id, ++ddIndex) {
			if(stricmp((*is)->destDir.c_str(), id->name.c_str()) == 0) {
				// Already exists, reuse index
				(*is)->ddIndex = ddIndex;
				isNew = false;
				break;
			}
		}

		if(isNew) {
			// Add new destination directory
			id = destDirVector.insert(destDirVector.end(), DestDir());
			id->name = (*is)->destDir;
			id->dir  = new DirectoryListing::Directory(root, "<<<" + id->name + ">>>", true, true);
			(*is)->ddIndex = ddIndex;
			//LogManager::getInstance()->message("Add dir " + is->destDir +  " with index " + Util::toString(is->ddIndex) + " (item: " + is->searchString + ")"); 
		} else {
			//LogManager::getInstance()->message("Don't add dir " + is->destDir +  " with index " + Util::toString(ddIndex) + " (item: " + is->searchString + ")"); 
		}
	}

	/*LogManager::getInstance()->message("File Collecion sizes, regex: " + Util::toString(regexCol.size()) + ", TTH " + Util::toString(tthCol.size()) + ", SS: " + Util::toString(ssCol.size()));
	LogManager::getInstance()->message("Directory Collecion sizes, regex: " + Util::toString(regexColDir.size()) + ", SS: " + Util::toString(ssColDir.size())); */
	/*if (compareTTH) {
		LogManager::getInstance()->message("compareTTH"); 
	}
	if (compareSS) {
		LogManager::getInstance()->message("compareSS"); 
	}
	if (compareRE) {
		LogManager::getInstance()->message("compareRE"); 
	} */
}

void ADLSearchManager::FinalizeDestinationDirectories(DestDirList& destDirVector, DirectoryListing::Directory* root) {
	string szDiscard = "<<<" + STRING(ADLS_DISCARD) + ">>>";

	// Add non-empty destination directories to the top level
	for(vector<DestDir>::iterator id = destDirVector.begin(); id != destDirVector.end(); ++id) {
		if(id->dir->files.size() == 0 && id->dir->directories.size() == 0) {
			delete (id->dir);
		} else if(stricmp(id->dir->getName(), szDiscard) == 0) {
			delete (id->dir);
		} else {
			root->directories.push_back(id->dir);
		}
	}	
}

void ADLSearchManager::matchListing(DirectoryListing& aDirList) noexcept {
	running++;
	setUser(aDirList.getHintedUser());

	DestDirList destDirs;
	PrepareDestinationDirectories(destDirs, aDirList.getRoot());
	setBreakOnFirst(BOOLSETTING(ADLS_BREAK_ON_FIRST));

	string path(aDirList.getRoot()->getName());
	matchRecurse(destDirs, aDirList.getRoot(), path);

	running--;
	FinalizeDestinationDirectories(destDirs, aDirList.getRoot());
}

void ADLSearchManager::matchRecurse(DestDirList &aDestList, DirectoryListing::Directory* aDir, string &aPath) {
	for(DirectoryListing::Directory::Iter dirIt = aDir->directories.begin(); dirIt != aDir->directories.end(); ++dirIt) {
		string tmpPath = aPath + "\\" + (*dirIt)->getName();
		MatchesDirectory(aDestList, *dirIt, tmpPath);
		matchRecurse(aDestList, *dirIt, tmpPath);
	}
	for(DirectoryListing::File::Iter fileIt = aDir->files.begin(); fileIt != aDir->files.end(); ++fileIt) {
		MatchesFile(aDestList, *fileIt, aPath);
	}
	stepUpDirectory(aDestList);
}

string ADLSearchManager::getConfigFile() {
	 return Util::getPath(Util::PATH_USER_CONFIG) + "ADLSearch.xml"; 
}

} // namespace dcpp

/**
 * @file
 * $Id: ADLSearch.cpp 466 2009-11-13 18:47:25Z BigMuscle $
 */
