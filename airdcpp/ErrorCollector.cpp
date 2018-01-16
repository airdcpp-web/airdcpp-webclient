/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#include "ErrorCollector.h"

#include "ResourceManager.h"
#include "Util.h"

#include <boost/range/algorithm/remove_if.hpp>

namespace dcpp {

ErrorCollector::ErrorCollector(int aTotalFileCount) : totalFileCount(aTotalFileCount) { }

void ErrorCollector::add(const string& aError, const string& aFile, bool aIsMinor) noexcept {
	errors.emplace(aError, Error(aFile, aIsMinor));

}

void ErrorCollector::clearMinor() noexcept {
	errors.erase(boost::remove_if(errors | map_values, [](const Error& e) { return e.isMinor; }).base(), errors.end());
}

string ErrorCollector::getMessage() const noexcept {
	if (errors.empty()) {
		return Util::emptyString;
	}

	StringList msg;

	//get individual errors
	StringSet errorNames;
	for (const auto& p : errors | map_keys) {
		errorNames.insert(p);
	}

	for (const auto& e : errorNames) {
		auto errorCount = errors.count(e);
		if (errorCount <= 3) {
			// Report each file
			StringList paths;
			auto k = errors.equal_range(e);
			for (auto i = k.first; i != k.second; ++i) {
				paths.push_back(i->second.file);
			}

			auto pathStr = Util::toString(", ", paths);
			msg.push_back(STRING_F(X_FILE_NAMES, e % pathStr));
		} else {
			// Too many errors, report the total failed count
			msg.push_back(STRING_F(X_FILE_COUNT, e % errorCount % totalFileCount));
		}
	}

	return Util::toString(", ", msg);
}
};