/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_CONNECTION_TYPE_H
#define DCPLUSPLUS_DCPP_CONNECTION_TYPE_H

namespace dcpp {

/** Different kinds of P2P connections - use CONNECTION_TYPE_LAST when undefined.
This is a global enum as it has uses in various places of the lib. */

enum ConnectionType {
	CONNECTION_TYPE_DOWNLOAD,
	CONNECTION_TYPE_UPLOAD,
	CONNECTION_TYPE_PM,

	CONNECTION_TYPE_LAST
};

} // namespace dcpp

#endif
