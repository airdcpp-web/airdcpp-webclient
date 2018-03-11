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

#ifndef DCPP_RELEVANCESEARCH_H
#define DCPP_RELEVANCESEARCH_H

#include "typedefs.h"
#include "forward.h"

#include "SearchQuery.h"
#include "Util.h"

#include <string>

namespace dcpp {
	inline static string stripNick(const string& aNick) {
		if (aNick.empty() || aNick.substr(0, 1) != "[") return aNick;

		auto x = aNick.find(']');
		// Avoid full deleting of [IMCOOL][CUSIHAVENOTHINGELSETHANBRACKETS]-type nicks
		if ((x != string::npos) && (aNick.substr(x + 1).length() > 0)) {
			return aNick.substr(x + 1);
		}

		return aNick;
	}

	template<class T>
	class RelevanceSearch {
	public:
		typedef function<string(const T&)> StringF;
		RelevanceSearch(const string& aStr, StringF&& aStringF) : query(aStr, StringList(), StringList(), Search::MATCH_NAME_PARTIAL), stringF(aStringF) {

		}

		void match(const T& aItem) noexcept {
			auto name = stringF(aItem);
			if (query.matchesStr(name)) {
				results.insert({ aItem, SearchQuery::getRelevanceScore(query, 0, false, name) });
			}
		}

		vector<T> getResults(size_t aCount) noexcept {
			vector<T> ret;
			for (auto i = results.begin(); (i != results.end()) && (ret.size() < aCount); ++i) {
				ret.push_back((*i).item);
			}

			return ret;
		}
	private:
		struct Match {
			T item;
			double scores;
		};

		struct Sort {
			bool operator()(const Match& left, const Match& right) const { return left.scores > right.scores; }
		};

		multiset<Match, Sort> results;

		StringF stringF;
		SearchQuery query;
	};
}

#endif // !defined(SEARCHQUERY_H)