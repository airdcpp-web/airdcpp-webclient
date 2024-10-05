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

#ifndef DCPLUSPLUS_WEBSERVER_MENU_MANAGER_H
#define DCPLUSPLUS_WEBSERVER_MENU_MANAGER_H

#include "forward.h"

#include "Access.h"
#include "ContextMenuItem.h"
#include "ContextMenuManagerListener.h"

#include <airdcpp/core/ActionHook.h>
#include <airdcpp/core/Singleton.h>
#include <airdcpp/core/Speaker.h>



#define CONTEXT_MENU(type, name, name2) \
	ActionHook<GroupedContextMenuItemPtr, const vector<type>&, const ContextMenuItemListData&> name##MenuHook; \
	GroupedContextMenuItemList get##name2##Menu(const vector<type>& aItems, const ContextMenuItemListData& aListData) const noexcept { \
		return ActionHook<GroupedContextMenuItemPtr>::normalizeData(name##MenuHook.runHooksData(aListData.caller, aItems, aListData)); \
	} \
	void onClick##name2##Item(const vector<type>& aItems, const ContextMenuItemClickData& aClickData) noexcept { \
		fire(ContextMenuManagerListener::name2##MenuSelected(), aItems, aClickData); \
	}


#define ENTITY_CONTEXT_MENU(type, name, name2, entityType) \
	ActionHook<GroupedContextMenuItemPtr, const vector<type>&, const ContextMenuItemListData&, const entityType&> name##MenuHook; \
	GroupedContextMenuItemList get##name2##Menu(const vector<type>& aItems, const ContextMenuItemListData& aListData, const entityType& aEntity) const noexcept { \
		return ActionHook<GroupedContextMenuItemPtr>::normalizeData(name##MenuHook.runHooksData(aListData.caller, aItems, aListData, aEntity)); \
	} \
	void onClick##name2##Item(const vector<type>& aItems, const ContextMenuItemClickData& aClickData, const entityType& aEntity) noexcept { \
		fire(ContextMenuManagerListener::name2##MenuSelected(), aItems, aEntity, aClickData); \
	}


namespace webserver {

class ContextMenuManager : public Speaker<ContextMenuManagerListener>
{

public:
	static const string URLS_SUPPORT;

	CONTEXT_MENU(QueueToken, queueBundle, QueueBundle);
	CONTEXT_MENU(QueueToken, queueFile, QueueFile);
	CONTEXT_MENU(TTHValue, shareRoot, ShareRoot);
	CONTEXT_MENU(CID, user, User);
	CONTEXT_MENU(TransferToken, transfer, Transfer);
	CONTEXT_MENU(FavoriteHubToken, favoriteHub, FavoriteHub);
	CONTEXT_MENU(HintedUser, hintedUser, HintedUser);
	CONTEXT_MENU(string, extension, Extension);

	ENTITY_CONTEXT_MENU(DirectoryListingItemToken, filelistItem, FilelistItem, DirectoryListingPtr);
	ENTITY_CONTEXT_MENU(TTHValue, groupedSearchResult, GroupedSearchResult, SearchInstancePtr);
	ENTITY_CONTEXT_MENU(dcpp::SID, hubUser, HubUser, ClientPtr);
	ENTITY_CONTEXT_MENU(MessageHighlightToken, hubMessageHighlight, HubMessageHighlight, ClientPtr);
	ENTITY_CONTEXT_MENU(MessageHighlightToken, privateChatMessageHighlight, PrivateChatMessageHighlight, PrivateChatPtr);

	ContextMenuManager();
	~ContextMenuManager() override;
};

} // namespace webserver

#endif // DCPLUSPLUS_WEBSERVER_MENU_MANAGER_H
