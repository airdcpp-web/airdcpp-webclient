/*
 * Copyright (C) 2011-2013 AirDC++ Project
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

#include "AirUtil.h"
#include "Util.h"
#include "ThrottleManager.h"

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
#include <boost/scoped_array.hpp>
#include <boost/algorithm/string/trim.hpp>

#ifdef _WIN32
#include <ShlObj.h>
#include <IPHlpApi.h>
#pragma comment(lib, "iphlpapi.lib")

#else

#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#include <net/if.h>
#endif

#endif

namespace dcpp {

boost::regex AirUtil::releaseReg;
boost::regex AirUtil::subDirRegPlain;
boost::regex AirUtil::crcReg;

string AirUtil::privKeyFile;
string AirUtil::tempDLDir;

AwayMode AirUtil::away = AWAY_OFF;
time_t AirUtil::awayTime;

string AirUtil::getDirDupePath(DupeType aType, const string& aPath) {
	if (aType == SHARE_DUPE || aType == PARTIAL_SHARE_DUPE) {
		auto ret = ShareManager::getInstance()->getDirPaths(aPath);
		return ret.empty() ? Util::emptyString : ret.front();
	} else {
		auto ret = QueueManager::getInstance()->getDirPaths(aPath);
		return ret.empty() ? Util::emptyString : ret.front();
	}
}

string AirUtil::getDupePath(DupeType aType, const TTHValue& aTTH) {
	if (aType == SHARE_DUPE) {
		try {
			return ShareManager::getInstance()->getRealPath(aTTH);
		} catch(...) { }
	} else {
		StringList localPaths = QueueManager::getInstance()->getTargets(aTTH);
		if (!localPaths.empty()) {
			return localPaths.front();
		}
	}
	return Util::emptyString;
}

DupeType AirUtil::checkDirDupe(const string& aDir, int64_t aSize) {
	const auto sd = ShareManager::getInstance()->isDirShared(aDir, aSize);
	if (sd > 0) {
		return sd == 2 ? SHARE_DUPE : PARTIAL_SHARE_DUPE;
	} else {
		const auto qd = QueueManager::getInstance()->isDirQueued(aDir);
		if (qd > 0)
			return qd == 1 ? QUEUE_DUPE : FINISHED_DUPE;
	}
	return DUPE_NONE;
}

DupeType AirUtil::checkFileDupe(const TTHValue& aTTH) {
	if (ShareManager::getInstance()->isFileShared(aTTH)) {
		return SHARE_DUPE;
	} else {
		const int qd = QueueManager::getInstance()->isFileQueued(aTTH);
		if (qd > 0) {
			return qd == 1 ? QUEUE_DUPE : FINISHED_DUPE; 
		}
	}
	return DUPE_NONE;
}

DupeType AirUtil::checkFileDupe(const string& aFileName, int64_t aSize) {
	if (ShareManager::getInstance()->isFileShared(aFileName, aSize)) {
		return SHARE_DUPE;
	} else {
		const int qd = QueueManager::getInstance()->isFileQueued(AirUtil::getTTH(aFileName, aSize));
		if (qd > 0) {
			return qd == 1 ? QUEUE_DUPE : FINISHED_DUPE; 
		}
	}
	return DUPE_NONE;
}

TTHValue AirUtil::getTTH(const string& aFileName, int64_t aSize) {
	TigerHash tmp;
	string str = Text::toLower(aFileName) + Util::toString(aSize);
	tmp.update(str.c_str(), str.length());
	return TTHValue(tmp.finalize());
}

void AirUtil::init() {
	releaseReg.assign(getReleaseRegBasic());
	subDirRegPlain.assign(R"((((S(eason)?)|DVD|CD|(D|DIS(K|C))).?([0-9](0-9)?))|Sample.?|Proof.?|Cover.?|.{0,5}Sub(s|pack)?)", boost::regex::icase);
	crcReg.assign(R"(.{5,200}\s(\w{8})$)");
}

void AirUtil::updateCachedSettings() {
	privKeyFile = Text::toLower(SETTING(TLS_PRIVATE_KEY_FILE));
	tempDLDir = Text::toLower(SETTING(TEMP_DOWNLOAD_DIRECTORY));
}

void AirUtil::getIpAddresses(IpList& addresses, bool v6) {
#ifdef _WIN32
	ULONG len =	8192; // begin with 8 kB, it should be enough in most of cases
	for(int i = 0; i < 3; ++i)
	{
		PIP_ADAPTER_ADDRESSES adapterInfo = (PIP_ADAPTER_ADDRESSES)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
		ULONG ret = GetAdaptersAddresses(v6 ? AF_INET6 : AF_INET, GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL, adapterInfo, &len);
		bool freeObject = true;

		if(ret == ERROR_SUCCESS)
		{
			for(PIP_ADAPTER_ADDRESSES pAdapterInfo = adapterInfo; pAdapterInfo != NULL; pAdapterInfo = pAdapterInfo->Next)
			{
				// we want only enabled ethernet interfaces
				if(pAdapterInfo->OperStatus == IfOperStatusUp && (pAdapterInfo->IfType == IF_TYPE_ETHERNET_CSMACD || pAdapterInfo->IfType == IF_TYPE_IEEE80211))
				{
					PIP_ADAPTER_UNICAST_ADDRESS ua;
					for (ua = pAdapterInfo->FirstUnicastAddress; ua != NULL; ua = ua->Next) {
						//get the name of the adapter
						char buf[BUFSIZ];
						memset(buf, 0, BUFSIZ);
						getnameinfo(ua->Address.lpSockaddr, ua->Address.iSockaddrLength, buf, sizeof(buf), NULL, 0,NI_NUMERICHOST);

						//is it a local address?
						/*SOCKADDR_IN6* pAddr = (SOCKADDR_IN6*) ua->Address.lpSockaddr;
						BYTE prefix[8] = { 0xFE, 0x80 };
						auto fLinkLocal = (memcmp(pAddr->sin6_addr.u.Byte, prefix, sizeof(prefix)) == 0);*/

						addresses.emplace_back(Text::fromT(tstring(pAdapterInfo->FriendlyName)), buf, ua->OnLinkPrefixLength);
					}
					freeObject = false;
				}
			}
		}

		if(freeObject)
			HeapFree(GetProcessHeap(), 0, adapterInfo);

		if(ret != ERROR_BUFFER_OVERFLOW)
			break;
	}
#else

#ifdef HAVE_IFADDRS_H
	struct ifaddrs *ifap;

	if (getifaddrs(&ifap) == 0) {
		for (struct ifaddrs *i = ifap; i != NULL; i = i->ifa_next) {
			struct sockaddr *sa = i->ifa_addr;

			// If the interface is up, is not a loopback and it has an address
			if ((i->ifa_flags & IFF_UP) && !(i->ifa_flags & IFF_LOOPBACK) && sa != NULL) {
				void* src = nullptr;
				socklen_t len;
				uint32_t scope = 0;

				if (!v6 && sa->sa_family == AF_INET) {
					// IPv4 address
					struct sockaddr_in* sai = (struct sockaddr_in*)sa;
					src = (void*) &(sai->sin_addr);
					len = INET_ADDRSTRLEN;
					scope = 4;
				} else if (v6 && sa->sa_family == AF_INET6) {
					// IPv6 address
					struct sockaddr_in6* sai6 = (struct sockaddr_in6*)sa;
					src = (void*) &(sai6->sin6_addr);
					len = INET6_ADDRSTRLEN;
					scope = sai6->sin6_scope_id;
				}

				// Convert the binary address to a string and add it to the output list
				if (src) {
					char address[len];
					inet_ntop(sa->sa_family, src, address, len);
					addresses.emplace_back("Unknown", (string)address, (uint8_t)scope);
				}
			}
		}
		freeifaddrs(ifap);
	}
#endif

#endif
}

string AirUtil::getLocalIp(bool v6, bool allowPrivate /*true*/) {
	const auto& bindAddr = v6 ? CONNSETTING(BIND_ADDRESS6) : CONNSETTING(BIND_ADDRESS);
	if(!bindAddr.empty() && bindAddr != SettingsManager::getInstance()->getDefault(v6 ? SettingsManager::BIND_ADDRESS6 : SettingsManager::BIND_ADDRESS)) {
		return bindAddr;
	}

	IpList addresses;
	getIpAddresses(addresses, v6);
	if (addresses.empty())
		return Util::emptyString;

	auto p = boost::find_if(addresses, [v6](const AddressInfo& aAddress) { return !Util::isPrivateIp(aAddress.ip, v6); });
	if (p != addresses.end())
		return p->ip;

	return allowPrivate ? addresses.front().ip : Util::emptyString;
}

int AirUtil::getSlotsPerUser(bool download, double value, int aSlots, SettingsManager::SettingProfile aProfile) {
	if (!SETTING(MCN_AUTODETECT) && value == 0) {
		return download ? SETTING(MAX_MCN_DOWNLOADS) : SETTING(MAX_MCN_UPLOADS);
	}

	if (aProfile == SettingsManager::PROFILE_LAN) {
		return 1;
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


int AirUtil::getSlots(bool download, double value, SettingsManager::SettingProfile aProfile) {
	if (!SETTING(DL_AUTODETECT) && value == 0 && download) {
		//LogManager::getInstance()->message("Slots1");
		return SETTING(DOWNLOAD_SLOTS);
	} else if (!SETTING(UL_AUTODETECT) && value == 0 && !download) {
		//LogManager::getInstance()->message("Slots2");
		return SETTING(SLOTS);
	}

	double speed;
	if (value != 0) {
		speed=value;
	} else if (download) {
		int limit = SETTING(AUTO_DETECTION_USE_LIMITED) ? ThrottleManager::getInstance()->getDownLimit() : 0;
		speed = limit > 0 ? (limit * 8.00) / 1024.00 : Util::toDouble(SETTING(DOWNLOAD_SPEED));
	} else {
		int limit = SETTING(AUTO_DETECTION_USE_LIMITED) ? ThrottleManager::getInstance()->getUpLimit() : 0;
		speed = limit > 0 ? (limit * 8.00) / 1024.00 : Util::toDouble(SETTING(UPLOAD_SPEED));
	}

	int slots=3;

	bool rar = aProfile == SettingsManager::PROFILE_RAR;
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

string AirUtil::getPrioText(int prio) {
	switch(prio) {
		case QueueItemBase::PAUSED_FORCE: return STRING(PAUSED_FORCED);
		case QueueItemBase::PAUSED: return STRING(PAUSED);
		case QueueItemBase::LOWEST: return STRING(LOWEST);
		case QueueItemBase::LOW: return STRING(LOW);
		case QueueItemBase::NORMAL: return STRING(NORMAL);
		case QueueItemBase::HIGH: return STRING(HIGH);
		case QueueItemBase::HIGHEST: return STRING(HIGHEST);
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
			tmp = STRING(NO_MATCHED_FILES);
		}
	}
	return tmp;
}

//fuldc ftp logger support
void AirUtil::fileEvent(const string& tgt, bool file /*false*/) {
#ifdef _WIN32
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

				boost::scoped_array<TCHAR> cmdLineBuf(new TCHAR[cmdLine.length() + 1]);
				_tcscpy(&cmdLineBuf[0], cmdLine.c_str());

				boost::scoped_array<TCHAR> cmdBuf(new TCHAR[cmd.length() + 1]);
				_tcscpy(&cmdBuf[0], cmd.c_str());

				if(::CreateProcess(&cmdBuf[0], &cmdLineBuf[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
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

			boost::scoped_array<TCHAR> cmdLineBuf(new TCHAR[cmdLine.length() + 1]);
			_tcscpy(&cmdLineBuf[0], cmdLine.c_str());

			boost::scoped_array<TCHAR> cmdBuf(new TCHAR[cmd.length() + 1]);
			_tcscpy(&cmdBuf[0], cmd.c_str());

			if(::CreateProcess(&cmdBuf[0], &cmdLineBuf[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
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
									if(Util::strnicmp(dir, _T("sample"), 6) == 0 ||
										Util::strnicmp(dir, _T("subs"), 4) == 0 ||
										Util::strnicmp(dir, _T("cover"), 5) == 0 ||
										Util::strnicmp(dir, _T("cd"), 2) == 0) {
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
#endif
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

void AirUtil::getRegexMatchesT(const tstring& aString, TStringList& l, const boost::wregex& aReg) {
	auto start = aString.begin();
	auto end = aString.end();
	boost::match_results<tstring::const_iterator> result;
	try {
		while(boost::regex_search(start, end, result, aReg, boost::match_default)) {
			l.emplace_back(tstring( result[0].first, result[0].second));
			start = result[0].second;
		}
	} catch(...) { 
		//...
	}
}

void AirUtil::getRegexMatches(const string& aString, StringList& l, const boost::regex& aReg) {
	auto start = aString.begin();
	auto end = aString.end();
	boost::match_results<string::const_iterator> result;
	try {
		while(boost::regex_search(start, end, result, aReg, boost::match_default)) {
			l.emplace_back(string( result[0].first, result[0].second));
			start = result[0].second;
		}
	} catch(...) { 
		//...
	}
}

const string AirUtil::getLinkUrl() {
	return R"(((?:[a-z][\w-]{0,10})?:/{1,3}|www\d{0,3}[.]|magnet:\?[^\s=]+=|spotify:|[a-z0-9.\-]+[.][a-z]{2,4}/)(?:[^\s()<>]+|\(([^\s()<>]+|(\([^\s()<>]+\)))*\))+(?:\(([^\s()<>]+|(\([^\s()<>]+\)))*\)|[^\s`()\[\]{};:'\".,<>?«»“”‘’]))";
}

const string AirUtil::getReleaseRegLong(bool chat) {
	if (chat)
		return R"(((?<=\s)|^)(?=\S*[A-Z]\S*)(([A-Z0-9]|\w[A-Z0-9])[A-Za-z0-9-]*)(\.|_|(-(?=\S*\d{4}\S+)))(\S+)-(\w{2,})(?=(\W)?\s|$))";
	else
		return R"((?=\S*[A-Z]\S*)(([A-Z0-9]|\w[A-Z0-9])[A-Za-z0-9-]*)(\.|_|(-(?=\S*\d{4}\S+)))(\S+)-(\w{2,}))";
}

const string AirUtil::getReleaseRegBasic() {
	return R"(((?=\S*[A-Za-z]\S*)[A-Z0-9]\S{3,})-([A-Za-z0-9_]{2,}))";
}

bool AirUtil::removeDirectoryIfEmptyRe(const string& aPath, int maxAttempts, int attempts) {
	/* recursive check for empty dirs */
	for(FileFindIter i(aPath, "*"); i != FileFindIter(); ++i) {
		try {
			if(i->isDirectory()) {
				if (i->getFileName().compare(".") == 0 || i->getFileName().compare("..") == 0)
					continue;

				string dir = aPath + i->getFileName() + PATH_SEPARATOR;
				if (!removeDirectoryIfEmptyRe(dir, maxAttempts, 0))
					return false;
			} else if (Util::getFileExt(i->getFileName()) == ".dctmp") {
				if (attempts == maxAttempts) {
					return false;
				}

				Thread::sleep(500);
				attempts++;
				return removeDirectoryIfEmptyRe(aPath, maxAttempts, attempts);
			} else {
				return false;
			}
		} catch(const FileException&) { } 
	}
	File::removeDirectory(aPath);
	return true;
}

void AirUtil::removeDirectoryIfEmpty(const string& tgt, int maxAttempts /*3*/, bool silent /*false*/) {
	if (!removeDirectoryIfEmptyRe(tgt, maxAttempts, 0) && !silent) {
		LogManager::getInstance()->message(STRING_F(DIRECTORY_NOT_REMOVED, tgt), LogManager::LOG_INFO);
	}
}

bool AirUtil::isAdcHub(const string& hubUrl) {
	if(Util::strnicmp("adc://", hubUrl.c_str(), 6) == 0) {
		return true;
	} else if(Util::strnicmp("adcs://", hubUrl.c_str(), 7) == 0) {
		return true;
	}
	return false;
}

bool AirUtil::isHubLink(const string& hubUrl) {
	return isAdcHub(hubUrl) || Util::strnicmp("dchub://", hubUrl.c_str(), 8) == 0;
}

string AirUtil::convertMovePath(const string& aPath, const string& aParent, const string& aTarget) {
	return aTarget + aPath.substr(aParent.length(), aPath.length() - aParent.length());
}

string AirUtil::regexEscape(const string& aStr, bool isWildcard) {
	if (aStr.empty())
		return Util::emptyString;

	//don't replace | and ? if it's wildcard
	static const boost::regex re_boostRegexEscape(isWildcard ? R"([\^\.\$\(\)\[\]\*\+\?\/\\])" : R"([\^\.\$\|\(\)\[\]\*\+\?\/\\])");
    const string rep("\\\\\\1&");
    string result = regex_replace(aStr, re_boostRegexEscape, rep, boost::match_default | boost::format_sed);
	if (isWildcard) {
		//convert * and ?
		boost::replace_all(result, "\\*", ".*");
		boost::replace_all(result, "\\?", ".");
		result = "^(" + result + ")$";
	}
    return result;
}

void AirUtil::setAway(AwayMode aAway) {
	if(aAway != away)
		ClientManager::getInstance()->infoUpdated();

	if((aAway == AWAY_MANUAL) || (getAwayMode() == AWAY_MANUAL && aAway == AWAY_OFF) ) //only save the state if away mode is set by user
		SettingsManager::getInstance()->set(SettingsManager::AWAY, aAway > 0);

	away = aAway;

	if (away > AWAY_OFF)
		awayTime = time(NULL);
}

string AirUtil::getAwayMessage(const string& aAwayMsg, ParamMap& params) { 
	params["idleTI"] = Util::formatSeconds(time(NULL) - awayTime);
	return Util::formatParams(aAwayMsg, params);
}

string AirUtil::subtractCommonDirs(const string& toCompare, const string& toSubtract, char separator) {
	if (toSubtract.length() > 3) {
		string::size_type i = toSubtract.length()-2;
		string::size_type j;
		for(;;) {
			j = toSubtract.find_last_of(separator, i);
			if(j == string::npos || (int)(toCompare.length() - (toSubtract.length() - j)) < 0) //also check if it goes out of scope for toCompare
				break;
			if(Util::stricmp(toSubtract.substr(j), toCompare.substr(toCompare.length() - (toSubtract.length()-j))) != 0)
				break;
			i = j - 1;
		}
		return toSubtract.substr(0, i+2);
	}
	return toSubtract;
}

pair<string, string::size_type> AirUtil::getDirName(const string& aPath, char separator) {
	if (aPath.size() < 3)
		return { aPath, false };

	//get the directory to search for
	bool isSub = false;
	string::size_type i = aPath.back() == separator ? aPath.size() - 2 : aPath.size() - 1, j;
	for (;;) {
		j = aPath.find_last_of(separator, i);
		if (j == string::npos) {
			j = 0;
			break;
		}

		//auto remoteDir = dir.substr(j + 1, i - j);
		if (!boost::regex_match(aPath.substr(j + 1, i - j), subDirRegPlain)) {
			j++;
			break;
		}

		isSub = true;
		i = j - 1;
	}

	//return { aPath.substr(j, i - j + 1), isSub };
	return make_pair(aPath.substr(j, i - j + 1), isSub ? i + 2 : string::npos);
}

string AirUtil::getTitle(const string& searchTerm) {
	auto ret = Text::toLower(searchTerm);

	//Remove group name
	size_t pos = ret.rfind("-");
	if (pos != string::npos)
		ret = ret.substr(0, pos);

	//replace . with space
	pos = 0;
	while ((pos = ret.find_first_of("._", pos)) != string::npos) {
		ret.replace(pos, 1, " ");
	}

	//remove words after year/episode
	boost::regex reg;
	reg.assign("(((\\[)?((19[0-9]{2})|(20[0-1][0-9]))|(s[0-9]([0-9])?(e|d)[0-9]([0-9])?)|(Season(\\.)[0-9]([0-9])?)).*)");

	boost::match_results<string::const_iterator> result;
	if (boost::regex_search(ret, result, reg, boost::match_default)) {
		ret = ret.substr(0, result.position());
	}

	//boost::regex_replace(ret, reg, Util::emptyStringT, boost::match_default | boost::format_sed);

	//remove extra words
	string extrawords [] = { "multisubs", "multi", "dvdrip", "dvdr", "real proper", "proper", "ultimate directors cut", "directors cut", "dircut", "x264", "pal", "complete", "limited", "ntsc", "bd25",
		"bd50", "bdr", "bd9", "retail", "bluray", "nordic", "720p", "1080p", "read nfo", "dts", "hdtv", "pdtv", "hddvd", "repack", "internal", "custom", "subbed", "unrated", "recut",
		"extended", "dts51", "finsub", "swesub", "dksub", "nosub", "remastered", "2disc", "rf", "fi", "swe", "stv", "r5", "festival", "anniversary edition", "bdrip", "ac3", "xvid",
		"ws", "int" };
	pos = 0;
	ret += ' ';
	auto arrayLength = sizeof (extrawords) / sizeof (*extrawords);
	while (pos < arrayLength) {
		boost::replace_all(ret, " " + extrawords[pos] + " ", " ");
		pos++;
	}

	//trim spaces from the end
	boost::trim_right(ret);
	return ret;
}

}