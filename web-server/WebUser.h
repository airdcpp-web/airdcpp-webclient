/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_WEBUSER_H
#define DCPLUSPLUS_DCPP_WEBUSER_H

#include <web-server/stdinc.h>
#include <airdcpp/GetSet.h>

namespace webserver {
	class WebUser;
	typedef std::shared_ptr<WebUser> WebUserPtr;

	class WebUser {
	public:
		WebUser(const std::string& aUserName, const std::string& aPassword) : userName(aUserName), password(aPassword) {

		}

		GETSET(std::string, userName, UserName);
		GETSET(std::string , password, Password);

		WebUser(WebUser&) = delete;
		WebUser& operator=(WebUser&) = delete;
	private:

	};

}

#endif