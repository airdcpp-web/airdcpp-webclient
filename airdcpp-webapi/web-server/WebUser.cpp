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
#include <web-server/WebUser.h>

#include <airdcpp/hash/value/Encoder.h>
#include <airdcpp/util/text/StringTokenizer.h>
#include <airdcpp/hash/value/MerkleTree.h>
#include <airdcpp/util/Util.h>

#include <boost/range/numeric.hpp>
#include <boost/range/adaptor/map.hpp>

namespace webserver {
	const vector<string> WebUser::accessStrings = {
		"admin",

		"search",
		"download",
		"transfers",

		"events_view",
		"events_edit",

		"queue_view",
		"queue_edit",

		"favorite_hubs_view",
		"favorite_hubs_edit",

		"settings_view",
		"settings_edit",

		"share_view",
		"share_edit",

		"filesystem_view",
		"filesystem_edit",

		"hubs_view",
		"hubs_edit",
		"hubs_send",

		"private_chat_view",
		"private_chat_edit",
		"private_chat_send",

		"filelists_view",
		"filelists_edit",

		"view_file_view",
		"view_file_edit",
	};

	Access WebUser::stringToAccess(const string& aStr) noexcept {
		auto pos = find(accessStrings.begin(), accessStrings.end(), aStr);
		if (pos == accessStrings.end()) {
			return Access::LAST;
		}

		return static_cast<Access>(pos - accessStrings.begin());
	}

	const string& WebUser::accessToString(Access aAccess) noexcept {
		return accessStrings[static_cast<AccessType>(aAccess)];
	}

	string WebUser::hashPassword(const string& aPasswordPlain) noexcept {
		TigerHash tmp;
		tmp.update(aPasswordPlain.c_str(), aPasswordPlain.length());
		return TTHValue(tmp.finalize());
	}

	WebUser::WebUser(const std::string& aUserName, const std::string& aPasswordHashOrPlain, bool aIsAdmin) : userName(aUserName) {
		setPassword(aPasswordHashOrPlain);
		clearPermissions();
		if (aIsAdmin) {
			permissions[Access::ADMIN] = true;
		}
	}

	void WebUser::setPassword(const std::string& aPasswordHashOrPlain) noexcept {
		if (aPasswordHashOrPlain.length() == 39 && Encoder::isBase32(aPasswordHashOrPlain.c_str())) {
			// Hashed already
			passwordHash = aPasswordHashOrPlain;
		} else {
			// Convert to hash
			passwordHash = hashPassword(aPasswordHashOrPlain);
		}
	}

	bool WebUser::isAdmin() const noexcept {
		return permissions.at(Access::ADMIN);
	}

	void WebUser::clearPermissions() noexcept {
		for (auto i = 0; i < static_cast<AccessType>(Access::LAST); i++) {
			permissions[static_cast<Access>(i)] = false;
		}
	}

	void WebUser::setPermissions(const string& aStr) noexcept {
		auto lst = StringTokenizer<string>(aStr, ',');
		setPermissions(lst.getTokens());
	}

	void WebUser::setPermissions(const StringList& aPermissions) noexcept {
		clearPermissions();
		for (const auto& p : aPermissions) {
			auto access = stringToAccess(p);
			if (access != Access::LAST) {
				permissions[access] = true;
			}
		}
	}

	StringList WebUser::permissionsToStringList(const AccessList& aPermissions) noexcept {
		StringList ret;
		for (const auto& access: aPermissions) {
			ret.push_back(accessToString(access));
		}

		return ret;
	}

	AccessList WebUser::getPermissions() const noexcept {
		AccessList ret;
		for (const auto& [access, enabled] : permissions) {
			if (enabled) {
				ret.push_back(access);
			}
		}

		return ret;
	}


	int WebUser::countPermissions() const noexcept {
		return boost::accumulate(permissions | boost::adaptors::map_values, 0);
	}

	bool WebUser::validateUsername(const string& aUsername) noexcept {
		boost::regex reg(R"(\w+)");
		return boost::regex_match(aUsername, reg);
	}

	bool WebUser::matchPassword(const string& aPasswordPlain) const noexcept {
		return hashPassword(aPasswordPlain) == passwordHash;
	}

	string WebUser::getPermissionsStr() const noexcept {
		return Util::toString(",", permissionsToStringList(getPermissions()));
	}

	bool WebUser::hasPermission(Access aAccess) const noexcept {
		using enum Access;
		if (aAccess == ANY) {
			return true;
		}

		dcassert(aAccess != NONE);
		if (aAccess == NONE) {
			return false;
		}

		return permissions.at(aAccess) || permissions.at(ADMIN);
	}
}
