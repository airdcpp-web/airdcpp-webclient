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

/* This vector-like container is optimized for fast lookup and inserting items that are sorted already */

template<typename T, template<class V, class = std::allocator<V> > class ContainerT, class keyType, class SortOperator, class NameOperator>
class SortedVector : public ContainerT<T> {

public:
	typename ContainerT<T>::iterator it;

	std::pair<typename ContainerT<T>::const_iterator, bool> insert_sorted(const T& aItem) {
		if (this->empty()) {
			this->push_back(aItem);
			return make_pair(this->begin(), true);
		} else {
			int res = SortOperator()(NameOperator()(this->back()), NameOperator()(aItem));
			if (res < 0) {
				this->push_back(aItem);
				return make_pair(this->end() - 1, true);
			} else if (res == 0) {
				//return the dupe
				return make_pair(this->end() - 1, false);
			}
		}

		auto p = getPos(this->begin(), this->end(), NameOperator()(aItem));
		if (p.second) {
			//return the dupe
			return make_pair(p.first, false);
		}

		//insert
		p.first = this->insert(p.first, aItem);
		return make_pair(p.first, true);
	}

	template<typename... ArgT>
	std::pair<typename ContainerT<T>::const_iterator, bool> emplace_sorted(const keyType& aKey, ArgT&& ... args) {
		if (this->empty()) {
			this->emplace_back(aKey, std::forward<ArgT>(args)...);
			return make_pair(this->begin(), true);
		} else {
			int res = SortOperator()(NameOperator()(this->back()), aKey);
			if (res < 0) {
				this->emplace_back(aKey, std::forward<ArgT>(args)...);
				return make_pair(this->end() - 1, true);
			} else if (res == 0) {
				//return the dupe
				return make_pair(this->end() - 1, false);
			}
		}

		auto p = getPos(this->begin(), this->end(), aKey);
		if (p.second) {
			//return the dupe
			return make_pair(p.first, false);
		}

		//insert
		p.first = this->emplace(p.first, aKey, std::forward<ArgT>(args)...);
		return make_pair(p.first, true);
	}

	typename ContainerT<T>::const_iterator find(const keyType& aKey) const {
		auto pos = getPos(this->cbegin(), this->cend(), aKey);
		return pos.second ? pos.first : this->cend();
	}

	typename ContainerT<T>::iterator find(const keyType& aKey) {
		auto pos = getPos(this->begin(), this->end(), aKey);
		return pos.second ? pos.first : this->end();
	}

	bool erase_key(const keyType& aKey) {
		auto pos = getPos(this->begin(), this->end(), aKey);
		if (pos.second) {
			this->erase(pos.first);
			return true;
		}

		return false;
	}

	/*int binary_search(int left, int right, const keyType& key) {
		while (left <= right) {
			int middle = (left + right) / 2;

			auto res = SortOperator()(key, NameOperator()((*(begin() + middle))));
			if (res == 0)
				return middle;
			else if (res < 0)
				right = middle - 1;
			else
				left = middle + 1;
		}
		return -1;
	}*/
private:
	template<typename IterT>
	std::pair<IterT, bool> getPos(IterT first, IterT last, const keyType& key) const {
		IterT it;
		typename std::iterator_traits<IterT>::difference_type count, step;
		count = std::distance(first, last);

		while (count > 0) {
			it = first;
			step = count / 2;
			std::advance(it, step);
			auto res = SortOperator()(NameOperator()(*it), key);
			if (res < 0) {
				first = ++it;
				count -= step + 1;
			} else if (res == 0) {
				return make_pair(it, true);
			} else {
				count = step;
			}
		}
		return make_pair(first, false);
	}
};

//}

#endif
