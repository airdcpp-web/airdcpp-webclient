
/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#ifndef RECENT_ENTRY_H_
#define RECENT_ENTRY_H_

#include "GetSet.h"
#include "Pointer.h"
#include "typedefs.h"

#include <boost/algorithm/string/trim.hpp>

namespace dcpp {

class RecentEntry : public intrusive_ptr_base<RecentEntry> {
public:
	RecentEntry(const string& aUrl) : 
		url(aUrl), name("*"), description("*") {

	}

	GETSET(string, url, Url);
	GETSET(string, name, Name);
	GETSET(string, description, Description);
};

}
#endif