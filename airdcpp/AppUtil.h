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

#ifndef DCPLUSPLUS_DCPP_APP_UTIL_H
#define DCPLUSPLUS_DCPP_APP_UTIL_H

#include <airdcpp/compiler.h>
#include <airdcpp/constants.h>
#include <airdcpp/typedefs.h>

namespace dcpp {

class AppUtil  
{
public:
	enum Paths {
		/** Global configuration */
		PATH_GLOBAL_CONFIG,
		/** Per-user configuration (queue, favorites, ...) */
		PATH_USER_CONFIG,
		/** Language files */
		PATH_USER_LOCAL,
		/** Various resources (help files etc) */
		PATH_RESOURCES,
		/** Translations */
		PATH_LOCALE,
		/** Default download location */
		PATH_DOWNLOADS,
		/** Default file list location */
		PATH_FILE_LISTS,
		/** XML files for each bundle */
		PATH_BUNDLES,
		/** XML files for cached share structure */
		PATH_SHARECACHE,
		/** Temp files (viewed files, temp shared items...) */
		PATH_TEMP,

		PATH_LAST
	};

	// The client uses regular config directories or boot config file to determine the config path
	// if a custom path isn't provided
	static void initialize(const string& aConfigPath);

	static string getAppFilePath() noexcept;
	static string getAppFileName() noexcept;
	static string getAppPath() noexcept;
#ifndef _WIN32 
	static std::string appPath;
	static void setApp(const string& app) noexcept;
#endif

	/** Path of temporary storage */
	static string getOpenPath() noexcept;

	/** Path of configuration files */
	static const string& getPath(Paths path) noexcept { return paths[path]; }

	/** Migrate from pre-localmode config location */
	static void migrate(const string& file) noexcept;
	static void migrate(const string& aDir, const string& aPattern) noexcept;

	/** Path of file lists */
	static string getListPath() noexcept { return getPath(PATH_FILE_LISTS); }
	/** Path of bundles */
	static string getBundlePath() noexcept { return getPath(PATH_BUNDLES); }

	static bool usingLocalMode() noexcept { return localMode; }
	static bool wasUncleanShutdown;
private:
	/** In local mode, all config and temp files are kept in the same dir as the executable */
	static bool localMode;

	static string paths[PATH_LAST];
	
	static bool loadBootConfig(const string& aDirectoryPath) noexcept;

	static int osMinor;
	static int osMajor;
};

} // namespace dcpp

#endif // !defined(UTIL_H)
