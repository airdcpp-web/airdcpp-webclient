/*
 * Copyright (C) 2012-2013 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_MAGNET_H
#define DCPLUSPLUS_DCPP_MAGNET_H

#include "forward.h"
#include "AirUtil.h"
#include "User.h"

#include <string>

namespace dcpp {

using std::string;

/** Struct for a magnet uri */
struct Magnet {
	string fname, type, param, hash;
	//TTHValue tth;
	int64_t fsize;

	//extra information for downloading the files
	//string target, nick;

	explicit Magnet(const string& aLink);

	DupeType getDupeType() const;
	TTHValue getTTH() const;
};

}

#endif