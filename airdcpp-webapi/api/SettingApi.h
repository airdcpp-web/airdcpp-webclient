/*
* Copyright (C) 2011-2022 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SETTINGAPI_H
#define DCPLUSPLUS_DCPP_SETTINGAPI_H

#include <api/base/ApiModule.h>


namespace webserver {
	class ApiSettingItem;
	class SettingApi : public ApiModule {
	public:
		SettingApi(Session* aSession);
		~SettingApi();
	private:
		api_return handleGetDefinitions(ApiRequest& aRequest);
		api_return handleGetValues(ApiRequest& aRequest);
		api_return handleSetValues(ApiRequest& aRequest);
		api_return handleResetValues(ApiRequest& aRequest);
		api_return handleGetDefaultValues(ApiRequest& aRequest);
		api_return handleSetDefaultValues(ApiRequest& aRequest);

		typedef function<void(ApiSettingItem&)> KeyParserF;
		void parseSettingKeys(const json& aJson, KeyParserF aHandler, WebServerManager* aWsm);

		typedef function<void(ApiSettingItem&, const json& aValue)> ValueParserF;
		void parseSettingValues(const json& aJson, ValueParserF aHandler, WebServerManager* aWsm);

		static ApiSettingItem* getSettingItem(const string& aKey, WebServerManager* aWsm) noexcept;
	};
}

#endif