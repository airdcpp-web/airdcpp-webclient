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

#ifndef DCPLUSPLUS_WEBSERVER_MENU_MANAGER_LISTENER_H
#define DCPLUSPLUS_WEBSERVER_MENU_MANAGER_LISTENER_H

#include "forward.h"


namespace webserver {

struct ContextMenuItemClickData;
class ContextMenuManagerListener {
public:

	virtual ~ContextMenuManagerListener() = default;
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<1> QueueBundleMenuSelected;
	typedef X<2> QueueFileMenuSelected;
	typedef X<3> TransferMenuSelected;
	typedef X<4> ShareRootMenuSelected;
	typedef X<5> FavoriteHubMenuSelected;
	typedef X<6> UserMenuSelected;
	typedef X<8> HintedUserMenuSelected;
	typedef X<9> ExtensionMenuSelected;

	typedef X<15> FilelistItemMenuSelected;
	typedef X<16> GroupedSearchResultMenuSelected;
	typedef X<17> HubUserMenuSelected;
	typedef X<18> HubMessageHighlightMenuSelected;
	typedef X<19> PrivateChatMessageHighlightMenuSelected;


	virtual void on(QueueBundleMenuSelected, const vector<QueueToken>&, const ContextMenuItemClickData&) noexcept { }
	virtual void on(QueueFileMenuSelected, const vector<QueueToken>&, const ContextMenuItemClickData&) noexcept { }
	virtual void on(TransferMenuSelected, const vector<TransferToken>&, const ContextMenuItemClickData&) noexcept { }
	virtual void on(ShareRootMenuSelected, const vector<TTHValue>&, const ContextMenuItemClickData&) noexcept { }
	virtual void on(FavoriteHubMenuSelected, const vector<FavoriteHubToken>&, const ContextMenuItemClickData&) noexcept { }
	virtual void on(UserMenuSelected, const vector<CID>&, const ContextMenuItemClickData&) noexcept { }
	virtual void on(HintedUserMenuSelected, const vector<HintedUser>&, const ContextMenuItemClickData&) noexcept { }

	virtual void on(ExtensionMenuSelected, const vector<string>&, const ContextMenuItemClickData&) noexcept { }

	virtual void on(FilelistItemMenuSelected, const vector<DirectoryListingItemToken>&, const DirectoryListingPtr&, const ContextMenuItemClickData&) noexcept { }
	virtual void on(GroupedSearchResultMenuSelected, const vector<TTHValue>&, const SearchInstancePtr&, const ContextMenuItemClickData&) noexcept { }
	virtual void on(HubUserMenuSelected, const vector<dcpp::SID>&, const ClientPtr&, const ContextMenuItemClickData&) noexcept { }
	virtual void on(HubMessageHighlightMenuSelected, const vector<MessageHighlightToken>&, const ClientPtr&, const ContextMenuItemClickData&) noexcept { }
	virtual void on(PrivateChatMessageHighlightMenuSelected, const vector<MessageHighlightToken>&, const PrivateChatPtr&, const ContextMenuItemClickData&) noexcept { }
};

} // namespace webserver

#endif // DCPLUSPLUS_WEBSERVER_MENU_MANAGER_H
