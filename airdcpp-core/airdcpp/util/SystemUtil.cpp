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

#include <airdcpp/util/SystemUtil.h>
#include <airdcpp/util/Util.h>

#include <boost/algorithm/string/trim.hpp>

#ifdef _WIN32

#include <airdcpp/core/header/w.h>
#include <shellapi.h>
#include <VersionHelpers.h>

#endif

#include <airdcpp/util/PathUtil.h>
#include <airdcpp/core/classes/ScopedFunctor.h>

#include <random>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/utsname.h>
#include <ctype.h>

#endif

namespace dcpp {

#ifdef _WIN32

string SystemUtil::getSystemUsername() noexcept {
	DWORD size = 0;
	::GetUserName(0, &size);
	if (size > 1) {
		tstring str(size - 1, 0);
		if (::GetUserName(&str[0], &size)) {
			return Text::fromT(str);
		}
	}

	return "airdcpp";
}

int SystemUtil::runSystemCommand(const string& aCommand) noexcept {
	// std::system would flash a console window

	STARTUPINFOW si;
	PROCESS_INFORMATION pi;

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	if (!CreateProcessW(NULL, (LPWSTR)Text::toT(aCommand).c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
		return -1;
	}

	WaitForSingleObject(pi.hProcess, INFINITE);

	DWORD exitCode = 0;
	if (!GetExitCodeProcess(pi.hProcess, &exitCode)) {
		return -1;
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return exitCode;
}

#else

string SystemUtil::getSystemUsername() noexcept {
	char buf[64] = { 0 };
	if (getlogin_r(buf, sizeof(buf) - 1) != 0) {
		return "airdcpp";
	}

	return buf;
}

int SystemUtil::runSystemCommand(const string& aCommand) noexcept {
	return std::system(aCommand.c_str());
}
	
#endif

string SystemUtil::translateError(int aError) noexcept {
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
	return strerror(aError);
#endif // _WIN32
}

string SystemUtil::formatLastError() noexcept {
#ifdef _WIN32
	int error = GetLastError();
#else
	int error = errno;
#endif
	return translateError(error);
}

bool SystemUtil::isOSVersionOrGreater(int major, int minor) noexcept {
#ifdef _WIN32
	return IsWindowsVersionOrGreater((WORD)major, (WORD)minor, 0);
#else // _WIN32
	return true;
#endif
}

string SystemUtil::getOsVersion(bool http /*false*/) noexcept {
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