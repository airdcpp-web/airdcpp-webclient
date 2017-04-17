/*
* Copyright (C) 2001-2017 Jacek Sieka, arnetheduck on gmail point com
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

#include "stdinc.h"
#include "SharePathValidator.h"

#include "AirUtil.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "ShareManager.h"
#include "SimpleXML.h"

#ifdef _WIN32
# include <ShlObj.h>
#endif

namespace dcpp {

SharePathValidator::SharePathValidator() {
#ifdef _WIN32
	// don't share Windows directory
	TCHAR path[MAX_PATH];
	::SHGetFolderPath(NULL, CSIDL_WINDOWS, NULL, SHGFP_TYPE_CURRENT, path);

	winDir = Text::fromT(path) + PATH_SEPARATOR;
#endif

	reloadSkiplist();
}

bool SharePathValidator::matchSkipList(const string& aName) const noexcept {
	RLock l(cs);
	return skipList.match(aName); 
}

StringSet forbiddenExtension = {
	".dctmp",
	".tmp",
	".temp",
	".!ut", //uTorrent
	".bc!", //BitComet
	".missing",
	".bak",
	".bad",
};

void SharePathValidator::checkSharedName(const string& aPath, bool aIsDir, int64_t aFileSize /*0*/) const {
	const auto fileName = aIsDir ? Util::getLastDir(aPath) : Util::getFileName(aPath);

	if (matchSkipList(aIsDir ? Util::getLastDir(aPath) : fileName)) {
		throw ShareException(STRING(SKIPLIST_SHARE_MATCH));
	}

	if (!aIsDir) {
		if (strcmp(fileName.c_str(), "DCPlusPlus.xml") == 0 ||
			strcmp(fileName.c_str(), "Favorites.xml") == 0 ||
			strcmp(aPath.c_str(), SETTING(TLS_PRIVATE_KEY_FILE).c_str()) == 0
		) {
			throw ShareException(STRING(DONT_SHARE_APP_DIRECTORY));
		}

		// Check for forbidden file extensions
		if (SETTING(REMOVE_FORBIDDEN) && forbiddenExtension.find(Text::toLower(Util::getFileExt(fileName))) != forbiddenExtension.end()) {
			throw ShareException(STRING(FORBIDDEN_FILE_EXT));
		}

		if (SETTING(NO_ZERO_BYTE) && aFileSize == 0) {
			throw ShareException(STRING(ZERO_BYTE_SHARE));
		}

		if (SETTING(MAX_FILE_SIZE_SHARED) != 0 && aFileSize > Util::convertSize(SETTING(MAX_FILE_SIZE_SHARED), Util::MB)) {
			throw ShareException(STRING(BIG_FILE_NOT_SHARED));
		}
	} else {
#ifdef _WIN32
		// don't share Windows directory
		if (strncmp(aPath.c_str(), winDir.c_str(), winDir.length()) == 0) {
			throw ShareException(STRING(DONT_SHARE_APP_DIRECTORY));
		}
#endif
	}
}



StringSet SharePathValidator::getExcludedPaths() const noexcept {
	RLock l(cs);
	return excludedPaths;
}

void SharePathValidator::setExcludedPaths(const StringSet& aPaths) noexcept {
	WLock l(cs);
	excludedPaths = aPaths;
}

void SharePathValidator::addExcludedPath(const string& aPath) {

	{
		// Make sure this is a sub folder of a shared folder

		StringList rootPaths;
		ShareManager::getInstance()->getRootPaths(rootPaths);

		if (boost::find_if(rootPaths, [&aPath](const string& aRootPath) { return AirUtil::isSubLocal(aPath, aRootPath); }) == rootPaths.end()) {
			throw ShareException(STRING(PATH_NOT_SHARED));
		}
	}

	StringList toRemove;

	{
		WLock l(cs);

		// Subfolder of an already excluded folder?
		if (boost::find_if(excludedPaths, [&aPath](const string& aExcludedPath) { return AirUtil::isParentOrExactLocal(aExcludedPath, aPath); }) != excludedPaths.end()) {
			throw ShareException(STRING(PATH_ALREADY_EXCLUDED));
		}

		// No use for excluded subfolders of this path
		copy_if(excludedPaths.begin(), excludedPaths.end(), back_inserter(toRemove), [&aPath](const string& aExcluded) {
			return AirUtil::isSubLocal(aExcluded, aPath);
		});

		excludedPaths.insert(aPath);
	}

	for (const auto& p : toRemove) {
		removeExcludedPath(p);
	}
}

bool SharePathValidator::removeExcludedPath(const string& aPath) noexcept {
	{
		WLock l(cs);
		if (excludedPaths.erase(aPath) == 0) {
			return false;
		}
	}

	return true;
}

bool SharePathValidator::isExcluded(const string& aPath) const noexcept {
	RLock l(cs);
	return excludedPaths.find(aPath) != excludedPaths.end();
}

void SharePathValidator::loadExcludes(SimpleXML& aXml) noexcept {
	if (aXml.findChild("NoShare")) {
		aXml.stepIn();
		while (aXml.findChild("Directory")) {
			auto path = aXml.getChildData();

			excludedPaths.insert(path);
		}
		aXml.stepOut();
	}
}

void SharePathValidator::saveExcludes(SimpleXML& aXml) const noexcept {
	aXml.addTag("NoShare");
	aXml.stepIn();

	{
		RLock l(cs);
		for (const auto& path : excludedPaths) {
			aXml.addTag("Directory", path);
		}
	}

	aXml.stepOut();
}

void SharePathValidator::validate(FileFindIter& aIter, const string& aPath, bool aSkipQueueCheck) const {
	if (!SETTING(SHARE_HIDDEN) && aIter->isHidden()) {
		throw FileException("File is hidden");
	}

	if (!SETTING(SHARE_FOLLOW_SYMLINKS) && aIter->isLink()) {
		throw FileException("File is a symbolic link");
	}

	if (aIter->isDirectory()) {
		checkSharedName(aPath, true);

		if (!aSkipQueueCheck) {
			auto bundle = QueueManager::getInstance()->findDirectoryBundle(aPath);
			if (bundle && !bundle->isCompleted()) {
				throw QueueException("Directory is inside an unfinished bundle");
			}
		}

		if (isExcluded(aPath)) {
			throw ShareException("Directory is excluded from share");
		}

		auto error = directoryValidationHook.runHooksError(aPath);
		if (error) {
			throw ShareException(error->formatError(error));
		}
	} else {
		auto size = aIter->getSize();
		checkSharedName(aPath, false, size);

		auto error = fileValidationHook.runHooksError(aPath, size);
		if (error) {
			throw ShareException(error->formatError(error));
		}
	}
}

void SharePathValidator::validateRootPath(const string& aRealPath) const throw(ShareException) {
	if (aRealPath.empty()) {
		throw ShareException(STRING(NO_DIRECTORY_SPECIFIED));
	}

	if (!SETTING(SHARE_HIDDEN) && File::isHidden(aRealPath)) {
		throw ShareException(STRING(DIRECTORY_IS_HIDDEN));
	}
#ifdef _WIN32
	// don't share Windows directory
	if (strncmp(aRealPath.c_str(), winDir.c_str(), winDir.length()) == 0) {
		throw ShareException(STRING_F(FORBIDDEN_FILE_EXT, aRealPath));
	}
#endif

	if (aRealPath == Util::getAppFilePath() || aRealPath == Util::getPath(Util::PATH_USER_CONFIG) || aRealPath == Util::getPath(Util::PATH_USER_LOCAL)) {
		throw ShareException(STRING(DONT_SHARE_APP_DIRECTORY));
	}
}

void SharePathValidator::reloadSkiplist() {
	WLock l(cs);
	skipList.pattern = SETTING(SKIPLIST_SHARE);
	skipList.setMethod(SETTING(SHARE_SKIPLIST_USE_REGEXP) ? StringMatch::REGEX : StringMatch::WILDCARD);
	skipList.prepare();
}

void SharePathValidator::validatePathTokens(const string& aBasePath, const StringList& aTokens, bool aSkipQueueCheck) const {
	if (aTokens.empty()) {
		return;
	}

	auto curPath = aBasePath;

	for (const auto& currentName : aTokens) {
		curPath += currentName + PATH_SEPARATOR;

		FileFindIter i(curPath);
		if (i != FileFindIter()) {
			validate(i, curPath, aSkipQueueCheck);
		} else {
			throw FileException(STRING(FILE_NOT_FOUND));
		}
	}
}

}