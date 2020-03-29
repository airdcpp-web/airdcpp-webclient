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

#include "typedefs.h"

#include "ActionHook.h"
#include "GetSet.h"
#include "Singleton.h"
#include "Speaker.h"



#define CONTEXT_MENU(type, name, name2) \
	ActionHook<ContextMenuItemList, const vector<type>&> name##MenuHook; \
	ContextMenuItemList get##name2##Menu(const vector<type>& aItems) const noexcept { \
		return normalizeMenuItems(name##MenuHook.runHooksData(aItems)); \
	} \
	void onClick##name2##Item(const vector<type>& aItems, const string& aHookId, const string& aMenuItemId) noexcept { \
		fire(ContextMenuManagerListener::name2##MenuSelected(), aItems, aHookId, aMenuItemId); \
	}


#define ENTITY_CONTEXT_MENU(type, name, name2, entityType) \
	ActionHook<ContextMenuItemList, const vector<type>&, const entityType&> name##MenuHook; \
	ContextMenuItemList get##name2##Menu(const vector<type>& aItems, const entityType& aEntity) const noexcept { \
		return normalizeMenuItems(name##MenuHook.runHooksData(aItems, aEntity)); \
	} \
	void onClick##name2##Item(const vector<type>& aItems, const string& aHookId, const string& aMenuItemId, const entityType& aEntity) noexcept { \
		fire(ContextMenuManagerListener::name2##MenuSelected(), aItems, aEntity, aHookId, aMenuItemId); \
	}


namespace dcpp {
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

		typedef X<10> FilelistItemMenuSelected;
		typedef X<11> GroupedSearchResultMenuSelected;
		typedef X<12> HubUserMenuSelected;


		virtual void on(QueueBundleMenuSelected, const vector<uint32_t>&, const string& /*aHookId*/, const string& /*aMenuItemId*/) noexcept { }
		virtual void on(QueueFileMenuSelected, const vector<uint32_t>&, const string& /*aHookId*/, const string& /*aMenuItemId*/) noexcept { }
		virtual void on(TransferMenuSelected, const vector<uint32_t>&, const string& /*aHookId*/, const string& /*aMenuItemId*/) noexcept { }
		virtual void on(ShareRootMenuSelected, const vector<TTHValue>&, const string& /*aHookId*/, const string& /*aMenuItemId*/) noexcept { }
		virtual void on(FavoriteHubMenuSelected, const vector<uint32_t>&, const string& /*aHookId*/, const string& /*aMenuItemId*/) noexcept { }
		virtual void on(UserMenuSelected, const vector<CID>&, const string& /*aHookId*/, const string& /*aMenuItemId*/) noexcept { }
		virtual void on(HintedUserMenuSelected, const vector<HintedUser>&, const string& /*aHookId*/, const string& /*aMenuItemId*/) noexcept { }

		virtual void on(FilelistItemMenuSelected, const vector<uint32_t>&, const DirectoryListingPtr&, const string& /*aHookId*/, const string& /*aMenuItemId*/) noexcept { }
		virtual void on(GroupedSearchResultMenuSelected, const vector<TTHValue>&, const SearchInstancePtr&, const string& /*aHookId*/, const string& /*aMenuItemId*/) noexcept { }
		virtual void on(HubUserMenuSelected, const vector<uint32_t>&, const ClientPtr&, const string& /*aHookId*/, const string& /*aMenuItemId*/) noexcept { }

		virtual void on(ExtensionMenuSelected, const vector<string>&, const string& /*aHookId*/, const string& /*aMenuItemId*/) noexcept { }
	};

	class ContextMenuItem {
	public:
		ContextMenuItem(const string& aId, const string& aTitle, const StringMap& aIconInfo, const string& aHookId) : id(aId), title(aTitle), iconInfo(aIconInfo), hookId(aHookId) {

		}

		GETSET(string, id, Id);
		GETSET(string, title, Title);
		GETSET(StringMap, iconInfo, IconInfo);
		GETSET(string, hookId, HookId);
	private:
	};

	class ContextMenuManager : public Speaker<ContextMenuManagerListener>, public Singleton<ContextMenuManager>
	{

	public:
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

		typedef vector<ContextMenuItem> MenuItemList;

		ContextMenuManager();
		~ContextMenuManager();

		static ContextMenuItemList normalizeMenuItems(const ActionHookDataList<ContextMenuItemList>& aResult) noexcept;
	private:
	};

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_MENU_MANAGER_H
