/*
 * Copyright (C) 2011-2012 AirDC++ Project
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
#include <direct.h>
#include "AirUtil.h"
#include "Util.h"

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
#include "ShareManager.h"
#include <locale.h>
#include <boost/date_time/format_date_parser.hpp>
#include <boost/algorithm/string/replace.hpp>

#ifdef _WIN32
# include <ShlObj.h>
#else
# include <dirent.h>
# include <sys/stat.h>
# include <unistd.h>
# include <fnmatch.h>
#endif

namespace dcpp {

DupeType AirUtil::checkDupe(const string& aDir, int64_t aSize) {
	auto sd = ShareManager::getInstance()->isDirShared(aDir, aSize);
	if (sd > 0) {
		return sd == 2 ? SHARE_DUPE : PARTIAL_SHARE_DUPE;
	} else {
		auto qd = QueueManager::getInstance()->isDirQueued(aDir);
		if (qd > 0)
			return qd == 1 ? QUEUE_DUPE : FINISHED_DUPE;
	}
	return DUPE_NONE;
}

DupeType AirUtil::checkDupe(const TTHValue& aTTH, const string& aFileName) {
	if (ShareManager::getInstance()->isFileShared(aTTH, aFileName)) {
		return SHARE_DUPE;
	} else {
		int qd = QueueManager::getInstance()->isFileQueued(aTTH, aFileName);
		if (qd > 0) {
			return qd == 1 ? QUEUE_DUPE : FINISHED_DUPE; 
		}
	}
	return DUPE_NONE;
}

void AirUtil::init() {
	releaseReg.Init(getReleaseRegBasic());
	releaseReg.study();
	subDirRegPath.Init("(.*\\\\((((DVD)|(CD)|(DIS(K|C))).?([0-9](0-9)?))|(Sample)|(Proof)|(Cover(s)?)|(.{0,5}Sub(s|pack)?)))", PCRE_CASELESS);
	subDirRegPath.study();
}

void AirUtil::updateCachedSettings() {
	privKeyFile = Text::toLower(SETTING(TLS_PRIVATE_KEY_FILE));
	tempDLDir = Text::toLower(SETTING(TEMP_DOWNLOAD_DIRECTORY));
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
		if (subDirRegPath.match(dirMatch) > 0) {
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

bool AirUtil::listRegexMatch(const StringList& l, const boost::regex& aReg) {
	return find_if(l.begin(), l.end(), [&](const string& s) { return regex_match(s, aReg); } ) != l.end();
}

int AirUtil::listRegexCount(const StringList& l, const boost::regex& aReg) {
	return count_if(l.begin(), l.end(), [&](const string& s) { return regex_match(s, aReg); } );
}

void AirUtil::listRegexSubtract(StringList& l, const boost::regex& aReg) {
	l.erase(remove_if(l.begin(), l.end(), [&](const string& s) { return regex_match(s, aReg); }), l.end());
}

string AirUtil::formatMatchResults(int matches, int newFiles, const BundleList& bundles, bool partial) {
	string tmp;
	if(partial) {
		//partial lists
		if (bundles.size() == 1) {
			tmp = STRING_F(MATCH_SOURCE_ADDED, newFiles % bundles.front()->getName().c_str());
		} else {
			tmp = STRING_F(MATCH_SOURCE_ADDED_X_BUNDLES, newFiles % (int)bundles.size());
		}
	} else {
		//full lists
		if (matches > 0) {
			if (bundles.size() == 1) {
				tmp = STRING_F(MATCHED_FILES_BUNDLE, matches % bundles.front()->getName().c_str() % newFiles);
			} else {
				tmp = STRING_F(MATCHED_FILES_X_BUNDLES, matches % (int)bundles.size() % newFiles);
			}
		} else {
			tmp = CSTRING(NO_MATCHED_FILES);
		}
	}
	return tmp;
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

bool AirUtil::isParentOrExact(const string& aDir, const string& aSub) {
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
	::RemoveDirectoryW(Text::toT(Util::FormatPath(aPath)).c_str());
	return true;
}

void AirUtil::removeIfEmpty(const string& tgt) {
	if (!isEmpty(tgt)) {
		LogManager::getInstance()->message("The folder " + tgt + " isn't empty, not removed", LogManager::LOG_INFO);
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

bool AirUtil::isHubLink(const string& hubUrl) {
	if(strnicmp("adc://", hubUrl.c_str(), 6) == 0) {
		return true;
	} else if(strnicmp("adcs://", hubUrl.c_str(), 7) == 0) {
		return true;
	} else if(strnicmp("dchub://", hubUrl.c_str(), 8) == 0) {
		return true;
	}
	return false;
}

string AirUtil::stripHubUrl(const string& url) {
	
	if(strnicmp("adc://", url.c_str(), 6) == 0) {
		return "_" + Util::validateFileName(url.substr(6, url.length()));
	} else if(strnicmp("adcs://", url.c_str(), 7) == 0) {
		return "_" + Util::validateFileName(url.substr(7, url.length()));

	}
	return Util::emptyString;
}

string AirUtil::convertMovePath(const string& aPath, const string& aParent, const string& aTarget) {
	return aTarget + aPath.substr(aParent.length(), aPath.length() - aParent.length());
}

string AirUtil::regexEscape(const string& aStr, bool isWildcard) {
	//don't replace | and ? if it's wildcard
    static const boost::regex re_boostRegexEscape(isWildcard ? "[\\^\\.\\$\\(\\)\\[\\]\\*\\+\\/\\\\]" : "[\\^\\.\\$\\|\\(\\)\\[\\]\\*\\+\\?\\/\\\\]");
    const string rep("\\\\\\1&");
    string result = regex_replace(aStr, re_boostRegexEscape, rep, boost::match_default | boost::format_sed);
	if (isWildcard) {
		//convert * to .*
		boost::replace_all(result, "\\*", ".*");
	}
    return "^(" + result + ")$";
}

}