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

	winDir = Text::toLower(Text::fromT(path)) + PATH_SEPARATOR;
#endif

	reloadSkiplist();
}

bool SharePathValidator::matchSkipList(const string& aName) const noexcept {
	RLock l(cs);
	return skipList.match(aName); 
}

string lastMessage;
uint64_t messageTick = 0;
bool SharePathValidator::checkSharedName(const string& aPath, const string& aPathLower, bool isDir, bool aReport /*true*/, int64_t size /*0*/) const noexcept {
	auto report = [&](const string& aMsg) {
		// There may be sequential modification notifications for monitored files so don't spam the same message many times
		if (aReport && (lastMessage != aMsg || messageTick + 3000 < GET_TICK())) {
			LogManager::getInstance()->message(aMsg, LogMessage::SEV_INFO);
			lastMessage = aMsg;
			messageTick = GET_TICK();
		}
	};

	string aNameLower = isDir ? Util::getLastDir(aPathLower) : Util::getFileName(aPathLower);

	if (matchSkipList(isDir ? Util::getLastDir(aPath) : Util::getFileName(aPath))) {
		if (SETTING(REPORT_SKIPLIST))
			report(STRING(SKIPLIST_HIT) + aPath);
		return false;
	}

	if (!isDir) {
		//dcassert(File::getSize(aPath) == size);
		string fileExt = Util::getFileExt(aNameLower);
		if ((strcmp(aNameLower.c_str(), "dcplusplus.xml") == 0) ||
			(strcmp(aNameLower.c_str(), "favorites.xml") == 0) ||
			(strcmp(fileExt.c_str(), ".dctmp") == 0) ||
			(strcmp(fileExt.c_str(), ".antifrag") == 0))
		{
			return false;
		}

		//check for forbidden file patterns
		if (SETTING(REMOVE_FORBIDDEN)) {
			string::size_type nameLen = aNameLower.size();
			if ((strcmp(fileExt.c_str(), ".tdc") == 0) ||
				(strcmp(fileExt.c_str(), ".getright") == 0) ||
				(strcmp(fileExt.c_str(), ".temp") == 0) ||
				(strcmp(fileExt.c_str(), ".tmp") == 0) ||
				(strcmp(fileExt.c_str(), ".jc!") == 0) ||	//FlashGet
				(strcmp(fileExt.c_str(), ".dmf") == 0) ||	//Download Master
				(strcmp(fileExt.c_str(), ".!ut") == 0) ||	//uTorrent
				(strcmp(fileExt.c_str(), ".bc!") == 0) ||	//BitComet
				(strcmp(fileExt.c_str(), ".missing") == 0) ||
				(strcmp(fileExt.c_str(), ".bak") == 0) ||
				(strcmp(fileExt.c_str(), ".bad") == 0) ||
				(nameLen > 9 && aNameLower.rfind("part.met") == nameLen - 8) ||
				(aNameLower.find("__padding_") == 0) ||			//BitComet padding
				(aNameLower.find("__incomplete__") == 0)		//winmx
				) {
				report(STRING(FORBIDDEN_FILE) + aPath);
				return false;
			}
		}

		if (strcmp(aPathLower.c_str(), AirUtil::privKeyFile.c_str()) == 0) {
			return false;
		}

		if (SETTING(NO_ZERO_BYTE) && !(size > 0))
			return false;

		if (SETTING(MAX_FILE_SIZE_SHARED) != 0 && size > Util::convertSize(SETTING(MAX_FILE_SIZE_SHARED), Util::MB)) {
			report(STRING(BIG_FILE_NOT_SHARED) + " " + aPath + " (" + Util::formatBytes(size) + ")");
			return false;
		}
	} else {
#ifdef _WIN32
		// don't share Windows directory
		if (aPathLower.length() >= winDir.length() && strcmp(aPathLower.substr(0, winDir.length()).c_str(), winDir.c_str()) == 0) {
			return false;
		}
#endif
	}

	return true;
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

bool SharePathValidator::validate(FileFindIter& aIter, const string& aPath, const string& aPathLower, bool aReportErrors) const noexcept {
	if (!SETTING(SHARE_HIDDEN) && aIter->isHidden()) {
		return false;
	}

	if (!SETTING(SHARE_FOLLOW_SYMLINKS) && aIter->isLink()) {
		return false;
	}

	if (aIter->isDirectory()) {
		if (!checkSharedName(aPath, aPathLower, true, aReportErrors)) {
			return false;
		}

		auto bundle = QueueManager::getInstance()->findDirectoryBundle(aPath);
		if (bundle && !bundle->isCompleted()) {
			return false;
		}

		if (isExcluded(aPath)) {
			return false;
		}
	} else if (!checkSharedName(aPath, aPathLower, false, aReportErrors, aIter->getSize())) {
		return false;
	}

	return true;
}

void SharePathValidator::validateRootPath(const string& realPath) const throw(ShareException) {
	if (realPath.empty()) {
		throw ShareException(STRING(NO_DIRECTORY_SPECIFIED));
	}

	if (!SETTING(SHARE_HIDDEN) && File::isHidden(realPath)) {
		throw ShareException(STRING(DIRECTORY_IS_HIDDEN));
	}
#ifdef _WIN32
	//need to throw here, so throw the error and dont use airutil
	TCHAR path[MAX_PATH];
	::SHGetFolderPath(NULL, CSIDL_WINDOWS, NULL, SHGFP_TYPE_CURRENT, path);
	string windows = Text::fromT((tstring)path) + PATH_SEPARATOR;
	// don't share Windows directory
	if (Util::strnicmp(realPath, windows, windows.length()) == 0) {
		throw ShareException(STRING_F(CHECK_FORBIDDEN, realPath));
	}
#endif

	if (realPath == Util::getAppFilePath() || realPath == Util::getPath(Util::PATH_USER_CONFIG) || realPath == Util::getPath(Util::PATH_USER_LOCAL)) {
		throw ShareException(STRING(DONT_SHARE_APP_DIRECTORY));
	}
}

void SharePathValidator::reloadSkiplist() {
	WLock l(cs);
	skipList.pattern = SETTING(SKIPLIST_SHARE);
	skipList.setMethod(SETTING(SHARE_SKIPLIST_USE_REGEXP) ? StringMatch::REGEX : StringMatch::WILDCARD);
	skipList.prepare();
}

bool SharePathValidator::validatePathTokens(const string& aBasePath, const StringList& aTokens) const noexcept {
	if (aTokens.empty()) {
		return true;
	}

	auto curPath = aBasePath;
	auto curPathLower = Text::toLower(aBasePath);

	for (const auto& currentName : aTokens) {
		curPath += currentName + PATH_SEPARATOR;
		curPathLower += Text::toLower(currentName) + PATH_SEPARATOR;

		FileFindIter i(curPath);
		if (i != FileFindIter()) {
			if (!validate(i, curPath, curPathLower, false)) {
				return false;
			}
		} else {
			return false;
		}
	}

	return true;
}

}