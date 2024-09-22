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

#ifndef DCPLUSPLUS_DCPP_SESSIONAPI_H
#define DCPLUSPLUS_DCPP_SESSIONAPI_H

#include <web-server/WebUserManagerListener.h>

#include <api/base/ApiModule.h>

#include <airdcpp/typedefs.h>

namespace webserver {
	class SessionApi : public SubscribableApiModule, private WebUserManagerListener {
	public:
		explicit SessionApi(Session* aSession);
		~SessionApi() override;

		// Session isn't associated yet when these get called...
		static api_return handleLogin(RouterRequest& aRequest);
		static api_return handleSocketConnect(RouterRequest& aRequest);
	private:
		api_return failAuthenticatedRequest(ApiRequest& aRequest);

		api_return handleRemoveCurrentSession(ApiRequest& aRequest);
		api_return handleActivity(ApiRequest& aRequest);

		api_return handleGetSessions(ApiRequest& aRequest);
		api_return handleGetCurrentSession(ApiRequest& aRequest) const;

		api_return handleGetSession(ApiRequest& aRequest);
		api_return handleRemoveSession(ApiRequest& aRequest);

		api_return logout(ApiRequest& aRequest, const SessionPtr& aSession);

		static json serializeLoginInfo(const SessionPtr& aSession, const string& aRefreshToken);
		static json serializeSession(const SessionPtr& aSession) noexcept;
		static string getSessionType(const SessionPtr& aSession) noexcept;

		void on(WebUserManagerListener::SessionCreated, const SessionPtr& aSession) noexcept override;
		void on(WebUserManagerListener::SessionRemoved, const SessionPtr& aSession, int aReason) noexcept override;

		SessionPtr parseSessionParam(ApiRequest& aRequest);
	};
}

#endif