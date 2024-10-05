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

#ifndef DCPLUSPLUS_DCPP_TYPEDEFS_H_
#define DCPLUSPLUS_DCPP_TYPEDEFS_H_

#include <stdint.h>
#include <airdcpp/forward.h>

#include <boost/variant.hpp>

namespace dcpp {

using AsyncF = std::function<void ()>;

using StringList = vector<string>;

using StringPair = pair<string, string>;
using StringPairList = vector<StringPair>;

using OrderedStringMap = std::map<string, string>;
using StringMap = std::unordered_map<string, string>;
using StringListMap = std::unordered_map<string, StringList>;

using ProfileTokenSet = std::set<int>;

using OrderedStringSet = std::set<string>;
using StringSet = std::unordered_set<string>;

using StringIntMap = std::unordered_map<string, int>;

using WStringList = vector<wstring>;

using WStringPair = pair<wstring, wstring>;
using WStringPairList = vector<WStringPair>;

using WStringMap = unordered_map<wstring, wstring>;

using ByteVector = vector<uint8_t>;

using ProfileToken = int;
using OptionalProfileToken = optional<ProfileToken>;
using ProfileTokenList = vector<ProfileToken>;
using ProfileTokenStringList = vector<pair<ProfileToken, string>>;

using GroupedDirectoryMap = map<string, OrderedStringSet>;

#ifdef UNICODE

using tstring = wstring;
using TStringList = WStringList;

using TStringPair = WStringPair;
using TStringPairList = WStringPairList;

using TStringMap = WStringMap;

#else

typedef string tstring;
typedef StringList TStringList;

typedef StringPair TStringPair;
typedef StringPairList TStringPairList;

typedef StringMap TStringMap;

#endif

using ParamMap = unordered_map<string, boost::variant<string, std::function<string ()>>>;

}

#endif /* TYPEDEFS_H_ */
