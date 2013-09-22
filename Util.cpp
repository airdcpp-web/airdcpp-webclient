/* 
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

long Util::mUptimeSeconds = 0;
string Util::emptyString;
wstring Util::emptyStringW;
tstring Util::emptyStringT;

string Util::paths[Util::PATH_LAST];
StringList Util::params;

bool Util::localMode = true;
bool Util::wasUncleanShutdown = false;

int Util::osMinor;
int Util::osMajor;

static void sgenrand(unsigned long seed);

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

static string getDownloadsPath(const string& def) {
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

string Util::getTempPath() {
#ifdef _WIN32
	TCHAR buf[MAX_PATH + 1];
	DWORD x = GetTempPath(MAX_PATH, buf);
	return Text::fromT(tstring(buf, x)) + INST_NAME + PATH_SEPARATOR_STR;
#else
	return "/tmp/";
#endif
}

string Util::getOpenPath() {
	return getTempPath() + "Opened Items" + PATH_SEPARATOR_STR;
}

string Util::getOpenPath(const string& aFileName) {
	auto fileName = Util::getFileName(aFileName);
	auto pos = fileName.rfind('.');
	if (pos != string::npos) {
		fileName.insert(pos,  "_" + Util::toString(Util::rand()));
	} else {
		fileName += "_" + Util::toString(Util::rand());
	}

	return getOpenPath() + fileName;
}

void Util::addParam(const string& aParam) {
	if (find(params.begin(), params.end(), aParam) == params.end())
		params.push_back(aParam);
}

bool Util::hasParam(const string& aParam) {
	return find(params.begin(), params.end(), aParam) != params.end();
}

tstring Util::getParams(bool isFirst) {
	if (params.empty()) {
		return Util::emptyStringT;
	}

	return Text::toT((isFirst ? Util::emptyString : " ") + Util::toString(" ", params)).c_str();
}

string Util::getAppName() {
#ifdef _WIN32
	TCHAR buf[MAX_PATH+1];
	DWORD x = GetModuleFileName(NULL, buf, MAX_PATH);
	return Text::fromT(tstring(buf, x));
#else
	return "airdcpp";
#endif
}

void Util::initialize() {
	Text::initialize();

	sgenrand((unsigned long)time(NULL));

#if (_MSC_VER >= 1400)
	_set_invalid_parameter_handler(reinterpret_cast<_invalid_parameter_handler>(invalidParameterHandler));
#endif

#ifdef _WIN32
	string exePath = Util::getFilePath(getAppName());

	// Global config path is the AirDC++ executable path...
	paths[PATH_GLOBAL_CONFIG] = exePath;

	paths[PATH_USER_CONFIG] = paths[PATH_GLOBAL_CONFIG] + "Settings\\";

	loadBootConfig();

	if(!File::isAbsolute(paths[PATH_USER_CONFIG])) {
		paths[PATH_USER_CONFIG] = paths[PATH_GLOBAL_CONFIG] + paths[PATH_USER_CONFIG];
	}

	paths[PATH_USER_CONFIG] = validateFileName(paths[PATH_USER_CONFIG]);

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

	OSVERSIONINFOEX ver;
	memzero(&ver, sizeof(OSVERSIONINFOEX));
	if(!GetVersionEx((OSVERSIONINFO*)&ver)) 
	{
		ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	}
	GetVersionEx((OSVERSIONINFO*)&ver);
	ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

	osMajor = ver.dwMajorVersion;
	osMinor = ver.dwMinorVersion;
#else
	paths[PATH_GLOBAL_CONFIG] = "/etc/";
	const char* home_ = getenv("HOME");
	string home = home_ ? Text::toUtf8(home_) : "/tmp/";

	paths[PATH_USER_CONFIG] = home + "/.airdc++/";

	loadBootConfig();

	if(!File::isAbsolute(paths[PATH_USER_CONFIG])) {
		paths[PATH_USER_CONFIG] = paths[PATH_GLOBAL_CONFIG] + paths[PATH_USER_CONFIG];
	}

	paths[PATH_USER_CONFIG] = validateFileName(paths[PATH_USER_CONFIG]);

	if(localMode) {
		// @todo implement...
	}

	paths[PATH_USER_LOCAL] = paths[PATH_USER_CONFIG];
	paths[PATH_RESOURCES] = "/usr/share/";
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

void Util::migrate(const string& file) {
	if(localMode) {
		return;
	}

	if(File::getSize(file) != -1) {
		return;
	}

	string fname = getFileName(file);
	string old = paths[PATH_GLOBAL_CONFIG] + "Settings\\" + fname;
	if(File::getSize(old) == -1) {
		return;
	}

	File::copyFile(old.c_str(), old + ".bak");
	try {
		File::renameFile(old, file);
	} catch(const FileException& /*e*/) {
		//LogManager::getInstance()->message("Settings migration for failed: " + e.getError());
	}
}

void Util::migrate(const string& aDir, const string& aPattern) {
	if (localMode)
		return;

	string old = Util::getPath(Util::PATH_GLOBAL_CONFIG) + "Settings\\" + Util::getLastDir(aDir) + "\\";

	if (Util::fileExists(old)) {
		auto fileList = File::findFiles(old, aPattern);
		for (auto& path: fileList) {
			try {
				File::renameFile(path, aDir + Util::getFileName(path));
			} catch(const FileException& /*e*/) {
				//LogManager::getInstance()->message("Settings migration for failed: " + e.getError());
			}
		}
	}
}

void Util::loadBootConfig() {
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
string Util::validateFileName(string tmp) {
	string::size_type i = 0;

	// First, eliminate forbidden chars
	while( (i = tmp.find_first_of(badChars, i)) != string::npos) {
		tmp[i] = '_';
		i++;
	}

	// Then, eliminate all ':' that are not the second letter ("c:\...")
	i = 0;
	while( (i = tmp.find(':', i)) != string::npos) {
		if(i == 1) {
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
	i = 1;
	while( (i = tmp.find("\\\\", i)) != string::npos) {
		tmp.erase(i+1, 1);
	}
	i = 1;
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


	return tmp;
}

bool Util::checkExtension(const string& tmp) {
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

string Util::cleanPathChars(const string& str) {
	string ret(str);
	string::size_type i = 0;
	while((i = ret.find_first_of("/.\\", i)) != string::npos) {
		ret[i] = '_';
	}
	return ret;
}

string Util::addBrackets(const string& s) {
	return '<' + s + '>';
}

string Util::getShortTimeString(time_t t) {
	char buf[255];
	tm* _tm = localtime(&t);
	if(_tm == NULL) {
		strcpy(buf, "xx:xx");
	} else {
		strftime(buf, 254, SETTING(TIME_STAMPS_FORMAT).c_str(), _tm);
	}
	return Text::toUtf8(buf);
}

void Util::sanitizeUrl(string& url) {
	boost::algorithm::trim_if(url, boost::is_space() || boost::is_any_of("<>\""));
}

/**
 * Decodes a URL the best it can...
 * Default ports:
 * http:// -> port 80
 * dchub:// -> port 411
 */
void Util::decodeUrl(const string& url, string& protocol, string& host, string& port, string& path, string& query, string& fragment) {
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

map<string, string> Util::decodeQuery(const string& query) {
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
wstring Util::formatSecondsW(int64_t aSec, bool supressHours /*false*/) {
	wchar_t buf[64];
	if (!supressHours)
		snwprintf(buf, sizeof(buf), L"%01lu:%02d:%02d", (unsigned long)(aSec / (60*60)), (int)((aSec / 60) % 60), (int)(aSec % 60));
	else
		snwprintf(buf, sizeof(buf), L"%02d:%02d", (int)(aSec / 60), (int)(aSec % 60));	
	return buf;
}
#endif

string Util::formatSeconds(int64_t aSec, bool supressHours /*false*/) {
	char buf[64];
	if (!supressHours)
		snprintf(buf, sizeof(buf), "%01lu:%02d:%02d", (unsigned long)(aSec / (60*60)), (int)((aSec / 60) % 60), (int)(aSec % 60));
	else
		snprintf(buf, sizeof(buf), "%02d:%02d", (int)(aSec / 60), (int)(aSec % 60));	
	return buf;
}

string Util::formatTime(int64_t aSec, bool translate, bool perMinute) {
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

string Util::formatBytes(int64_t aBytes) {
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
wstring Util::formatBytesW(int64_t aBytes) {
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

int64_t Util::convertSize(int64_t aValue, Util::SizeUnits valueType, Util::SizeUnits to /*B*/) {
	if (valueType > to) {
		return aValue * pow(1024LL, static_cast<int64_t>(valueType - to));
	} else if (valueType < to) {
		return aValue / pow(1024LL, static_cast<int64_t>(to - valueType));
	}
	return aValue;
}

string Util::formatConnectionSpeed(int64_t aBytes) {
	/*if (aBytes < 0) {
	aBytes = abs(aBytes);
	} */
	aBytes *= 8;
	char buf[64];
	if (aBytes < 1000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000.0), "Kbit/s");
	} else if (aBytes < 1000000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000000.0), "Mbit/s");
	} else if (aBytes < (int64_t) 1000000000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000000000.0), "Gbit/s");
	} else if (aBytes < (int64_t) 1000000000000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000000000000.0), "Tbit/s");
	} else if (aBytes < (int64_t) 1000000000000000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000000000000000.0), "Pbit/s");
	}

	return buf;
}

#ifdef _WIN32
wstring Util::formatConnectionSpeedW(int64_t aBytes) {
	wchar_t buf[64];
	aBytes *= 8;
	if (aBytes < 1000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000.0), _T("Kbit/s"));
	} else if (aBytes < 1000000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000000.0), _T("Mbit/s"));
	} else if (aBytes < (int64_t) 1000000000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000000000.0), _T("Gbit/s"));
	} else if (aBytes < (int64_t) 1000000000000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000000000000.0), _T("Tbit/s"));
	} else if (aBytes < (int64_t) 1000000000000000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000000000000000.0), _T("Pbit/s"));
	}/* else {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1152921504606846976.0), CWSTRING(EB));
	}*/

	return buf;
}

wstring Util::formatExactSizeW(int64_t aBytes) {
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

string Util::formatExactSize(int64_t aBytes) {
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

bool Util::isPrivateIp(const string& ip, bool v6) {
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
static wchar_t utf8ToLC(ccp& str) {
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
		wchar_t c = Text::asciiToLower((char)str[0]);
		str++;
		return c;
	}

	return Text::toLower(c);
}

string Util::toString(const string& sep, const StringList& lst) {
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

int Util::stricmp(const char* a, const char* b) {
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

int Util::strnicmp(const char* a, const char* b, size_t n) {
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

string Util::encodeURI(const string& aString, bool reverse) {
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
	string operator()(const string& s) const { return s; }
	string operator()(const std::function<string ()>& f) const { return f(); }
};

/**
 * This function takes a string and a set of parameters and transforms them according to
 * a simple formatting rule, similar to strftime. In the message, every parameter should be
 * represented by %[name]. It will then be replaced by the corresponding item in 
 * the params stringmap. After that, the string is passed through strftime with the current
 * date/time and then finally written to the log file. If the parameter is not present at all,
 * it is removed from the string completely...
 */

string Util::formatParams(const string& msg, const ParamMap& params, FilterF filter) {
	string result = msg;

	string::size_type i, j, k;
	i = 0;
	while (( j = result.find("%[", i)) != string::npos) {
		if( (result.size() < j + 2) || ((k = result.find(']', j + 2)) == string::npos) ) {
			break;
		}

		auto param = params.find(result.substr(j + 2, k - j - 2));

		if(param == params.end()) {
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

bool Util::validatePath(const string &sPath) {
	if(sPath.empty())
		return false;

#ifdef _WIN32
	if((sPath.substr(1, 2) == ":\\") || (sPath.substr(0, 2) == "\\\\")) {
		if(GetFileAttributes(Text::toT(sPath).c_str()) & FILE_ATTRIBUTE_DIRECTORY)
			return true;
	}

	return false;
#else
	return true;
#endif
}

bool Util::fileExists(const string &aFile) {
	if(aFile.empty())
		return false;

#ifdef _WIN32
	string path = aFile;
	
	if(path.size() > 2 && (path[1] == ':' || path[0] == '/' || path[0] == '\\')) //if its absolute path use the unc name.
		path = Util::FormatPath(aFile);

	DWORD attr = GetFileAttributes(Text::toT(path).c_str());
	return (attr != 0xFFFFFFFF);
#else
	return File::getSize(aFile) != -1;
#endif
}

string Util::formatTime(const string &msg, const time_t t) {
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
static void sgenrand(unsigned long seed) {
	/* setting initial seeds to mt[N] using         */
	/* the generator Line 25 of Table 1 in          */
	/* [KNUTH 1981, The Art of Computer Programming */
	/*    Vol. 2 (2nd Ed.), pp102]                  */
	mt[0]= seed & 0xffffffff;
	for (mti=1; mti<N; mti++)
		mt[mti] = (69069 * mt[mti-1]) & 0xffffffff;
}

uint32_t Util::rand() {
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

int Util::randInt(int min, int max) {
	std::random_device rd;
	mt19937 gen(rd());
	uniform_int_distribution<> dist(min, max);
    return dist(gen);
}

string Util::getDateTime(time_t t) {
	if (t == 0)
		return Util::emptyString;

	char buf[64];
	tm _tm = *localtime(&t);
	strftime(buf, 64, SETTING(DATE_FORMAT).c_str(), &_tm);

	return buf;
}

#ifdef _WIN32
wstring Util::getDateTimeW(time_t t) {
	if (t == 0)
		return Util::emptyStringT;

	TCHAR buf[64];
	tm _tm;
	localtime_s(&_tm, &t);
	wcsftime(buf, 64, Text::toT(SETTING(DATE_FORMAT)).c_str(), &_tm);
	
	return buf;
}
#endif

string Util::getTimeString() {
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

string Util::getTimeStamp(time_t t) {
	char buf[255];
	tm* _tm = localtime(&t);
	if (_tm == NULL) {
		strcpy(buf, "xx:xx");
	} else {
		strftime(buf, 254, SETTING(TIME_STAMPS_FORMAT).c_str(), _tm);
	}
	return Text::acpToUtf8(buf);
}

string Util::toAdcFile(const string& file) {
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
string Util::toNmdcFile(const string& file) {
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

string Util::getFilePath(const string& path, const char separator) {
	string::size_type i = path.rfind(separator);
	return (i != string::npos) ? path.substr(0, i + 1) : path;
}
string Util::getFileName(const string& path, const char separator) {
	string::size_type i = path.rfind(separator);
	return (i != string::npos) ? path.substr(i + 1) : path;
}
string Util::getFileExt(const string& path) {
	string::size_type i = path.rfind('.');
	return (i != string::npos) ? path.substr(i) : Util::emptyString;
}
string Util::getLastDir(const string& path, const char separator) {
	string::size_type i = path.rfind(separator);
	if(i == string::npos)
		return path;

	string::size_type j = path.rfind(separator, i - 1);
	if (j == string::npos)
		return i == path.length()-1 ? path.substr(0, i) : path;

	return path.substr(j+1, i-j-1);
}

string Util::getParentDir(const string& path, const char separator /*PATH_SEPARATOR*/, bool allowEmpty /*false*/) {
	string::size_type i = path.rfind(separator);
	if(i == string::npos)
		return allowEmpty ? Util::emptyString : path;

	string::size_type j = path.rfind(separator, i - 1);
	if (j != string::npos) 
		return path.substr(0, j+1);
	
	return allowEmpty ? Util::emptyString : path;
}

wstring Util::getFilePath(const wstring& path) {
	wstring::size_type i = path.rfind(PATH_SEPARATOR);
	return (i != wstring::npos) ? path.substr(0, i + 1) : path;
}
wstring Util::getFileName(const wstring& path) {
	wstring::size_type i = path.rfind(PATH_SEPARATOR);
	return (i != wstring::npos) ? path.substr(i + 1) : path;
}
wstring Util::getFileExt(const wstring& path) {
	wstring::size_type i = path.rfind('.');
	return (i != wstring::npos) ? path.substr(i) : Util::emptyStringW;
}
wstring Util::getLastDir(const wstring& path) {
	wstring::size_type i = path.rfind(PATH_SEPARATOR);
	if(i == wstring::npos)
		return Util::emptyStringW;

	wstring::size_type j = path.rfind(PATH_SEPARATOR, i-1);
	if (j == wstring::npos)
		return i == path.length()-1 ? path.substr(0, i) : path;

	return path.substr(j+1, i-j-1);
}

string Util::translateError(int aError) {
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
int Util::DefaultSort(const wchar_t *a, const wchar_t *b, bool noCase /*=  true*/) {
	if(SETTING(NAT_SORT)) {
		int v1, v2;
		while(*a != 0 && *b != 0) {
			v1 = 0; v2 = 0;
			bool t1 = isNumeric(*a);
			bool t2 = isNumeric(*b);
			if(t1 != t2) return (t1) ? -1 : 1;

			if(!t1 && noCase) {
				if(Text::toLower(*a) != Text::toLower(*b))
					return ((int)Text::toLower(*a)) - ((int)Text::toLower(*b));
				a++; b++;
			} else if(!t1) {
				if(*a != *b)
					return ((int)*a) - ((int)*b);
				a++; b++;
			} else {
			    while(isNumeric(*a)) {
			       v1 *= 10;
			       v1 += *a - '0';
			       a++;
			    }

	            while(isNumeric(*b)) {
		           v2 *= 10;
		           v2 += *b - '0';
		           b++;
		        }

				if(v1 != v2)
					return (v1 < v2) ? -1 : 1;
			}			
		}

		return noCase ? (((int)Text::toLower(*a)) - ((int)Text::toLower(*b))) : (((int)*a) - ((int)*b));
	} else {
		return noCase ? Util::stricmp(a, b) : Util::stricmp(a, b);
	}
}

void Util::replace(string& aString, const string& findStr, const string& replaceStr) {
   string::size_type offset = 0;
   while((offset = aString.find(findStr, offset)) != string::npos) {
      aString.replace(offset, findStr.length(), replaceStr);
      offset += replaceStr.length();
   }
}

tstring Util::replaceT(const tstring& aString, const tstring& fStr, const tstring& rStr) {
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


static inline bool is_base64(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}
string Util::base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) {
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

string Util::base64_decode(string const& encoded_string) {
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

string Util::getReleaseDir(const string& aDir, bool cut) {
	if (aDir.empty())
		return aDir;

	string dir = aDir.back() != '\\' ? getNmdcFilePath(aDir) : aDir;

	boost::regex reg;
	reg.assign("(.*\\\\((((DVD)|(CD)|(DIS(K|C))).?([0-9](0-9)?))|(Sample)|(Proof)|(Cover(s)?)|(.{0,5}Sub(s|pack)?))\\\\)", boost::regex_constants::icase);
	for (;;) {
		if (regex_match(dir, reg)) {
			if(dir[dir.size() -1] == '\\')
				dir = dir.substr(0, dir.size()-1);
			size_t dpos = dir.rfind("\\");
			if(dpos != string::npos) {
				dir = dir.substr(0,dpos+1);
			} else {
				break;
			}
		} else {
			break;
		}
	}

	return cut ? Util::getNmdcLastDir(dir) : dir;
}

string Util::getOsVersion(bool http /*false*/, bool doubleStr /*false*/) {
#ifdef _WIN32
	typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);
	typedef BOOL (WINAPI *PGPI)(DWORD, DWORD, DWORD, DWORD, PDWORD);
	string os;

	OSVERSIONINFOEX ver;
	memset(&ver, 0, sizeof(OSVERSIONINFOEX));
	ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	SYSTEM_INFO si;
	PGNSI pGNSI;
	DWORD dwType;
	PGPI pGPI;
	ZeroMemory(&si, sizeof(SYSTEM_INFO));
	pGNSI = (PGNSI) GetProcAddress(
		GetModuleHandle(TEXT("kernel32.dll")), "GetNativeSystemInfo");
	if(NULL != pGNSI)
		pGNSI(&si);
	else GetSystemInfo(&si);

	if(!GetVersionEx((OSVERSIONINFO*)&ver)) {
		ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
		if(!GetVersionEx((OSVERSIONINFO*)&ver)) {
			os = "Windows (version unknown)";
		}
	}

	if (http) {
		TCHAR buf[255];
		_stprintf(buf, _T("%d.%d"),
			(DWORD)ver.dwMajorVersion, (DWORD)ver.dwMinorVersion);

		os = "(Windows " + Text::fromT(buf);
		if ( si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64 )
			os += "; WOW64)";
		else 
			os += ")";
	} else if (doubleStr) {
		TCHAR buf[255];
		_stprintf(buf, _T("%d.%d"),
			(DWORD)ver.dwMajorVersion, (DWORD)ver.dwMinorVersion);
		return Text::fromT(buf);
	} else {
		if(os.empty()) {
			if(ver.dwPlatformId != VER_PLATFORM_WIN32_NT) {
				os = "Win9x/ME/Junk";
			} else if(ver.dwMajorVersion == 4) {
				os = "Windows NT4";
			} else if(ver.dwMajorVersion == 5) {
				switch(ver.dwMinorVersion) {
					case 0: os = "Windows 2000"; break;
					case 1:
						if (ver.wSuiteMask & VER_SUITE_PERSONAL)
							os = "Windows XP Home Edition";
						else
							os = "Windows XP Professional";
						break;
					case 2: 
						if(GetSystemMetrics(SM_SERVERR2))
							os = "Windows Server 2003 R2";
						else if (ver.wProductType == VER_NT_WORKSTATION &&
								si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64)
							os = "Windows XP Professional x64 Edition";
						else
							os = "Windows Server 2003";
						break;
					default: os = "Unknown Windows NT5";
				}
			} else if(ver.dwMajorVersion == 6) {
				switch(ver.dwMinorVersion) {
					case 0:
						if (ver.wProductType == VER_NT_WORKSTATION)
							os = "Windows Vista";
						else
							os = "Windows Server 2008";
						break;
					case 1:
						if (ver.wProductType == VER_NT_WORKSTATION)
							os = "Windows 7";
						else
							os = "Windows Server 2008 R2";
						break;
					case 2:
						{
							// http://msdn.microsoft.com/en-us/library/windows/desktop/dn302074(v=vs.85).aspx
							if (IsWindows8Point1OrGreater()) {
								if (IsWindowsServer())
									os = "Windows Server 2012 R2";
								else
									os = "Windows 8.1";
							} else {
								if (ver.wProductType == VER_NT_WORKSTATION)
									os = "Windows 8";
								else
									os = "Windows Server 2012";
							}
						}
						break;
					case 3:
						if (ver.wProductType == VER_NT_WORKSTATION)
							os = "Windows 8.1";
						else
							os = "Windows Server 2012 R2";
						break;
					default: os = "Unknown Windows 6-family";
				}
			}
		}

		if(ver.dwMajorVersion == 6) {
			pGPI = (PGPI) GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "GetProductInfo");
			pGPI(ver.dwMajorVersion, ver.dwMinorVersion, 0, 0, &dwType);
			 switch(dwType)
			 {
				case PRODUCT_ULTIMATE:
				   os += " Ultimate Edition";
				   break;
				case PRODUCT_PROFESSIONAL:
				   os += " Professional";
				   break;
				case PRODUCT_PROFESSIONAL_WMC:
					os += " Professional with Media Center";
					break;
				case PRODUCT_HOME_PREMIUM:
				   os += " Home Premium Edition";
				   break;
				case PRODUCT_HOME_BASIC:
				   os += " Home Basic Edition";
				   break;
				case PRODUCT_ENTERPRISE:
				   os += " Enterprise Edition";
				   break;
				case PRODUCT_BUSINESS:
				   os += " Business Edition";
				   break;
				case PRODUCT_STARTER:
				   os += " Starter Edition";
				   break;
				case PRODUCT_CLUSTER_SERVER:
				   os += " Cluster Server Edition";
				   break;
				case PRODUCT_DATACENTER_SERVER:
				   os += " Datacenter Edition";
				   break;
				case PRODUCT_DATACENTER_SERVER_CORE:
				   os += " Datacenter Edition (core installation)";
				   break;
				case PRODUCT_ENTERPRISE_SERVER:
				   os += " Enterprise Edition";
				   break;
				case PRODUCT_ENTERPRISE_SERVER_CORE:
				   os += " Enterprise Edition (core installation)";
				   break;
				case PRODUCT_ENTERPRISE_SERVER_IA64:
				   os += " Enterprise Edition for Itanium-based Systems";
				   break;
				case PRODUCT_SMALLBUSINESS_SERVER:
				   os += " Small Business Server";
				   break;
				case PRODUCT_SMALLBUSINESS_SERVER_PREMIUM:
				   os += " Small Business Server Premium Edition";
				   break;
				case PRODUCT_STANDARD_SERVER:
				   os += " Standard Edition";
				   break;
				case PRODUCT_STANDARD_SERVER_CORE:
				   os += " Standard Edition (core installation)";
				   break;
				case PRODUCT_WEB_SERVER:
				   os += " Web Server Edition";
				   break;
			 }

			if ( ver.dwMajorVersion >= 6 ) {
				if ( si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64 )
					os += " 64-bit";
				else if (si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_INTEL )
					os += " 32-bit";
			}

			tstring spver(ver.szCSDVersion);
			if(!spver.empty()) {
				os += " " + Text::fromT(spver);
			}
		}
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