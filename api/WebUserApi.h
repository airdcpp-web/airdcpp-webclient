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

#ifndef DCPLUSPLUS_DCPP_WEBUSER_API_H
#define DCPLUSPLUS_DCPP_WEBUSER_API_H

#include <web-server/stdinc.h>

#include <api/ApiModule.h>
#include <api/common/ListViewController.h>

#include <web-server/WebUserManagerListener.h>

namespace webserver {
	class WebUserApi : public ApiModule, private WebUserManagerListener {
	public:
		WebUserApi(Session* aSession);
		~WebUserApi();

		int getVersion() const noexcept {
			return 0;
		}

		const PropertyList properties = {
			{ PROP_NAME, "username", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
			{ PROP_PERMISSIONS, "permissions", TYPE_LIST_NUMERIC, SERIALIZE_CUSTOM, SORT_CUSTOM },
			{ PROP_ACTIVE_SESSIONS, "active_sessions", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
			{ PROP_LAST_LOGIN, "last_login", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
		};

		enum Properties {
			PROP_TOKEN = -1,
			PROP_NAME,
			PROP_PERMISSIONS,
			PROP_ACTIVE_SESSIONS,
			PROP_LAST_LOGIN,
			PROP_LAST
		};
	private:
		api_return handleGetUsers(ApiRequest& aRequest);
		api_return handleAddUser(ApiRequest& aRequest);
		api_return handleUpdateUser(ApiRequest& aRequest);
		api_return handleRemoveUser(ApiRequest& aRequest);
		void parseUser(WebUserPtr& aUser, const json& j, bool aIsNew);

		void on(WebUserManagerListener::UserAdded, const WebUserPtr& aUser) noexcept;
		void on(WebUserManagerListener::UserUpdated, const WebUserPtr& aUser) noexcept;
		void on(WebUserManagerListener::UserRemoved, const WebUserPtr& aUser) noexcept;

		PropertyItemHandler<WebUserPtr> itemHandler;

		typedef ListViewController<WebUserPtr, PROP_LAST> RootView;
		RootView view;

		WebUserManager& um;
		WebUserList getUsers() const noexcept;
	};
}

#endif