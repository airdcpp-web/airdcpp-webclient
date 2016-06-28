/*
 * Copyright (C) 2013-2016 AirDC++ Project
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
	std::pair<typename ContainerT<T>::const_iterator, bool> insert_sorted(const T& aItem) {
		if (ContainerT<T>::empty()) {
			ContainerT<T>::push_back(aItem);
			return { ContainerT<T>::begin(), true };
		} else {
			int res = SortOperator()(NameOperator()(ContainerT<T>::back()), NameOperator()(aItem));
			if (res < 0) {
				ContainerT<T>::push_back(aItem);
				return { ContainerT<T>::end() - 1, true };
			} else if (res == 0) {
				//return the dupe
				return { ContainerT<T>::end() - 1, false };
			}
		}

		auto p = getPos(ContainerT<T>::begin(), ContainerT<T>::end(), NameOperator()(aItem));
		if (p.second) {
			//return the dupe
			return { p.first, false };
		}

		//insert
		p.first = ContainerT<T>::insert(p.first, aItem);
		return { p.first, true };
	}

	template<typename... ArgT>
	std::pair<typename ContainerT<T>::const_iterator, bool> emplace_sorted(const keyType& aKey, ArgT&& ... args) {
		if (ContainerT<T>::empty()) {
			ContainerT<T>::emplace_back(aKey, std::forward<ArgT>(args)...);
			return { ContainerT<T>::begin(), true };
		} else {
			int res = SortOperator()(NameOperator()(ContainerT<T>::back()), aKey);
			if (res < 0) {
				ContainerT<T>::emplace_back(aKey, std::forward<ArgT>(args)...);
				return { ContainerT<T>::end() - 1, true };
			} else if (res == 0) {
				//return the dupe
				return { ContainerT<T>::end() - 1, false };
			}
		}

		auto p = getPos(ContainerT<T>::begin(), ContainerT<T>::end(), aKey);
		if (p.second) {
			//return the dupe
			return { p.first, false };
		}

		//insert
		p.first = ContainerT<T>::emplace(p.first, aKey, std::forward<ArgT>(args)...);
		return { p.first, true };
	}

	typename ContainerT<T>::const_iterator find(const keyType& aKey) const {
		auto pos = getPos(ContainerT<T>::cbegin(), ContainerT<T>::cend(), aKey);
		return pos.second ? pos.first : ContainerT<T>::cend();
	}

	typename ContainerT<T>::iterator find(const keyType& aKey) {
		auto pos = getPos(ContainerT<T>::begin(), ContainerT<T>::end(), aKey);
		return pos.second ? pos.first : ContainerT<T>::end();
	}

	bool erase_key(const keyType& aKey) {
		auto pos = getPos(ContainerT<T>::begin(), ContainerT<T>::end(), aKey);
		if (pos.second) {
			ContainerT<T>::erase(pos.first);
			return true;
		}

		return false;
	}

	void push_back(typename ContainerT<T>::value_type&& _Val) = delete;
	void push_back(const typename ContainerT<T>::value_type& _Val) = delete;

	template<class... _Valty>
	void emplace_back(_Valty&&... _Val) = delete;
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
				return { it, true };
			} else {
				count = step;
			}
		}
		return { first, false };
	}
};

//}

#endif
