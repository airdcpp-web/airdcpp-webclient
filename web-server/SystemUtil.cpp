/*
* Copyright (C) 2011-2024 AirDC++ Project
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
#include <web-server/SystemUtil.h>

#include <airdcpp/util/text/Text.h>

namespace webserver {
	string SystemUtil::getHostname() noexcept {
#ifdef _WIN32
		TCHAR computerName[1024];
		DWORD size = 1024;
		GetComputerName(computerName, &size);
		return Text::fromT(computerName);
#else
		char hostname[128];
		gethostname(hostname, sizeof hostname);
		return hostname;
#endif
	}

	string SystemUtil::getPlatform() noexcept {
#ifdef _WIN32
		return "win32";
#elif APPLE
		return "darwin";
#elif __linux__
		return "linux";
#elif __FreeBSD__
		return "freebsd";
#else
		return "other";
#endif
	}
}