/*
* Copyright (C) 2013-2014 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SETTINGHOLDER_
#define DCPLUSPLUS_DCPP_SETTINGHOLDER_

#include "SettingsManager.h"

namespace dcpp {


class SettingHolder {
public:
	typedef std::function < void(const string&)> ErrorFunction;

	SettingHolder(ErrorFunction errorF);
	~SettingHolder();

	const int prevTCP = SETTING(TCP_PORT);
	const int prevUDP = SETTING(UDP_PORT);
	const int prevTLS = SETTING(TLS_PORT);

	const int prevConn4 = SETTING(INCOMING_CONNECTIONS);
	const int prevConn6 = SETTING(INCOMING_CONNECTIONS6);
	const string prevMapper = SETTING(MAPPER);
	const string prevBind = SETTING(BIND_ADDRESS);
	const string prevBind6 = SETTING(BIND_ADDRESS6);
	const int prevProxy = CONNSETTING(OUTGOING_CONNECTIONS);


	const bool prevGeo = SETTING(GET_USER_COUNTRY);
	const string prevGeoFormat = SETTING(COUNTRY_FORMAT);

	const string prevHighPrio = SETTING(HIGH_PRIO_FILES);
	const bool prevHighPrioRegex = SETTING(HIGHEST_PRIORITY_USE_REGEXP);

	const string prevShareSkiplist = SETTING(SKIPLIST_SHARE);
	const bool prevShareSkiplistRegex = SETTING(SHARE_SKIPLIST_USE_REGEXP);

	const string prevDownloadSkiplist = SETTING(SKIPLIST_DOWNLOAD);
	const bool prevDownloadSkiplistRegex = SETTING(DOWNLOAD_SKIPLIST_USE_REGEXP);

	const string prevFreeSlotMatcher = SETTING(FREE_SLOTS_EXTENSIONS);
	const string prevTranslation = SETTING(LANGUAGE_FILE);

private:
	ErrorFunction errorF;
	void showError(const string& aError) const noexcept;
};

}

#endif