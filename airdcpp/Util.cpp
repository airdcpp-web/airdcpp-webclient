/* 
 * Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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
#include "Util.h"

#include <boost/algorithm/string/trim.hpp>

#ifdef _WIN32

#include "w.h"
#include "shlobj.h"
#include <shellapi.h>
#include <VersionHelpers.h>

#endif

#include "FastAlloc.h"
#include "File.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "SimpleXML.h"
#include "ScopedFunctor.h"
#include "User.h"
#include "version.h"

#include <random>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/utsname.h>
#include <ctype.h>

#endif
#include <locale.h>

namespace dcpp {

#ifndef NO_FAST_ALLOC
FastCriticalSection FastAllocBase::cs;
#endif

string Util::emptyString;
wstring Util::emptyStringW;
tstring Util::emptyStringT;

string Util::paths[Util::PATH_LAST];
StringList Util::startupParams;

#ifndef _WIN32
string Util::appPath;
#endif

bool Util::localMode = true;
bool Util::wasUncleanShutdown = false;

static void sgenrand(unsigned long seed) noexcept;

extern "C" void bz_internal_error(int errcode) { 
	dcdebug("bzip2 internal error: %d\n", errcode); 
}

#if (_MSC_VER >= 1400)
void WINAPI invalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
	//do nothing, this exist because vs2k5 crt needs it not to crash on errors.
}
#endif

#ifdef _WIN32

typedef HRESULT (WINAPI* _SHGetKnownFolderPath)(GUID& rfid, DWORD dwFlags, HANDLE hToken, PWSTR *ppszPath);

static string getDownloadsPath(const string& def) noexcept {
	// Try Vista downloads path
	static _SHGetKnownFolderPath getKnownFolderPath = 0;
	static HINSTANCE shell32 = NULL;

	if(!shell32) {
	    shell32 = ::LoadLibrary(_T("Shell32.dll"));
	    if(shell32)
	    {
	    	getKnownFolderPath = (_SHGetKnownFolderPath)::GetProcAddress(shell32, "SHGetKnownFolderPath");

	    	if(getKnownFolderPath) {
	    		 PWSTR path = NULL;
	             // Defined in KnownFolders.h.
	             static GUID downloads = {0x374de290, 0x123f, 0x4565, {0x91, 0x64, 0x39, 0xc4, 0x92, 0x5e, 0x46, 0x7b}};
	    		 if(getKnownFolderPath(downloads, 0, NULL, &path) == S_OK) {
	    			 string ret = Text::fromT(path) + "\\";
	    			 ::CoTaskMemFree(path);
	    			 return ret;
	    		 }
	    	}
	    }
	}

	return def + "Downloads\\";
}

#endif

string Util::getTempPath() noexcept {
#ifdef _WIN32
	TCHAR buf[MAX_PATH + 1];
	DWORD x = GetTempPath(MAX_PATH, buf);
	return Text::fromT(tstring(buf, x)) + INST_NAME + PATH_SEPARATOR_STR;
#else
	return "/tmp/";
#endif
}

string Util::getOpenPath() noexcept {
	return getTempPath() + "Opened Items" + PATH_SEPARATOR_STR;
}

void Util::addStartupParam(const string& aParam) noexcept {
	if (aParam.empty())
		return;

	if (!hasStartupParam(aParam))
		startupParams.push_back(aParam);
}

bool Util::hasStartupParam(const string& aParam) noexcept {
	return find(startupParams.begin(), startupParams.end(), aParam) != startupParams.end();
}

string Util::getStartupParams(bool isFirst) noexcept {
	if (startupParams.empty()) {
		return Util::emptyString;
	}

	return string(isFirst ? Util::emptyString : " ") + Util::toString(" ", startupParams);
}

optional<string> Util::getStartupParam(const string& aKey) noexcept {
	for (const auto& p : startupParams) {
		auto pos = p.find("=");
		if (pos != string::npos && pos != p.length() && Util::strnicmp(p, aKey, pos) == 0)
			return p.substr(pos + 1, p.length() - pos - 1);
	}

	return optional<string>();
}

#ifdef _WIN32

string Util::getAppPath() noexcept {
	TCHAR buf[MAX_PATH+1];
	DWORD x = GetModuleFileName(NULL, buf, MAX_PATH);
	return Text::fromT(tstring(buf, x));
}

#else

void Util::setApp(const string& app) noexcept {
	appPath = app;
}

string Util::getAppPath() noexcept {
	return appPath;
}
	
#endif

string Util::getAppFilePath() noexcept {
	return getFilePath(getAppPath());
}

string Util::getAppFileName() noexcept {
	return getFileName(getAppPath());
}

void Util::initialize(const string& aConfigPath) {
	Text::initialize();

	sgenrand((unsigned long)time(NULL));

#if (_MSC_VER >= 1400)
	_set_invalid_parameter_handler(reinterpret_cast<_invalid_parameter_handler>(invalidParameterHandler));
#endif

#ifdef _WIN32
	string exePath = getAppFilePath();

	// Global config path is the AirDC++ executable path...
	paths[PATH_GLOBAL_CONFIG] = exePath;

	paths[PATH_USER_CONFIG] = !aConfigPath.empty() ? aConfigPath : paths[PATH_GLOBAL_CONFIG] + "Settings\\";

	loadBootConfig();

	if(!File::isAbsolutePath(paths[PATH_USER_CONFIG])) {
		paths[PATH_USER_CONFIG] = paths[PATH_GLOBAL_CONFIG] + paths[PATH_USER_CONFIG];
	}

	paths[PATH_USER_CONFIG] = validatePath(paths[PATH_USER_CONFIG], true);

	if(localMode) {
		paths[PATH_USER_LOCAL] = paths[PATH_USER_CONFIG];
		paths[PATH_DOWNLOADS] = paths[PATH_USER_CONFIG] + "Downloads\\";
	} else {
		TCHAR buf[MAX_PATH+1] = { 0 };
		if(::SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, buf) == S_OK) {
			paths[PATH_USER_CONFIG] = Text::fromT(buf) + "\\AirDC++\\";
		}

		paths[PATH_DOWNLOADS] = getDownloadsPath(paths[PATH_USER_CONFIG]);
		paths[PATH_USER_LOCAL] = ::SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, buf) == S_OK ? Text::fromT(buf) + "\\AirDC++\\" : paths[PATH_USER_CONFIG];
	}
	
	paths[PATH_RESOURCES] = exePath;
	paths[PATH_LOCALE] = (localMode ? exePath : paths[PATH_USER_LOCAL]) + "Language\\";

#else
	paths[PATH_GLOBAL_CONFIG] = "/etc/";
	const char* home_ = getenv("HOME");
	string home = home_ ? Text::toUtf8(home_) : "/tmp/";

	paths[PATH_USER_CONFIG] = !aConfigPath.empty() ? aConfigPath : home + "/.airdc++/";

	loadBootConfig();

	if(!File::isAbsolutePath(paths[PATH_USER_CONFIG])) {
		paths[PATH_USER_CONFIG] = paths[PATH_GLOBAL_CONFIG] + paths[PATH_USER_CONFIG];
	}

	paths[PATH_USER_CONFIG] = validatePath(paths[PATH_USER_CONFIG], true);

	if(localMode) {
		// @todo implement...
	}

	paths[PATH_USER_LOCAL] = paths[PATH_USER_CONFIG];
	paths[PATH_RESOURCES] = RESOURCE_DIRECTORY;
	paths[PATH_LOCALE] = paths[PATH_RESOURCES] + "locale/";
	paths[PATH_DOWNLOADS] = home + "/Downloads/";
#endif

	paths[PATH_FILE_LISTS] = paths[PATH_USER_CONFIG] + "FileLists" PATH_SEPARATOR_STR;
	paths[PATH_HUB_LISTS] = paths[PATH_USER_LOCAL] + "HubLists" PATH_SEPARATOR_STR;
	paths[PATH_NOTEPAD] = paths[PATH_USER_CONFIG] + "Notepad.txt";
	paths[PATH_EMOPACKS] = paths[PATH_RESOURCES] + "EmoPacks" PATH_SEPARATOR_STR;
	paths[PATH_BUNDLES] = paths[PATH_USER_CONFIG] + "Bundles" PATH_SEPARATOR_STR;
	paths[PATH_THEMES] = paths[PATH_GLOBAL_CONFIG] + "Themes" PATH_SEPARATOR_STR;
	paths[PATH_SHARECACHE] = paths[PATH_USER_LOCAL] + "ShareCache" PATH_SEPARATOR_STR;

	File::ensureDirectory(paths[PATH_USER_CONFIG]);
	File::ensureDirectory(paths[PATH_USER_LOCAL]);
	File::ensureDirectory(paths[PATH_THEMES]);
	File::ensureDirectory(paths[PATH_LOCALE]);
}

void Util::migrate(const string& file) noexcept {
	if(localMode) {
		return;
	}

	if(File::getSize(file) != -1) {
		return;
	}

	auto fname = getFileName(file);
	auto oldPath = paths[PATH_GLOBAL_CONFIG] + "Settings" + PATH_SEPARATOR + fname;
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

void Util::migrate(const string& aNewDir, const string& aPattern) noexcept {
	if (localMode)
		return;

	auto oldDir = Util::getPath(Util::PATH_GLOBAL_CONFIG) + "Settings" + PATH_SEPARATOR + Util::getLastDir(aNewDir) + PATH_SEPARATOR;

	if (Util::fileExists(oldDir)) {
		// don't migrate if there are files in the new directory already
		auto fileListNew = File::findFiles(aNewDir, aPattern);
		if (fileListNew.empty()) {
			auto fileList = File::findFiles(oldDir, aPattern);
			for (auto& path : fileList) {
				try {
					File::renameFile(path, aNewDir + Util::getFileName(path));
				} catch (const FileException& /*e*/) {
					//LogManager::getInstance()->message("Settings migration for failed: " + e.getError());
				}
			}
		}
	}

	/*try {
		File::renameFile(oldDir, oldDir + ".old");
	} catch (FileException& e) {
		// ...
	}*/
}

void Util::loadBootConfig() noexcept {
	// Load boot settings
	try {
		SimpleXML boot;
		boot.fromXML(File(getPath(PATH_GLOBAL_CONFIG) + "dcppboot.xml", File::READ, File::OPEN).read());
		boot.stepIn();

		if(boot.findChild("LocalMode")) {
			localMode = boot.getChildData() != "0";
		}

		boot.resetCurrentChild();
		
		if(boot.findChild("ConfigPath")) {
			ParamMap params;
#ifdef _WIN32
			// @todo load environment variables instead? would make it more useful on *nix
			TCHAR path[MAX_PATH];

			params["APPDATA"] = Text::fromT((::SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path), path));
			params["PERSONAL"] = Text::fromT((::SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, path), path));
#endif
			paths[PATH_USER_CONFIG] = Util::formatParams(boot.getChildData(), params);
		}
	} catch(const Exception& ) {
		// Unable to load boot settings...
	}
}

#ifdef _WIN32
static const char badChars[] = { 
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
		17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
		31, '<', '>', '/', '"', '|', '?', '*', 0
};
#else

static const char badChars[] = { 
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
	17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
	31, '<', '>', '\\', '"', '|', '?', '*', 0
};
#endif

/**
 * Replaces all strange characters in a file with '_'
 * @todo Check for invalid names such as nul and aux...
 */
string Util::cleanPathChars(string tmp, bool isFileName) noexcept {
	string::size_type i = 0;

	// First, eliminate forbidden chars
	while( (i = tmp.find_first_of(badChars, i)) != string::npos) {
		tmp[i] = '_';
		i++;
	}

	// Then, eliminate all ':' that are not the second letter ("c:\...")
	i = 0;
	while( (i = tmp.find(':', i)) != string::npos) {
		if (i == 1 && !isFileName) {
			i++;
			continue;
		}
		tmp[i] = '_';	
		i++;
	}

	// Remove the .\ that doesn't serve any purpose
	i = 0;
	while( (i = tmp.find("\\.\\", i)) != string::npos) {
		tmp.erase(i+1, 2);
	}
	i = 0;
	while( (i = tmp.find("/./", i)) != string::npos) {
		tmp.erase(i+1, 2);
	}

	// Remove any double \\ that are not at the beginning of the path...
	i = isFileName ? 0 : 1;
	while( (i = tmp.find("\\\\", i)) != string::npos) {
		tmp.erase(i+1, 1);
	}
	i = isFileName ? 0 : 1;
	while( (i = tmp.find("//", i)) != string::npos) {
		tmp.erase(i+1, 1);
	}

	// And last, but not least, the infamous ..\! ...
	i = 0;
	while( ((i = tmp.find("\\..\\", i)) != string::npos) ) {
		tmp[i + 1] = '_';
		tmp[i + 2] = '_';
		tmp[i + 3] = '_';
		i += 2;
	}
	i = 0;
	while( ((i = tmp.find("/../", i)) != string::npos) ) {
		tmp[i + 1] = '_';
		tmp[i + 2] = '_';
		tmp[i + 3] = '_';
		i += 2;
	}

	// Dots at the end of path names aren't popular
	i = 0;
	while( ((i = tmp.find(".\\", i)) != string::npos) ) {
		if(i != 0)
			tmp[i] = '_';
		i += 1;
	}
	i = 0;
	while( ((i = tmp.find("./", i)) != string::npos) ) {
		if(i != 0)
			tmp[i] = '_';
		i += 1;
	}

	if (isFileName) {
		tmp = cleanPathSeparators(tmp);
	}


	return tmp;
}

string Util::cleanPathSeparators(const string& str) noexcept {
	string ret(str);
	string::size_type i = 0;
	while ((i = ret.find_first_of("/\\", i)) != string::npos) {
		ret[i] = '_';
	}
	return ret;
}


bool Util::checkExtension(const string& tmp) noexcept {
	for(size_t i = 0, n = tmp.size(); i < n; ++i) {
		if (tmp[i] < 0 || tmp[i] == 32 || tmp[i] == ':') {
			return false;
		}
	}
	if(tmp.find_first_of(badChars, 0) != string::npos) {
		return false;
	}
	return true;
}

string Util::addBrackets(const string& s) noexcept {
	return '<' + s + '>';
}

string Util::getShortTimeString(time_t t) noexcept {
	char buf[255];
	tm* _tm = localtime(&t);
	if(_tm == NULL) {
		strcpy(buf, "xx:xx");
	} else {
		strftime(buf, 254, SETTING(TIME_STAMPS_FORMAT).c_str(), _tm);
	}
	return Text::toUtf8(buf);
}

void Util::sanitizeUrl(string& url) noexcept {
	boost::algorithm::trim_if(url, boost::is_space() || boost::is_any_of("<>\""));
}

/**
 * Decodes a URL the best it can...
 * Default ports:
 * http:// -> port 80
 * dchub:// -> port 411
 */
void Util::decodeUrl(const string& url, string& protocol, string& host, string& port, string& path, string& query, string& fragment) noexcept {
	auto fragmentEnd = url.size();
	auto fragmentStart = url.rfind('#');

	size_t queryEnd;
	if(fragmentStart == string::npos) {
		queryEnd = fragmentStart = fragmentEnd;
	} else {
		dcdebug("f");
		queryEnd = fragmentStart;
		fragmentStart++;
	}

	auto queryStart = url.rfind('?', queryEnd);
	size_t fileEnd;

	if(queryStart == string::npos) {
		fileEnd = queryStart = queryEnd;
	} else {
		dcdebug("q");
		fileEnd = queryStart;
		queryStart++;
	}

	auto protoStart = 0;
	auto protoEnd = url.find("://", protoStart);

	auto authorityStart = protoEnd == string::npos ? protoStart : protoEnd + 3;
	auto authorityEnd = url.find_first_of("/#?", authorityStart);

	size_t fileStart;
	if(authorityEnd == string::npos) {
		authorityEnd = fileStart = fileEnd;
	} else {
		dcdebug("a");
		fileStart = authorityEnd;
	}

	protocol = (protoEnd == string::npos ? Util::emptyString : url.substr(protoStart, protoEnd - protoStart));

	if(authorityEnd > authorityStart) {
		dcdebug("x");
		size_t portStart = string::npos;
		if(url[authorityStart] == '[') {
			// IPv6?
			auto hostEnd = url.find(']');
			if(hostEnd == string::npos) {
				return;
			}

			host = url.substr(authorityStart + 1, hostEnd - authorityStart - 1);
			if(hostEnd + 1 < url.size() && url[hostEnd + 1] == ':') {
				portStart = hostEnd + 2;
			}
		} else {
			size_t hostEnd;
			portStart = url.find(':', authorityStart);
			if(portStart != string::npos && portStart > authorityEnd) {
				portStart = string::npos;
			}

			if(portStart == string::npos) {
				hostEnd = authorityEnd;
			} else {
				hostEnd = portStart;
				portStart++;
			}

			dcdebug("h");
			host = url.substr(authorityStart, hostEnd - authorityStart);
		}

		if(portStart == string::npos) {
			if(protocol == "http") {
				port = "80";
			} else if(protocol == "https") {
				port = "443";
			} else if(protocol == "dchub"  || protocol.empty()) {
				port = "411";
			}
		} else {
			dcdebug("p");
			port = url.substr(portStart, authorityEnd - portStart);
		}
	}

	dcdebug("\n");
	path = url.substr(fileStart, fileEnd - fileStart);
	query = url.substr(queryStart, queryEnd - queryStart);
	fragment = url.substr(fragmentStart, fragmentEnd - fragmentStart);
}

void Util::parseIpPort(const string& aIpPort, string& ip, string& port) noexcept {
	string::size_type i = aIpPort.rfind(':');
	if (i == string::npos) {
		ip = aIpPort;
	} else {
		ip = aIpPort.substr(0, i);
		port = aIpPort.substr(i + 1);
	}
}

map<string, string> Util::decodeQuery(const string& query) noexcept {
	map<string, string> ret;
	size_t start = 0;
	while(start < query.size()) {
		auto eq = query.find('=', start);
		if(eq == string::npos) {
			break;
		}

		auto param = eq + 1;
		auto end = query.find('&', param);

		if(end == string::npos) {
			end = query.size();
		}

		if(eq > start && end > param) {
			ret[query.substr(start, eq-start)] = query.substr(param, end - param);
		}

		start = end + 1;
	}

	return ret;
}

#ifdef _WIN32
wstring Util::formatSecondsW(int64_t aSec, bool supressHours /*false*/) noexcept {
	wchar_t buf[64];
	if (!supressHours)
		snwprintf(buf, sizeof(buf), L"%01lu:%02d:%02d", (unsigned long)(aSec / (60*60)), (int)((aSec / 60) % 60), (int)(aSec % 60));
	else
		snwprintf(buf, sizeof(buf), L"%02d:%02d", (int)(aSec / 60), (int)(aSec % 60));	
	return buf;
}
#endif

string Util::formatSeconds(int64_t aSec, bool supressHours /*false*/) noexcept {
	char buf[64];
	if (!supressHours)
		snprintf(buf, sizeof(buf), "%01lu:%02d:%02d", (unsigned long)(aSec / (60*60)), (int)((aSec / 60) % 60), (int)(aSec % 60));
	else
		snprintf(buf, sizeof(buf), "%02d:%02d", (int)(aSec / 60), (int)(aSec % 60));	
	return buf;
}

string Util::formatTime(int64_t aSec, bool translate, bool perMinute) noexcept {
	string formatedTime;

	uint64_t n, i;
	i = 0;

	auto appendTime = [&] (const string& aTranslatedS, const string& aEnglishS, const string& aTranslatedP, const string& aEnglishP) -> void {
		if (perMinute && i == 2) //add max 2 values
			return;

		char buf[128];
		if(n >= 2) {
			snprintf(buf, sizeof(buf), ("%d " + ((translate ? Text::toLower(aTranslatedP) : aEnglishP) + " ")).c_str(), n);
		} else {
			snprintf(buf, sizeof(buf), ("%d " + ((translate ? Text::toLower(aTranslatedS) : aEnglishS) + " ")).c_str(), n);
		}
		formatedTime += (string)buf;
		i++;
	};

	n = aSec / (24*3600*365);
	aSec %= (24*3600*365);
	if(n) {
		appendTime(STRING(YEAR), "year", STRING(YEARS), "years");
	}

	n = aSec / (24*3600*30);
	aSec %= (24*3600*30);
	if(n) {
		appendTime(STRING(MONTH), "month", STRING(MONTHS), "months");
	}

	n = aSec / (24*3600*7);
	aSec %= (24*3600*7);
	if(n) {
		appendTime(STRING(WEEK), "week", STRING(WEEKS), "weeks");
	}

	n = aSec / (24*3600);
	aSec %= (24*3600);
	if(n) {
		appendTime(STRING(DAY), "day", STRING(DAYS), "days");
	}

	n = aSec / (3600);
	aSec %= (3600);
	if(n) {
		appendTime(STRING(HOUR), "hour", STRING(HOURS), "hours");
	}

	n = aSec / (60);
	aSec %= (60);
	if(n || perMinute) {
		appendTime(STRING(MINUTE), "min", STRING(MINUTES), "min");
	}

	n = aSec;
	if(++i <= 3 && !perMinute) {
		appendTime(STRING(SECOND), "sec", STRING(SECONDS), "sec");
	}

	return (!formatedTime.empty() ? formatedTime.erase(formatedTime.size()-1) : formatedTime);
}

string Util::formatBytes(int64_t aBytes) noexcept {
	/*if (aBytes < 0) {
		aBytes = abs(aBytes);
	} */
	char buf[64];
	if (aBytes < 1024) {
		snprintf(buf, sizeof(buf), "%d %s", (int)(aBytes&0xffffffff), CSTRING(B));
	} else if(aBytes < 1048576) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double)aBytes/(1024.0), CSTRING(KiB));
	} else if(aBytes < 1073741824) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double)aBytes/(1048576.0), CSTRING(MiB));
	} else if(aBytes < (int64_t)1099511627776) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double)aBytes/(1073741824.0), CSTRING(GiB));
	} else if(aBytes < (int64_t)1125899906842624) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double)aBytes/(1099511627776.0), CSTRING(TiB));
	} else if(aBytes < (int64_t)1152921504606846976)  {
		snprintf(buf, sizeof(buf), "%.02f %s", (double)aBytes/(1125899906842624.0), CSTRING(PiB));
	} else {
		snprintf(buf, sizeof(buf), "%.02f %s", (double)aBytes/(1152921504606846976.0), CSTRING(EiB));
	}

	return buf;
}

#ifdef _WIN32
wstring Util::formatBytesW(int64_t aBytes) noexcept {
	wchar_t buf[64];
	if(aBytes < 1024) {
		snwprintf(buf, sizeof(buf), L"%d %s", (int)(aBytes&0xffffffff), CWSTRING(B));
	} else if(aBytes < 1048576) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double)aBytes/(1024.0), CWSTRING(KiB));
	} else if(aBytes < 1073741824) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double)aBytes/(1048576.0), CWSTRING(MiB));
	} else if(aBytes < (int64_t)1099511627776) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double)aBytes/(1073741824.0), CWSTRING(GiB));
	} else if(aBytes < (int64_t)1125899906842624) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double)aBytes/(1099511627776.0), CWSTRING(TiB));
	} else if(aBytes < (int64_t)1152921504606846976)  {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double)aBytes/(1125899906842624.0), CWSTRING(PiB));
	} else {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double)aBytes/(1152921504606846976.0), CWSTRING(EiB));
	}
	return buf;
}
#endif

int64_t Util::convertSize(int64_t aValue, Util::SizeUnits valueType, Util::SizeUnits to /*B*/) noexcept {
	if (valueType > to) {
		return aValue * static_cast<int64_t>(pow(1024LL, static_cast<int64_t>(valueType - to)));
	} else if (valueType < to) {
		return aValue / static_cast<int64_t>(pow(1024LL, static_cast<int64_t>(to - valueType)));
	}
	return aValue;
}

string Util::formatConnectionSpeed(int64_t aBytes) noexcept {
	aBytes *= 8;
	char buf[64];
	if (aBytes < 1000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000.0), CSTRING(KBITS));
	} else if (aBytes < 1000000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000000.0), CSTRING(MBITS));
	} else if (aBytes < (int64_t) 1000000000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000000000.0), CSTRING(GBITS));
	} else if (aBytes < (int64_t) 1000000000000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000000000000.0), CSTRING(TBITS));
	} else if (aBytes < (int64_t) 1000000000000000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000000000000000.0), CSTRING(PBITS));
	}

	return buf;
}

#ifdef _WIN32
wstring Util::formatConnectionSpeedW(int64_t aBytes) noexcept {
	wchar_t buf[64];
	aBytes *= 8;
	if (aBytes < 1000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000.0), CWSTRING(KBITS));
	} else if (aBytes < 1000000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000000.0), CWSTRING(MBITS));
	} else if (aBytes < (int64_t) 1000000000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000000000.0), CWSTRING(GBITS));
	} else if (aBytes < (int64_t) 1000000000000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000000000000.0), CWSTRING(TBITS));
	} else if (aBytes < (int64_t) 1000000000000000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000000000000000.0), CWSTRING(PBITS));
	}

	return buf;
}

wstring Util::formatExactSizeW(int64_t aBytes) noexcept {
//#ifdef _WIN32	
	wchar_t buf[64];
	wchar_t number[64];
	NUMBERFMT nf;
	snwprintf(number, sizeof(number), _T("%I64d"), aBytes);
	wchar_t Dummy[16];
	TCHAR sep[2] = _T(",");
    
	/*No need to read these values from the system because they are not
	used to format the exact size*/
	nf.NumDigits = 0;
	nf.LeadingZero = 0;
	nf.NegativeOrder = 0;
	nf.lpDecimalSep = sep;

	GetLocaleInfo( LOCALE_USER_DEFAULT, LOCALE_SGROUPING, Dummy, 16 );
	nf.Grouping = _wtoi(Dummy);
	GetLocaleInfo( LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, Dummy, 16 );
	nf.lpThousandSep = Dummy;

	GetNumberFormat(LOCALE_USER_DEFAULT, 0, number, &nf, buf, 64);
		
	snwprintf(buf, sizeof(buf), _T("%s %s"), buf, CTSTRING(B));
	return buf;
/*#else
		wchar_t buf[64];
		snwprintf(buf, sizeof(buf), L"%'lld", (long long int)aBytes);
		return tstring(buf) + TSTRING(B);
#endif*/
}
#endif

string Util::formatExactSize(int64_t aBytes) noexcept {
#ifdef _WIN32
	TCHAR tbuf[128];
	TCHAR number[64];
	NUMBERFMT nf;
	_sntprintf(number, 64, _T("%I64d"), aBytes);
	TCHAR Dummy[16];
	TCHAR sep[2] = _T(",");

	/*No need to read these values from the system because they are not
	used to format the exact size*/
	nf.NumDigits = 0;
	nf.LeadingZero = 0;
	nf.NegativeOrder = 0;
	nf.lpDecimalSep = sep;

	GetLocaleInfo(LOCALE_SYSTEM_DEFAULT, LOCALE_SGROUPING, Dummy, 16);
	nf.Grouping = Util::toInt(Text::fromT(Dummy));
	GetLocaleInfo(LOCALE_SYSTEM_DEFAULT, LOCALE_STHOUSAND, Dummy, 16);
	nf.lpThousandSep = Dummy;

	GetNumberFormat(LOCALE_USER_DEFAULT, 0, number, &nf, tbuf, sizeof(tbuf) / sizeof(tbuf[0]));

	char buf[128];
	_snprintf(buf, sizeof(buf), "%s %s", buf, CSTRING(B));
	return buf;
#else
	char buf[128];
	snprintf(buf, sizeof(buf), "%'lld ", (long long int)aBytes);
	return string(buf) + STRING(B);
#endif
}

bool Util::isPrivateIp(const string& ip, bool v6) noexcept {
	if (v6) {
		return ip.length() > 5 && ip.substr(0, 4) == "fe80";
	} else {
		struct in_addr addr;

		addr.s_addr = inet_addr(ip.c_str());

		if (addr.s_addr  != INADDR_NONE) {
			unsigned long haddr = ntohl(addr.s_addr);
			return ((haddr & 0xff000000) == 0x0a000000 || // 10.0.0.0/8
					(haddr & 0xff000000) == 0x7f000000 || // 127.0.0.0/8
					(haddr & 0xffff0000) == 0xa9fe0000 || // 169.254.0.0/16
					(haddr & 0xfff00000) == 0xac100000 || // 172.16.0.0/12
					(haddr & 0xffff0000) == 0xc0a80000);  // 192.168.0.0/16
		}
	}
	return false;
}

typedef const uint8_t* ccp;
static wchar_t utf8ToLC(ccp& str) noexcept {
	wchar_t c = 0;
   if(str[0] & 0x80) { 
      if(str[0] & 0x40) { 
         if(str[0] & 0x20) { 
            if(str[1] == 0 || str[2] == 0 ||
				!((((unsigned char)str[1]) & ~0x3f) == 0x80) ||
					!((((unsigned char)str[2]) & ~0x3f) == 0x80))
				{
					str++;
					return 0;
				}
				c = ((wchar_t)(unsigned char)str[0] & 0xf) << 12 |
					((wchar_t)(unsigned char)str[1] & 0x3f) << 6 |
					((wchar_t)(unsigned char)str[2] & 0x3f);
				str += 3;
			} else {
				if(str[1] == 0 ||
					!((((unsigned char)str[1]) & ~0x3f) == 0x80)) 
				{
					str++;
					return 0;
				}
				c = ((wchar_t)(unsigned char)str[0] & 0x1f) << 6 |
					((wchar_t)(unsigned char)str[1] & 0x3f);
				str += 2;
			}
		} else {
			str++;
			return 0;
		}
	} else {
		c = Text::asciiToLower((char)str[0]);
		str++;
		return c;
	}

	return Text::toLower(c);
}

string Util::toString(const string& sep, const StringList& lst) noexcept {
	string ret;
	for(auto i = lst.begin(), iend = lst.end(); i != iend; ++i) {
		ret += *i;
		if(i + 1 != iend)
			ret += sep;
	}
	return ret;
}

string::size_type Util::findSubString(const string& aString, const string& aSubString, string::size_type start) noexcept {
	if(aString.length() < start)
		return (string::size_type)string::npos;

	if(aString.length() - start < aSubString.length())
		return (string::size_type)string::npos;

	if(aSubString.empty())
		return 0;

	// Hm, should start measure in characters or in bytes? bytes for now...
	const uint8_t* tx = (const uint8_t*)aString.c_str() + start;
	const uint8_t* px = (const uint8_t*)aSubString.c_str();

	const uint8_t* end = tx + aString.length() - start - aSubString.length() + 1;

	wchar_t wp = utf8ToLC(px);

	while(tx < end) {
		const uint8_t* otx = tx;
		if(wp == utf8ToLC(tx)) {
			const uint8_t* px2 = px;
			const uint8_t* tx2 = tx;

			for(;;) {
				if(*px2 == 0)
					return otx - (uint8_t*)aString.c_str();

				if(utf8ToLC(px2) != utf8ToLC(tx2))
					break;
			}
		}
	}
	return (string::size_type)string::npos;
}

wstring::size_type Util::findSubString(const wstring& aString, const wstring& aSubString, wstring::size_type pos) noexcept {
	if(aString.length() < pos)
		return static_cast<wstring::size_type>(wstring::npos);

	if(aString.length() - pos < aSubString.length())
		return static_cast<wstring::size_type>(wstring::npos);

	if(aSubString.empty())
		return 0;

	wstring::size_type j = 0;
	wstring::size_type end = aString.length() - aSubString.length() + 1;

	for(; pos < end; ++pos) {
		if(Text::toLower(aString[pos]) == Text::toLower(aSubString[j])) {
			wstring::size_type tmp = pos+1;
			bool found = true;
			for(++j; j < aSubString.length(); ++j, ++tmp) {
				if(Text::toLower(aString[tmp]) != Text::toLower(aSubString[j])) {
					j = 0;
					found = false;
					break;
				}
			}

			if(found)
				return pos;
		}
	}
	return static_cast<wstring::size_type>(wstring::npos);
}

int Util::stricmp(const char* a, const char* b) noexcept {
	while(*a) {
		wchar_t ca = 0, cb = 0;
		int na = Text::utf8ToWc(a, ca);
		int nb = Text::utf8ToWc(b, cb);
		ca = Text::toLower(ca);
		cb = Text::toLower(cb);
		if(ca != cb) {
			return (int)ca - (int)cb;
		}
		a += abs(na);
		b += abs(nb);
	}
	wchar_t ca = 0, cb = 0;
	Text::utf8ToWc(a, ca);
	Text::utf8ToWc(b, cb);

	return (int)Text::toLower(ca) - (int)Text::toLower(cb);
}

int Util::strnicmp(const char* a, const char* b, size_t n) noexcept {
	const char* end = a + n;
	while(*a && a < end) {
		wchar_t ca = 0, cb = 0;
		int na = Text::utf8ToWc(a, ca);
		int nb = Text::utf8ToWc(b, cb);
		ca = Text::toLower(ca);
		cb = Text::toLower(cb);
		if(ca != cb) {
			return (int)ca - (int)cb;
		}
		a += abs(na);
		b += abs(nb);
	}
	wchar_t ca = 0, cb = 0;
	Text::utf8ToWc(a, ca);
	Text::utf8ToWc(b, cb);
	return (a >= end) ? 0 : ((int)Text::toLower(ca) - (int)Text::toLower(cb));
}

string Util::encodeURI(const string& aString, bool reverse) noexcept {
	// reference: rfc2396
	string tmp = aString;
	if(reverse) {
		string::size_type idx;
		for(idx = 0; idx < tmp.length(); ++idx) {
			if(tmp.length() > idx + 2 && tmp[idx] == '%' && isxdigit(tmp[idx+1]) && isxdigit(tmp[idx+2])) {
				tmp[idx] = fromHexEscape(tmp.substr(idx+1,2));
				tmp.erase(idx+1, 2);
			} else { // reference: rfc1630, magnet-uri draft
				if(tmp[idx] == '+')
					tmp[idx] = ' ';
			}
		}
	} else {
		const string disallowed = ";/?:@&=+$," // reserved
			                      "<>#%\" "    // delimiters
		                          "{}|\\^[]`"; // unwise
		string::size_type idx, loc;
		for(idx = 0; idx < tmp.length(); ++idx) {
			if(tmp[idx] == ' ') {
				tmp[idx] = '+';
			} else {
				if(tmp[idx] <= 0x1F || tmp[idx] >= 0x7f || (loc = disallowed.find_first_of(tmp[idx])) != string::npos) {
					tmp.replace(idx, 1, toHexEscape(tmp[idx]));
					idx+=2;
				}
			}
		}
	}
	return tmp;
}


// used to parse the boost::variant params of the formatParams function.
struct GetString : boost::static_visitor<string> {
	string operator()(const string& s) const noexcept { return s; }
	string operator()(const std::function<string ()>& f) const noexcept { return f(); }
};

/**
 * This function takes a string and a set of parameters and transforms them according to
 * a simple formatting rule, similar to strftime. In the message, every parameter should be
 * represented by %[name]. It will then be replaced by the corresponding item in 
 * the params stringmap. After that, the string is passed through strftime with the current
 * date/time and then finally written to the log file. If the parameter is not present at all,
 * it is removed from the string completely...
 */

string Util::formatParams(const string& aMsg, const ParamMap& aParams, FilterF filter) noexcept {
	string result = aMsg;

	string::size_type i, j, k;
	i = 0;
	while (( j = result.find("%[", i)) != string::npos) {
		if( (result.size() < j + 2) || ((k = result.find(']', j + 2)) == string::npos) ) {
			break;
		}

		auto param = aParams.find(result.substr(j + 2, k - j - 2));

		if(param == aParams.end()) {
			result.erase(j, k - j + 1);
			i = j;

		} else {
			auto replacement = boost::apply_visitor(GetString(), param->second);

			// replace all % in params with %% for strftime
			replace("%", "%%", replacement);

			if(filter) {
				replacement = filter(replacement);
			}

			result.replace(j, k - j + 1, replacement);
			i = j + replacement.size();
		}
	}

	result = formatTime(result, time(NULL));

	return result;
}

bool Util::isAdcPath(const string& aPath) noexcept {
	return !aPath.empty() && aPath.front() == '/' && aPath.back() == '/';
}

bool Util::fileExists(const string &aFile) noexcept {
	if(aFile.empty())
		return false;

#ifdef _WIN32
	DWORD attr = GetFileAttributes(Text::toT(Util::formatPath(aFile)).c_str());
	return (attr != 0xFFFFFFFF);
#else
	return File::getSize(aFile) != -1;
#endif
}

string Util::formatTime(const string &msg, const time_t t) noexcept {
	if (!msg.empty()) {
		string ret = msg;
		tm* loc = localtime(&t);
		if(!loc) {
			return Util::emptyString;
		}

#ifndef _WIN64
		// Work it around :P
		string::size_type i = 0;
		while((i = ret.find("%", i)) != string::npos) {
			if(string("aAbBcdHIjmMpSUwWxXyYzZ%").find(ret[i+1]) == string::npos) {
				ret.replace(i, 1, "%%");
			}
			i += 2;
		}
#endif

		size_t bufsize = ret.size() + 256;
		string buf(bufsize, 0);

		errno = 0;

		buf.resize(strftime(&buf[0], bufsize-1, ret.c_str(), loc));
		
		while(buf.empty()) {
			if(errno == EINVAL)
				return Util::emptyString;

			bufsize+=64;
			buf.resize(bufsize);
			buf.resize(strftime(&buf[0], bufsize-1, ret.c_str(), loc));
		}

#ifdef _WIN32
		if(!Text::validateUtf8(buf))
#endif
		{
			buf = Text::toUtf8(buf);
		}
		return buf;
	}
	return Util::emptyString;
}

/* Below is a high-speed random number generator with much
   better granularity than the CRT one in msvc...(no, I didn't
   write it...see copyright) */ 
/* Copyright (C) 1997 Makoto Matsumoto and Takuji Nishimura.
   Any feedback is very welcome. For any question, comments,       
   see http://www.math.keio.ac.jp/matumoto/emt.html or email       
   matumoto@math.keio.ac.jp */       
/* Period parameters */  
#define N 624
#define M 397
#define MATRIX_A 0x9908b0df   /* constant vector a */
#define UPPER_MASK 0x80000000 /* most significant w-r bits */
#define LOWER_MASK 0x7fffffff /* least significant r bits */

/* Tempering parameters */   
#define TEMPERING_MASK_B 0x9d2c5680
#define TEMPERING_MASK_C 0xefc60000
#define TEMPERING_SHIFT_U(y)  (y >> 11)
#define TEMPERING_SHIFT_S(y)  (y << 7)
#define TEMPERING_SHIFT_T(y)  (y << 15)
#define TEMPERING_SHIFT_L(y)  (y >> 18)

static unsigned long mt[N]; /* the array for the state vector  */
static int mti=N+1; /* mti==N+1 means mt[N] is not initialized */

/* initializing the array with a NONZERO seed */
static void sgenrand(unsigned long seed) noexcept {
	/* setting initial seeds to mt[N] using         */
	/* the generator Line 25 of Table 1 in          */
	/* [KNUTH 1981, The Art of Computer Programming */
	/*    Vol. 2 (2nd Ed.), pp102]                  */
	mt[0]= seed & 0xffffffff;
	for (mti=1; mti<N; mti++)
		mt[mti] = (69069 * mt[mti-1]) & 0xffffffff;
}

uint32_t Util::rand() noexcept {
	unsigned long y;
	static unsigned long mag01[2]={0x0, MATRIX_A};
	/* mag01[x] = x * MATRIX_A  for x=0,1 */

	if (mti >= N) { /* generate N words at one time */
		int kk;

		if (mti == N+1)   /* if sgenrand() has not been called, */
			sgenrand(4357); /* a default initial seed is used   */

		for (kk=0;kk<N-M;kk++) {
			y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
			mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1];
		}
		for (;kk<N-1;kk++) {
			y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
			mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1];
		}
		y = (mt[N-1]&UPPER_MASK)|(mt[0]&LOWER_MASK);
		mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1];

		mti = 0;
	}

	y = mt[mti++];
	y ^= TEMPERING_SHIFT_U(y);
	y ^= TEMPERING_SHIFT_S(y) & TEMPERING_MASK_B;
	y ^= TEMPERING_SHIFT_T(y) & TEMPERING_MASK_C;
	y ^= TEMPERING_SHIFT_L(y);

	return y; 
}

int Util::randInt(int min, int max) noexcept {
	std::random_device rd;
	mt19937 gen(rd());
	uniform_int_distribution<> dist(min, max);
    return dist(gen);
}

string Util::getDateTime(time_t t) noexcept {
	if (t == 0)
		return Util::emptyString;

	char buf[64];
	tm _tm = *localtime(&t);
	strftime(buf, 64, SETTING(DATE_FORMAT).c_str(), &_tm);

	return buf;
}

#ifdef _WIN32
wstring Util::getDateTimeW(time_t t) noexcept {
	if (t == 0)
		return Util::emptyStringT;

	TCHAR buf[64];
	tm _tm;
	localtime_s(&_tm, &t);
	wcsftime(buf, 64, Text::toT(SETTING(DATE_FORMAT)).c_str(), &_tm);
	
	return buf;
}
#endif

string Util::getTimeString() noexcept {
	char buf[64];
	time_t _tt;
	time(&_tt);
	tm* _tm = localtime(&_tt);
	if(_tm == NULL) {
		strcpy(buf, "xx:xx:xx");
	} else {
		strftime(buf, 64, "%X", _tm);
	}
	return buf;
}

string Util::getTimeStamp(time_t t) noexcept {
	char buf[255];
	tm* _tm = localtime(&t);
	if (_tm == NULL) {
		strcpy(buf, "xx:xx");
	} else {
		strftime(buf, 254, SETTING(TIME_STAMPS_FORMAT).c_str(), _tm);
	}
	return Text::acpToUtf8(buf);
}

string Util::toAdcFile(const string& file) noexcept {
	if(file == "files.xml.bz2" || file == "files.xml")
		return file;

	string ret;
	ret.reserve(file.length() + 1);
	ret += '/';
	ret += file;
	for(string::size_type i = 0; i < ret.length(); ++i) {
		if(ret[i] == '\\') {
			ret[i] = '/';
		}
	}
	return ret;
}
string Util::toNmdcFile(const string& file) noexcept {
	if(file.empty())
		return Util::emptyString;

	string ret(file.substr(1));
	for(string::size_type i = 0; i < ret.length(); ++i) {
		if(ret[i] == '/') {
			ret[i] = '\\';
		}
	}
	return ret;
}

string Util::getFilePath(const string& path, const char separator) noexcept {
	string::size_type i = path.rfind(separator);
	return (i != string::npos) ? path.substr(0, i + 1) : path;
}
string Util::getFileName(const string& path, const char separator) noexcept {
	string::size_type i = path.rfind(separator);
	return (i != string::npos) ? path.substr(i + 1) : path;
}
string Util::getFileExt(const string& path) noexcept {
	string::size_type i = path.rfind('.');
	return (i != string::npos) ? path.substr(i) : Util::emptyString;
}
string Util::getLastDir(const string& path, const char separator) noexcept {
	string::size_type i = path.rfind(separator);
	if(i == string::npos)
		return path;

	string::size_type j = path.rfind(separator, i - 1);
	if (j == string::npos)
		return path.substr(0, i);

	return path.substr(j+1, i-j-1);
}

string Util::getParentDir(const string& path, const char separator /*PATH_SEPARATOR*/, bool allowEmpty /*false*/) noexcept {
	string::size_type i = path.rfind(separator);
	if(i == string::npos)
		return allowEmpty ? Util::emptyString : path;

	string::size_type j = path.rfind(separator, i - 1);
	if (j != string::npos) 
		return path.substr(0, j+1);
	
	return allowEmpty ? Util::emptyString : path;
}

wstring Util::getFilePath(const wstring& path) noexcept {
	wstring::size_type i = path.rfind(PATH_SEPARATOR);
	return (i != wstring::npos) ? path.substr(0, i + 1) : path;
}
wstring Util::getFileName(const wstring& path) noexcept {
	wstring::size_type i = path.rfind(PATH_SEPARATOR);
	return (i != wstring::npos) ? path.substr(i + 1) : path;
}
wstring Util::getFileExt(const wstring& path) noexcept {
	wstring::size_type i = path.rfind('.');
	return (i != wstring::npos) ? path.substr(i) : Util::emptyStringW;
}
wstring Util::getLastDir(const wstring& path) noexcept {
	wstring::size_type i = path.rfind(PATH_SEPARATOR);
	if(i == wstring::npos)
		return Util::emptyStringW;

	wstring::size_type j = path.rfind(PATH_SEPARATOR, i-1);
	if (j == wstring::npos)
		return i == path.length()-1 ? path.substr(0, i) : path;

	return path.substr(j+1, i-j-1);
}

string Util::translateError(int aError) noexcept {
#ifdef _WIN32
	LPTSTR lpMsgBuf;
	DWORD chars = FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER | 
		FORMAT_MESSAGE_FROM_SYSTEM | 
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		aError,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		(LPTSTR) &lpMsgBuf,
		0,
		NULL 
		);
	if(chars == 0) {
		return string();
	}
	string tmp = Text::fromT(lpMsgBuf);
	// Free the buffer.
	LocalFree( lpMsgBuf );
	string::size_type i = 0;

	while( (i = tmp.find_first_of("\r\n", i)) != string::npos) {
		tmp.erase(i, 1);
	}
	return tmp;
#else // _WIN32
	return Text::toUtf8(strerror(aError));
#endif // _WIN32
}

/* natural sorting */
int Util::DefaultSort(const wchar_t *a, const wchar_t *b) noexcept {
	int v1, v2;
	while(*a != 0 && *b != 0) {
		v1 = 0; v2 = 0;
		auto t1 = iswdigit(*a);
		auto t2 = iswdigit(*b);
		if(t1 != t2) return (t1) ? -1 : 1;

		if(!t1) {
			if(Text::toLower(*a) != Text::toLower(*b))
				return ((int)Text::toLower(*a)) - ((int)Text::toLower(*b));
			a++; b++;
		} else if(!t1) {
			if(*a != *b)
				return ((int)*a) - ((int)*b);
			a++; b++;
		} else {
			while(iswdigit(*a)) {
			    v1 *= 10;
			    v1 += *a - '0';
			    a++;
			}

	        while(iswdigit(*b)) {
		        v2 *= 10;
		        v2 += *b - '0';
		        b++;
		    }

			if(v1 != v2)
				return (v1 < v2) ? -1 : 1;
		}			
	}

	return (int)Text::toLower(*a) - (int)Text::toLower(*b);
}

int Util::DefaultSort(const char* a, const char* b) noexcept {
	int v1, v2;
	while (*a != 0 && *b != 0) {
		v1 = 0; v2 = 0;

		wchar_t ca = 0, cb = 0;
		int na = abs(Text::utf8ToWc(a, ca));
		int nb = abs(Text::utf8ToWc(b, cb));

		auto t1 = iswdigit(ca);
		auto t2 = iswdigit(cb);
		if (t1 != t2) return (t1) ? -1 : 1;

		if (!t1) {
			auto caLower = Text::toLower(ca);
			auto cbLower = Text::toLower(cb);
			if (caLower != cbLower)
				return ((int)caLower) - ((int)cbLower);

			a += na; b += nb;
		} else {
			while (iswdigit(ca)) {
				v1 *= 10;
				v1 += *a - '0';

				a += na;
				na = Text::utf8ToWc(a, ca);
			}

			while (iswdigit(cb)) {
				v2 *= 10;
				v2 += *b - '0';

				b += nb;
				nb = Text::utf8ToWc(b, cb);
			}

			if (v1 != v2)
				return (v1 < v2) ? -1 : 1;
		}
	}

	return Util::stricmp(a, b);
}

void Util::replace(string& aString, const string& findStr, const string& replaceStr) noexcept {
   string::size_type offset = 0;
   while((offset = aString.find(findStr, offset)) != string::npos) {
      aString.replace(offset, findStr.length(), replaceStr);
      offset += replaceStr.length();
   }
}

tstring Util::replaceT(const tstring& aString, const tstring& fStr, const tstring& rStr) noexcept {
	tstring tmp = aString;
	tstring::size_type pos = 0;
	while( (pos = tmp.find(fStr, pos)) != tstring::npos ) {
		tmp.replace(pos, fStr.length(), rStr);
		pos += rStr.length() - fStr.length();
	}

	return tmp;
}
	
//password protect

static const string base64_chars = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";


static inline bool is_base64(unsigned char c) noexcept {
    return (isalnum(c) || (c == '+') || (c == '/'));
}
string Util::base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) noexcept {
	string ret;
	int i = 0;
	int j = 0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];

	while(in_len--) {
		char_array_3[i++] = *(bytes_to_encode++);
		if(i == 3) {
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;
			for(i = 0; (i <4) ; i++)
				ret += base64_chars[char_array_4[i]];

			i = 0;
		}
	}

	if(i) {
		for(j = i; j < 3; j++)
			char_array_3[j] = '\0';

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;

		for (j = 0; (j < i + 1); j++)
			ret += base64_chars[char_array_4[j]];

		while((i++ < 3))
			ret += '=';

    }

    return ret;
}

string Util::base64_decode(string const& encoded_string) noexcept {
	int in_len = encoded_string.size();
	int i = 0;
	int j = 0;
	int in_ = 0;
	unsigned char char_array_4[4], char_array_3[3];
	string ret;

	while(in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
		char_array_4[i++] = encoded_string[in_]; in_++;
		if(i ==4) {
			for (i = 0; i <4; i++)
				char_array_4[i] = static_cast<unsigned char>(base64_chars.find(char_array_4[i]));

			char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

			for(i = 0; (i < 3); i++)
				ret += char_array_3[i];

			i = 0;
		}
	}

	if(i) {
		for(j = i; j <4; j++)
			char_array_4[j] = 0;

		for(j = 0; j <4; j++)
			char_array_4[j] = static_cast<unsigned char>(base64_chars.find(char_array_4[j]));

		char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
		char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
		char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

		for (j = 0; (j < i - 1); j++)
			ret += char_array_3[j];
	}

	return ret;
}

bool Util::IsOSVersionOrGreater(int major, int minor) noexcept {
#ifdef _WIN32
	return IsWindowsVersionOrGreater((WORD)major, (WORD)minor, 0);
#else // _WIN32
	return true;
#endif
}

string Util::getOsVersion(bool http /*false*/) noexcept {
#ifdef _WIN32
	typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);
	typedef BOOL(WINAPI *PGPI)(DWORD, DWORD, DWORD, DWORD, PDWORD);

	SYSTEM_INFO si;
	PGNSI pGNSI;
	ZeroMemory(&si, sizeof(SYSTEM_INFO));
	pGNSI = (PGNSI)GetProcAddress(
		GetModuleHandle(TEXT("kernel32.dll")), "GetNativeSystemInfo");
	if (NULL != pGNSI)
		pGNSI(&si);
	else GetSystemInfo(&si);

	auto formatHttp = [&](int major, int minor, string& os) -> string {
		TCHAR buf[255];
		_stprintf(buf, _T("%d.%d"),
			(DWORD)major, (DWORD)minor);

		os = "(Windows " + Text::fromT(buf);
		if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
			os += "; WOW64)";
		else
			os += ")";
		return os;
	};


	HKEY hk;
	TCHAR buf[512];
	string os = "Windows";
	string regkey = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
	auto err = ::RegOpenKeyEx(HKEY_LOCAL_MACHINE, Text::toT(regkey).c_str(), 0, KEY_READ, &hk);
	if (err == ERROR_SUCCESS) {
		ScopedFunctor([&hk] { RegCloseKey(hk); });

		DWORD bufLen = sizeof(buf);
		DWORD type;
		err = ::RegQueryValueEx(hk, _T("ProductName"), 0, &type, (LPBYTE)buf, &bufLen);
		if (err == ERROR_SUCCESS) {
			os = Text::fromT(buf);
		}

		ZeroMemory(&buf, sizeof(buf));
		if (http) {
			err = ::RegQueryValueEx(hk, _T("CurrentVersion"), 0, &type, (LPBYTE)buf, &bufLen);
			if (err == ERROR_SUCCESS) {
				auto osv = Text::fromT(buf);
				boost::regex expr{ "(\\d+)\\.(\\d+)" };
				boost::smatch osver;
				if (boost::regex_search(osv, osver, expr)) {
					return formatHttp(Util::toInt(osver[1]), Util::toInt(osver[2]), os);
				}
			}
		}
	}

	if (!os.empty()) {
		if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
			os += " 64-bit";
		else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
			os += " 32-bit";
	}

	return os;

#else // _WIN32
	utsname n;

	if(uname(&n) != 0) {
		return "unix (unknown version)";
	}

	return string(n.sysname) + " " + string(n.release) + " (" + string(n.machine) + ")";

#endif // _WIN32
}

} // namespace dcpp