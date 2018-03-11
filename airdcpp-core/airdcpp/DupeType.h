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

#ifndef DCPP_DUPETYPE_H
#define DCPP_DUPETYPE_H

namespace dcpp {

enum DupeType : uint8_t {
	DUPE_NONE,
	DUPE_SHARE_PARTIAL,
	DUPE_SHARE_FULL,
	DUPE_QUEUE_PARTIAL,
	DUPE_QUEUE_FULL,
	DUPE_FINISHED_PARTIAL,
	DUPE_FINISHED_FULL,
	DUPE_SHARE_QUEUE
};

}

#endif