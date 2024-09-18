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

#ifndef DCPLUSPLUS_WEBSERVER_NPMREPOSITORY_H
#define DCPLUSPLUS_WEBSERVER_NPMREPOSITORY_H

#include "forward.h"

#include <airdcpp/CriticalSection.h>
#include <airdcpp/Message.h>
#include <airdcpp/Singleton.h>

namespace dcpp {
	struct HttpDownload;
}

namespace webserver {
	class NpmRepository {
	public:
		static const string repository;

		using InstallF = std::function<void (const string& /*id*/, const string& /*url*/, const string& /*sha*/)>;

		NpmRepository(InstallF&& aInstallF, ModuleLogger&& aLoggerF);
		~NpmRepository();

		void checkUpdates(const string& aName, const string& aCurrentVersion) noexcept;
		void install(const string& aName) noexcept;

		NpmRepository(NpmRepository&) = delete;
		NpmRepository& operator=(NpmRepository&) = delete;
	private:
		void onPackageInfoDownloaded(const string& aName, const string& aCurrentVersion) noexcept;
		void checkPackageData(const string& aPackageData, const string& aName, const string& aCurrentVersion) const;

		using HttpDownloadMap = map<string, shared_ptr<HttpDownload>>;
		HttpDownloadMap httpDownloads;

		mutable SharedMutex cs;

		const InstallF installF;
		const ModuleLogger loggerF;
	};
}

#endif