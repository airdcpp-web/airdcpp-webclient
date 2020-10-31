/*
 * Copyright (C) 2001-2021 Jacek Sieka, arnetheduck on gmail point com
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

#include "File.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "ScopedFunctor.h"
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
name("ADLSearch"),
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

void ADLSearch::setDestDir(const string& aDestDir) noexcept {
	if (!aDestDir.empty()) {
		name = Util::cleanPathSeparators(aDestDir);
	}
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

void ADLSearchManager::load() noexcept {
	if (running > 0) {
		log(CSTRING(ADLSEARCH_IN_PROGRESS), LogMessage::SEV_ERROR);
		return;
	}

	// Clear current
	collection.clear();

	SettingsManager::loadSettingFile(CONFIG_DIR, CONFIG_NAME, [this](SimpleXML& xml) {
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

					if (xml.findChild("DestDirectory")) {
						search.setDestDir(xml.getChildData());
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
	});

	for(auto& s: collection) {
		s.prepare();
	}
}

bool ADLSearchManager::addCollection(ADLSearch& search, int index) noexcept {
	if (running > 0) {
		log(CSTRING(ADLSEARCH_IN_PROGRESS), LogMessage::SEV_ERROR);
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

void ADLSearchManager::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(ADL_SEARCH));
}

bool ADLSearchManager::removeCollection(int index) noexcept {
	if (running > 0) {
		log(CSTRING(ADLSEARCH_IN_PROGRESS), LogMessage::SEV_ERROR);
		return false;
	}

	collection.erase(collection.begin() + index);
	dirty = true;
	return true;
}

bool ADLSearchManager::changeState(int index, bool aIsActive) noexcept {
	if (running > 0) {
		log(CSTRING(ADLSEARCH_IN_PROGRESS), LogMessage::SEV_ERROR);
		return false;
	}

	collection[index].isActive = aIsActive;
	dirty = true;
	return true;
}

bool ADLSearchManager::updateCollection(ADLSearch& search, int index) noexcept {
	if (running > 0) {
		log(CSTRING(ADLSEARCH_IN_PROGRESS), LogMessage::SEV_ERROR);
		return false;
	}

	collection[index] = search;
	search.prepare();
	dirty = true;
	return true;
}

void ADLSearchManager::save(bool force /*false*/) noexcept {
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
		xml.addTag("DestDirectory", search.getDestDir());
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

void ADLSearchManager::MatchesFile(DestDirList& destDirVector, const DirectoryListing::File::Ptr& currentFile, const string& aAdcPath) noexcept {
	// Add to any substructure being stored
	for(auto& id: destDirVector) {
		if(id.subdir != NULL) {
			auto copyFile = make_shared<DirectoryListing::File>(*currentFile, true);
			dcassert(id.subdir->getAdls());

			id.subdir->files.push_back(copyFile);
		}
		id.fileAdded = false;	// Prepare for next stage
	}

	// Prepare to match searches
	if(currentFile->getName().size() < 1) {
		return;
	}

	dcassert(Util::isAdcDirectoryPath(aAdcPath));

	// Use NMDC path for matching due to compatibility reasons
	const auto nmdcPath = Util::toNmdcFile(aAdcPath + currentFile->getName());

	// Match searches
	for(auto& is: collection) {
		if(destDirVector[is.ddIndex].fileAdded) {
			continue;
		}
		if(is.matchesFile(currentFile->getName(), nmdcPath, currentFile->getSize())) {
			auto copyFile = make_shared<DirectoryListing::File>(*currentFile, true);
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

void ADLSearchManager::MatchesDirectory(DestDirList& destDirVector, const DirectoryListing::Directory::Ptr& currentDir, const string& aAdcPath) noexcept {
	dcassert(Util::isAdcDirectoryPath(aAdcPath));

	// Add to any substructure being stored
	for (auto& id: destDirVector) {
		if (id.subdir) {
			auto newDir = DirectoryListing::AdlDirectory::create(aAdcPath, id.subdir, currentDir->getName());
			id.subdir = newDir.get();
		}
	}

	// Prepare to match searches
	if(currentDir->getName().size() < 1) {
		return;
	}

	for (auto& is: collection) {
		if(destDirVector[is.ddIndex].subdir) {
			continue;
		}

		if(is.matchesDirectory(currentDir->getName())) {
			auto newDir = DirectoryListing::AdlDirectory::create(aAdcPath, destDirVector[is.ddIndex].dir.get(), currentDir->getName());;
			destDirVector[is.ddIndex].subdir = newDir.get();
			if(breakOnFirst) {
				// Found a match, search no more
				break;
			}
		}
	}
}

void ADLSearchManager::stepUpDirectory(DestDirList& destDirVector) noexcept {
	for(auto id = destDirVector.begin(); id != destDirVector.end(); ++id) {
		if(id->subdir) {
			id->subdir = id->subdir->getParent();
			if(id->subdir == id->dir.get()) {
				id->subdir = nullptr;
			}
		}
	}
}

void ADLSearchManager::PrepareDestinationDirectories(DestDirList& destDirs, DirectoryListing::Directory::Ptr& root) noexcept {
	// Load default destination directory (index = 0)
	destDirs.clear();
	DestDir dir = { "ADLSearch", DirectoryListing::Directory::create(root.get(), "<<<ADLSearch>>>", DirectoryListing::Directory::TYPE_ADLS, GET_TIME()) };
	destDirs.push_back(std::move(dir));

	// Scan all loaded searches
	for(auto& is: collection) {
		// Check empty destination directory
		if(is.getDestDir().size() == 0) {
			// Set to default
			is.ddIndex = 0;
			continue;
		}

		// Check if exists
		bool isNew = true;
		long ddIndex = 0;
		for(auto id = destDirs.cbegin(); id != destDirs.cend(); ++id, ++ddIndex) {
			if(Util::stricmp(is.getDestDir().c_str(), id->name.c_str()) == 0) {
				// Already exists, reuse index
				is.ddIndex = ddIndex;
				isNew = false;
				break;
			}
		}

		if(isNew) {
			// Add new destination directory
			DestDir newDir = { is.getDestDir(),
				DirectoryListing::Directory::create(root.get(), "<<<" + is.getDestDir() + ">>>",
					DirectoryListing::Directory::TYPE_ADLS, GET_TIME()) 
			};

			destDirs.push_back(std::move(newDir));
			is.ddIndex = ddIndex;
		}
	}
}

void ADLSearchManager::FinalizeDestinationDirectories(DestDirList& destDirs, DirectoryListing::Directory::Ptr& root) noexcept {
	string szDiscard("<<<" + string("Discard") + ">>>");

	// Add non-empty destination directories to the top level
	for(auto& i: destDirs) {
		if(i.dir->files.empty() && i.dir->directories.empty()) {
			continue;;
		} 
		
		if(Util::stricmp(i.dir->getName(), szDiscard) == 0) {
			continue;
		}

		root->directories.emplace(&i.dir->getName(), i.dir);
	}
}

void ADLSearchManager::matchListing(DirectoryListing& aDirList) {
	running++;
	ScopedFunctor([&] { running--; });

	setUser(aDirList.getHintedUser());
	auto root = aDirList.getRoot();

	DestDirList destDirs;
	PrepareDestinationDirectories(destDirs, root);
	setBreakOnFirst(SETTING(ADLS_BREAK_ON_FIRST));

	string path(aDirList.getRoot()->getName());
	matchRecurse(destDirs, aDirList.getRoot(), path, aDirList);

	FinalizeDestinationDirectories(destDirs, root);
}

void ADLSearchManager::matchRecurse(DestDirList &aDestList, const DirectoryListing::Directory::Ptr& aDir, const string& aAdcPath, DirectoryListing& aDirList) {
	if (aDirList.getClosing()) {
		throw AbortException();
	}

	for (const auto& dir: aDir->directories | map_values) {
		auto subAdcPath = aAdcPath + dir->getName() + ADC_SEPARATOR_STR;
		MatchesDirectory(aDestList, dir, subAdcPath);
		matchRecurse(aDestList, dir, subAdcPath, aDirList);
	}

	for (const auto& file: aDir->files) {
		MatchesFile(aDestList, file, aAdcPath);
	}

	stepUpDirectory(aDestList);
}

} // namespace dcpp