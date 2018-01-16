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

#ifndef DCPP_AUTOSEARCHQUEUE_H
#define DCPP_AUTOSEARCHQUEUE_H

#include <airdcpp/typedefs.h>

#include "AutoSearch.h"

#include <airdcpp/PrioritySearchQueue.h>

namespace dcpp {

	class Searches : public PrioritySearchQueue<AutoSearchPtr> {
	public:
		Searches() : PrioritySearchQueue(SettingsManager::AUTOSEARCH_EVERY) {}
		~Searches() {}

		void addItem(AutoSearchPtr& as) {
			addSearchPrio(as);
			searches.emplace(as->getToken(), as);
		}

		void removeItem(AutoSearchPtr& as) noexcept {
			removeSearchPrio(as);
			searches.erase(as->getToken());
		}

		bool hasItem(AutoSearchPtr& as) {
			return searches.find(as->getToken()) != searches.end();
		}

		AutoSearchPtr getItem(const ProfileToken& aToken) const {
			auto ret = searches.find(aToken);
			return ret != searches.end() ? ret->second : nullptr;
		}

		AutoSearchPtr getItem(const void* aSearch) const {
			auto i = find_if(searches | map_values, [&](const AutoSearchPtr& s) {
				return s.get() == aSearch;
			});
			return i.base() != searches.end() ? *i : nullptr;
		}

		AutoSearchMap& getItems() { return searches; }
		const AutoSearchMap& getItems() const { return searches; }
	private:
		/** Bundles by token */
		AutoSearchMap searches;
	};
}

#endif