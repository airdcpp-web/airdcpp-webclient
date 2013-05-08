/*
 * Copyright (C) 2013 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SETTINGITEM_
#define DCPLUSPLUS_DCPP_SETTINGITEM_

#include "stdinc.h"

#include "ResourceManager.h"
#include "Util.h"

#include <boost/variant.hpp>

namespace dcpp {


struct SettingItem {
	typedef boost::variant<string, bool, int, double> SettingValue;

	int key;
	SettingValue profileValue;
	ResourceManager::Strings name;

	SettingValue getCurValue() const;

	bool isSet() const;
	bool isDefault() const;

	/*void useProfileValue() const;*/

	void setDefault(bool reset) const;

	const string& getName() const;
	bool isProfileCurrent() const;

	string profileToString() const;
	string currentToString() const;

	struct ToString : boost::static_visitor<string> {
		ToString(int aKey) : key(aKey) { }

		string operator()(const string& s) const {
			return s;
		}

		string operator()(int s) const {
			return Util::toString(s);
		}

		string operator()(double d) const {
			return Util::toString(d);
		}

		string operator()(bool b) const {
			return b ? STRING(ENABLED) : STRING(DISABLED);
		}
	private:
		int key;
	};

	class CompareKey {
	public:
		CompareKey(int aKey) : key(aKey) { }
		bool operator()(const SettingItem& s) { return s.key == key; }
	private:
		int key;
	};

	bool operator==(const SettingItem& aSetting) const { return aSetting.key == key; }
	struct Hash {
		size_t operator()(const SettingItem& x) const { return hash<int>()(x.key); }
	};
};

}

#endif