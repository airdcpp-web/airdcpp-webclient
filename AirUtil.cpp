/*

*/
#include "stdinc.h"
#include <direct.h>
#include "AirUtil.h"
#include "Util.h"
#include "format.h"

#include "File.h"
#include "QueueManager.h"
#include "SettingsManager.h"
#include "ConnectivityManager.h"
#include "ResourceManager.h"
#include "StringTokenizer.h"
#include "SimpleXML.h"
#include "Socket.h"
#include "LogManager.h"
#include "Wildcards.h"
#include <locale.h>
#include <boost/date_time/format_date_parser.hpp>

#ifdef _WIN32
# include <ShlObj.h>
#else
# include <dirent.h>
# include <sys/stat.h>
# include <unistd.h>
# include <fnmatch.h>
#endif

namespace dcpp {

void AirUtil::init() {
	releaseReg.Init(getReleaseRegBasic());
	releaseReg.study();
	subDirReg.Init("(.*\\\\((((DVD)|(CD)|(DIS(K|C))).?([0-9](0-9)?))|(Sample)|(Proof)|(Cover(s)?)|(.{0,5}Sub(s|pack)?)))", PCRE_CASELESS);
	subDirReg.study();
#ifdef _WIN32
	// don't share Windows directory
	TCHAR path[MAX_PATH];
	::SHGetFolderPath(NULL, CSIDL_WINDOWS, NULL, SHGFP_TYPE_CURRENT, path);
	winDir = Text::fromT((tstring)path) + PATH_SEPARATOR;
#endif
}

void AirUtil::updateCachedSettings() {
	if(BOOLSETTING(SHARE_SKIPLIST_USE_REGEXP)){
		try{
			skiplistReg.assign(SETTING(SKIPLIST_SHARE));
		}catch(...) {
			skiplistReg.assign("(.*\\.(scn|asd|lnk|url|log|crc|dat|sfk|mxm))$|(rushchk.log)");
			LogManager::getInstance()->message("Error setting Share skiplist! using default: (.*\\.(scn|asd|lnk|url|log|crc|dat|sfk|mxm))$|(rushchk.log) ");
		}
	}
	privKeyFile = Text::toLower(SETTING(TLS_PRIVATE_KEY_FILE));
	tempDLDir = Text::toLower(SETTING(TEMP_DOWNLOAD_DIRECTORY));
}

bool AirUtil::matchSkiplist(const string& str) {
	try {
		if(boost::regex_search(str.begin(), str.end(), skiplistReg))
			return true;
	}catch(...) { }

	return false;
}

string AirUtil::getReleaseDir(const string& aName) {
	//LogManager::getInstance()->message("aName: " + aName);
	string dir=aName;
	if(dir[dir.size() -1] == '\\') 
		dir = dir.substr(0, (dir.size() -1));
	string dirMatch=dir;

	//check if the release name is the last one before checking subdirs
	int dpos = dirMatch.rfind("\\");
	if(dpos != string::npos) {
		dpos++;
		dirMatch = dirMatch.substr(dpos, dirMatch.size()-dpos);
	} else {
		dpos=0;
	}

	if (releaseReg.match(dirMatch) > 0) {
		dir = Text::toLower(dir.substr(dpos, dir.size()));
		return dir;
	}


	//check the subdirs then
	dpos=dir.size();
	dirMatch=dir;
	bool match=false;
	for (;;) {
		if (subDirReg.match(dirMatch) > 0) {
			dpos = dirMatch.rfind("\\");
			if(dpos != string::npos) {
				match=true;
				dirMatch = dirMatch.substr(0,dpos);
			} else {
				break;
			}
		} else {
			break;
		}
	}

	if (!match)
		return Util::emptyString;
	
	//check the release name again without subdirs
	dpos = dirMatch.rfind("\\");
	if(dpos != string::npos) {
		dpos++;
		dirMatch = dirMatch.substr(dpos, dirMatch.size()-dpos);
	} else {
		dpos=0;
	}

	if (releaseReg.match(dirMatch) > 0) {
		dir = Text::toLower(dir.substr(dpos, dir.size()));
		return dir;
	} else {
		return Util::emptyString;
	}


}
string AirUtil::getLocalIp() {
	const auto& bindAddr = CONNSETTING(BIND_ADDRESS);
	if(!bindAddr.empty() && bindAddr != SettingsManager::getInstance()->getDefault(SettingsManager::BIND_ADDRESS)) {
		return bindAddr;
	}

	string tmp;

	char buf[256];
	gethostname(buf, 255);
	hostent* he = gethostbyname(buf);
	if(he == NULL || he->h_addr_list[0] == 0)
		return Util::emptyString;
	sockaddr_in dest;
	int i = 0;

	// We take the first ip as default, but if we can find a better one, use it instead...
	memcpy(&(dest.sin_addr), he->h_addr_list[i++], he->h_length);
	tmp = inet_ntoa(dest.sin_addr);
	if(Util::isPrivateIp(tmp) || strncmp(tmp.c_str(), "169", 3) == 0) {
		while(he->h_addr_list[i]) {
			memcpy(&(dest.sin_addr), he->h_addr_list[i], he->h_length);
			string tmp2 = inet_ntoa(dest.sin_addr);
			if(!Util::isPrivateIp(tmp2) && strncmp(tmp2.c_str(), "169", 3) != 0) {
				tmp = tmp2;
			}
			i++;
		}
	}
	return tmp;
}

int AirUtil::getSlotsPerUser(bool download, double value, int aSlots) {
	if (!SETTING(MCN_AUTODETECT) && value == 0) {
		return download ? SETTING(MAX_MCN_DOWNLOADS) : SETTING(MAX_MCN_UPLOADS);
	}

	int totalSlots = aSlots;
	if (aSlots ==0)
		totalSlots = getSlots(download ? true : false);

	double speed = value;
	if (value == 0)
		speed = download ? Util::toDouble(SETTING(DOWNLOAD_SPEED)) : Util::toDouble(SETTING(UPLOAD_SPEED));

	//LogManager::getInstance()->message("Slots: " + Util::toString(slots));

	int slots;
	if (speed == 10) {
		slots=2;
	} else if (speed > 10 && speed <= 25) {
		slots=3;
	} else if (speed > 25 && speed <= 50) {
		slots=4;
	} else if (speed > 50 && speed <= 100) {
		slots=(speed/10)-1;
	} else if (speed > 100) {
		slots=15;
	} else {
		slots=1;
	}

	if (slots > totalSlots)
		slots = totalSlots;
	//LogManager::getInstance()->message("Slots: " + Util::toString(slots) + " TotalSlots: " + Util::toString(totalSlots) + " Speed: " + Util::toString(speed));
	return slots;
}


int AirUtil::getSlots(bool download, double value, bool rarLimits) {
	if (!SETTING(DL_AUTODETECT) && value == 0 && download) {
		//LogManager::getInstance()->message("Slots1");
		return SETTING(DOWNLOAD_SLOTS);
	} else if (!SETTING(UL_AUTODETECT) && value == 0 && !download) {
		//LogManager::getInstance()->message("Slots2");
		return SETTING(SLOTS);
	}

	double speed;
	if (download) {
		(value != 0) ? speed=value : speed = Util::toDouble(SETTING(DOWNLOAD_SPEED));
	} else {
		(value != 0) ? speed=value : speed = Util::toDouble(SETTING(UPLOAD_SPEED));
	}

	int slots=3;

	bool rar = ((SETTING(SETTINGS_PROFILE) == SettingsManager::PROFILE_RAR) && (value == 0)) || (rarLimits && value != 0);
	if (speed <= 1) {
		if (rar) {
			slots=1;
		} else {
			download ? slots=6 : slots=2;
		}
	} else if (speed > 1 && speed <= 2.5) {
		if (rar) {
			slots=2;
		} else {
			download ? slots=15 : slots=3;
		}
	} else if (speed > 2.5 && speed <= 4) {
		if (rar) {
			download ? slots=3 : slots=2;
		} else {
			download ? slots=15 : slots=4;
		}
	} else if (speed > 4 && speed <= 6) {
		if (rar) {
			download ? slots=3 : slots=3;
		} else {
			download ? slots=20 : slots=5;
		}
	} else if (speed > 6 && speed < 10) {
		if (rar) {
			download ? slots=5 : slots=3;
		} else {
			download ? slots=20 : slots=6;
		}
	} else if (speed >= 10 && speed <= 50) {
		if (rar) {
			speed <= 20 ?  slots=4 : slots=5;
			if (download) {
				slots=slots+3;
			}
		} else {
			download ? slots=30 : slots=8;
		}
	} else if(speed > 50 && speed < 100) {
		if (rar) {
			slots= speed / 10;
			if (download)
				slots=slots+4;
		} else {
			download ? slots=40 : slots=12;
		}
	} else if (speed >= 100) {
		if (rar) {
			if (download) {
				slots = speed / 7;
			} else {
				slots = speed / 12;
				if (slots > 15)
					slots=15;
			}
		} else {
			if (download) {
				slots=50;
			} else {
				slots= speed / 7;
				if (slots > 30 && !download)
					slots=30;
			}
		}
	}
	//LogManager::getInstance()->message("Slots3: " + Util::toString(slots));
	return slots;

}

int AirUtil::getSpeedLimit(bool download, double value) {

	if (!SETTING(DL_AUTODETECT) && value == 0 && download) {
		//LogManager::getInstance()->message("Slots1");
		return SETTING(MAX_DOWNLOAD_SPEED);
	} else if (!SETTING(UL_AUTODETECT) && value == 0 && !download) {
		//LogManager::getInstance()->message("Slots2");
		return SETTING(MIN_UPLOAD_SPEED);
	}

	if (value == 0)
		value = download ? Util::toDouble(SETTING(DOWNLOAD_SPEED)) : Util::toDouble(SETTING(UPLOAD_SPEED));

	return download ? value*105 : value*60;
}

int AirUtil::getMaxAutoOpened(double value) {
	if (!SETTING(UL_AUTODETECT) && value == 0) {
		return SETTING(AUTO_SLOTS);
	}

	if (value == 0)
		value = Util::toDouble(SETTING(UPLOAD_SPEED));

	int slots=1;

	if (value < 1) {
		slots=1;
	} else if (value >= 1 && value <= 5) {
		slots=2;
	}  else if (value > 5 && value <= 20) {
		slots=3;
	} else if (value > 20 && value < 100) {
		slots=4;
	} else if (value == 100) {
		slots=6;
	} else if (value >= 100) {
		slots=10;
	}

	return slots;
}

string AirUtil::getLocale() {
	string locale="en-US";

	if (SETTING(LANGUAGE_SWITCH) == 1) {
		locale = "sv-SE";
	} else if (SETTING(LANGUAGE_SWITCH) == 2) {
		locale = "fi-FI";
	} else if (SETTING(LANGUAGE_SWITCH) == 3) {
		locale = "it-IT";
	} else if (SETTING(LANGUAGE_SWITCH) == 4) {
		locale = "hu-HU";
	} else if (SETTING(LANGUAGE_SWITCH) == 5) {
		locale = "ro-RO";
	} else if (SETTING(LANGUAGE_SWITCH) == 6) {
		locale = "da-DK";
	} else if (SETTING(LANGUAGE_SWITCH) == 7) {
		locale = "no-NO";
	} else if (SETTING(LANGUAGE_SWITCH) == 8) {
		locale = "pt-PT";
	} else if (SETTING(LANGUAGE_SWITCH) == 9) {
		locale = "pl-PL";
	} else if (SETTING(LANGUAGE_SWITCH) == 10) {
		locale = "fr-FR";
	} else if (SETTING(LANGUAGE_SWITCH) == 11) {
		locale = "nl-NL";
	} else if (SETTING(LANGUAGE_SWITCH) == 12) {
		locale = "ru-RU";
	} else if (SETTING(LANGUAGE_SWITCH) == 13) {
		locale = "de-DE";
	}
	return locale;
}

void AirUtil::setProfile(int profile, bool setSkiplist) {
	/*Make settings depending selected client settings profile
	Note that if add a setting to one profile will need to add it to other profiles too*/
	if(profile == 0 && SETTING(SETTINGS_PROFILE) != SettingsManager::PROFILE_PUBLIC) {
		SettingsManager::getInstance()->set(SettingsManager::MIN_SEGMENT_SIZE, 1024);
		//add more here

		SettingsManager::getInstance()->set(SettingsManager::SETTINGS_PROFILE, SettingsManager::PROFILE_PUBLIC);

	} else if (profile == 1) {
		if (SETTING(SETTINGS_PROFILE) != SettingsManager::PROFILE_RAR) {
			SettingsManager::getInstance()->set(SettingsManager::SEGMENTS_MANUAL, false);
			SettingsManager::getInstance()->set(SettingsManager::MIN_SEGMENT_SIZE, 10240000);
			SettingsManager::getInstance()->set(SettingsManager::CHECK_SFV, true);
			SettingsManager::getInstance()->set(SettingsManager::CHECK_NFO, true);
			SettingsManager::getInstance()->set(SettingsManager::CHECK_EXTRA_SFV_NFO, true);
			SettingsManager::getInstance()->set(SettingsManager::CHECK_EXTRA_FILES, true);
			SettingsManager::getInstance()->set(SettingsManager::CHECK_DUPES, true);
			SettingsManager::getInstance()->set(SettingsManager::MAX_FILE_SIZE_SHARED, 600);
			SettingsManager::getInstance()->set(SettingsManager::SEARCH_TIME, 5);
			SettingsManager::getInstance()->set(SettingsManager::AUTO_SEARCH_LIMIT, 5);
			SettingsManager::getInstance()->set(SettingsManager::AUTO_FOLLOW, false);
			//add more here

			SettingsManager::getInstance()->set(SettingsManager::SETTINGS_PROFILE, SettingsManager::PROFILE_RAR);
		}

		if (setSkiplist) {
			SettingsManager::getInstance()->set(SettingsManager::SHARE_SKIPLIST_USE_REGEXP, true);
			SettingsManager::getInstance()->set(SettingsManager::SKIPLIST_SHARE, "(.*(\\.(scn|asd|lnk|cmd|conf|dll|url|log|crc|dat|sfk|mxm|txt|message|iso|inf|sub|exe|img|bin|aac|mrg|tmp|xml|sup|ini|db|debug|pls|ac3|ape|par2|htm(l)?|bat|idx|srt|doc(x)?|ion|b4s|bgl|cab|cat|bat)$))|((All-Files-CRC-OK|xCOMPLETEx|imdb.nfo|- Copy|(.*\\s\\(\\d\\).*)).*$)");
			updateCachedSettings();
		}

	} else if (profile == 2 && SETTING(SETTINGS_PROFILE) != SettingsManager::PROFILE_PRIVATE) {
		SettingsManager::getInstance()->set(SettingsManager::SEGMENTS_MANUAL, false);
		SettingsManager::getInstance()->set(SettingsManager::MIN_SEGMENT_SIZE, 1024);
		SettingsManager::getInstance()->set(SettingsManager::AUTO_FOLLOW, false);
		//add more here
			
		SettingsManager::getInstance()->set(SettingsManager::SETTINGS_PROFILE, SettingsManager::PROFILE_PRIVATE);
	}
}

string AirUtil::getPrioText(int prio) {
	switch(prio) {
		case 0: return STRING(PAUSED);
		case 1: return STRING(LOWEST);
		case 2: return STRING(LOW);
		case 3: return STRING(NORMAL);
		case 4: return STRING(HIGH);
		case 5: return STRING(HIGHEST);
		default: return STRING(PAUSED);
	}
}


bool AirUtil::checkSharedName(const string& fullPath, bool dir, bool report /*true*/, const int64_t& size /*0*/) {
	/* THE PATH SHOULD BE CONVERTED TO LOWERCASE BEFORE SENDING IT HERE */
	string aName;
	if (dir)
		aName = Util::getLastDir(fullPath);
	else
		aName = Util::getFileName(fullPath);

	if(aName == "." || aName == "..")
		return false;

	if(BOOLSETTING(SHARE_SKIPLIST_USE_REGEXP)){
		if(AirUtil::matchSkiplist(Text::utf8ToAcp(aName))) {
			if(BOOLSETTING(REPORT_SKIPLIST) && report)
				LogManager::getInstance()->message("Share Skiplist blocked file, not shared: " + aName /*+ " (" + STRING(DIRECTORY) + ": \"" + aName + "\")"*/);
			return false;
		}
	} else {
		try {
			if (Wildcard::patternMatch(Text::utf8ToAcp(aName), Text::utf8ToAcp(SETTING(SKIPLIST_SHARE)), '|' )) {   // or validate filename for bad chars?
				if(BOOLSETTING(REPORT_SKIPLIST) && report)
					LogManager::getInstance()->message("Share Skiplist blocked file, not shared: " + aName /*+ " (" + STRING(DIRECTORY) + ": \"" + aName + "\")"*/);
				return false;
			}
		} catch(...) { }
	}

	if (!dir) {
		string fileExt = Util::getFileExt(aName);
		if( (strcmp(aName.c_str(), "dcplusplus.xml") == 0) || 
			(strcmp(aName.c_str(), "favorites.xml") == 0) ||
			(strcmp(fileExt.c_str(), ".dctmp") == 0) ||
			(strcmp(fileExt.c_str(), ".antifrag") == 0) ) 
		{
			return false;
		}

		//check for forbidden file patterns
		if(BOOLSETTING(REMOVE_FORBIDDEN)) {
			string::size_type nameLen = aName.size();
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
				(nameLen > 9 && aName.rfind("part.met") == nameLen - 8) ||				
				(aName.find("__padding_") == 0) ||			//BitComet padding
				(aName.find("__incomplete__") == 0)		//winmx
				) {
					if (report) {
						LogManager::getInstance()->message("Forbidden file will not be shared: " + aName/* + " (" + STRING(DIRECTORY) + ": \"" + aName + "\")"*/);
					}
					return false;
			}
		}

		if(compare(fullPath, privKeyFile) == 0) {
			return false;
		}

		if(BOOLSETTING(NO_ZERO_BYTE) && !(size > 0))
			return false;

		if ((SETTING(MAX_FILE_SIZE_SHARED) != 0) && (size > (SETTING(MAX_FILE_SIZE_SHARED)*1024*1024))) {
			if (report) {
				LogManager::getInstance()->message(STRING(BIG_FILE_NOT_SHARED) + " " + fullPath);
			}
			return false;
		}
	} else {
#ifdef _WIN32
		// don't share Windows directory
		if(fullPath.length() >= winDir.length() && compare(fullPath.substr(0, winDir.length()), winDir) == 0)
			return false;
#endif
		if((compare(fullPath, tempDLDir) == 0)) {
			return false;
		}
	}
	return true;
}

string AirUtil::getMountPath(const string& aPath) {
	TCHAR buf[MAX_PATH];
	TCHAR buf2[MAX_PATH];
	string::size_type l = aPath.length();
	for (;;) {
		l = aPath.rfind('\\', l-2);
		if (l == string::npos || l <= 1)
			break;
		if (GetVolumeNameForVolumeMountPoint(Text::toT(aPath.substr(0, l+1)).c_str(), buf, MAX_PATH) && GetVolumePathNamesForVolumeName(buf, buf2, MAX_PATH, NULL)) {
			return Text::fromT(buf2);
		}
	}
	return Util::emptyString;
}

string AirUtil::getMountPath(const string& aPath, const StringSet& aVolumes) {
	if (aVolumes.find(aPath) != aVolumes.end()) {
		return aPath;
	}
	string::size_type l = aPath.length();
	for (;;) {
		l = aPath.rfind('\\', l-2);
		if (l == string::npos || l <= 1)
			break;
		if (aVolumes.find(aPath.substr(0, l+1)) != aVolumes.end()) {
			return aPath.substr(0, l+1);
		}
	}
	//network path?
	if (aPath.length() > 2 && aPath.substr(0,2) == "\\\\") {
		l = aPath.find("\\", 2);
		if (l != string::npos) {
			//get the drive letter
			l = aPath.find("\\", l+1);
			if (l != string::npos) {
				return aPath.substr(0, l+1);
			}
		}
	}
	return Util::emptyString;
}

void AirUtil::getTarget(StringList& targets, string& target, int64_t& freeSpace) {
	StringSet volumes;
	getVolumes(volumes);
	map<string, pair<string, int64_t>> targetMap;
	int64_t tmpSize = 0;

	for(auto i = targets.begin(); i != targets.end(); ++i) {
		string target = getMountPath(*i, volumes);
		if (!target.empty() && targetMap.find(target) == targetMap.end()) {
			int64_t free = 0;
			if (GetDiskFreeSpaceEx(Text::toT(target).c_str(), NULL, (PULARGE_INTEGER)&tmpSize, (PULARGE_INTEGER)&free)) {
				targetMap[target] = make_pair(*i, free);
			}
		}
	}

	if (targetMap.empty()) {
		if (!targets.empty()) {
			target = targets.front();
			GetDiskFreeSpaceEx(Text::toT(target).c_str(), NULL, (PULARGE_INTEGER)&tmpSize, (PULARGE_INTEGER)&freeSpace);
		}
		return;
	}

	QueueManager::getInstance()->getDiskInfo(targetMap, volumes);

	for(auto i = targetMap.begin(); i != targetMap.end(); ++i) {
		if (i->second.second > freeSpace) {
			freeSpace = i->second.second;
			target = i->second.first;
		}
	}
}

bool AirUtil::getDiskInfo(const string& aPath, int64_t& freeSpace) {
	StringSet volumes;
	AirUtil::getVolumes(volumes);
	string pathVol = AirUtil::getMountPath(aPath, volumes);
	if (pathVol.empty()) {
		return false;
	}

	int64_t totalSize = 0;
	GetDiskFreeSpaceEx(Text::toT(pathVol).c_str(), NULL, (PULARGE_INTEGER)&totalSize, (PULARGE_INTEGER)&freeSpace);

	map<string, pair<string, int64_t>> targetMap;
	targetMap[pathVol] = make_pair(aPath, freeSpace);

	QueueManager::getInstance()->getDiskInfo(targetMap, volumes);
	freeSpace = targetMap[pathVol].second;
	return true;
}

void AirUtil::getVolumes(StringSet& volumes) {
	TCHAR   buf[MAX_PATH];  
	HANDLE  hVol;    
	BOOL    found;
	TCHAR   buf2[MAX_PATH];

	// lookup drive volumes.
	hVol = FindFirstVolume(buf, MAX_PATH);
	if(hVol != INVALID_HANDLE_VALUE) {
		found = true;
		//GetVolumePathNamesForVolumeName(buf, buf2, MAX_PATH, NULL);
		//while we find drive volumes.
		while(found) {
			if(GetDriveType(buf) != DRIVE_CDROM && GetVolumePathNamesForVolumeName(buf, buf2, MAX_PATH, NULL)) {
				volumes.insert(Text::fromT(buf2));
			}
			found = FindNextVolume( hVol, buf, MAX_PATH );
		}
   		found = FindVolumeClose(hVol);
	}

	// and a check for mounted Network drives, todo fix a better way for network space
	ULONG drives = _getdrives();
	TCHAR drive[3] = { _T('A'), _T(':'), _T('\0') };

	while(drives != 0) {
		if(drives & 1 && ( GetDriveType(drive) != DRIVE_CDROM && GetDriveType(drive) == DRIVE_REMOTE) ){
			string path = Text::fromT(drive);
			if( path[ path.length() -1 ] != PATH_SEPARATOR ) {
				path += PATH_SEPARATOR;
			}
			volumes.insert(path);
		}

		++drive[0];
		drives = (drives >> 1);
	}
}

bool AirUtil::listRegexMatch(const StringList& l, const boost::regex& aReg) {
	return find_if(l.begin(), l.end(), [&](const string& s) { return regex_match(s, aReg); } ) != l.end();
}

int AirUtil::listRegexCount(const StringList& l, const boost::regex& aReg) {
	return count_if(l.begin(), l.end(), [&](const string& s) { return regex_match(s, aReg); } );
}

string AirUtil::formatMatchResults(int matches, int newFiles, const BundleList& bundles, bool partial) {
	string tmp;
	if(partial) {
		//partial lists
		if (bundles.size() == 1) {
			tmp = str(boost::format(STRING(MATCH_SOURCE_ADDED)) % 
				newFiles % 
				bundles.front()->getName().c_str());
		} else {
			tmp = str(boost::format(STRING(MATCH_SOURCE_ADDED_X_BUNDLES)) % 
				newFiles % 
				(int)bundles.size());
		}
	} else {
		//full lists
		if (matches > 0) {
			if (bundles.size() == 1) {
				tmp = str(boost::format(STRING(MATCHED_FILES_BUNDLE)) % 
					matches % 
					bundles.front()->getName().c_str() %
					newFiles);
			} else {
				tmp = str(boost::format(STRING(MATCHED_FILES_X_BUNDLES)) % 
					matches % 
					(int)bundles.size() %
					newFiles);
			}
		} else {
			tmp = CSTRING(NO_MATCHED_FILES);
		}
	}
	return tmp;
}


string AirUtil::convertMovePath(const string& aSourceCur, const string& aSourceRoot, const string& aTarget) noexcept {
	//cut the filename
	string oldDir = Util::getDir(aSourceCur, false, false);
	string target = aTarget;

	if (oldDir.length() > aSourceRoot.length()) {
		target += oldDir.substr(aSourceRoot.length(), oldDir.length() - aSourceRoot.length());
	}

	if(aSourceCur[aSourceCur.size() -1] != '\\') {
		target += Util::getFileName(aSourceCur);
		//LogManager::getInstance()->message("NEW TARGET (FILE): " + target + " OLD FILE: " + aSource);
	} else {
		//LogManager::getInstance()->message("NEW TARGET (DIR): " + target + " OLD DIR: " + aSource);
	}
	return target;
}

//fuldc ftp logger support
void AirUtil::fileEvent(const string& tgt, bool file /*false*/) {
	string target = tgt;
	if(file) {
		if(File::getSize(target) != -1) {
			StringPair sp = SettingsManager::getInstance()->getFileEvent(SettingsManager::ON_FILE_COMPLETE);
			if(sp.first.length() > 0) {
				STARTUPINFO si = { sizeof(si), 0 };
				PROCESS_INFORMATION pi = { 0 };
				ParamMap params;
				params["file"] = target;
				wstring cmdLine = Text::toT(Util::formatParams(sp.second, params));
				wstring cmd = Text::toT(sp.first);

				AutoArray<TCHAR> cmdLineBuf(cmdLine.length() + 1);
				_tcscpy(cmdLineBuf, cmdLine.c_str());

				AutoArray<TCHAR> cmdBuf(cmd.length() + 1);
				_tcscpy(cmdBuf, cmd.c_str());

				if(::CreateProcess(cmdBuf, cmdLineBuf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
					::CloseHandle(pi.hThread);
					::CloseHandle(pi.hProcess);
				}
			}
		}
	} else {
	if(File::createDirectory(target)) {
		StringPair sp = SettingsManager::getInstance()->getFileEvent(SettingsManager::ON_DIR_CREATED);
		if(sp.first.length() > 0) {
			STARTUPINFO si = { sizeof(si), 0 };
			PROCESS_INFORMATION pi = { 0 };
			ParamMap params;
			params["dir"] = target;
			wstring cmdLine = Text::toT(Util::formatParams(sp.second, params));
			wstring cmd = Text::toT(sp.first);

			AutoArray<TCHAR> cmdLineBuf(cmdLine.length() + 1);
			_tcscpy(cmdLineBuf, cmdLine.c_str());

			AutoArray<TCHAR> cmdBuf(cmd.length() + 1);
			_tcscpy(cmdBuf, cmd.c_str());

			if(::CreateProcess(cmdBuf, cmdLineBuf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
				//wait for the process to finish executing
				if(WAIT_OBJECT_0 == WaitForSingleObject(pi.hProcess, INFINITE)) {
					DWORD code = 0;
					//retrieve the error code to check if we should stop this download.
					if(0 != GetExitCodeProcess(pi.hProcess, &code)) {
						if(code != 0) { //assume 0 is the only valid return code, everything else is an error
							string::size_type end = target.find_last_of("\\/");
							if(end != string::npos) {
								tstring tmp = Text::toT(target.substr(0, end));
								RemoveDirectory(tmp.c_str());

								//the directory we removed might be a sub directory of
								//the real one, check to see if that's the case.
								end = tmp.find_last_of(_T("\\/"));
								if(end != string::npos) {
									tstring dir = tmp.substr(end+1);
									if( strnicmp(dir, _T("sample"), 6) == 0 ||
										strnicmp(dir, _T("subs"), 4) == 0 ||
										strnicmp(dir, _T("cover"), 5) == 0 ||
										strnicmp(dir, _T("cd"), 2) == 0) {
											RemoveDirectory(tmp.substr(0, end).c_str());
									}
								}
								
								::CloseHandle(pi.hThread);
								::CloseHandle(pi.hProcess);

								throw QueueException("An external sfv tool stopped the download of this file");
							}
						}
					}
				}
				
				::CloseHandle(pi.hThread);
				::CloseHandle(pi.hProcess);
				}
			}
		}
	}
}

bool AirUtil::stringRegexMatch(const string& aReg, const string& aString) {
	if (aReg.empty())
		return false;

	try {
		boost::regex reg(aReg);
		return boost::regex_match(aString, reg);
	} catch(...) { }
	return false;
}

bool AirUtil::isSub(const string& aDir, const string& aParent) {
	/* returns true if aDir is a subdir of aParent */
	return (aDir.length() > aParent.length() && (stricmp(aDir.substr(0, aParent.length()), aParent) == 0));
}

bool AirUtil::isParent(const string& aDir, const string& aSub) {
	/* returns true if aSub is a subdir of aDir OR both are the same dir */
	return (aSub.length() >= aDir.length() && (stricmp(aSub.substr(0, aDir.length()), aDir) == 0));
}

const string AirUtil::getReleaseRegLong(bool chat) {
	if (chat)
		return "((?<=\\s)(?=\\S*[A-Z]\\S*)(([A-Z0-9]|\\w[A-Z0-9])[A-Za-z0-9-]*)(\\.|_|(-(?=\\S*\\d{4}\\S+)))(\\S+)-(\\w{2,})(?=(\\W)?\\s))";
	else
		return "(?=\\S*[A-Z]\\S*)(([A-Z0-9]|\\w[A-Z0-9])[A-Za-z0-9-]*)(\\.|_|(-(?=\\S*\\d{4}\\S+)))(\\S+)-(\\w{2,})";
}

const string AirUtil::getReleaseRegBasic() {
	return "(((?=\\S*[A-Za-z]\\S*)[A-Z0-9]\\S{3,})-([A-Za-z0-9]{2,}))";
}

bool AirUtil::isEmpty(const string& aPath) {
	/* recursive check for empty dirs */
	for(FileFindIter i(aPath + "*"); i != FileFindIter(); ++i) {
		try {
			if(i->isDirectory()) {
				if (strcmpi(i->getFileName().c_str(), ".") == 0 || strcmpi(i->getFileName().c_str(), "..") == 0)
					continue;

				string dir = aPath + i->getFileName() + PATH_SEPARATOR;
				if (!isEmpty(dir))
					return false;
			} else {
				return false;
			}
		} catch(const FileException&) { } 
	}
	::RemoveDirectoryW(Text::toT(aPath).c_str());
	return true;
}

void AirUtil::removeIfEmpty(const string& tgt) {
	if (!isEmpty(tgt)) {
		LogManager::getInstance()->message("The folder " + tgt + " isn't empty, not removed");
	}
}

uint32_t AirUtil::getLastWrite(const string& path) {
	FileFindIter ff = FileFindIter(path);

	if (ff != FileFindIter()) {
		return ff->getLastWriteTime();
	}

	return 0;
}

bool AirUtil::isAdcHub(const string& hubUrl) {
	if(strnicmp("adc://", hubUrl.c_str(), 6) == 0) {
		return true;
	} else if(strnicmp("adcs://", hubUrl.c_str(), 7) == 0) {
		return true;
	}
	return false;
}

}