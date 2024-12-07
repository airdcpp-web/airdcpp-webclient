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

#ifndef DCPLUSPLUS_DCPP_HUBSETTINGS_H
#define DCPLUSPLUS_DCPP_HUBSETTINGS_H

#include <string>

#include <airdcpp/forward.h>
#include <airdcpp/core/classes/tribool.h>

namespace dcpp {

using std::string;

#define HUB_SETTING_DEFAULT_INT numeric_limits<int>::min()

/** Stores settings to be applied to a hub. There are 3 HubSettings levels in DC++: global; per
favorite hub group; per favorite hub entry. */
struct HubSettings
{
	enum HubStrSetting {
		HubStrFirst,

		Nick = HubStrFirst,
		Description,
		Email,
		UserIp,
		UserIp6,
		AwayMsg,
		NmdcEncoding,
		// don't forget to edit stringNames in HubSettings.cpp when adding a def here!

		HubStrLast
	};

	enum HubBoolSetting {
		HubBoolFirst = HubStrLast + 1,

		ShowJoins = HubBoolFirst,
		FavShowJoins,
		LogMainChat,
		ChatNotify,
		// don't forget to edit boolNames in HubSettings.cpp when adding a def here!

		HubBoolLast
	};

	enum HubIntSetting {
		HubIntFirst = HubBoolLast + 1,
		
		SearchInterval = HubIntFirst,
		Connection,
		Connection6,
		ShareProfile,
		// don't forget to edit intNames in HubSettings.cpp when adding a def here!

		HubIntLast
	};

	static bool defined(const string& s) { return !s.empty(); }
	static bool defined(tribool b) { return !indeterminate(b); }
	static bool defined(int b) { return b != HUB_SETTING_DEFAULT_INT; }

	HubSettings();

	const string& get(HubStrSetting setting) const;
	const tribool& get(HubBoolSetting setting) const;
	const int& get(HubIntSetting setting) const;
	string& get(HubStrSetting setting);
	tribool& get(HubBoolSetting setting);
	int& get(HubIntSetting setting);

	/** Apply a set of sub-settings that may override current ones. Strings are overridden when not
	null. Tribools are overridden when not in an indeterminate state. */
	void merge(const HubSettings& sub);

	void load(SimpleXML& xml);
	void save(SimpleXML& xml) const;

	bool operator==(const HubSettings& aOther) const noexcept;
private:
	enum { StringCount = HubStrLast - HubStrFirst,
		BoolCount = HubBoolLast - HubBoolFirst,
		IntCount = HubIntLast - HubIntFirst };

	static const string stringNames[StringCount];
	static const string boolNames[BoolCount];
	static const string intNames[IntCount];

	string strings[StringCount];
	tribool bools[BoolCount];
	int ints[IntCount];
};

} // namespace dcpp

#endif
