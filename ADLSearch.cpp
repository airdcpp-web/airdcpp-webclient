/*
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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

#define CONFIG_NAME "ADLSearch.xml"
#define CONFIG_DIR Util::PATH_USER_CONFIG

namespace dcpp {
	
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
		if(Util::stricmp(s.c_str(), "Filename") == 0) {
			return OnlyFile;
		} else if(Util::stricmp(s.c_str(), "Directory") == 0) {
			return OnlyDirectory;
		} else if(Util::stricmp(s.c_str(), "Full Path") == 0) {
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
	if(Util::stricmp(s.c_str(), "B") == 0) {
		return SizeBytes;
	} else if(Util::stricmp(s.c_str(), "KiB") == 0) {
		return SizeKiloBytes;
	} else if(Util::stricmp(s.c_str(), "MiB") == 0) {
		return SizeMegaBytes;
	} else if(Util::stricmp(s.c_str(), "GiB") == 0) {
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
ADLSearchManager::ADLSearchManager() : running(0), user(HintedUser()), dirty(false) {
	load();
}

ADLSearchManager::~ADLSearchManager() {
	save(true);
}

ADLSearch::SourceType ADLSearchManager::StringToSourceType(const string& s) {
	if(Util::stricmp(s.c_str(), "Filename") == 0) {
		return ADLSearch::OnlyFile;
	} else if(Util::stricmp(s.c_str(), "Directory") == 0) {
		return ADLSearch::OnlyDirectory;
	} else if(Util::stricmp(s.c_str(), "Full Path") == 0) {
		return ADLSearch::FullPath;
	} else {
		return ADLSearch::OnlyFile;
	}
}

void ADLSearchManager::load()
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
		SettingsManager::loadSettingFile(xml, CONFIG_DIR, CONFIG_NAME);

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

					xml.resetCurrentChild();
					/* For compatibility, remove in some point */
					if(xml.findChild("IsRegExp")) {
						if (Util::toInt(xml.getChildData()) > 0) {
							search.setRegEx(true);
							xml.resetCurrentChild();

							if(xml.findChild("IsCaseSensitive")) {
								if (Util::toInt(xml.getChildData()) == 0) {
									search.match.pattern.insert(0, "(?i:");
									search.match.pattern.insert(search.match.pattern.size(), ")");
								}
							}
						}
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
	} catch(const Exception& e) { 
		LogManager::getInstance()->message(STRING_F(LOAD_FAILED_X, CONFIG_NAME % e.getError()), LogManager::LOG_ERROR);
	}

	for(auto& s: collection) {
		s.prepare();
	}
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
	dirty = true;
	return true;
}

bool ADLSearchManager::removeCollection(int index) {
	if (running > 0) {
		LogManager::getInstance()->message(CSTRING(ADLSEARCH_IN_PROGRESS), LogManager::LOG_ERROR);
		return false;
	}

	collection.erase(collection.begin() + index);
	dirty = true;
	return true;
}

bool ADLSearchManager::changeState(int index, bool aIsActive) {
	if (running > 0) {
		LogManager::getInstance()->message(CSTRING(ADLSEARCH_IN_PROGRESS), LogManager::LOG_ERROR);
		return false;
	}

	collection[index].isActive = aIsActive;
	dirty = true;
	return true;
}

bool ADLSearchManager::updateCollection(ADLSearch& search, int index) {
	if (running > 0) {
		LogManager::getInstance()->message(CSTRING(ADLSEARCH_IN_PROGRESS), LogManager::LOG_ERROR);
		return false;
	}

	collection[index] = search;
	search.prepare();
	dirty = true;
	return true;
}

void ADLSearchManager::save(bool force /*false*/) {
	if (!dirty && !force)
		return;

	dirty = false;

	SimpleXML xml;

	xml.addTag("ADLSearch");
	xml.stepIn();

	// Predicted several groups of searches to be differentiated
	// in multiple categories. Not implemented yet.
	xml.addTag("SearchGroup");
	xml.stepIn();

	// Save all	searches
	for(auto& search: collection) {
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
	}

	xml.stepOut();

	xml.stepOut();

	SettingsManager::saveSettingFile(xml, CONFIG_DIR, CONFIG_NAME);
}

void ADLSearchManager::MatchesFile(DestDirList& destDirVector, const DirectoryListing::File *currentFile, string& fullPath) {
	// Add to any substructure being stored
	for(auto& id: destDirVector) {
		if(id.subdir != NULL) {
			DirectoryListing::File *copyFile = new DirectoryListing::File(*currentFile, true);
			dcassert(id.subdir->getAdls());

			id.subdir->files.push_back(copyFile);
		}
		id.fileAdded = false;	// Prepare for next stage
	}

	// Prepare to match searches
	if(currentFile->getName().size() < 1) {
		return;
	}

	string filePath = fullPath + "\\" + currentFile->getName();
	// Match searches
	for(auto& is: collection) {
		if(destDirVector[is.ddIndex].fileAdded) {
			continue;
		}
		if(is.matchesFile(currentFile->getName(), filePath, currentFile->getSize())) {
			DirectoryListing::File *copyFile = new DirectoryListing::File(*currentFile, true);
			destDirVector[is.ddIndex].dir->files.push_back(copyFile);
			destDirVector[is.ddIndex].fileAdded = true;

			if(is.isAutoQueue){
				try {
					QueueManager::getInstance()->createFileBundle(SETTING(DOWNLOAD_DIRECTORY) + currentFile->getName(),
						currentFile->getSize(), currentFile->getTTH(), getUser(), currentFile->getRemoteDate());
				} catch(const Exception&) { }
			}

			if(breakOnFirst) {
				// Found a match, search no more
				break;
			}
		}
	}
}

void ADLSearchManager::MatchesDirectory(DestDirList& destDirVector, const DirectoryListing::Directory::Ptr& currentDir, string& fullPath) {
	// Add to any substructure being stored
	for(auto& id: destDirVector) {
		if(id.subdir) {
			DirectoryListing::Directory* newDir =
				new DirectoryListing::AdlDirectory(fullPath.substr(1) + "\\", id.subdir, currentDir->getName());
			id.subdir->directories.push_back(newDir);
			id.subdir = newDir;
		}
	}

	// Prepare to match searches
	if(currentDir->getName().size() < 1) {
		return;
	}

	for(auto& is: collection) {
		if(destDirVector[is.ddIndex].subdir) {
			continue;
		}
		if(is.matchesDirectory(currentDir->getName())) {
			destDirVector[is.ddIndex].subdir =
				new DirectoryListing::AdlDirectory(fullPath.substr(1) + "\\", destDirVector[is.ddIndex].dir, currentDir->getName());
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

void ADLSearchManager::PrepareDestinationDirectories(DestDirList& destDirs, DirectoryListing::Directory::Ptr& root) {
	// Load default destination directory (index = 0)
	destDirs.clear();
	DestDir dir = { "ADLSearch", new DirectoryListing::Directory(root.get(), "<<<ADLSearch>>>", DirectoryListing::Directory::TYPE_ADLS, GET_TIME()) };
	destDirs.push_back(std::move(dir));

	// Scan all loaded searches
	for(auto& is: collection) {
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
			DestDir dir = { is.destDir, new DirectoryListing::Directory(root.get(), "<<<" + is.destDir + ">>>", DirectoryListing::Directory::TYPE_ADLS, GET_TIME()) };
			destDirs.push_back(std::move(dir));
			is.ddIndex = ddIndex;
		}
	}
}

void ADLSearchManager::FinalizeDestinationDirectories(DestDirList& destDirs, DirectoryListing::Directory::Ptr& root) {
	/*string szDiscard = "<<<" + STRING(ADLS_DISCARD) + ">>>";

	// Add non-empty destination directories to the top level
	for(auto id = destDirVector.begin(); id != destDirVector.end(); ++id) {
		if(id->dir->files.size() == 0 && id->dir->directories.size() == 0) {
			delete (id->dir);
		} else if(Util::stricmp(id->dir->getName(), szDiscard) == 0) {
			delete (id->dir);
		} else {
			root->directories.push_back(id->dir);
		}
	}*/

	string szDiscard("<<<" + string("Discard") + ">>>");

	// Add non-empty destination directories to the top level
	for(auto& i: destDirs) {
		if(i.dir->files.empty() && i.dir->directories.empty()) {
			delete i.dir;
		} else if(Util::stricmp(i.dir->getName(), szDiscard) == 0) {
			delete i.dir;
		} else {
			root->directories.push_back(i.dir);
		}
	}
}

void ADLSearchManager::matchListing(DirectoryListing& aDirList) noexcept {
	running++;
	setUser(aDirList.getHintedUser());
	auto root = aDirList.getRoot();

	DestDirList destDirs;
	PrepareDestinationDirectories(destDirs, root);
	setBreakOnFirst(SETTING(ADLS_BREAK_ON_FIRST));

	string path(aDirList.getRoot()->getName());
	matchRecurse(destDirs, aDirList.getRoot(), path, aDirList);

	running--;
	FinalizeDestinationDirectories(destDirs, root);
}

void ADLSearchManager::matchRecurse(DestDirList &aDestList, const DirectoryListing::Directory::Ptr& aDir, string &aPath, DirectoryListing& aDirList) {
	if(aDirList.getClosing())
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

} // namespace dcpp