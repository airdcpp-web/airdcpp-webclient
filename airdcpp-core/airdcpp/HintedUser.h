/*
 * Copyright (C) 2001-2023 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_HINTEDUSER_H_
#define DCPLUSPLUS_DCPP_HINTEDUSER_H_

#include <string>

#include "forward.h"
#include "User.h"

namespace dcpp {

using std::string;

/** User pointer associated to a hub url */
struct HintedUser {
	UserPtr user = nullptr;
	string hint;

	HintedUser() { }
	HintedUser(const UserPtr& user_, const string& hint_) : user(user_), hint(hint_) { }

	bool operator==(const UserPtr& rhs) const noexcept {
		return user == rhs;
	}
	bool operator==(const HintedUser& rhs) const noexcept {
		return user == rhs.user;
		// ignore the hint, we don't want lists with multiple instances of the same user...
	}

	operator UserPtr() const noexcept { return user; }
	explicit operator bool() const noexcept { return user ? true : false; }
};

}

#endif /* HINTEDUSER_H_ */
