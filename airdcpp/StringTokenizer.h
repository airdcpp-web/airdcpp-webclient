/*
 * Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_STRING_TOKENIZER_H
#define DCPLUSPLUS_DCPP_STRING_TOKENIZER_H


#include "stdinc.h"

namespace dcpp {

template<class T, template<class V, class = std::allocator<V> > class ContainerT = vector>
class StringTokenizer
{
private:
	ContainerT<T> tokens;

	template<class SeparatorT>
	StringTokenizer(const T& aString, const SeparatorT& aToken, bool aAllowEmptyTokens, size_t aSeparatorLength) {
		string::size_type i = 0;
		string::size_type j = 0;
		while ((i = aString.find(aToken, j)) != string::npos) {
			if (aAllowEmptyTokens || j != i)
				tokens.push_back(aString.substr(j, i - j));
			j = i + aSeparatorLength;
		}

		if (j < aString.size())
			tokens.push_back(aString.substr(j, aString.size() - j));
	}
public:
	StringTokenizer(const T& aString, const typename T::value_type& aToken, bool aAllowEmptyTokens = false) : 
		StringTokenizer(aString, aToken, aAllowEmptyTokens, 1) {
		
	}

	StringTokenizer(const T& aString, const char* aToken, bool aAllowEmptyTokens = false) : 
		StringTokenizer(aString, aToken, aAllowEmptyTokens, strlen(aToken)) {
		
	}

	StringTokenizer(const T& aString, const wchar_t* aToken, bool aAllowEmptyTokens = false) :
		StringTokenizer(aString, aToken, aAllowEmptyTokens, wcslen(aToken)) {
		
	}

	StringTokenizer(const T& aString, const boost::regex& aRegex, bool aAllowEmptyTokens = false) {
		boost::sregex_token_iterator cur{ aString.begin(), aString.end(), aRegex, -1 }, last;

		while (cur != last) {
			if (aAllowEmptyTokens || (*cur).length() != 0) {
				tokens.push_back((*cur).str());
			}

			cur++;
		}
	}

	ContainerT<T>& getTokens() { return tokens; }

	~StringTokenizer() { }
};

} // namespace dcpp

#endif // !defined(STRING_TOKENIZER_H)