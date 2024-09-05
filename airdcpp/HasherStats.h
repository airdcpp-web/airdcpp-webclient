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

#ifndef DCPLUSPLUS_DCPP_HASHERSTATS_H
#define DCPLUSPLUS_DCPP_HASHERSTATS_H

#include "typedefs.h"

namespace dcpp {

	class HasherStats {
	public:
		HasherStats(HasherStats* aParent = nullptr) : parent(aParent) {}

		int64_t sizeHashed = 0;
		uint64_t hashTime = 0;
		int filesHashed = 0;

		string formatDuration() const noexcept;
		string formatSpeed() const noexcept;
		string formatSize() const noexcept;

		void addFile(int64_t aSize, uint64_t aHashTime) noexcept;

		HasherStats(const HasherStats&) = delete;
	private:
		HasherStats* parent;
	};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_HASHERSTATS_H)