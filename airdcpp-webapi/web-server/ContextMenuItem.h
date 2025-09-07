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

#ifndef DCPLUSPLUS_WEBSERVER_MENU_ITEM_H
#define DCPLUSPLUS_WEBSERVER_MENU_ITEM_H

#include "forward.h"

#include "Access.h"

#include <web-server/ApiSettingItem.h>

#include <airdcpp/core/header/typedefs.h>
#include <airdcpp/core/ActionHook.h>
#include <airdcpp/core/types/GetSet.h>


namespace webserver {

using ContextMenuSupportList = StringList;

struct ContextMenuItemListData {
	ContextMenuItemListData(const ContextMenuSupportList& aSupports, const AccessList aAccess, CallerPtr aCaller) noexcept :
		caller(aCaller), supports(aSupports), access(aAccess) {}

	CallerPtr caller;
	const ContextMenuSupportList supports;
	const AccessList access;
};

struct ContextMenuItemClickData {
	ContextMenuItemClickData(const string& aHookId, const string& aMenuItemId, const ContextMenuSupportList& aSupports, const AccessList aAccess, const SettingValueMap& aFormValues) noexcept :
		hookId(aHookId), menuItemId(aMenuItemId), supports(aSupports), access(aAccess), formValues(aFormValues) {}

	const string hookId;
	const string menuItemId;
	const ContextMenuSupportList supports;
	const AccessList access;
	const SettingValueMap formValues;
};

class ContextMenuItem {
public:
	typedef vector<std::shared_ptr<ContextMenuItem>> List;

	ContextMenuItem(
		const string& aId, const string& aTitle, const StringMap& aIconInfo, const ActionHookSubscriber& aHook, 
		const StringList& aUrls, const ExtensionSettingItem::List& aFormFieldDefinitions, const ContextMenuItem::List& aChildren
	) :
		id(aId), title(aTitle), iconInfo(aIconInfo), hook(aHook), urls(aUrls), formFieldDefinitions(aFormFieldDefinitions), children(aChildren) {

	}

	GETSET(string, id, Id);
	GETSET(string, title, Title);
	GETSET(StringMap, iconInfo, IconInfo);
	GETSET(ActionHookSubscriber, hook, Hook);
	GETSET(StringList, urls, Urls);
	GETSET(ExtensionSettingItem::List, formFieldDefinitions, FormFieldDefinitions);

	GETSET(List, children, Children);
};

class GroupedContextMenuItem {
public:
	GroupedContextMenuItem(const string& aId, const string& aTitle, const StringMap& aIconInfo, const ContextMenuItemList& aItems) :
		id(aId), title(aTitle), iconInfo(aIconInfo), items(aItems) {

	}

	GETSET(string, id, Id);
	GETSET(string, title, Title);
	GETSET(StringMap, iconInfo, IconInfo);

	GETSET(ContextMenuItemList, items, Items);
};

} // namespace webserver

#endif // DCPLUSPLUS_WEBSERVER_MENU_MANAGER_H
