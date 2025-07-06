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

#ifndef DCPLUSPLUS_DCPP_USER_API_H
#define DCPLUSPLUS_DCPP_USER_API_H

#include <api/base/SubscribableApiModule.h>

#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/hub/ClientManagerListener.h>
#include <airdcpp/hub/UserConnectResult.h>

#include <airdcpp/user/ignore/IgnoreManagerListener.h>

namespace webserver {
	class UserApi : public SubscribableApiModule, private IgnoreManagerListener, private ClientManagerListener {
	public:
		UserApi(Session* aSession);
		~UserApi();
	private:
		UserPtr getUser(ApiRequest& aRequest);

		api_return handleIgnore(ApiRequest& aRequest);
		api_return handleUnignore(ApiRequest& aRequest);
		api_return handleGetIgnores(ApiRequest& aRequest);

		api_return handleGetUser(ApiRequest& aRequest);
		api_return handleSearchNicks(ApiRequest& aRequest);
		api_return handleSearchHintedUser(ApiRequest& aRequest);

		api_return handleGrantSlot(ApiRequest& aRequest);

		void on(IgnoreManagerListener::IgnoreAdded, const UserPtr& aUser) noexcept override;
		void on(IgnoreManagerListener::IgnoreRemoved, const UserPtr& aUser) noexcept override;

		void on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool) noexcept override;
		void on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept override;
		void on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool) noexcept override;

		static json serializeConnectResult(const optional<UserConnectResult> aResult) noexcept;
	};
}

#endif