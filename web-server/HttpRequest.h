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

#ifndef DCPLUSPLUS_WEBSERVER_HTTP_REQUEST_H
#define DCPLUSPLUS_WEBSERVER_HTTP_REQUEST_H

#include "forward.h"

#include <airdcpp/core/header/typedefs.h>


namespace webserver {
	struct HttpRequest {
		const SessionPtr& session;
		const string& ip;
		const std::string& path;
		const websocketpp::http::parser::request& httpRequest;
		const bool secure;
	};
}

#endif