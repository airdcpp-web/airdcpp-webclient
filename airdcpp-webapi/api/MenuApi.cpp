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

#include "stdinc.h"

#include <api/MenuApi.h>

#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>

#include <api/QueueBundleUtils.h>

#include <web-server/JsonUtil.h>
#include <web-server/Session.h>
#include <web-server/WebServerManager.h>

#include <airdcpp/Bundle.h>

#include <airdcpp/DirectoryListingManager.h>
#include <airdcpp/ClientManager.h>
#include <airdcpp/SearchManager.h>
#include <airdcpp/SearchInstance.h>
#include <airdcpp/DirectoryListing.h>


#define CONTEXT_MENU_HANDLER(menuId, hook, hook2, idType, idDeserializerFunc, idSerializerFunc, access) \
	createHook(toHookId(menuId), [this](const string& aId, const string& aName) { \
		return cmm.hook##MenuHook.addSubscriber( \
			aId, \
			aName, \
			[this](const vector<idType>& aSelections, const AccessList& aAccessList, const ActionHookResultGetter<ContextMenuItemList>& aResultGetter) { \
				return MenuApi::menuListHookHandler<idType>(aSelections, aAccessList, aResultGetter, menuId, idSerializerFunc); \
			} \
		); \
	}, [this](const string& aId) { \
		cmm.hook##MenuHook.removeSubscriber(aId); \
	}); \
	INLINE_MODULE_METHOD_HANDLER(access, METHOD_POST, (EXACT_PARAM(menuId), EXACT_PARAM("select")), [=](ApiRequest& aRequest) { \
		return handleClickItem<idType>( \
			aRequest, \
			menuId, \
			std::bind(&ContextMenuManager::onClick##hook2##Item, &cmm, placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4), \
			idDeserializerFunc \
		); \
	}); \
	INLINE_MODULE_METHOD_HANDLER(access, METHOD_POST, (EXACT_PARAM(menuId), EXACT_PARAM("list")), [=](ApiRequest& aRequest) { \
		return handleListItems<idType>( \
			aRequest, \
			std::bind(&ContextMenuManager::get##hook2##Menu, &cmm, placeholders::_1, placeholders::_2), \
			idDeserializerFunc \
		); \
	});

#define ENTITY_CONTEXT_MENU_HANDLER(menuId, hook, hook2, idType, idDeserializerFunc, idSerializerFunc, entityType, entityDeserializerFunc, access) \
	createHook(toHookId(menuId), [this](const string& aId, const string& aName) { \
		return cmm.hook##MenuHook.addSubscriber( \
			aId, \
			aName, \
			[this](const vector<idType>& aSelections, const AccessList& aAccessList, const entityType& aEntity, const ActionHookResultGetter<ContextMenuItemList>& aResultGetter) { \
				return MenuApi::menuListHookHandler<idType>(aSelections, aAccessList, aResultGetter, menuId, idSerializerFunc, aEntity->getToken()); \
			} \
		); \
	}, [this](const string& aId) { \
		cmm.hook##MenuHook.removeSubscriber(aId); \
	}); \
	INLINE_MODULE_METHOD_HANDLER(access, METHOD_POST, (EXACT_PARAM(menuId), EXACT_PARAM("select")), [=](ApiRequest& aRequest) { \
		const auto entityId = JsonUtil::getRawField("entity_id", aRequest.getRequestBody()); \
		auto entity = entityDeserializerFunc(entityId, "entity_id"); \
		return handleClickItem<idType>( \
			aRequest,  \
			menuId, \
			[=](const vector<idType>& aSelectedIds, const AccessList& aAccessList, const string& aHookId, const string& aMenuId) { \
				return cmm.onClick##hook2##Item(aSelectedIds, aAccessList, aHookId, aMenuId, entity); \
			}, \
			idDeserializerFunc \
		); \
	}); \
	INLINE_MODULE_METHOD_HANDLER(access, METHOD_POST, (EXACT_PARAM(menuId), EXACT_PARAM("list")), [=](ApiRequest& aRequest) { \
		const auto entityId = JsonUtil::getRawField("entity_id", aRequest.getRequestBody()); \
		auto entity = entityDeserializerFunc(entityId, "entity_id"); \
		return handleListItems<idType>( \
			aRequest, \
			[=](const vector<idType>& aSelectedIds, const AccessList& aAccessList) { \
				return cmm.get##hook2##Menu(aSelectedIds, aAccessList, entity); \
			}, \
			idDeserializerFunc \
		); \
	});

namespace webserver {
	MenuApi::MenuApi(Session* aSession) : 
		cmm(aSession->getServer()->getContextMenuManager()),
		HookApiModule(
			aSession,
			Access::ANY,
			{
				"queue_bundle_menuitem_selected",
				"queue_file_menuitem_selected",
				"transfer_menuitem_selected",
				"favorite_hub_menuitem_selected",
				"share_root_menuitem_selected",
				"user_menuitem_selected",
				"hinted_user_menuitem_selected",
				"extension_menuitem_selected",
				"hub_user_menuitem_selected",
				"grouped_search_result_menuitem_selected",
				"filelist_item_menuitem_selected",
			},
			Access::ANY
		) {

		cmm.addListener(this);

		CONTEXT_MENU_HANDLER("queue_bundle", queueBundle, QueueBundle, uint32_t, Deserializer::defaultArrayValueParser<uint32_t>, Serializer::defaultArrayValueSerializer<uint32_t>, Access::ANY);
		CONTEXT_MENU_HANDLER("queue_file", queueFile, QueueFile, uint32_t, Deserializer::defaultArrayValueParser<uint32_t>, Serializer::defaultArrayValueSerializer<uint32_t>, Access::ANY);
		CONTEXT_MENU_HANDLER("transfer", transfer, Transfer, uint32_t, Deserializer::defaultArrayValueParser<uint32_t>, Serializer::defaultArrayValueSerializer<uint32_t>, Access::ANY);
		CONTEXT_MENU_HANDLER("favorite_hub", favoriteHub, FavoriteHub, uint32_t, Deserializer::defaultArrayValueParser<uint32_t>, Serializer::defaultArrayValueSerializer<uint32_t>, Access::ANY);

		CONTEXT_MENU_HANDLER("share_root", shareRoot, ShareRoot, TTHValue, Deserializer::tthArrayValueParser, Serializer::defaultArrayValueSerializer<TTHValue>, Access::ANY);
		CONTEXT_MENU_HANDLER("user", user, User, CID, Deserializer::cidArrayValueParser, Serializer::defaultArrayValueSerializer<CID>, Access::ANY);
		CONTEXT_MENU_HANDLER("hinted_user", hintedUser, HintedUser, HintedUser, Deserializer::hintedUserArrayValueParser, Serializer::serializeHintedUser, Access::ANY);
		CONTEXT_MENU_HANDLER("extension", extension, Extension, string, Deserializer::defaultArrayValueParser<string>, Serializer::defaultArrayValueSerializer<string>, Access::ANY);

		const auto parseFilelist = [](const json& aJson, const string& aFieldName) {
			auto cidStr = JsonUtil::parseValue<string>(aFieldName, aJson, false);
			auto user = Deserializer::getUser(cidStr, true);

			auto filelist = DirectoryListingManager::getInstance()->findList(user);
			if (!filelist) {
				JsonUtil::throwError(aFieldName, JsonUtil::ERROR_INVALID, "Invalid session ID");
			}

			return filelist;
		};

		const auto parseSearchInstance = [](const json& aJson, const string& aFieldName) {
			auto instanceId = JsonUtil::parseValue<uint32_t>(aFieldName, aJson, false);
			auto instance = SearchManager::getInstance()->getSearchInstance(instanceId);
			if (!instance) {
				JsonUtil::throwError(aFieldName, JsonUtil::ERROR_INVALID, "Invalid session ID");
			}

			return instance;
		};

		const auto parseClient = [](const json& aJson, const string& aFieldName) {
			auto sessionId = JsonUtil::parseValue<uint32_t>(aFieldName, aJson, false);
			auto instance = ClientManager::getInstance()->getClient(sessionId);
			if (!instance) {
				JsonUtil::throwError(aFieldName, JsonUtil::ERROR_INVALID, "Invalid session ID");
			}

			return instance;
		};

		ENTITY_CONTEXT_MENU_HANDLER("hub_user", hubUser, HubUser, uint32_t, Deserializer::defaultArrayValueParser<uint32_t>, Serializer::defaultArrayValueSerializer<uint32_t>, ClientPtr, parseClient, Access::ANY);
		ENTITY_CONTEXT_MENU_HANDLER("filelist_item", filelistItem, FilelistItem, uint32_t, Deserializer::defaultArrayValueParser<uint32_t>, Serializer::defaultArrayValueSerializer<uint32_t>, DirectoryListingPtr, parseFilelist, Access::ANY);
		ENTITY_CONTEXT_MENU_HANDLER("grouped_search_result", groupedSearchResult, GroupedSearchResult, TTHValue, Deserializer::tthArrayValueParser, Serializer::defaultArrayValueSerializer<TTHValue>, SearchInstancePtr, parseSearchInstance, Access::ANY);
	}

	MenuApi::~MenuApi() {
		cmm.removeListener(this);
	}

	json MenuApi::serializeMenuItem(const ContextMenuItemPtr& aMenuItem) {
		return {
			{ "id", aMenuItem->getId() },
			{ "title", aMenuItem->getTitle() },
			{ "icon", aMenuItem->getIconInfo() },
			{ "hook_id", aMenuItem->getHookId() },
		};
	}

	ContextMenuItemList MenuApi::deserializeMenuItems(const json& aData, const ActionHookResultGetter<ContextMenuItemList>& aResultGetter) {
		const auto menuItemsJson = JsonUtil::getArrayField("menuitems", aData, true);

		ContextMenuItemList ret;
		for (const auto& menuItem: menuItemsJson) {
			ret.push_back(toMenuItem(menuItem, aResultGetter));
		}

		return ret;
	}

	StringMap MenuApi::deserializeIconInfo(const json& aJson) {
		StringMap iconInfo;
		if (!aJson.is_null()) {
			if (!aJson.is_object()) {
				JsonUtil::throwError("icon", JsonUtil::ERROR_INVALID, "Field must be an object");
			}

			for (const auto& entry : aJson.items()) {
				iconInfo[entry.key()] = entry.value();
			}
		}

		return iconInfo;
	}

	ContextMenuItemPtr MenuApi::toMenuItem(const json& aData, const ActionHookResultGetter<ContextMenuItemList>& aResultGetter) {
		const auto id = JsonUtil::getField<string>("id", aData, false);
		const auto title = JsonUtil::getField<string>("title", aData, false);
		const auto iconInfo = deserializeIconInfo(JsonUtil::getOptionalRawField("icon", aData, false));

		return make_shared<ContextMenuItem>(id, title, iconInfo, aResultGetter.getId());
	}

	void MenuApi::onMenuItemSelected(const string& aMenuId, const json& aSelectedIds, const AccessList& aAccessList, const string& aHookId, const string& aMenuItemId, const json& aEntityId) noexcept {
		maybeSend(aMenuId + "_menuitem_selected", [&]() {
			json ret = {
				{ "hook_id", aHookId },
				{ "menu_id", aMenuId },
				{ "menuitem_id", aMenuItemId },
				{ "selected_ids", aSelectedIds },
				{ "entity_id", aEntityId },
				{ "permissions", Serializer::serializePermissions(aAccessList) }
			};

			return ret;
		});
	}

	void MenuApi::on(ContextMenuManagerListener::QueueBundleMenuSelected, const vector<uint32_t>& aSelectedIds, const AccessList& aAccessList, const string& aHookId, const string& aMenuItemId) noexcept {
		onMenuItemSelected("queue_bundle", aSelectedIds, aAccessList, aHookId, aMenuItemId);
	}

	void MenuApi::on(ContextMenuManagerListener::QueueFileMenuSelected, const vector<uint32_t>& aSelectedIds, const AccessList& aAccessList, const string& aHookId, const string& aMenuItemId) noexcept {
		onMenuItemSelected("queue_file", aSelectedIds, aAccessList, aHookId, aMenuItemId);
	}

	void MenuApi::on(ContextMenuManagerListener::TransferMenuSelected, const vector<uint32_t>& aSelectedIds, const AccessList& aAccessList, const string& aHookId, const string& aMenuItemId) noexcept {
		onMenuItemSelected("transfer", aSelectedIds, aAccessList, aHookId, aMenuItemId);
	}

	void MenuApi::on(ContextMenuManagerListener::ShareRootMenuSelected, const vector<TTHValue>& aSelectedIds, const AccessList& aAccessList, const string& aHookId, const string& aMenuItemId) noexcept {
		onMenuItemSelected("share_root", aSelectedIds, aAccessList, aHookId, aMenuItemId);
	}

	void MenuApi::on(ContextMenuManagerListener::FavoriteHubMenuSelected, const vector<uint32_t>& aSelectedIds, const AccessList& aAccessList, const string& aHookId, const string& aMenuItemId) noexcept {
		onMenuItemSelected("favorite_hub", aSelectedIds, aAccessList, aHookId, aMenuItemId);
	}

	void MenuApi::on(ContextMenuManagerListener::UserMenuSelected, const vector<CID>& aSelectedIds, const AccessList& aAccessList, const string& aHookId, const string& aMenuItemId) noexcept {
		onMenuItemSelected("user", aSelectedIds, aAccessList, aHookId, aMenuItemId);
	}

	void MenuApi::on(ContextMenuManagerListener::HintedUserMenuSelected, const vector<HintedUser>& aSelectedIds, const AccessList& aAccessList, const string& aHookId, const string& aMenuItemId) noexcept {
		onMenuItemSelected("hinted_user", Serializer::serializeList(aSelectedIds, Serializer::serializeHintedUser), aAccessList, aHookId, aMenuItemId);
	}

	void MenuApi::on(ContextMenuManagerListener::HubUserMenuSelected, const vector<uint32_t>& aSelectedIds, const AccessList& aAccessList, const ClientPtr& aClient, const string& aHookId, const string& aMenuItemId) noexcept {
		onMenuItemSelected("hub_user", aSelectedIds, aAccessList, aHookId, aMenuItemId, aClient->getToken());
	}

	void MenuApi::on(ContextMenuManagerListener::GroupedSearchResultMenuSelected, const vector<TTHValue>& aSelectedIds, const AccessList& aAccessList, const SearchInstancePtr& aInstance, const string& aHookId, const string& aMenuItemId) noexcept {
		onMenuItemSelected("grouped_search_result", aSelectedIds, aAccessList, aHookId, aMenuItemId, aInstance->getToken());
	}

	void MenuApi::on(ContextMenuManagerListener::FilelistItemMenuSelected, const vector<uint32_t>& aSelectedIds, const AccessList& aAccessList, const DirectoryListingPtr& aList, const string& aHookId, const string& aMenuItemId) noexcept {
		onMenuItemSelected("filelist_item", aSelectedIds, aAccessList, aHookId, aMenuItemId, aList->getToken());
	}

	void MenuApi::on(ContextMenuManagerListener::ExtensionMenuSelected, const vector<string>& aSelectedIds, const AccessList& aAccessList, const string& aHookId, const string& aMenuItemId) noexcept {
		onMenuItemSelected("extension", aSelectedIds, aAccessList, aHookId, aMenuItemId);
	}
}
