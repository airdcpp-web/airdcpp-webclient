/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_HUBENTRY_H_
#define DCPLUSPLUS_DCPP_HUBENTRY_H_

#include <airdcpp/core/types/GetSet.h>
#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/settings/HubSettings.h>

namespace dcpp {

	using std::string;


	class ShareProfile;
	class FavoriteHubEntry : public HubSettings {
	public:
		using Ptr = FavoriteHubEntry *;
		using List = vector<Ptr>;

		enum ConnectState {
			STATE_DISCONNECTED,
			STATE_CONNECTING,
			STATE_CONNECTED
		};

		FavoriteHubEntry() noexcept;

		GETSET(string, name, Name);
		GETSET(string, description, Description);
		GETSET(string, password, Password);
		GETSET(string, server, Server);

		// GUI settings
		GETSET(string, headerOrder, HeaderOrder);
		GETSET(string, headerWidths, HeaderWidths);
		GETSET(string, headerVisible, HeaderVisible);
		IGETSET(uint16_t, bottom, Bottom, 0);
		IGETSET(uint16_t, top, Top, 0);
		IGETSET(uint16_t, left, Left, 0);
		IGETSET(uint16_t, right, Right, 0);

		IGETSET(int, chatUserSplit, ChatUserSplit, 0);
		IGETSET(bool, userliststate, UserListState, true);

		IGETSET(ConnectState, connectState, ConnectState, STATE_DISCONNECTED);
		IGETSET(ClientToken, currentHubToken, CurrentHubToken, 0);

		IGETSET(bool, autoConnect, AutoConnect, true);
		GETSET(string, group, Group);
		GETSET(FavoriteHubToken, token, Token);

		bool isAdcHub() const noexcept;
		string getShareProfileName() const noexcept;
	};
}
#endif /*DCPLUSPLUS_DCPP_HUBENTRY_H_*/
