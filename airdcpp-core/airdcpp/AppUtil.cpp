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

#include "stdinc.h"
#include "AppUtil.h"

#ifdef _WIN32
#include "shlobj.h"
#endif

#include "Exception.h"
#include "FastAlloc.h"
#include "File.h"
#include "LogManager.h"
#include "PathUtil.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "SimpleXML.h"
#include "StartupParams.h"
#include "Util.h"
#include "ValueGenerator.h"

#include <random>

namespace dcpp {

#ifndef NO_FAST_ALLOC
FastCriticalSection FastAllocBase::cs;
#endif

string AppUtil::paths[AppUtil::PATH_LAST];

#ifdef _WIN32
	bool AppUtil::localMode = true;
#else
	bool AppUtil::localMode = false;
	string AppUtil::appPath;
#endif

bool AppUtil::wasUncleanShutdown = false;

extern "C" void bz_internal_error(int errcode) { 
	dcdebug("bzip2 internal error: %d\n", errcode); 
}

#if (_MSC_VER >= 1400)
void WINAPI invalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
	//do nothing, this exist because vs2k5 crt needs it not to crash on errors.
}
#endif

#ifdef _WIN32

static string getDownloadsPath(const string& def) noexcept {
	PWSTR path = NULL;
	if (SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_CREATE, NULL, &path) == S_OK) {
		return PathUtil::validatePath(Text::fromT(path), true);
	}

	return def + "Downloads\\";
}

#endif

string AppUtil::getOpenPath() noexcept {
	return paths[PATH_TEMP] + "Opened Items" + PATH_SEPARATOR_STR;
}

void StartupParams::addParam(const string& aParam) noexcept {
	if (aParam.empty())
		return;

	if (!hasParam(aParam))
		params.push_back(aParam);
}

bool StartupParams::removeParam(const string& aParam) noexcept {
	auto param = find(params.begin(), params.end(), aParam);
	if (param != params.end()) {
		params.erase(param);
		return true;
	}

	return false;
}

bool StartupParams::hasParam(const string& aParam, int aPos) const noexcept {
	auto param = find(params.begin(), params.end(), aParam);
	if (param != params.end()) {
		return aPos == -1 || distance(params.begin(), param) == aPos;
	}

	return false;
}

string StartupParams::formatParams(bool aIsFirst) const noexcept {
	if (params.empty()) {
		return Util::emptyString;
	}

	return string(aIsFirst ? Util::emptyString : " ") + Util::toString(" ", StringList(params.begin(), params.end()));
}

optional<string> StartupParams::getValue(const string& aKey) const noexcept {
	for (const auto& p: params) {
		auto pos = p.find("=");
		if (pos != string::npos && pos != p.length() && Util::strnicmp(p, aKey, pos) == 0)
			return p.substr(pos + 1, p.length() - pos - 1);
	}

	return optional<string>();
}

#ifdef _WIN32

string AppUtil::getAppPath() noexcept {
	TCHAR buf[MAX_PATH+1];
	DWORD x = GetModuleFileName(NULL, buf, MAX_PATH);
	return Text::fromT(tstring(buf, x));
}

#else

void AppUtil::setApp(const string& app) noexcept {
	appPath = app;
}

string AppUtil::getAppPath() noexcept {
	return appPath;
}

#endif

string AppUtil::getAppFilePath() noexcept {
	return PathUtil::getFilePath(getAppPath());
}

string AppUtil::getAppFileName() noexcept {
	return PathUtil::getFileName(getAppPath());
}

void AppUtil::initialize(const string& aConfigPath) {
	const auto exeDirectoryPath = getAppFilePath();

	auto initConfig = [&]() {
		// Prefer boot config from the same directory
		if (loadBootConfig(exeDirectoryPath)) {
			paths[PATH_GLOBAL_CONFIG] = exeDirectoryPath;
		} else if (paths[PATH_GLOBAL_CONFIG] != exeDirectoryPath) {
			// Linux may also use a separate global config directory
			loadBootConfig(paths[PATH_GLOBAL_CONFIG]);
		}

		// USER CONFIG
		{
			if (!aConfigPath.empty()) {
				paths[PATH_USER_CONFIG] = aConfigPath;
			}

			if (!paths[PATH_USER_CONFIG].empty() && !File::isAbsolutePath(paths[PATH_USER_CONFIG])) {
				paths[PATH_USER_CONFIG] = paths[PATH_GLOBAL_CONFIG] + paths[PATH_USER_CONFIG];
			}

			paths[PATH_USER_CONFIG] = PathUtil::validatePath(paths[PATH_USER_CONFIG], true);
		}

		if (localMode) {
			if (paths[PATH_USER_CONFIG].empty()) {
				paths[PATH_USER_CONFIG] = exeDirectoryPath + "Settings" + PATH_SEPARATOR_STR;
			}

			paths[PATH_DOWNLOADS] = paths[PATH_USER_CONFIG] + "Downloads" + PATH_SEPARATOR_STR;
			paths[PATH_USER_LOCAL] = paths[PATH_USER_CONFIG];

			if (paths[PATH_RESOURCES].empty()) {
				paths[PATH_RESOURCES] = exeDirectoryPath;
			}
		}
	};

#ifdef _WIN32

	_set_invalid_parameter_handler(reinterpret_cast<_invalid_parameter_handler>(invalidParameterHandler));

	paths[PATH_GLOBAL_CONFIG] = exeDirectoryPath;
	initConfig();

	{
		// Instance-specific temp path
		if (paths[PATH_TEMP].empty()) {
			TCHAR buf[MAX_PATH + 1];
			DWORD x = GetTempPath(MAX_PATH, buf);
			paths[PATH_TEMP] = Text::fromT(tstring(buf, x)) + INST_NAME + PATH_SEPARATOR_STR;
		}

		File::ensureDirectory(paths[PATH_TEMP]);
	}

	if (!localMode) {
		TCHAR buf[MAX_PATH + 1] = { 0 };
		if (::SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, buf) == S_OK) {
			paths[PATH_USER_CONFIG] = Text::fromT(buf) + "\\AirDC++\\";
		}

		paths[PATH_DOWNLOADS] = getDownloadsPath(paths[PATH_USER_CONFIG]);
		paths[PATH_USER_LOCAL] = ::SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, buf) == S_OK ? Text::fromT(buf) + "\\AirDC++\\" : paths[PATH_USER_CONFIG];
		paths[PATH_RESOURCES] = exeDirectoryPath;
	}
#else
	// Usually /etc/airdcpp/
	paths[PATH_GLOBAL_CONFIG] = GLOBAL_CONFIG_DIRECTORY;

	initConfig();

	if (!localMode) {
		const char* home_ = getenv("HOME");
		string home = home_ ? home_ : "/tmp/";

		if (paths[PATH_USER_CONFIG].empty()) {
			paths[PATH_USER_CONFIG] = home + "/.airdc++/";
		}

		paths[PATH_DOWNLOADS] = home + "/Downloads/";
		paths[PATH_USER_LOCAL] = paths[PATH_USER_CONFIG];
		paths[PATH_RESOURCES] = RESOURCE_DIRECTORY;
	}

	// Temp path
	if (paths[PATH_TEMP].empty()) {
		paths[PATH_TEMP] = "/tmp/";
	} else {
		File::ensureDirectory(paths[PATH_TEMP]);
	}
#endif

	paths[PATH_LOCALE] = (localMode ? exeDirectoryPath : paths[PATH_USER_LOCAL]) + "Language" PATH_SEPARATOR_STR;
	paths[PATH_FILE_LISTS] = paths[PATH_USER_CONFIG] + "FileLists" PATH_SEPARATOR_STR;
	paths[PATH_BUNDLES] = paths[PATH_USER_CONFIG] + "Bundles" PATH_SEPARATOR_STR;
	paths[PATH_SHARECACHE] = paths[PATH_USER_LOCAL] + "ShareCache" PATH_SEPARATOR_STR;

	File::ensureDirectory(paths[PATH_USER_CONFIG]);
	File::ensureDirectory(paths[PATH_USER_LOCAL]);
	File::ensureDirectory(paths[PATH_LOCALE]);
}

void AppUtil::migrate(const string& file) noexcept {
	if (localMode) {
		return;
	}

	if (File::getSize(file) != -1) {
		return;
	}

	auto fname = PathUtil::getFileName(file);
	auto oldPath = AppUtil::getAppFilePath() + "Settings" + PATH_SEPARATOR + fname;
	if (File::getSize(oldPath) == -1) {
		return;
	}

	try {
		File::copyFile(oldPath.c_str(), oldPath + ".bak");
		File::renameFile(oldPath, file);
	} catch(const FileException& /*e*/) {
		//LogManager::getInstance()->message("Settings migration for failed: " + e.getError());
	}
}

void AppUtil::migrate(const string& aNewDir, const string& aPattern) noexcept {
	if (localMode)
		return;

	auto oldDir = getAppFilePath() + "Settings" + PATH_SEPARATOR + PathUtil::getLastDir(aNewDir) + PATH_SEPARATOR;
	if (!PathUtil::fileExists(oldDir)) {
		return;
	}

	// Don't migrate if there are files in the new directory already
	if (!File::findFiles(aNewDir, aPattern).empty()) {
		return;
	}

	// Move the content
	try {
		File::moveDirectory(oldDir, aNewDir, aPattern);
	} catch (const FileException&) {

	}
}

bool AppUtil::loadBootConfig(const string& aDirectoryPath) noexcept {
	string xmlFilePath;
	if (PathUtil::fileExists(aDirectoryPath + "dcppboot.xml.user")) {
		xmlFilePath = aDirectoryPath + "dcppboot.xml.user";
	} else {
		xmlFilePath = aDirectoryPath + "dcppboot.xml";
	}

	try {
		SimpleXML boot;
		boot.fromXML(File(xmlFilePath, File::READ, File::OPEN).read());
		boot.stepIn();

		if(boot.findChild("LocalMode")) {
			localMode = boot.getChildData() != "0";
		}
		boot.resetCurrentChild();

		auto validatePath = [](Paths aPathType) {
			if (!paths[aPathType].empty()) {
				paths[aPathType] = PathUtil::ensureTrailingSlash(paths[aPathType]);
				if (!File::isAbsolutePath(paths[aPathType])) {
					paths[aPathType] = File::makeAbsolutePath(paths[aPathType]);
				}
			}
		};

		auto getSystemPathParams = []() {
			ParamMap params;
#ifdef _WIN32
			// @todo load environment variables instead? would make it more useful on *nix
			TCHAR tmpPath[MAX_PATH];

			params["APPDATA"] = Text::fromT((::SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, tmpPath), tmpPath));
			params["PERSONAL"] = Text::fromT((::SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, tmpPath), tmpPath));
#else
			const char* home_ = getenv("HOME");
			params["HOME"] = home_ ? home_ : "/tmp/";
#endif
			return params;
		};
	
		if (boot.findChild("ConfigPath")) {
			paths[PATH_USER_CONFIG] = Util::formatParams(boot.getChildData(), getSystemPathParams());
		}
		boot.resetCurrentChild();

		if (boot.findChild("TempPath")) {
			paths[PATH_TEMP] = Util::formatParams(boot.getChildData(), getSystemPathParams());
			validatePath(PATH_TEMP);
		}

		boot.resetCurrentChild();


		return true;
	} catch(const Exception& ) {
		// Unable to load boot settings...
	}

	return false;
}

} // namespace dcpp