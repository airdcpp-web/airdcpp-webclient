/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_MENU_MANAGER_H
#define DCPLUSPLUS_DCPP_MENU_MANAGER_H

#include "stdinc.h"

#include "Access.h"

#include <web-server/ApiSettingItem.h>

#include <airdcpp/ActionHook.h>
#include <airdcpp/GetSet.h>
#include <airdcpp/Singleton.h>
#include <airdcpp/Speaker.h>



#define CONTEXT_MENU(type, name, name2) \
	ActionHook<ContextMenuItemList, const vector<type>&, const ContextMenuItemListData&> name##MenuHook; \
	ContextMenuItemList get##name2##Menu(const vector<type>& aItems, const ContextMenuItemListData& aListData) const noexcept { \
		return ActionHook<ContextMenuItemList>::normalizeListItems(name##MenuHook.runHooksData(aListData.caller, aItems, aListData)); \
	} \
	void onClick##name2##Item(const vector<type>& aItems, const ContextMenuItemClickData& aClickData) noexcept { \
		fire(ContextMenuManagerListener::name2##MenuSelected(), aItems, aClickData); \
	}


#define ENTITY_CONTEXT_MENU(type, name, name2, entityType) \
	ActionHook<ContextMenuItemList, const vector<type>&, const ContextMenuItemListData&, const entityType&> name##MenuHook; \
	ContextMenuItemList get##name2##Menu(const vector<type>& aItems, const ContextMenuItemListData& aListData, const entityType& aEntity) const noexcept { \
		return ActionHook<ContextMenuItemList>::normalizeListItems(name##MenuHook.runHooksData(aListData.caller, aItems, aListData, aEntity)); \
	} \
	void onClick##name2##Item(const vector<type>& aItems, const ContextMenuItemClickData& aClickData, const entityType& aEntity) noexcept { \
		fire(ContextMenuManagerListener::name2##MenuSelected(), aItems, aEntity, aClickData); \
	}


namespace webserver {
	typedef StringList ContextMenuSupportList;

	struct ContextMenuItemListData {
		ContextMenuItemListData(const ContextMenuSupportList& aSupports, const AccessList aAccess, const void* aCaller) noexcept :
			supports(aSupports), access(aAccess), caller(aCaller) {}

		const void* caller;
		const ContextMenuSupportList supports;
		const AccessList access;
	};

	struct ContextMenuItemClickData {
		ContextMenuItemClickData(const string& aHookId, const string& aMenuItemId, const ContextMenuSupportList& aSupports, const AccessList aAccess, const SettingValueMap aFormValues) noexcept :
			hookId(aHookId), menuItemId(aMenuItemId), supports(aSupports), access(aAccess), formValues(aFormValues) {}

		const string hookId;
		const string menuItemId;
		const ContextMenuSupportList supports;
		const AccessList access;
		const SettingValueMap formValues;
	};

	class ContextMenuManagerListener {
	public:

		virtual ~ContextMenuManagerListener() { }
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


		virtual void on(QueueBundleMenuSelected, const vector<uint32_t>&, const ContextMenuItemClickData&) noexcept { }
		virtual void on(QueueFileMenuSelected, const vector<uint32_t>&, const ContextMenuItemClickData&) noexcept { }
		virtual void on(TransferMenuSelected, const vector<uint32_t>&, const ContextMenuItemClickData&) noexcept { }
		virtual void on(ShareRootMenuSelected, const vector<TTHValue>&, const ContextMenuItemClickData&) noexcept { }
		virtual void on(FavoriteHubMenuSelected, const vector<uint32_t>&, const ContextMenuItemClickData&) noexcept { }
		virtual void on(UserMenuSelected, const vector<CID>&, const ContextMenuItemClickData&) noexcept { }
		virtual void on(HintedUserMenuSelected, const vector<HintedUser>&, const ContextMenuItemClickData&) noexcept { }

		virtual void on(ExtensionMenuSelected, const vector<string>&, const ContextMenuItemClickData&) noexcept { }

		virtual void on(FilelistItemMenuSelected, const vector<uint32_t>&, const DirectoryListingPtr&, const ContextMenuItemClickData&) noexcept { }
		virtual void on(GroupedSearchResultMenuSelected, const vector<TTHValue>&, const SearchInstancePtr&, const ContextMenuItemClickData&) noexcept { }
		virtual void on(HubUserMenuSelected, const vector<uint32_t>&, const ClientPtr&, const ContextMenuItemClickData&) noexcept { }
		virtual void on(HubMessageHighlightMenuSelected, const vector<uint32_t>&, const ClientPtr&, const ContextMenuItemClickData&) noexcept { }
		virtual void on(PrivateChatMessageHighlightMenuSelected, const vector<uint32_t>&, const PrivateChatPtr&, const ContextMenuItemClickData&) noexcept { }
	};

	class ContextMenuItem {
	public:
		ContextMenuItem(const string& aId, const string& aTitle, const StringMap& aIconInfo, const string& aHookId, const StringList& aUrls, const ExtensionSettingItem::List& aFormFieldDefinitions) :
			id(aId), title(aTitle), iconInfo(aIconInfo), hookId(aHookId), urls(aUrls), formFieldDefinitions(aFormFieldDefinitions) {

		}

		GETSET(string, id, Id);
		GETSET(string, title, Title);
		GETSET(StringMap, iconInfo, IconInfo);
		GETSET(string, hookId, HookId);
		GETSET(StringList, urls, Urls);
		GETSET(ExtensionSettingItem::List, formFieldDefinitions, FormFieldDefinitions);
	private:
	};

	class ContextMenuManager : public Speaker<ContextMenuManagerListener>
	{

	public:
		static const string URLS_SUPPORT;

		CONTEXT_MENU(uint32_t, queueBundle, QueueBundle);
		CONTEXT_MENU(uint32_t, queueFile, QueueFile);
		CONTEXT_MENU(TTHValue, shareRoot, ShareRoot);
		CONTEXT_MENU(CID, user, User);
		CONTEXT_MENU(uint32_t, transfer, Transfer);
		CONTEXT_MENU(uint32_t, favoriteHub, FavoriteHub);
		CONTEXT_MENU(HintedUser, hintedUser, HintedUser);
		CONTEXT_MENU(string, extension, Extension);

		ENTITY_CONTEXT_MENU(uint32_t, filelistItem, FilelistItem, DirectoryListingPtr);
		ENTITY_CONTEXT_MENU(TTHValue, groupedSearchResult, GroupedSearchResult, SearchInstancePtr);
		ENTITY_CONTEXT_MENU(uint32_t, hubUser, HubUser, ClientPtr);
		ENTITY_CONTEXT_MENU(uint32_t, hubMessageHighlight, HubMessageHighlight, ClientPtr);
		ENTITY_CONTEXT_MENU(uint32_t, privateChatMessageHighlight, PrivateChatMessageHighlight, PrivateChatPtr);

		typedef vector<ContextMenuItem> MenuItemList;

		ContextMenuManager();
		~ContextMenuManager();
	private:
	};

} // namespace webserver

#endif // DCPLUSPLUS_DCPP_MENU_MANAGER_H
