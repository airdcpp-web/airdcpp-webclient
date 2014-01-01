/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_TRIBOOL_H
#define DCPLUSPLUS_DCPP_TRIBOOL_H

#include <boost/logic/tribool.hpp>

using boost::indeterminate;
using boost::tribool;

// conversions between tribools and ints, with 2 being the indeterminate value
namespace dcpp {
	inline tribool to3bool(int x) { if (x == 2) return tribool(indeterminate); return tribool(x == 1); }
	inline int toInt(tribool x) { return x ? 1 : !x ? 0 : 2; }

	//to keep the compatibility with DC++....
	inline tribool to3boolXml(int x) { if(x) { return tribool(x == 1); } return tribool(indeterminate); }
	inline int toIntXml(tribool x) { return x ? 1 : !x ? 2 : 0; }
}

#endif
