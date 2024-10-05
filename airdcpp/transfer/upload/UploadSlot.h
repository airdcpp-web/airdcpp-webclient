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

#ifndef DCPLUSPLUS_DCPP_UPLOAD_SLOT_H
#define DCPLUSPLUS_DCPP_UPLOAD_SLOT_H

#include <airdcpp/core/header/typedefs.h>

namespace dcpp {

struct UploadSlot {

	// Note: the "best" slot type should have the highest number
	enum Type : uint8_t {
		NOSLOT = 0,

		// File-specific
		FILESLOT = 1,

		// Persistent
		USERSLOT = 2
	};


	UploadSlot(Type aType, const string& aSource) : type(aType), source(aSource) {}

	Type type;
	string source;

	static Type toType(const std::optional<UploadSlot>& aSlot) noexcept {
		return aSlot ? aSlot->type : NOSLOT;
	}
};

typedef std::optional<UploadSlot> OptionalUploadSlot;

}

#endif