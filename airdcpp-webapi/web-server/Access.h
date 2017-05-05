/*
* Copyright (C) 2011-2017 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_ACCESS_H
#define DCPLUSPLUS_DCPP_ACCESS_H


namespace webserver {
	typedef int8_t AccessType;

	// Remember to edit WebUser::accessStrings as well
	enum class Access: AccessType {
		NONE = -2,
		ANY = -1,
		ADMIN = 0,

		SEARCH,
		DOWNLOAD,
		TRANSFERS,

		EVENTS_VIEW,
		EVENTS_EDIT,

		QUEUE_VIEW,
		QUEUE_EDIT,

		FAVORITE_HUBS_VIEW,
		FAVORITE_HUBS_EDIT,

		SETTINGS_VIEW,
		SETTINGS_EDIT,

		FILESYSTEM_VIEW,
		FILESYSTEM_EDIT,

		HUBS_VIEW,
		HUBS_EDIT,
		HUBS_SEND,

		PRIVATE_CHAT_VIEW,
		PRIVATE_CHAT_EDIT,
		PRIVATE_CHAT_SEND,

		FILELISTS_VIEW,
		FILELISTS_EDIT,

		VIEW_FILES_VIEW,
		VIEW_FILES_EDIT,

		LAST,
	};

	typedef map<Access, bool> AccessMap;
}

#endif