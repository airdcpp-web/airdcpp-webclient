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

#ifndef DCPLUSPLUS_DCPP_USERCONNECTRESULT_H_
#define DCPLUSPLUS_DCPP_USERCONNECTRESULT_H_

#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/core/types/GetSet.h>
#include <airdcpp/util/Util.h>

namespace dcpp {


struct UserConnectResult {
	void onSuccess(const string_view& aHubHint) noexcept {
		success = true;
		hubHint = aHubHint;
	}

	void onMinorError(const string_view& aError) noexcept {
		lastError = aError;
		protocolError = false;
	}

	void onProtocolError(const string_view& aError) noexcept {
		lastError = aError;
		protocolError = true;
	}

	void resetError() noexcept {
		lastError = Util::emptyString;
		protocolError = false;
	}


	GETPROP(string, lastError, Error);
	IGETPROP(bool, protocolError, IsProtocolError, false);

	GETPROP(string, hubHint, HubHint);
	IGETPROP(bool, success, IsSuccess, false);
};

}

#endif /* DCPLUSPLUS_DCPP_BUNDLEINFO_H_ */
