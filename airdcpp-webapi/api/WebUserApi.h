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

#ifndef DCPLUSPLUS_DCPP_WEBUSER_API_H
#define DCPLUSPLUS_DCPP_WEBUSER_API_H

#include <api/base/SubscribableApiModule.h>
#include <api/WebUserUtils.h>
#include <api/common/ListViewController.h>

#include <web-server/WebUserManagerListener.h>

namespace webserver {
	class WebUserApi : public SubscribableApiModule, private WebUserManagerListener {
	public:
		WebUserApi(Session* aSession);
		~WebUserApi();
	private:
		api_return handleGetUsers(ApiRequest& aRequest);
		api_return handleAddUser(ApiRequest& aRequest);
		api_return handleGetUser(ApiRequest& aRequest);
		api_return handleUpdateUser(ApiRequest& aRequest);
		api_return handleRemoveUser(ApiRequest& aRequest);

		bool updateUserProperties(WebUserPtr& aUser, const json& j, bool aIsNew);

		void on(WebUserManagerListener::UserAdded, const WebUserPtr& aUser) noexcept override;
		void on(WebUserManagerListener::UserUpdated, const WebUserPtr& aUser) noexcept override;
		void on(WebUserManagerListener::UserRemoved, const WebUserPtr& aUser) noexcept override;

		typedef ListViewController<WebUserPtr, WebUserUtils::PROP_LAST> RootView;
		RootView view;

		WebUserManager& um;
		WebUserList getUsers() const noexcept;

		WebUserPtr parseUserNameParam(ApiRequest& aRequest);
	};
}

#endif