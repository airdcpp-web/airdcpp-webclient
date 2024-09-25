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

#ifndef DCPLUSPLUS_DCPP_STARTUP_PARAMS_H
#define DCPLUSPLUS_DCPP_STARTUP_PARAMS_H

#include <airdcpp/typedefs.h>

namespace dcpp {

class StartupParams {
public:
	using ParamList = deque<string>;

	bool hasParam(const string& aParam, int aPos = -1) const noexcept;
	void addParam(const string& aParam) noexcept;
	bool removeParam(const string& aParam) noexcept;
	optional<string> getValue(const string& aKey) const noexcept;

	const ParamList& getParams() const noexcept {
		return params;
	}

	string formatParams(bool aIsFirst) const noexcept;
	size_t size() const noexcept {
		return params.size();
	}

	void pop_front() noexcept {
		params.pop_front();
	}
private:
	ParamList params;
};

} // namespace dcpp

#endif // !defined(UTIL_H)
