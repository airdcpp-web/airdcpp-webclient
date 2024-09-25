/*
 * Copyright (C) 2011-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_ADC_SUPPORTS_H
#define DCPLUSPLUS_DCPP_ADC_SUPPORTS_H

#include <airdcpp/typedefs.h>
#include <airdcpp/debug.h>

#include <airdcpp/CriticalSection.h>

namespace dcpp {

class AdcSupports {
public:
	bool add(const string& aSupport) noexcept {
		dcassert(aSupport.length() == 4);

		WLock l(cs);
		if (ranges::find(supports, aSupport) != supports.end()) {
			return false;
		}

		supports.push_back(aSupport);
		return true;
	}

	bool remove(const string& aSupport) noexcept {
		dcassert(aSupport.length() == 4);

		WLock l(cs);
		auto i = ranges::find(supports, aSupport);
		if (i != supports.end()) {
			supports.erase(i);
			return true;
		}

		return false;
	}

	bool includes(const string& aSupport) const noexcept {
		dcassert(aSupport.length() == 4);

		RLock l(cs);
		return ranges::find(supports, aSupport) != supports.end();
	}

	StringList getAll() const noexcept {
		RLock l(cs);
		return supports;
	}

	void clear() noexcept {
		WLock l(cs);
		supports.clear();
	}

	void replace(const StringList& aSupports) noexcept {
		dcassert(ranges::all_of(aSupports, [](const string& aSupport) { return aSupport.length() == 4; }));
		WLock l(cs);
		supports = aSupports;
	}
private:
	mutable SharedMutex cs;
	StringList supports;
};


} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_ADC_SUPPORTS_H)
