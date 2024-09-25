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

#include <airdcpp/ErrorCollector.h>

#include <airdcpp/ResourceManager.h>
#include <airdcpp/Util.h>

namespace dcpp {

ErrorCollector::ErrorCollector(int aTotalFileCount) : totalFileCount(aTotalFileCount) { }

void ErrorCollector::add(const string& aError, const string& aFile, bool aIsMinor) noexcept {
	errors.emplace(aError, Error(aFile, aIsMinor));

}

void ErrorCollector::clearMinor() noexcept {
	std::erase_if(errors, [](const auto& errorPair) { return errorPair.second.isMinor; });
}

string ErrorCollector::getMessage() const noexcept {
	if (errors.empty()) {
		return Util::emptyString;
	}

	StringList msg;

	//get individual errors
	StringSet errorNames;
	for (const auto& p : errors | views::keys) {
		errorNames.insert(p);
	}

	for (const auto& errorName: errorNames) {
		auto errorCount = errors.count(errorName);
		if (errorCount <= 3) {
			// Report each file
			StringList paths;
			auto k = errors.equal_range(errorName);
			for (const auto& errorDetails: k | pair_to_range | views::values) {
				paths.push_back(errorDetails.file);
			}

			auto pathStr = Util::toString(", ", paths);
			msg.push_back(STRING_F(X_FILE_NAMES, errorName % pathStr));
		} else {
			// Too many errors, report the total failed count
			msg.push_back(STRING_F(X_FILE_COUNT, errorName % errorCount % totalFileCount));
		}
	}

	return Util::toString(", ", msg);
}
};