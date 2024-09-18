/*
 * Copyright (C) 2012-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_UPDATER_CREATOR_H
#define DCPLUSPLUS_DCPP_UPDATER_CREATOR_H

#include <airdcpp/typedefs.h>

namespace dcpp {

#ifndef NO_CLIENT_UPDATER

class UpdaterCreator {

public:
	static void signVersionFile(const string& aVersionFilePath, const string& aPrivateKeyFilePath, bool aMakeHeader = false);

	using FileListF = std::function<void(StringPairList&, const string&)>;
	// Create an updater zip file from the current application (it must be in the default "compiled" path)
	// Returns the path of the created updater file
	static string createUpdate(const FileListF& aFileListF) noexcept;

	static void updateVersionFile(const string& aUpdaterPath);
};

#endif

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_UPDATER_CREATOR_H