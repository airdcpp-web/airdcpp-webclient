/*
 * Copyright (C) 2011-2019 AirDC++ Project
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

#include "ConnectivityManager.h"
#include "File.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "ShareManager.h"
#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "ThrottleManager.h"
#include "Util.h"

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

boost::regex AirUtil::releaseRegBasic;
boost::regex AirUtil::releaseRegChat;
boost::regex AirUtil::subDirRegPlain;
boost::regex AirUtil::crcReg;
boost::regex AirUtil::lineBreakRegex;
boost::regex AirUtil::urlReg;

AirUtil::TimeCounter::TimeCounter(string aMsg) : start(GET_TICK()), msg(move(aMsg)) {

}

AirUtil::TimeCounter::~TimeCounter() {
	auto end = GET_TICK();
	LogManager::getInstance()->message(msg + ", took " + Util::toString(end - start) + " ms", LogMessage::SEV_INFO);
}

StringList AirUtil::getAdcDirectoryDupePaths(DupeType aType, const string& aAdcPath) {
	StringList ret;
	if (isShareDupe(aType)) {
		ret = ShareManager::getInstance()->getAdcDirectoryPaths(aAdcPath);
	} else {
		ret = QueueManager::getInstance()->getAdcDirectoryPaths(aAdcPath);
	}

	return ret;
}

StringList AirUtil::getFileDupePaths(DupeType aType, const TTHValue& aTTH) {
	StringList ret;
	if (isShareDupe(aType)) {
		ret = ShareManager::getInstance()->getRealPaths(aTTH);
	} else {
		ret = QueueManager::getInstance()->getTargets(aTTH);
	}

	return ret;
}

bool AirUtil::isShareDupe(DupeType aType) noexcept { 
	return aType == DUPE_SHARE_FULL || aType == DUPE_SHARE_PARTIAL; 
}

bool AirUtil::isQueueDupe(DupeType aType) noexcept {
	return aType == DUPE_QUEUE_FULL || aType == DUPE_QUEUE_PARTIAL;
}

bool AirUtil::isFinishedDupe(DupeType aType) noexcept {
	return aType == DUPE_FINISHED_FULL || aType == DUPE_FINISHED_PARTIAL;
}

DupeType AirUtil::checkAdcDirectoryDupe(const string& aAdcPath, int64_t aSize) {
	auto dupe = ShareManager::getInstance()->isAdcDirectoryShared(aAdcPath, aSize);
	if (dupe != DUPE_NONE) {
		return dupe;
	}

	return QueueManager::getInstance()->isAdcDirectoryQueued(aAdcPath, aSize);
}

string AirUtil::toOpenFileName(const string& aFileName, const TTHValue& aTTH) noexcept {
	return aTTH.toBase32() + "_" + Util::validateFileName(aFileName);
}

DupeType AirUtil::checkFileDupe(const TTHValue& aTTH) {
	if (ShareManager::getInstance()->isFileShared(aTTH)) {
		return DUPE_SHARE_FULL;
	}

	return QueueManager::getInstance()->isFileQueued(aTTH);
}

bool AirUtil::allowOpenDupe(DupeType aType) noexcept {
	return aType != DUPE_NONE;
}

TTHValue AirUtil::getTTH(const string& aFileName, int64_t aSize) noexcept {
	TigerHash tmp;
	string str = Text::toLower(aFileName) + Util::toString(aSize);
	tmp.update(str.c_str(), str.length());
	return TTHValue(tmp.finalize());
}

TTHValue AirUtil::getPathId(const string& aPath) noexcept {
	TigerHash tmp;
	auto str = Text::toLower(aPath);
	tmp.update(str.c_str(), str.length());
	return TTHValue(tmp.finalize());
}

void AirUtil::init() {
	releaseRegBasic.assign(getReleaseRegBasic());
	releaseRegChat.assign(getReleaseRegLong(true));
	urlReg.assign(getUrlReg());
	subDirRegPlain.assign(getSubDirReg(), boost::regex::icase);
	crcReg.assign(R"(.{5,200}\s(\w{8})$)");
	lineBreakRegex.assign(R"(\n|\r)");


#if defined _WIN32 && defined _DEBUG
	dcassert(AirUtil::isParentOrExactLocal(R"(C:\Projects\)", R"(C:\Projects\)"));
	dcassert(AirUtil::isParentOrExactLocal(R"(C:\Projects\)", R"(C:\Projects\test)"));
	dcassert(AirUtil::isParentOrExactLocal(R"(C:\Projects)", R"(C:\Projects\test)"));
	dcassert(AirUtil::isParentOrExactLocal(R"(C:\Projects\)", R"(C:\Projects\test)"));
	dcassert(!AirUtil::isParentOrExactLocal(R"(C:\Projects)", R"(C:\Projectstest)"));
	dcassert(!AirUtil::isParentOrExactLocal(R"(C:\Projectstest)", R"(C:\Projects)"));
	dcassert(!AirUtil::isParentOrExactLocal(R"(C:\Projects\test)", ""));
	dcassert(AirUtil::isParentOrExactLocal("", R"(C:\Projects\test)"));

	dcassert(!AirUtil::isSubLocal(R"(C:\Projects\)", R"(C:\Projects\)"));
	dcassert(AirUtil::isSubLocal(R"(C:\Projects\test)", R"(C:\Projects\)"));
	dcassert(AirUtil::isSubLocal(R"(C:\Projects\test)", R"(C:\Projects)"));
	dcassert(!AirUtil::isSubLocal(R"(C:\Projectstest)", R"(C:\Projects)"));
	dcassert(!AirUtil::isSubLocal(R"(C:\Projects)", R"(C:\Projectstest)"));
	dcassert(AirUtil::isSubLocal(R"(C:\Projects\test)", ""));
	dcassert(!AirUtil::isSubLocal("", R"(C:\Projects\test)"));

	dcassert(AirUtil::compareFromEndAdc(R"(Downloads\1\)", R"(/Downloads/1/)") == 0);
	dcassert(AirUtil::compareFromEndAdc(R"(Downloads\1\)", R"(/Download/1/)") == 10);

	dcassert(AirUtil::compareFromEndAdc(R"(E:\Downloads\Projects\CD1\)", R"(/CD1/)") == 0);
	dcassert(AirUtil::compareFromEndAdc(R"(E:\Downloads\1\)", R"(/1/)") == 0);
	dcassert(AirUtil::compareFromEndAdc(R"(/Downloads/Projects/CD1/)", R"(/cd1/)") == 0);
	dcassert(AirUtil::compareFromEndAdc(R"(/Downloads/1/)", R"(/1/)") == 0);


	// MATCH PATHS (NMDC)
	dcassert(AirUtil::getAdcMatchPath(R"(/SHARE/Random/CommonSub/File1.zip)", R"(E:\Downloads\Bundle\CommonSub\File1.zip)", R"(E:\Downloads\Bundle\)", true) == ADC_ROOT_STR);
	dcassert(AirUtil::getAdcMatchPath(R"(/SHARE/Bundle/Bundle/CommonSub/File1.zip)", R"(E:\Downloads\Bundle\CommonSub\File1.zip)", R"(E:\Downloads\Bundle\)", true) == R"(E:\Downloads\Bundle\)");

	// MATCH PATHS (ADC)

	// Different remote bundle path
	dcassert(AirUtil::getAdcMatchPath(R"(/SHARE/Bundle/RandomRemoteDir/File1.zip)", R"(E:\Downloads\Bundle\RandomLocalDir\File1.zip)", R"(E:\Downloads\Bundle\)", false) == R"(/SHARE/Bundle/RandomRemoteDir/)");
	dcassert(AirUtil::getAdcMatchPath(R"(/SHARE/RandomRemoteBundle/File1.zip)", R"(E:\Downloads\Bundle\File1.zip)", R"(E:\Downloads\Bundle\)", false) == R"(/SHARE/RandomRemoteBundle/)");

	// Common directory name for file parent
	dcassert(AirUtil::getAdcMatchPath(R"(/SHARE/Bundle/RandomRemoteDir/CommonSub/File1.zip)", R"(E:\Downloads\Bundle\RandomLocalDir\CommonSub\File1.zip)", R"(E:\Downloads\Bundle\)", false) == R"(/SHARE/Bundle/RandomRemoteDir/CommonSub/)");

	// Subpath is shorter than subdir in main
	dcassert(AirUtil::getAdcMatchPath(R"(/CommonSub/File1.zip)", R"(E:\Downloads\Bundle\RandomLocalDir\CommonSub\File1.zip)", R"(E:\Downloads\Bundle\)", false) == R"(/CommonSub/)");

	// Exact match
	dcassert(AirUtil::getAdcMatchPath(R"(/CommonParent/Bundle/Common/File1.zip)", R"(E:\CommonParent\Bundle\Common\File1.zip)", R"(E:\CommonParent\Bundle\)", false) == R"(/CommonParent/Bundle/)");

	// Short parent
	dcassert(AirUtil::getAdcMatchPath(R"(/1/File1.zip)", R"(E:\Bundle\File1.zip)", R"(E:\Bundle\)", false) == R"(/1/)");

	// Invalid path 1 (the result won't matter, just don't crash here)
	dcassert(AirUtil::getAdcMatchPath(R"(File1.zip)", R"(E:\Bundle\File1.zip)", R"(E:\Bundle\)", false) == R"(File1.zip)");

	// Invalid path 2 (the result won't matter, just don't crash here)
	dcassert(AirUtil::getAdcMatchPath(R"(/File1.zip)", R"(E:\Bundle\File1.zip)", R"(E:\Bundle\)", false) == R"(/)");
#endif
}

AirUtil::AdapterInfoList AirUtil::getBindAdapters(bool v6) {
	// Get the addresses and sort them
	auto bindAddresses = getNetworkAdapters(v6);
	sort(bindAddresses.begin(), bindAddresses.end(), [](const AdapterInfo& lhs, const AdapterInfo& rhs) {
		if (lhs.adapterName.empty() && rhs.adapterName.empty()) {
			return Util::stricmp(lhs.ip, rhs.ip) < 0;
		}

		return Util::stricmp(lhs.adapterName, rhs.adapterName) < 0;
	});

	// "Any" adapter
	bindAddresses.emplace(bindAddresses.begin(), STRING(ANY), v6 ? "::" : "0.0.0.0", static_cast<uint8_t>(0));

	// Current address not listed?
	const auto& setting = v6 ? SETTING(BIND_ADDRESS6) : SETTING(BIND_ADDRESS);
	auto cur = boost::find_if(bindAddresses, [&setting](const AirUtil::AdapterInfo& aInfo) { return aInfo.ip == setting; });
	if (cur == bindAddresses.end()) {
		bindAddresses.emplace_back(STRING(UNKNOWN), setting, static_cast<uint8_t>(0));
		cur = bindAddresses.end() - 1;
	}

	return bindAddresses;
}

AirUtil::AdapterInfoList AirUtil::getNetworkAdapters(bool v6) {
	AdapterInfoList adapterInfos;

#ifdef _WIN32
	ULONG len =	15360; //"The recommended method of calling the GetAdaptersAddresses function is to pre-allocate a 15KB working buffer pointed to by the AdapterAddresses parameter"
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

						adapterInfos.emplace_back(Text::fromT(tstring(pAdapterInfo->FriendlyName)), buf, ua->OnLinkPrefixLength);
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
	struct ifaddrs* ifap;

	if (getifaddrs(&ifap) == 0) {
		for (struct ifaddrs* i = ifap; i != NULL; i = i->ifa_next) {
			struct sockaddr* sa = i->ifa_addr;

			// If the interface is up, is not a loopback and it has an address
			if ((i->ifa_flags & IFF_UP) && !(i->ifa_flags & IFF_LOOPBACK) && sa != NULL) {
				void* src = nullptr;
				socklen_t len;

				if (!v6 && sa->sa_family == AF_INET) {
					// IPv4 address
					struct sockaddr_in* sai = (struct sockaddr_in*)sa;
					src = (void*)&(sai->sin_addr);
					len = INET_ADDRSTRLEN;
				}
				else if (v6 && sa->sa_family == AF_INET6) {
					// IPv6 address
					struct sockaddr_in6* sai6 = (struct sockaddr_in6*)sa;
					src = (void*)&(sai6->sin6_addr);
					len = INET6_ADDRSTRLEN;
				}

				// Convert the binary address to a string and add it to the output list
				if (src) {
					char address[len];
					inet_ntop(sa->sa_family, src, address, len);
					// TODO: get the prefix
					adapterInfos.emplace_back("Unknown", (string)address, 0);
				}
}
		}
		freeifaddrs(ifap);
	}
#endif

#endif

	return adapterInfos;
}

string AirUtil::getLocalIp(bool v6) noexcept {
	const auto& bindAddr = v6 ? CONNSETTING(BIND_ADDRESS6) : CONNSETTING(BIND_ADDRESS);
	if (!bindAddr.empty() && bindAddr != SettingsManager::getInstance()->getDefault(v6 ? SettingsManager::BIND_ADDRESS6 : SettingsManager::BIND_ADDRESS)) {
		return bindAddr;
	}

	// No bind address configured, try to find a public address
	auto adapters = getNetworkAdapters(v6);
	if (adapters.empty()) {
		return Util::emptyString;
	}

	auto p = boost::find_if(adapters, [v6](const AdapterInfo& aAdapterInfo) { return Util::isPublicIp(aAdapterInfo.ip, v6); });
	if (p != adapters.end()) {
		return p->ip;
	}

	return adapters.front().ip;
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
		slots= static_cast<int>((speed/10)-1);
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


int AirUtil::getSlots(bool aIsDownload, double aValue, SettingsManager::SettingProfile aProfile) {
	if (!SETTING(DL_AUTODETECT) && aValue == 0 && aIsDownload) {
		//LogManager::getInstance()->message("Slots1");
		return SETTING(DOWNLOAD_SLOTS);
	} else if (!SETTING(UL_AUTODETECT) && aValue == 0 && !aIsDownload) {
		//LogManager::getInstance()->message("Slots2");
		return SETTING(UPLOAD_SLOTS);
	}

	double speed;
	if (aValue != 0) {
		speed = aValue;
	} else if (aIsDownload) {
		int limit = SETTING(AUTO_DETECTION_USE_LIMITED) ? ThrottleManager::getInstance()->getDownLimit() : 0;
		speed = limit > 0 ? (limit * 8.00) / 1024.00 : Util::toDouble(SETTING(DOWNLOAD_SPEED));
	} else {
		int limit = SETTING(AUTO_DETECTION_USE_LIMITED) ? ThrottleManager::getInstance()->getUpLimit() : 0;
		speed = limit > 0 ? (limit * 8.00) / 1024.00 : Util::toDouble(SETTING(UPLOAD_SPEED));
	}

	int slots = 3;

	// Don't try to understand the formula used in here
	bool rar = aProfile == SettingsManager::PROFILE_RAR;
	if (speed <= 1) {
		if (rar) {
			slots=1;
		} else {
			aIsDownload ? slots=6 : slots=2;
		}
	} else if (speed > 1 && speed <= 2.5) {
		if (rar) {
			slots=2;
		} else {
			aIsDownload ? slots=15 : slots=3;
		}
	} else if (speed > 2.5 && speed <= 4) {
		if (rar) {
			aIsDownload ? slots=3 : slots=2;
		} else {
			aIsDownload ? slots=15 : slots=4;
		}
	} else if (speed > 4 && speed <= 6) {
		if (rar) {
			aIsDownload ? slots=3 : slots=3;
		} else {
			aIsDownload ? slots=20 : slots=5;
		}
	} else if (speed > 6 && speed < 10) {
		if (rar) {
			aIsDownload ? slots=5 : slots=3;
		} else {
			aIsDownload ? slots=20 : slots=6;
		}
	} else if (speed >= 10 && speed <= 50) {
		if (rar) {
			speed <= 20 ?  slots=4 : slots=5;
			if (aIsDownload) {
				slots=slots+3;
			}
		} else {
			aIsDownload ? slots=30 : slots=8;
		}
	} else if(speed > 50 && speed < 100) {
		if (rar) {
			slots= static_cast<int>(speed / 10);
			if (aIsDownload)
				slots=slots+4;
		} else {
			aIsDownload ? slots=40 : slots=12;
		}
	} else if (speed >= 100) {
		// Curves: https://www.desmos.com/calculator/vfywkguiej
		if (rar) {
			if (aIsDownload) {
				slots = static_cast<int>(ceil((log(speed + 750) - 6.61) * 100));
			} else {
				slots = static_cast<int>(ceil((log(speed + 70.0) - 4.4) * 10));
			}
		} else {
			if (aIsDownload) {
				slots = static_cast<int>((speed * 0.10) + 40);
			} else {
				slots = static_cast<int>((speed * 0.04) + 15);
			}
		}
	}

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

	return static_cast<int>(download ? value*105 : value*60);
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

string AirUtil::getPrioText(Priority aPriority) noexcept {
	switch(aPriority) {
		case Priority::PAUSED_FORCE: return STRING(PAUSED_FORCED);
		case Priority::PAUSED: return STRING(PAUSED);
		case Priority::LOWEST: return STRING(LOWEST);
		case Priority::LOW: return STRING(LOW);
		case Priority::NORMAL: return STRING(NORMAL);
		case Priority::HIGH: return STRING(HIGH);
		case Priority::HIGHEST: return STRING(HIGHEST);
		default: return STRING(PAUSED);
	}
}

bool AirUtil::listRegexMatch(const StringList& l, const boost::regex& aReg) {
	return find_if(l.begin(), l.end(), [&](const string& s) { return regex_match(s, aReg); } ) != l.end();
}

int AirUtil::listRegexCount(const StringList& l, const boost::regex& aReg) {
	return static_cast<int>(count_if(l.begin(), l.end(), [&](const string& s) { return regex_match(s, aReg); } ));
}

void AirUtil::listRegexSubtract(StringList& l, const boost::regex& aReg) {
	l.erase(remove_if(l.begin(), l.end(), [&](const string& s) { return regex_match(s, aReg); }), l.end());
}

string AirUtil::formatMatchResults(int aMatchingFiles, int aNewFiles, const BundleList& aBundles) noexcept {
	if (aMatchingFiles > 0) {
		if (aBundles.size() == 1) {
			return STRING_F(MATCHED_FILES_BUNDLE, aMatchingFiles % aBundles.front()->getName().c_str() % aNewFiles);
		} else {
			return STRING_F(MATCHED_FILES_X_BUNDLES, aMatchingFiles % (int)aBundles.size() % aNewFiles);
		}
	}

	return STRING(NO_MATCHED_FILES);;
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

bool AirUtil::isRelease(const string& aString) {

	try {
		return boost::regex_match(aString, releaseRegBasic);
	}
	catch (...) {}

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

const string AirUtil::getUrlReg() noexcept {
	return R"(((?:[a-z][\w-]{0,10})?:/{1,3}|www\d{0,3}[.]|magnet:\?[^\s=]+=|spotify:|[a-z0-9.\-]+[.][a-z]{2,4}/)(?:[^\s()<>]+|\(([^\s()<>]+|(\([^\s()<>]+\)))*\))+(?:\(([^\s()<>]+|(\([^\s()<>]+\)))*\)|[^\s`()\[\]{};:'\".,<>?«»“”‘’]))";
}

const string AirUtil::getReleaseRegLong(bool chat) noexcept {
	if (chat)
		return R"(((?<=\s)|^)(?=\S*[A-Z]\S*)(([A-Z0-9]|\w[A-Z0-9])[A-Za-z0-9-]*)(\.|_|(-(?=\S*\d{4}\S+)))(\S+)-(\w{2,})(?=(\W)?\s|$))";
	else
		return R"((?=\S*[A-Z]\S*)(([A-Z0-9]|\w[A-Z0-9])[A-Za-z0-9-]*)(\.|_|(-(?=\S*\d{4}\S+)))(\S+)-(\w{2,}))";
}

const string AirUtil::getReleaseRegBasic() noexcept {
	return R"(((?=\S*[A-Za-z]\S*)[A-Z0-9]\S{3,})-([A-Za-z0-9_]{2,}))";
}

const string AirUtil::getSubDirReg() noexcept {
	return R"((((S(eason)?)|DVD|CD|(D|DIS(K|C))).?([0-9](0-9)?))|Sample.?|Proof.?|Cover.?|.{0,5}Sub(s|pack)?)";
}

string AirUtil::getReleaseDir(const string& aDir, bool cut, const char separator) noexcept {
	auto p = getDirName(Util::getFilePath(aDir, separator), separator);
	if (cut) {
		return p.first;
	}

	// return with the path
	return p.second == string::npos ? aDir : aDir.substr(0, p.second);
}

bool AirUtil::removeDirectoryIfEmptyRe(const string& aPath, int aMaxAttempts, int aAttempts) {
	/* recursive check for empty dirs */
	for(FileFindIter i(aPath, "*"); i != FileFindIter(); ++i) {
		try {
			if(i->isDirectory()) {
				string dir = aPath + i->getFileName() + PATH_SEPARATOR;
				if (!removeDirectoryIfEmptyRe(dir, aMaxAttempts, 0))
					return false;
			} else if (Util::getFileExt(i->getFileName()) == ".dctmp") {
				if (aAttempts == aMaxAttempts) {
					return false;
				}

				Thread::sleep(500);
				return removeDirectoryIfEmptyRe(aPath, aMaxAttempts, aAttempts + 1);
			} else {
				return false;
			}
		} catch(const FileException&) { } 
	}

	File::removeDirectory(aPath);
	return true;
}

void AirUtil::removeDirectoryIfEmpty(const string& aPath, int aMaxAttempts /*3*/, bool aSilent /*false*/) {
	if (!removeDirectoryIfEmptyRe(aPath, aMaxAttempts, 0) && !aSilent) {
		LogManager::getInstance()->message(STRING_F(DIRECTORY_NOT_REMOVED, aPath), LogMessage::SEV_INFO);
	}
}

bool AirUtil::isAdcHub(const string& aHubUrl) noexcept {
	if(Util::strnicmp("adc://", aHubUrl.c_str(), 6) == 0) {
		return true;
	} else if(Util::strnicmp("adcs://", aHubUrl.c_str(), 7) == 0) {
		return true;
	}
	return false;
}

bool AirUtil::isSecure(const string& aHubUrl) noexcept {
	return Util::strnicmp("adcs://", aHubUrl.c_str(), 7) == 0 || Util::strnicmp("nmdcs://", aHubUrl.c_str(), 8) == 0;
}

bool AirUtil::isHubLink(const string& aHubUrl) noexcept {
	return isAdcHub(aHubUrl) || Util::strnicmp("dchub://", aHubUrl.c_str(), 8) == 0;
}

string AirUtil::regexEscape(const string& aStr, bool aIsWildcard) noexcept {
	if (aStr.empty())
		return Util::emptyString;

	//don't replace | and ? if it's wildcard
	static const boost::regex re_boostRegexEscape(aIsWildcard ? R"([\^\.\$\(\)\[\]\*\+\?\/\\])" : R"([\^\.\$\|\(\)\[\]\*\+\?\/\\])");
    const string rep("\\\\\\1&");
    string result = regex_replace(aStr, re_boostRegexEscape, rep, boost::match_default | boost::format_sed);
	if (aIsWildcard) {
		//convert * and ?
		boost::replace_all(result, "\\*", ".*");
		boost::replace_all(result, "\\?", ".");
		result = "^(" + result + ")$";
	}
    return result;
}

string AirUtil::subtractCommonParents(const string& aToCompare, const StringList& aToSubtract) noexcept {
	StringList converted;
	for (const auto& p : aToSubtract) {
		if (p.length() > aToCompare.length()) {
			converted.push_back(p.substr(aToCompare.length()));
		}
	}

	return Util::listToString(converted);
}

string AirUtil::subtractCommonDirs(const string& toCompare, const string& toSubtract, char separator) noexcept {
	auto res = compareFromEnd(toCompare, toSubtract, separator);
	if (res == string::npos) {
		return toSubtract;
	}

	return toSubtract.substr(0, res);
}

string AirUtil::getLastCommonDirectoryPathFromSub(const string& aMainPath, const string& aSubPath, char aSubSeparator, size_t aMainBaseLength) noexcept {
	auto pos = AirUtil::compareFromEnd(aMainPath, aSubPath, aSubSeparator);

	// Get the next directory
	if (pos < aSubPath.length()) {
		auto pos2 = aSubPath.find(aSubSeparator, pos + 1);
		if (pos2 != string::npos) {
			pos = pos2 + 1;
		}
	}

	auto mainSubSectionLength = aMainPath.length() - aMainBaseLength;
	return aSubPath.substr(0, max(pos, aSubPath.length() > mainSubSectionLength ? aSubPath.length() - mainSubSectionLength : 0));
}

size_t AirUtil::compareFromEnd(const string& aMainPath, const string& aSubPath, char aSubSeparator) noexcept {
	if (aSubPath.length() > 1) {
		string::size_type i = aSubPath.length() - 2;
		string::size_type j;
		for (;;) {
			j = aSubPath.find_last_of(aSubSeparator, i);
			if (j == string::npos) {
				j = 0; // compare from beginning
			} else {
				j++;
			}

			if (static_cast<int>(aMainPath.length() - (aSubPath.length() - j)) < 0)
				break; // out of scope for aMainPath

			if (Util::stricmp(aSubPath.substr(j, i - j + 1), aMainPath.substr(aMainPath.length() - (aSubPath.length() - j), i - j + 1)) != 0)
				break;

			if (j <= 1) {
				// Fully matched
				return 0;
			}

			i = j - 2;
		}

		return i + 2;
	}

	return aSubPath.length();
}


string AirUtil::getAdcMatchPath(const string& aRemoteFile, const string& aLocalFile, const string& aLocalBundlePath, bool aNmdc) noexcept {
	if (aNmdc) {
		// For simplicity, only perform the path comparison for ADC results
		if (Text::toLower(aRemoteFile).find(Text::toLower(Util::getLastDir(aLocalBundlePath))) != string::npos) {
			return aLocalBundlePath;
		}

		return ADC_ROOT_STR;
	}

	// Get last matching directory for matching recursive filelist from the user
	auto remoteFileDir = Util::getAdcFilePath(aRemoteFile);
	auto localBundleFileDir = Util::getFilePath(aLocalFile);
	return AirUtil::getLastCommonAdcDirectoryPathFromSub(localBundleFileDir, remoteFileDir, aLocalBundlePath.length());
}

pair<string, string::size_type> AirUtil::getDirName(const string& aPath, char aSeparator) noexcept {
	if (aPath.size() < 3)
		return { aPath, false };

	//get the directory to search for
	bool isSub = false;
	string::size_type i = aPath.back() == aSeparator ? aPath.size() - 2 : aPath.size() - 1, j;
	for (;;) {
		j = aPath.find_last_of(aSeparator, i);
		if (j == string::npos) {
			j = 0;
			break;
		}

		if (!boost::regex_match(aPath.substr(j + 1, i - j), subDirRegPlain)) {
			j++;
			break;
		}

		isSub = true;
		i = j - 1;
	}

	return { aPath.substr(j, i - j + 1), isSub ? i + 2 : string::npos };
}

string AirUtil::getTitle(const string& searchTerm) noexcept {
	auto ret = Text::toLower(searchTerm);

	//Remove group name
	size_t pos = ret.rfind('-');
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

/* returns true if aDir is a subdir of aParent */
bool AirUtil::isSub(const string& aTestSub, const string& aParent, const char separator) noexcept {
	if (aTestSub.length() <= aParent.length())
		return false;

	if (Util::strnicmp(aTestSub, aParent, aParent.length()) != 0)
		return false;

	// either the parent must end with a separator or it must follow in the subdirectory
	return aParent.empty() || aParent.back() == separator || aTestSub[aParent.length()] == separator;
}

/* returns true if aSub is a subdir of aDir OR both are the same dir */
bool AirUtil::isParentOrExact(const string& aTestParent, const string& aSub, const char aSeparator) noexcept {
	if (aSub.length() < aTestParent.length())
		return false;

	if (Util::strnicmp(aSub, aTestParent, aTestParent.length()) != 0)
		return false;

	// either the parent must end with a separator or it must follow in the subdirectory
	return aTestParent.empty() || aTestParent.length() == aSub.length() || aTestParent.back() == aSeparator || aSub[aTestParent.length()] == aSeparator;
}


bool AirUtil::isParentOrExactLower(const string& aParentLower, const string& aSubLower, const char aSeparator) noexcept {
	if (aSubLower.length() < aParentLower.length())
		return false;

	if (strncmp(aSubLower.c_str(), aParentLower.c_str(), aParentLower.length()) != 0) {
		return false;
	}

	// either the parent must end with a separator or it must follow in the subdirectory
	return aParentLower.empty() || aParentLower.length() == aSubLower.length() || aParentLower.back() == aSeparator || aSubLower[aParentLower.length()] == aSeparator;
}

}
