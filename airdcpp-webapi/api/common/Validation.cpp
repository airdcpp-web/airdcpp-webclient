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

#include "stdinc.h"

#include "Validation.h"

#include <web-server/JsonUtil.h>

#include <airdcpp/Util.h>

namespace webserver {
	const string& Validation::validateAdcDirectoryPath(const string& aPath) {
		if (!Util::isAdcDirectoryPath(aPath)) {
			JsonUtil::throwError("Path", JsonUtil::ERROR_INVALID, "Path " + aPath + " isn't a valid ADC directory path");
		}

		return aPath;
	}
}