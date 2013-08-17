/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_TYPEDEFS_H_
#define DCPLUSPLUS_DCPP_TYPEDEFS_H_

#include <stdint.h>
#include "forward.h"

#include "boost/variant.hpp"

namespace dcpp {

typedef std::function<void ()> AsyncF;

typedef vector<string> StringList;
typedef StringList::iterator StringIter;
typedef StringList::const_iterator StringIterC;

typedef pair<string, string> StringPair;
typedef vector<StringPair> StringPairList;
typedef StringPairList::iterator StringPairIter;

typedef pair<int64_t, string> IntStringPair;
typedef vector<IntStringPair> IntStringList;

typedef std::map<string, string> OrderedStringMap;
typedef std::unordered_map<string, string> StringMap;
typedef StringMap::iterator StringMapIter;
typedef std::unordered_map<string, StringList> StringListMap;

typedef std::unordered_set<int> ProfileTokenSet;

typedef std::set<string> OrderedStringSet;
typedef std::unordered_set<string> StringSet;
typedef StringSet::iterator StringSetIter;

typedef std::unordered_map <string, int> StringIntMap;
typedef std::unordered_map <string, int64_t> StringInt64Map;
typedef StringInt64Map::iterator StringInt64Iter;

typedef vector<wstring> WStringList;
typedef WStringList::iterator WStringIter;
typedef WStringList::const_iterator WStringIterC;

typedef pair<wstring, wstring> WStringPair;
typedef vector<WStringPair> WStringPairList;
typedef WStringPairList::iterator WStringPairIter;

typedef unordered_map<wstring, wstring> WStringMap;
typedef WStringMap::iterator WStringMapIter;

typedef vector<uint8_t> ByteVector;
typedef vector<Client*> ClientList;

typedef int ProfileToken;
typedef vector<ProfileToken> ProfileTokenList;
typedef vector<pair<ProfileToken, string>> ProfileTokenStringList;
typedef std::unordered_map <ProfileToken, string> ProfileTokenStringMap;

#ifdef UNICODE

typedef wstring tstring;
typedef WStringList TStringList;
typedef WStringIter TStringIter;
typedef WStringIterC TStringIterC;

typedef WStringPair TStringPair;
typedef WStringPairIter TStringPairIter;
typedef WStringPairList TStringPairList;

typedef WStringMap TStringMap;
typedef WStringMapIter TStringMapIter;

#else

typedef string tstring;
typedef StringList TStringList;
typedef StringIter TStringIter;
typedef StringIterC TStringIterC;

typedef StringPair TStringPair;
typedef StringPairIter TStringPairIter;
typedef StringPairList TStringPairList;

typedef StringMap TStringMap;
typedef StringMapIter TStringMapIter;

#endif

typedef unordered_map<string, boost::variant<string, std::function<string ()>>> ParamMap;

}

#endif /* TYPEDEFS_H_ */
