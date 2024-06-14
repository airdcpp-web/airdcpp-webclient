#pragma once
/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#ifndef DCPLUSPLUS_WEBSERVER_JSON_H
#define DCPLUSPLUS_WEBSERVER_JSON_H

#include <nlohmann/json.hpp>

#include <web-server/Exception.h>


namespace webserver {
	using json = nlohmann::json;

	using ArgumentException = webserver::JsonException;

	typedef std::map<std::string, json> SettingValueMap;
}

#endif // !defined(DCPLUSPLUS_WEBSERVER_JSON_H)
