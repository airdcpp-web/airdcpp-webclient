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

#ifndef DCPLUSPLUS_SORTEDVECTOR
#define DCPLUSPLUS_SORTEDVECTOR

#include "typedefs.h"

/* This vector container is optimized for fast lookup and inserting items that are sorted already */

template<typename T, class keyType, class SortOperator, class NameOperator>
class SortedVector : public std::vector<T> {

public:
	typename std::vector<T>::iterator it;

	std::pair<typename std::vector<T>::iterator, bool> insert_sorted(const T& aItem) {
		if (empty()) {
			push_back(aItem);
			return make_pair(begin(), true);
		} else if (SortOperator()(back(), aItem)) {
			if(NameOperator()(back()).compare(NameOperator()(aItem)) != 0) {
				push_back(aItem);
				return make_pair(end()-1, true);
			}
			return make_pair(end()-1, false);
		}

		auto hqr = equal_range(begin(), end(), aItem, SortOperator());
		if (hqr.first == hqr.second) {
			//it doesn't exist yet
			return make_pair(insert(hqr.first, aItem), true);
		}
			
		//return the dupe
		return make_pair(hqr.first, false);
	}

	/*boost::optional<T> find(const keyType& aKey) {
		const size_t start = 0;
		const size_t end = size()-1;

		auto pos = binary_search(start, end, aKey);
		return (pos != -1) ? this[pos] : this[pos];
	}*/

	typename std::vector<T>::iterator find(const keyType& aKey) {
		auto pos = binary_search(0, size()-1, aKey);
		return (pos != -1) ? begin()+pos : end();
	}

	int binary_search(int left, int right, const keyType& key) {
		while (left <= right) {
			int middle = (left + right) / 2;

			auto res = key.compare(NameOperator()((*(begin() + middle))));
			if (res == 0)
				return middle;
			else if (res < 0)
				right = middle - 1;
			else
				left = middle + 1;
		}
		return -1;
	}
private:



};

//}

#endif