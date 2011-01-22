/*
 * Copyright (C) 2001-2009 Jacek Sieka, arnetheduck on gmail point com
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
#include "DCPlusPlus.h"

#include "ADLSearch.h"
#include "QueueManager.h"
#include "ClientManager.h"

#include "File.h"
#include "SimpleXML.h"

namespace dcpp {

///////////////////////////////////////////////////////////////////////////////
//
//	Load old searches from disk
//
///////////////////////////////////////////////////////////////////////////////
void ADLSearchManager::Load()
{
	// Clear current
	collection.clear();

//ApexDC start
	// If there is nothing to load (new install) insert our default example
	if(!Util::fileExists(getConfigFile())) {
		ADLSearch search;
		search.searchString = "^(?=.*?\\b(pre.?teen|incest|lolita|r@ygold|rape|raped|underage|kiddy)\\b.*?\\b(sex|fuck|porn|xxx)\\b).+\\.(avi|mpg|mpeg|mov|jpg|bmp|gif|asf|wmv|dctmp|asf)$";
		search.sourceType = search.StringToSourceType("Filename");
		search.destDir = STRING(ADLS_DISCARD);
		search.adlsComment = "Filters preteen porn, this is an example only!";
		search.isRegexp = true;
		search.isCaseSensitive = false;
		collection.push_back(search);
	}
//ApexDC end


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
					ADLSearch search;

					//A brave attempt to load FulDC RegExp ADL
					//And Load compabillity with other clients to
					//It might work /Yada

					if(xml.findChild("SearchString")) {
						search.searchString = xml.getChildData();
						xml.resetCurrentChild();
					}
					if(xml.findChild("SourceType")) {
						search.sourceType = search.StringToSourceType(xml.getChildData());
						xml.resetCurrentChild();
					}
					if(xml.findChild("DestDirectory")) {
						search.destDir = xml.getChildData();
						xml.resetCurrentChild();
					}
					if(xml.findChild("AdlsComment")) {
						search.adlsComment = xml.getChildData();
						xml.resetCurrentChild();
					} else {
						search.adlsComment = ("none");
						xml.resetCurrentChild();
					}
					if(xml.findChild("IsActive")) {
						search.isActive = (Util::toInt(xml.getChildData()) != 0);
						xml.resetCurrentChild();
					}
					
					if(xml.findChild("IsForbidden")) {
						search.isForbidden = (Util::toInt(xml.getChildData()) != 0);
						xml.resetCurrentChild();
					} else {
						search.isForbidden = 0;
						xml.resetCurrentChild();
					}
					if(xml.findChild("IsRegExp")) {
						search.isRegexp = (Util::toInt(xml.getChildData()) != 0);
						xml.resetCurrentChild();
					} else if(search.searchString.find("$Re:") == 0){
						search.isRegexp = 1;
						xml.resetCurrentChild();
					} else {
						search.isRegexp = 0;
						xml.resetCurrentChild();
					}
					if(xml.findChild("IsCaseSensitive")) {
						search.isCaseSensitive = (Util::toInt(xml.getChildData()) != 0);
						xml.resetCurrentChild();
					} else if(search.searchString.find("$Re:") == 0){
						search.isCaseSensitive = 0;
						xml.resetCurrentChild();
					} else {
						search.isCaseSensitive = 1;
						xml.resetCurrentChild();
					}
					if(xml.findChild("MaxSize")) {
						search.maxFileSize = Util::toInt64(xml.getChildData());
						xml.resetCurrentChild();
					}
					if(xml.findChild("MinSize")) {
						search.minFileSize = Util::toInt64(xml.getChildData());
						xml.resetCurrentChild();
					}
					if(xml.findChild("SizeType")) {
						search.typeFileSize = search.StringToSizeType(xml.getChildData());
						xml.resetCurrentChild();
					}
					if(xml.findChild("IsAutoQueue")) {
						search.isAutoQueue = (Util::toInt(xml.getChildData()) != 0);
						xml.resetCurrentChild();
					}

					// Add search to collection
					if(search.searchString.size() > 0) {
						collection.push_back(search);
					}

					// Go to next search
					xml.stepOut();
				}
			}
		}
	} catch(const SimpleXMLException&) {
		return;
	} catch(const FileException&) {
		return;
	}
}

///////////////////////////////////////////////////////////////////////////////
//
//	Save current searches to disk
//
///////////////////////////////////////////////////////////////////////////////
void ADLSearchManager::Save()
{
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
			ADLSearch& search = *i;
			if(search.searchString.size() == 0) {
				continue;
			}
			string type = "type";
			xml.addTag("Search");
			xml.stepIn();

			xml.addTag("SearchString", search.searchString);
			xml.addChildAttrib(type, string("string"));

			xml.addTag("SourceType", search.SourceTypeToString(search.sourceType));
			xml.addChildAttrib(type, string("string"));

			xml.addTag("DestDirectory", search.destDir);
			xml.addChildAttrib(type, string("string"));

			xml.addTag("AdlsComment", search.adlsComment);
			xml.addChildAttrib(type, string("string"));

			xml.addTag("IsActive", search.isActive);
			xml.addChildAttrib(type, string("int"));

			xml.addTag("IsForbidden", search.isForbidden);
			xml.addChildAttrib(type, string("int"));

			xml.addTag("IsRegExp", search.isRegexp);
			xml.addChildAttrib(type, string("int"));

			xml.addTag("IsCaseSensitive", search.isCaseSensitive);
			xml.addChildAttrib(type, string("int"));

			xml.addTag("MaxSize", search.maxFileSize);
			xml.addChildAttrib(type, string("int64"));

			xml.addTag("MinSize", search.minFileSize);
			xml.addChildAttrib(type, string("int64"));

			xml.addTag("SizeType", search.SizeTypeToString(search.typeFileSize));
			xml.addChildAttrib(type, string("string"));

			xml.addTag("IsAutoQueue", search.isAutoQueue);
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
		} catch(const FileException&) {
			return;
		}
	} catch(const SimpleXMLException&) {
		return;
	}
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
	// Match searches
	for(SearchCollection::iterator is = collection.begin(); is != collection.end(); ++is) {
		if(destDirVector[is->ddIndex].fileAdded) {
			continue;
		}
		if(is->MatchesFile(currentFile->getName(), filePath, currentFile->getTTH().toBase32(), currentFile->getSize())) {
			DirectoryListing::File *copyFile = new DirectoryListing::File(*currentFile, true);
			if(is->isForbidden) {

				string comment, name;
				comment = is->adlsComment.c_str();
				name = currentFile->getName().c_str();

				StringMap params;
				params["AC"] = comment;
				params["AI"] = name;
			}
			destDirVector[is->ddIndex].dir->files.push_back(copyFile);
			destDirVector[is->ddIndex].fileAdded = true;

			if(is->isAutoQueue && !is->isForbidden) {
				try {
					QueueManager::getInstance()->add(SETTING(DOWNLOAD_DIRECTORY) + currentFile->getName(),
						currentFile->getSize(), currentFile->getTTH(), getUser());
				} catch(const Exception&) {	}
			}

			if(breakOnFirst) {
				// Found a match, search no more
				break;
			}
		}
	}
}

bool ADLSearch::SearchAll(const string& s) {
		//decide if regexps should be used
		if(isRegexp) {
			try {
			PME reg(searchString, isCaseSensitive ? "" : "i");
			if(reg.IsValid()) {
				return reg.match(s) > 0;
		}else{
				return false;
			}
			} catch (...) {
				LogManager::getInstance()->message("Adl Search regexp caught an Error");
			}
		} else {
		// Match all substrings
		for(StringSearch::List::const_iterator i = stringSearchList.begin(); i != stringSearchList.end(); ++i) {
			if(!i->match(s)) {
				return false;
			}
		}
		return (stringSearchList.size() != 0);
		}
	}


void ADLSearchManager::MatchesDirectory(DestDirList& destDirVector, DirectoryListing::Directory* currentDir, string& fullPath) {
	// Add to any substructure being stored
	for(DestDirList::iterator id = destDirVector.begin(); id != destDirVector.end(); ++id) {
		if(id->subdir != NULL) {
			DirectoryListing::Directory* newDir = 
				new DirectoryListing::AdlDirectory(fullPath, id->subdir, currentDir->getName());
			id->subdir->directories.push_back(newDir);
			id->subdir = newDir;
		}
	}

	// Prepare to match searches
	if(currentDir->getName().size() < 1) {
		return;
	}

	// Match searches
	for(SearchCollection::iterator is = collection.begin(); is != collection.end(); ++is) {
		if(destDirVector[is->ddIndex].subdir != NULL) {
			continue;
		}
		if(is->MatchesDirectory(currentDir->getName())) {
			destDirVector[is->ddIndex].subdir = 
				new DirectoryListing::AdlDirectory(fullPath, destDirVector[is->ddIndex].dir, currentDir->getName());
			destDirVector[is->ddIndex].dir->directories.push_back(destDirVector[is->ddIndex].subdir);
			if(breakOnFirst) {
				// Found a match, search no more
				break;
			}
		}
	}
}

void ADLSearchManager::PrepareDestinationDirectories(DestDirList& destDirVector, DirectoryListing::Directory* root, StringMap& params) {
	// Load default destination directory (index = 0)
	destDirVector.clear();
	vector<DestDir>::iterator id = destDirVector.insert(destDirVector.end(), DestDir());
	id->name = "ADLSearch";
	id->dir  = new DirectoryListing::Directory(root, "<<<" + id->name + ">>>", true, true);

	// Scan all loaded searches
	for(SearchCollection::iterator is = collection.begin(); is != collection.end(); ++is) {
		// Check empty destination directory
		if(is->destDir.size() == 0) {
			// Set to default
			is->ddIndex = 0;
			continue;
		}

		// Check if exists
		bool isNew = true;
		long ddIndex = 0;
		for(id = destDirVector.begin(); id != destDirVector.end(); ++id, ++ddIndex) {
			if(stricmp(is->destDir.c_str(), id->name.c_str()) == 0) {
				// Already exists, reuse index
				is->ddIndex = ddIndex;
				isNew = false;
				break;
			}
		}

		if(isNew) {
			// Add new destination directory
			id = destDirVector.insert(destDirVector.end(), DestDir());
			id->name = is->destDir;
			id->dir  = new DirectoryListing::Directory(root, "<<<" + id->name + ">>>", true, true);
			is->ddIndex = ddIndex;
		}
	}
	// Prepare all searches
	for(SearchCollection::iterator ip = collection.begin(); ip != collection.end(); ++ip) {
		ip->Prepare(params);
	}
}

void ADLSearchManager::matchListing(DirectoryListing& aDirList) throw() {
	StringMap params;
	params["userNI"] = ClientManager::getInstance()->getNicks(aDirList.getHintedUser())[0];
	params["userCID"] = aDirList.getUser()->getCID().toBase32();

	setUser(aDirList.getHintedUser());

	DestDirList destDirs;
	PrepareDestinationDirectories(destDirs, aDirList.getRoot(), params);
	setBreakOnFirst(BOOLSETTING(ADLS_BREAK_ON_FIRST));

	string path(aDirList.getRoot()->getName());
	matchRecurse(destDirs, aDirList.getRoot(), path);

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
	StepUpDirectory(aDestList);
}

} // namespace dcpp

/**
 * @file
 * $Id: ADLSearch.cpp 466 2009-11-13 18:47:25Z BigMuscle $
 */
