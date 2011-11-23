/*

*/

#include "stdinc.h"
#include "AirUtil.h"
#include "Util.h"

#include "File.h"
#include "SettingsManager.h"
#include "ResourceManager.h"
#include "StringTokenizer.h"
#include "SimpleXML.h"
#include "Socket.h"
#include "LogManager.h"
#include "Wildcards.h"
#include <locale.h>

namespace dcpp {

void AirUtil::init() {
	releaseReg.Init("(((?=\\S*[A-Za-z]\\S*)[A-Z0-9]\\S{3,})-([A-Za-z0-9]{2,}))");
	releaseReg.study();
	subDirReg.Init("(.*\\\\((((DVD)|(CD)|(DIS(K|C))).?([0-9](0-9)?))|(Sample)|(Proof)|(Cover(s)?)|(.{0,5}Sub(s|pack)?)))", PCRE_CASELESS);
	subDirReg.study();
}
void AirUtil::setSkiplist() {
	try{
	skiplistReg.assign(SETTING(SKIPLIST_SHARE));
	}catch(...) {
		LogManager::getInstance()->message("Error setting Share skiplist!");
	}
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
	if(!SettingsManager::getInstance()->isDefault(SettingsManager::BIND_INTERFACE)) {
		return Socket::getBindAddress();
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
	int slots;
	int totalSlots;
	double speed;

	if (download) {
		if (aSlots ==0) {
			totalSlots=getSlots(true);
		} else {
			totalSlots=aSlots;
		} 
		if (value != 0) {
			speed=value;
		} else {
			speed = Util::toDouble(SETTING(DOWNLOAD_SPEED));
		}
	} else {
		if (aSlots ==0) {
			totalSlots=getSlots(false);
		} else {
			totalSlots=aSlots;
		} 
		if (value != 0) {
			speed=value;
		} else {
			speed = Util::toDouble(SETTING(UPLOAD_SPEED));
		}
	}

	if (!SETTING(MCN_AUTODETECT) && value == 0) {
		if (download)
			return SETTING(MAX_MCN_DOWNLOADS);
		else
			return SETTING(MAX_MCN_UPLOADS);
	}

	//LogManager::getInstance()->message("Slots: " + Util::toString(slots));

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
		if (value != 0)
			speed=value;
		else
			speed = Util::toDouble(SETTING(DOWNLOAD_SPEED));
	} else {
		if (value != 0)
			speed=value;
		else
			speed = Util::toDouble(SETTING(UPLOAD_SPEED));
	}

	int slots=3;

	bool rar = false;
	if (((SETTING(SETTINGS_PROFILE) == SettingsManager::PROFILE_RAR) && (value == 0)) || (rarLimits && value != 0)) {
		rar=true;
	}

	if (speed <= 1) {
		if (rar) {
			slots=1;
		} else {
			if (download)
				slots=6;
			else
				slots=2;
		}
	} else if (speed > 1 && speed <= 2.5) {
		if (rar) {
			slots=2;
		} else {
			if (download)
				slots=15;
			else
				slots=3;
		}
	} else if (speed > 2.5 && speed <= 4) {
		if (rar) {
			if (!download) {
				slots=2;
			} else {
				slots=3;
			}
		} else {
			if (download)
				slots=15;
			else
				slots=4;
		}
	} else if (speed > 4 && speed <= 6) {
		if (rar) {
			if (!download) {
				slots=3;
			} else {
				slots=3;
			}
		} else {
			if (download)
				slots=20;
			else
				slots=5;
		}
	} else if (speed > 6 && speed < 10) {
		if (rar) {
			if (!download) {
				slots=3;
			} else {
				slots=5;
			}
		} else {
			if (download)
				slots=20;
			else
				slots=6;
		}
	} else if (speed >= 10 && speed <= 50) {
		if (rar) {
			if (speed <= 20) {
				slots=4;
			} else {
				slots=5;
			}
			if (download) {
				slots=slots+3;
			}
		} else {
			if (download)
				slots=30;
			else
				slots=8;
		}
	} else if(speed > 50 && speed < 100) {
		if (rar) {
			slots= speed / 10;
			if (download)
				slots=slots+4;
		} else {
			if (download)
				slots=40;
			else
				slots=12;
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


	string speed;
	if (download) {
		if (value != 0)
			speed=Util::toString(value);
		else
			speed = SETTING(DOWNLOAD_SPEED);
	} else {
		if (value != 0)
			speed=Util::toString(value);
		else
			speed = SETTING(UPLOAD_SPEED);
	}

	double lineSpeed = Util::toDouble(speed);

	int ret;
	if (download) {
		ret = lineSpeed*105;
	} else {
		ret = lineSpeed*60;
	}
	return ret;
}

int AirUtil::getMaxAutoOpened(double value) {
	if (!SETTING(UL_AUTODETECT) && value == 0) {
		return SETTING(AUTO_SLOTS);
	}

	double speed;
	if (value != 0)
		speed=value;
	else
		speed = Util::toDouble(SETTING(UPLOAD_SPEED));

	int slots=1;

	if (speed < 1) {
		slots=1;
	} else if (speed >= 1 && speed <= 5) {
		slots=2;
	}  else if (speed > 5 && speed <= 20) {
		slots=3;
	} else if (speed > 20 && speed < 100) {
		slots=4;
	} else if (speed == 100) {
		slots=6;
	} else if (speed >= 100) {
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
		SettingsManager::getInstance()->set(SettingsManager::EXTRA_PARTIAL_SLOTS, 2);
		SettingsManager::getInstance()->set(SettingsManager::MULTI_CHUNK, true);
		SettingsManager::getInstance()->set(SettingsManager::MIN_SEGMENT_SIZE, 1024);
		SettingsManager::getInstance()->set(SettingsManager::DOWNLOADS_EXPAND, false);
		//add more here

		SettingsManager::getInstance()->set(SettingsManager::SETTINGS_PROFILE, SettingsManager::PROFILE_PUBLIC);

	} else if (profile == 1) {
		if (SETTING(SETTINGS_PROFILE) != SettingsManager::PROFILE_RAR) {
			SettingsManager::getInstance()->set(SettingsManager::EXTRA_PARTIAL_SLOTS, 1);
			SettingsManager::getInstance()->set(SettingsManager::MULTI_CHUNK, true);
			SettingsManager::getInstance()->set(SettingsManager::SEGMENTS_MANUAL, false);
			SettingsManager::getInstance()->set(SettingsManager::MIN_SEGMENT_SIZE, 10240000);
			SettingsManager::getInstance()->set(SettingsManager::DOWNLOADS_EXPAND, true);
			SettingsManager::getInstance()->set(SettingsManager::CHECK_SFV, true);
			SettingsManager::getInstance()->set(SettingsManager::CHECK_NFO, true);
			SettingsManager::getInstance()->set(SettingsManager::CHECK_EXTRA_SFV_NFO, true);
			SettingsManager::getInstance()->set(SettingsManager::CHECK_EXTRA_FILES, true);
			SettingsManager::getInstance()->set(SettingsManager::CHECK_DUPES, true);
			SettingsManager::getInstance()->set(SettingsManager::MAX_FILE_SIZE_SHARED, 600);
			/*with rar hubs we dont need the matching, will lower ram usage not use that
			since autosearch adds sources to all rars. 
			But a good settings for max sources for autosearch depending download connection ?? 
			or well most users are found with 1 search anyway so second search wont find much more anyway*/
			SettingsManager::getInstance()->set(SettingsManager::AUTO_SEARCH_AUTO_MATCH, false);
			SettingsManager::getInstance()->set(SettingsManager::SEARCH_TIME, 5);
			SettingsManager::getInstance()->set(SettingsManager::AUTO_SEARCH_LIMIT, 10);
			SettingsManager::getInstance()->set(SettingsManager::AUTO_FOLLOW, false);
			SettingsManager::getInstance()->set(SettingsManager::OVERLAP_CHUNKS, false);
			//add more here

			SettingsManager::getInstance()->set(SettingsManager::SETTINGS_PROFILE, SettingsManager::PROFILE_RAR);
		}

		if (setSkiplist) {
			SettingsManager::getInstance()->set(SettingsManager::SHARE_SKIPLIST_USE_REGEXP, true);
			SettingsManager::getInstance()->set(SettingsManager::SKIPLIST_SHARE, "(.*(\\.(scn|asd|lnk|cmd|conf|dll|url|log|crc|dat|sfk|mxm|txt|message|iso|inf|sub|exe|img|bin|aac|mrg|tmp|xml|sup|ini|db|debug|pls|ac3|ape|par2|htm(l)?|bat|idx|srt|doc(x)?|ion|b4s|bgl|cab|cat|bat)$))|((All-Files-CRC-OK|xCOMPLETEx|imdb.nfo|- Copy|(.*\\s\\(\\d\\).*)).*$)");
		}

	} else if (profile == 2 && SETTING(SETTINGS_PROFILE) != SettingsManager::PROFILE_PRIVATE) {
		SettingsManager::getInstance()->set(SettingsManager::MULTI_CHUNK, true);
		SettingsManager::getInstance()->set(SettingsManager::EXTRA_PARTIAL_SLOTS, 2);
		SettingsManager::getInstance()->set(SettingsManager::SEGMENTS_MANUAL, false);
		SettingsManager::getInstance()->set(SettingsManager::MIN_SEGMENT_SIZE, 1024);
		SettingsManager::getInstance()->set(SettingsManager::DOWNLOADS_EXPAND, false);
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


bool AirUtil::checkSharedName(const string& aName, bool dir, bool report /*true*/) {
	if(aName.empty()) {
		LogManager::getInstance()->message("Invalid file name found while hashing folder "+ aName + ".");
		return false;
	}

	if(aName == "." || aName == "..")
		return false;

	if(BOOLSETTING(SHARE_SKIPLIST_USE_REGEXP)){
		if(AirUtil::matchSkiplist(Text::utf8ToAcp(aName))) {
			if(BOOLSETTING(REPORT_SKIPLIST) && report)
				LogManager::getInstance()->message("Share Skiplist blocked file, not shared: " + aName + " (" + STRING(DIRECTORY) + ": \"" + aName + "\")");
			return false;
		}
	} else {
		try {
			if (Wildcard::patternMatch( Text::utf8ToAcp(aName), Text::utf8ToAcp(SETTING(SKIPLIST_SHARE)), '|' )) {   // or validate filename for bad chars?
				if(BOOLSETTING(REPORT_SKIPLIST) && report)
					LogManager::getInstance()->message("Share Skiplist blocked file, not shared: " + aName + " (" + STRING(DIRECTORY) + ": \"" + aName + "\")");
				return false;
			}
		} catch(...) { }
	}

	if (!dir) {
		if( (stricmp(aName.c_str(), "DCPlusPlus.xml") == 0) || 
			(stricmp(aName.c_str(), "Favorites.xml") == 0) ||
			(stricmp(Util::getFileExt(aName).c_str(), ".dctmp") == 0) ||
			(stricmp(Util::getFileExt(aName).c_str(), ".antifrag") == 0) ) 
		{
			return false;
		}

		//check for forbidden file patterns
		if(BOOLSETTING(REMOVE_FORBIDDEN)) {
			string::size_type nameLen = aName.size();
			string fileExt = Util::getFileExt(aName);
			if ((stricmp(fileExt.c_str(), ".tdc") == 0) ||
				(stricmp(fileExt.c_str(), ".GetRight") == 0) ||
				(stricmp(fileExt.c_str(), ".temp") == 0) ||
				(stricmp(fileExt.c_str(), ".tmp") == 0) ||
				(stricmp(fileExt.c_str(), ".jc!") == 0) ||	//FlashGet
				(stricmp(fileExt.c_str(), ".dmf") == 0) ||	//Download Master
				(stricmp(fileExt.c_str(), ".!ut") == 0) ||	//uTorrent
				(stricmp(fileExt.c_str(), ".bc!") == 0) ||	//BitComet
				(stricmp(fileExt.c_str(), ".missing") == 0) ||
				(stricmp(fileExt.c_str(), ".bak") == 0) ||
				(stricmp(fileExt.c_str(), ".bad") == 0) ||
				(nameLen > 9 && aName.rfind("part.met") == nameLen - 8) ||				
				(aName.find("__padding_") == 0) ||			//BitComet padding
				(aName.find("__INCOMPLETE__") == 0) ||		//winmx
				(aName.find("__incomplete__") == 0)		//winmx
				) {
					if (report) {
						LogManager::getInstance()->message("Forbidden file will not be shared: " + aName + " (" + STRING(DIRECTORY) + ": \"" + aName + "\")");
					}
					return false;
			}
		}
	}
	return true;
}

}