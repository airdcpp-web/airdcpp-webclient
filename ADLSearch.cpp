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
#include <boost/range/algorithm/for_each.hpp>

namespace dcpp {

using boost::range::for_each;
	
// Constructor
ADLSearch::ADLSearch() : 
isActive(true), 
isAutoQueue(false), 
sourceType(OnlyFile), 
minFileSize(-1), 
maxFileSize(-1), 
typeFileSize(SizeBytes), 
destDir("ADLSearch"), 
ddIndex(0),
adlsComment("none") {
	match.pattern = "<Enter string>";
	setRegEx(false);
}

ADLSearch::SourceType ADLSearch::StringToSourceType(const string& s) {
		if(stricmp(s.c_str(), "Filename") == 0) {
			return OnlyFile;
		} else if(stricmp(s.c_str(), "Directory") == 0) {
			return OnlyDirectory;
		} else if(stricmp(s.c_str(), "Full Path") == 0) {
			return FullPath;
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
		}
	}

tstring ADLSearch::SourceTypeToDisplayString(SourceType t) {
	switch(t) {
		default:
		case OnlyFile:		return TSTRING(FILENAME);
		case OnlyDirectory:	return TSTRING(DIRECTORY);
		case FullPath:		return TSTRING(ADLS_FULL_PATH);
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

bool ADLSearch::searchAll(const string& s) {
	return match.match(s);
}

bool ADLSearch::isRegEx() const {
	return match.getMethod() == StringMatch::REGEX;
}

void ADLSearch::setRegEx(bool b) {
	match.setMethod(b ? StringMatch::REGEX : StringMatch::PARTIAL);
}

void ADLSearch::prepare() {
	match.prepare();
}

string ADLSearch::getPattern() {
	return match.pattern;
}

void ADLSearch::setPattern(const string& aPattern) {
	match.pattern = aPattern;
}

bool ADLSearch::matchesFile(const string& f, const string& fp, int64_t size) {
	// Check status
	if(!isActive) {
		return false;
	}

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

	// Do search
	switch(sourceType) {
	default:
	case OnlyDirectory:	return false;
	case OnlyFile:		return searchAll(f);
	case FullPath:		return searchAll(fp);
	}
}

bool ADLSearch::matchesDirectory(const string& d) {
	// Check status
	if(!isActive) {
		return false;
	}
	if(sourceType != OnlyDirectory) {
		return false;
	}

	// Do search
	return searchAll(d);
}

// Constructor/destructor
ADLSearchManager::ADLSearchManager() : running(0), user(HintedUser(UserPtr(), Util::emptyString)) {
	Load(); 
}

ADLSearchManager::~ADLSearchManager() {
	Save(); 
	//for_each(collection.begin(),collection.end(), DeleteFunction());
}

ADLSearch::SourceType ADLSearchManager::StringToSourceType(const string& s) {
	if(stricmp(s.c_str(), "Filename") == 0) {
		return ADLSearch::OnlyFile;
	} else if(stricmp(s.c_str(), "Directory") == 0) {
		return ADLSearch::OnlyDirectory;
	} else if(stricmp(s.c_str(), "Full Path") == 0) {
		return ADLSearch::FullPath;
	} else {
		return ADLSearch::OnlyFile;
	}
}

void ADLSearchManager::Load()
{
	if (running > 0) {
		LogManager::getInstance()->message(CSTRING(ADLSEARCH_IN_PROGRESS), LogManager::LOG_ERROR);
		return;
	}

	// Clear current
	collection.clear();

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

					if(xml.findChild("SearchString")) {
						search.match.pattern = xml.getChildData();
						if(xml.getBoolChildAttrib("RegEx")) {
							search.setRegEx(true);
						}
					} else {
						xml.resetCurrentChild();
					}

					if(xml.findChild("SourceType")) {
						search.sourceType = search.StringToSourceType(xml.getChildData());
					} else {
						xml.resetCurrentChild();
					}

					if(xml.findChild("DestDirectory")) {
						search.destDir = xml.getChildData();
					} else {
						xml.resetCurrentChild();
					}

					if(xml.findChild("AdlsComment")) {
						search.adlsComment = xml.getChildData();
					} else {
						search.adlsComment = "none";
						xml.resetCurrentChild();
					}

					if(xml.findChild("IsActive")) {
						search.isActive = (Util::toInt(xml.getChildData()) != 0);
					} else {
						xml.resetCurrentChild();
					}

					/* For compatibility, remove in some point */
					if(xml.findChild("IsRegExp")) {
						if (Util::toInt(xml.getChildData()) > 0) {
							search.setRegEx(true);
						}
					} else {
						xml.resetCurrentChild();
					}

					if(xml.findChild("MaxSize")) {
						search.maxFileSize = Util::toInt64(xml.getChildData());
					} else {
						xml.resetCurrentChild();
					}

					if(xml.findChild("MinSize")) {
						search.minFileSize = Util::toInt64(xml.getChildData());
					} else {
						xml.resetCurrentChild();
					}

					if(xml.findChild("SizeType")) {
						search.typeFileSize = search.StringToSizeType(xml.getChildData());
					} else {
						xml.resetCurrentChild();
					}

					if(xml.findChild("IsAutoQueue")) {
						search.isAutoQueue = (Util::toInt(xml.getChildData()) != 0);
					} else {
						xml.resetCurrentChild();
					}

					// Add search to collection
					if(!search.getPattern().empty()) {
						collection.push_back(search);
					}

					// Go to next search
					xml.stepOut();
				}
			}
		}
	}

	catch(const SimpleXMLException&) { }
	catch(const FileException&) { }

	/*for(auto& s: collection) {
		s.prepare();
	}*/
	for_each(collection, [](ADLSearch& as) { as.prepare(); });
}

bool ADLSearchManager::addCollection(ADLSearch& search, int index) {
	if (running > 0) {
		LogManager::getInstance()->message(CSTRING(ADLSEARCH_IN_PROGRESS), LogManager::LOG_ERROR);
		return false;
	}

	if (search.getPattern().empty()) {
		return false;
	}
	
	search.prepare();
	collection.insert(collection.begin() + index, search);
	return true;
}

bool ADLSearchManager::removeCollection(int index, bool move) {
	if (running > 0) {
		LogManager::getInstance()->message(CSTRING(ADLSEARCH_IN_PROGRESS), LogManager::LOG_ERROR);
		return false;
	}

	collection.erase(collection.begin() + index);
	return true;
}

bool ADLSearchManager::changeState(int index, bool aIsActive) {
	if (running > 0) {
		LogManager::getInstance()->message(CSTRING(ADLSEARCH_IN_PROGRESS), LogManager::LOG_ERROR);
		return false;
	}

	collection[index].isActive = aIsActive;
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
		for_each(collection, [&xml](ADLSearch& search) {
		//for(auto& search: collection) {
			xml.addTag("Search");
			xml.stepIn();

			xml.addTag("SearchString", search.match.pattern);
			xml.addChildAttrib("RegEx", search.isRegEx());
			xml.addTag("SourceType", search.SourceTypeToString(search.sourceType));
			xml.addTag("DestDirectory", search.destDir);
			xml.addTag("AdlsComment", search.adlsComment);
			xml.addTag("IsActive", search.isActive);
			xml.addTag("MaxSize", search.maxFileSize);
			xml.addTag("MinSize", search.minFileSize);
			xml.addTag("SizeType", search.SizeTypeToString(search.typeFileSize));
			xml.addTag("IsAutoQueue", search.isAutoQueue);

			xml.stepOut();
		});

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

void ADLSearchManager::MatchesFile(DestDirList& destDirVector, const DirectoryListing::File *currentFile, string& fullPath) {
	// Add to any substructure being stored
	//for(auto& id: destDirVector) {
	for_each(destDirVector, [currentFile](DestDir& id) {
		if(id.subdir != NULL) {
			DirectoryListing::File *copyFile = new DirectoryListing::File(*currentFile, true);
			dcassert(id.subdir->getAdls());

			id.subdir->files.push_back(copyFile);
		}
		id.fileAdded = false;	// Prepare for next stage
	});

	// Prepare to match searches
	if(currentFile->getName().size() < 1) {
		return;
	}

	string filePath = fullPath + "\\" + currentFile->getName();
	// Match searches
	for(auto i = collection.begin(); i != collection.end(); ++i) {
	//for(auto& is: collection) {
		auto& is = *i;
		if(destDirVector[is.ddIndex].fileAdded) {
			continue;
		}
		if(is.matchesFile(currentFile->getName(), filePath, currentFile->getSize())) {
			DirectoryListing::File *copyFile = new DirectoryListing::File(*currentFile, true);
			destDirVector[is.ddIndex].dir->files.push_back(copyFile);
			destDirVector[is.ddIndex].fileAdded = true;

			if(is.isAutoQueue){
				try {
					QueueManager::getInstance()->add(SETTING(DOWNLOAD_DIRECTORY) + currentFile->getName(),
						currentFile->getSize(), currentFile->getTTH(), getUser(), currentFile->getPath() + currentFile->getName());
				} catch(const Exception&) { }
			}

			if(breakOnFirst) {
				// Found a match, search no more
				break;
			}
		}
	}
}

void ADLSearchManager::MatchesDirectory(DestDirList& destDirVector, const DirectoryListing::Directory* currentDir, string& fullPath) {
	// Add to any substructure being stored
	//for(auto& id: destDirVector) {
	for_each(destDirVector, [currentDir, &fullPath](DestDir& id) {
		if(id.subdir != NULL) {
			DirectoryListing::Directory* newDir =
				new DirectoryListing::AdlDirectory(fullPath, id.subdir, currentDir->getName());
			id.subdir->directories.push_back(newDir);
			id.subdir = newDir;
		}
	});

	// Prepare to match searches
	if(currentDir->getName().size() < 1) {
		return;
	}

	for(auto i = collection.begin(); i != collection.end(); ++i) {
	//for(auto& is: collection) {
		auto& is = *i;
		if(destDirVector[is.ddIndex].subdir != NULL) {
			continue;
		}
		if(is.matchesDirectory(currentDir->getName())) {
			destDirVector[is.ddIndex].subdir =
				new DirectoryListing::AdlDirectory(fullPath, destDirVector[is.ddIndex].dir, currentDir->getName());
			destDirVector[is.ddIndex].dir->directories.push_back(destDirVector[is.ddIndex].subdir);
			if(breakOnFirst) {
				// Found a match, search no more
				break;
			}
		}
	}
}

void ADLSearchManager::stepUpDirectory(DestDirList& destDirVector) {
	for(auto id = destDirVector.begin(); id != destDirVector.end(); ++id) {
		if(id->subdir) {
			id->subdir = id->subdir->getParent();
			if(id->subdir == id->dir) {
				id->subdir = nullptr;
			}
		}
	}
}

void ADLSearchManager::PrepareDestinationDirectories(DestDirList& destDirs, DirectoryListing::Directory* root) {
	// Load default destination directory (index = 0)
	destDirs.clear();
	DestDir dir = { "ADLSearch", new DirectoryListing::Directory(root, "<<<ADLSearch>>>", true, true) };
	destDirs.push_back(std::move(dir));

	// Scan all loaded searches
	for(auto i = collection.begin(); i != collection.end(); ++i) {
	//for(auto& is: collection) {
		auto& is = *i;
		// Check empty destination directory
		if(is.destDir.size() == 0) {
			// Set to default
			is.ddIndex = 0;
			continue;
		}

		// Check if exists
		bool isNew = true;
		long ddIndex = 0;
		for(auto id = destDirs.cbegin(); id != destDirs.cend(); ++id, ++ddIndex) {
			if(Util::stricmp(is.destDir.c_str(), id->name.c_str()) == 0) {
				// Already exists, reuse index
				is.ddIndex = ddIndex;
				isNew = false;
				break;
			}
		}

		if(isNew) {
			// Add new destination directory
			DestDir dir = { is.destDir, new DirectoryListing::Directory(root, "<<<" + is.destDir + ">>>", true, true) };
			destDirs.push_back(std::move(dir));
			is.ddIndex = ddIndex;
		}
	}
}

void ADLSearchManager::FinalizeDestinationDirectories(DestDirList& destDirs, DirectoryListing::Directory* root) {
	/*string szDiscard = "<<<" + STRING(ADLS_DISCARD) + ">>>";

	// Add non-empty destination directories to the top level
	for(auto id = destDirVector.begin(); id != destDirVector.end(); ++id) {
		if(id->dir->files.size() == 0 && id->dir->directories.size() == 0) {
			delete (id->dir);
		} else if(stricmp(id->dir->getName(), szDiscard) == 0) {
			delete (id->dir);
		} else {
			root->directories.push_back(id->dir);
		}
	}*/

	string szDiscard("<<<" + string("Discard") + ">>>");

	// Add non-empty destination directories to the top level
	//for(auto& i: destDirs) {
	for_each(destDirs, [&root, &szDiscard](DestDir& i) {
		if(i.dir->files.empty() && i.dir->directories.empty()) {
			delete i.dir;
		} else if(Util::stricmp(i.dir->getName(), szDiscard) == 0) {
			delete i.dir;
		} else {
			root->directories.push_back(i.dir);
		}
	});
}

void ADLSearchManager::matchListing(DirectoryListing& aDirList) noexcept {
	running++;
	setUser(aDirList.getHintedUser());

	DestDirList destDirs;
	PrepareDestinationDirectories(destDirs, aDirList.getRoot());
	setBreakOnFirst(BOOLSETTING(ADLS_BREAK_ON_FIRST));

	string path(aDirList.getRoot()->getName());
	matchRecurse(destDirs, aDirList.getRoot(), path, aDirList);

	running--;
	FinalizeDestinationDirectories(destDirs, aDirList.getRoot());
}

void ADLSearchManager::matchRecurse(DestDirList &aDestList, const DirectoryListing::Directory* aDir, string &aPath, DirectoryListing& aDirList) {
	if(aDirList.getAbort())
		throw AbortException();

	for(auto dirIt = aDir->directories.begin(); dirIt != aDir->directories.end(); ++dirIt) {
		string tmpPath = aPath + "\\" + (*dirIt)->getName();
		MatchesDirectory(aDestList, *dirIt, tmpPath);
		matchRecurse(aDestList, *dirIt, tmpPath, aDirList);
	}
	for(auto fileIt = aDir->files.begin(); fileIt != aDir->files.end(); ++fileIt) {
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
