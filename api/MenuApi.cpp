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

#include "stdinc.h"

#include <api/MenuApi.h>

#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>
#include <api/common/SettingUtils.h>

#include <api/QueueBundleUtils.h>

#include <web-server/JsonUtil.h>
#include <web-server/Session.h>
#include <web-server/WebServerManager.h>

#include <airdcpp/Bundle.h>

#include <airdcpp/ClientManager.h>
#include <airdcpp/DirectoryListing.h>
#include <airdcpp/DirectoryListingManager.h>
#include <airdcpp/PrivateChatManager.h>
#include <airdcpp/SearchManager.h>
#include <airdcpp/SearchInstance.h>

#define MYMACRO(...) __VA_ARGS__

#define CONTEXT_MENU_HANDLER(menuId, hook, hook2, idType, idDeserializerFunc, idSerializerFunc, access) \
	createHook( \
		toHookId(menuId), \
		[this](ActionHookSubscriber&& aSubscriber) { \
			return cmm.hook##MenuHook.addSubscriber( \
				std::move(aSubscriber), \
				[this](const vector<idType>& aSelections, const ContextMenuItemListData& aListData, const MenuApi::MenuActionHookResultGetter& aResultGetter) { \
					return MenuApi::menuListHookHandler<idType>(aSelections, aListData, aResultGetter, menuId, idSerializerFunc); \
				} \
			); \
		}, [this](const string& aId) { \
			cmm.hook##MenuHook.removeSubscriber(aId); \
		}, [this] { \
			return cmm.hook##MenuHook.getSubscribers(); \
		} \
	); \
	INLINE_MODULE_METHOD_HANDLER(access, METHOD_POST, (EXACT_PARAM(menuId), EXACT_PARAM("select")), [this](ApiRequest& aRequest) { \
		return handleClickItem<idType>( \
			aRequest, \
			menuId, \
			std::bind(&ContextMenuManager::onClick##hook2##Item, &cmm, placeholders::_1, placeholders::_2), \
			idDeserializerFunc \
		); \
	}); \
	INLINE_MODULE_METHOD_HANDLER(access, METHOD_POST, (EXACT_PARAM(menuId), EXACT_PARAM("list")), [this](ApiRequest& aRequest) { \
		return handleListItems<idType>( \
			aRequest, \
			std::bind(&ContextMenuManager::get##hook2##Menu, &cmm, placeholders::_1, placeholders::_2), \
			idDeserializerFunc \
		); \
	}); \
	INLINE_MODULE_METHOD_HANDLER(access, METHOD_POST, (EXACT_PARAM(menuId), EXACT_PARAM("list_grouped")), [this](ApiRequest& aRequest) { \
		return handleListItemsGrouped<idType>( \
			aRequest, \
			std::bind(&ContextMenuManager::get##hook2##Menu, &cmm, placeholders::_1, placeholders::_2), \
			idDeserializerFunc \
		); \
	});

#define ENTITY_CONTEXT_MENU_HANDLER(menuId, hook, hook2, idType, idDeserializerFunc, idSerializerFunc, entityType, entityDeserializerFunc, access) \
	createHook(toHookId(menuId), [this](ActionHookSubscriber&& aSubscriber) { \
		return cmm.hook##MenuHook.addSubscriber( \
			std::move(aSubscriber), \
			[this](const vector<idType>& aSelections, const ContextMenuItemListData& aListData, const entityType& aEntity, const MenuApi::MenuActionHookResultGetter& aResultGetter) { \
				return MenuApi::menuListHookHandler<idType>(aSelections, aListData, aResultGetter, menuId, idSerializerFunc, aEntity->getToken()); \
			} \
		); \
	}, [this](const string& aId) { \
		cmm.hook##MenuHook.removeSubscriber(aId); \
	}, [this] { \
		return cmm.hook##MenuHook.getSubscribers(); \
	}); \
	INLINE_MODULE_METHOD_HANDLER(access, METHOD_POST, (EXACT_PARAM(menuId), EXACT_PARAM("select")), [MYMACRO(=, this)](ApiRequest& aRequest) { \
		const auto entityId = JsonUtil::getRawField("entity_id", aRequest.getRequestBody()); \
		auto entity = entityDeserializerFunc(entityId, "entity_id"); \
		return handleClickItem<idType>( \
			aRequest,  \
			menuId, \
			[MYMACRO(=, this)](const vector<idType>& aSelectedIds, const ContextMenuItemClickData& aClickData) { \
				return cmm.onClick##hook2##Item(aSelectedIds, aClickData, entity); \
			}, \
			idDeserializerFunc \
		); \
	}); \
	INLINE_MODULE_METHOD_HANDLER(access, METHOD_POST, (EXACT_PARAM(menuId), EXACT_PARAM("list")), [MYMACRO(=, this)](ApiRequest& aRequest) { \
		const auto entityId = JsonUtil::getRawField("entity_id", aRequest.getRequestBody()); \
		auto entity = entityDeserializerFunc(entityId, "entity_id"); \
		return handleListItems<idType>( \
			aRequest, \
			[MYMACRO(=, this)](const vector<idType>& aSelectedIds, const ContextMenuItemListData& aListData) { \
				return cmm.get##hook2##Menu(aSelectedIds, aListData, entity); \
			}, \
			idDeserializerFunc \
		); \
	}); \
	INLINE_MODULE_METHOD_HANDLER(access, METHOD_POST, (EXACT_PARAM(menuId), EXACT_PARAM("list_grouped")), [MYMACRO(=, this)](ApiRequest& aRequest) { \
		const auto entityId = JsonUtil::getRawField("entity_id", aRequest.getRequestBody()); \
		auto entity = entityDeserializerFunc(entityId, "entity_id"); \
		return handleListItemsGrouped<idType>( \
			aRequest, \
			[MYMACRO(=, this)](const vector<idType>& aSelectedIds, const ContextMenuItemListData& aListData) { \
				return cmm.get##hook2##Menu(aSelectedIds, aListData, entity); \
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
				"hub_message_highlight_menuitem_selected",
				"private_chat_message_highlight_menuitem_selected",
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

		const auto parsePrivateChat = [](const json& aJson, const string& aFieldName) {
			auto cid = JsonUtil::parseValue<string>(aFieldName, aJson, false);
			auto instance = PrivateChatManager::getInstance()->getChat(Deserializer::getUser(cid, false));
			if (!instance) {
				JsonUtil::throwError(aFieldName, JsonUtil::ERROR_INVALID, "Invalid session ID");
			}

			return instance;
		};

		ENTITY_CONTEXT_MENU_HANDLER("hub_user", hubUser, HubUser, uint32_t, Deserializer::defaultArrayValueParser<uint32_t>, Serializer::defaultArrayValueSerializer<uint32_t>, ClientPtr, parseClient, Access::ANY);
		ENTITY_CONTEXT_MENU_HANDLER("filelist_item", filelistItem, FilelistItem, uint32_t, Deserializer::defaultArrayValueParser<uint32_t>, Serializer::defaultArrayValueSerializer<uint32_t>, DirectoryListingPtr, parseFilelist, Access::ANY);
		ENTITY_CONTEXT_MENU_HANDLER("grouped_search_result", groupedSearchResult, GroupedSearchResult, TTHValue, Deserializer::tthArrayValueParser, Serializer::defaultArrayValueSerializer<TTHValue>, SearchInstancePtr, parseSearchInstance, Access::ANY);
		ENTITY_CONTEXT_MENU_HANDLER("hub_message_highlight", hubMessageHighlight, HubMessageHighlight, uint32_t, Deserializer::defaultArrayValueParser<uint32_t>, Serializer::defaultArrayValueSerializer<uint32_t>, ClientPtr, parseClient, Access::ANY);
		ENTITY_CONTEXT_MENU_HANDLER("private_chat_message_highlight", privateChatMessageHighlight, PrivateChatMessageHighlight, uint32_t, Deserializer::defaultArrayValueParser<uint32_t>, Serializer::defaultArrayValueSerializer<uint32_t>, PrivateChatPtr, parsePrivateChat, Access::ANY);
	}

	MenuApi::~MenuApi() {
		cmm.removeListener(this);
	}


	ContextMenuItemClickData MenuApi::deserializeClickData(const json& aJson, const AccessList& aPermissions) {
		const auto hookId = JsonUtil::getField<string>("hook_id", aJson, false);
		const auto menuItemId = JsonUtil::getField<string>("menuitem_id", aJson, false);
		const auto supports = JsonUtil::getOptionalFieldDefault<StringList>("supports", aJson, StringList());

		ExtensionSettingItem::List formFieldDefinitions = deserializeFormFieldDefinitions(aJson);
		SettingValueMap formValues;

		if (!formFieldDefinitions.empty()) {
			// Deserialize values
			auto valuesJson = JsonUtil::getRawField("form_value", aJson);
			for (const auto& elem: valuesJson.items()) {
				auto setting = ApiSettingItem::findSettingItem<ExtensionSettingItem>(formFieldDefinitions, elem.key());
				if (!setting) {
					JsonUtil::throwError(elem.key(), JsonUtil::ERROR_INVALID, "Definition for the value was not found");
				}

				formValues[elem.key()] = SettingUtils::validateValue(elem.value(), *setting, nullptr);
			}
		}

		return ContextMenuItemClickData(hookId, menuItemId, supports, aPermissions, formValues);
	}

	HookApiModule::HookCompletionDataPtr MenuApi::fireMenuHook(const string& aMenuId, const json& aSelectedIds, const ContextMenuItemListData& aListData, const json& aEntityId) {
		return fireHook(toHookId(aMenuId), WEBCFG(LIST_MENUITEMS_HOOK_TIMEOUT).num(), [&]() {
			return json({
				{ "selected_ids", aSelectedIds },
				{ "permissions", Serializer::serializePermissions(aListData.access) },
				{ "entity_id", aEntityId },
				{ "supports", aListData.supports },
			});
		});
	}

	json MenuApi::serializeMenuItem(const ContextMenuItemPtr& aMenuItem) {
		return {
			{ "id", aMenuItem->getId() },
			{ "title", aMenuItem->getTitle() },
			{ "icon", aMenuItem->getIconInfo() },
			{ "hook_id", aMenuItem->getHook().getId() },
			{ "urls", aMenuItem->getUrls() },
			{ "form_definitions", aMenuItem->getFormFieldDefinitions().empty() ? json() : Serializer::serializeList(aMenuItem->getFormFieldDefinitions(), SettingUtils::serializeDefinition) },
		};
	}

	json MenuApi::serializeGroupedMenuItem(const GroupedContextMenuItemPtr& aMenuItem) {
		return {
			{ "id", aMenuItem->getId() },
			{ "title", aMenuItem->getTitle() },
			{ "icon", aMenuItem->getIconInfo() },
			{ "items", Serializer::serializeList(aMenuItem->getItems(), serializeMenuItem) },
		};
	}

	GroupedContextMenuItemPtr MenuApi::deserializeMenuItems(const json& aData, const MenuActionHookResultGetter& aResultGetter) {
		const auto menuItemsJson = JsonUtil::getArrayField("menuitems", aData, true);
		const auto id = aResultGetter.getSubscriber().getId();
		const auto title = JsonUtil::getOptionalFieldDefault("title", aData, aResultGetter.getSubscriber().getName());
		const auto iconInfo = deserializeIconInfo(JsonUtil::getOptionalRawField("icon", aData, false));

		ContextMenuItemList items;
		for (const auto& menuItem: menuItemsJson) {
			items.push_back(toMenuItem(menuItem, aResultGetter));
		}

		return make_shared<GroupedContextMenuItem>(id, title, iconInfo, items);
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

	ContextMenuItemPtr MenuApi::toMenuItem(const json& aData, const MenuActionHookResultGetter& aResultGetter) {
		const auto id = JsonUtil::getField<string>("id", aData, false);
		const auto title = JsonUtil::getField<string>("title", aData, false);
		const auto iconInfo = deserializeIconInfo(JsonUtil::getOptionalRawField("icon", aData, false));
		const auto urls = JsonUtil::getOptionalFieldDefault<StringList>("urls", aData, StringList());

		return make_shared<ContextMenuItem>(id, title, iconInfo, aResultGetter.getSubscriber(), urls, deserializeFormFieldDefinitions(aData));
	}


	ExtensionSettingItem::List MenuApi::deserializeFormFieldDefinitions(const json& aJson) {
		const auto formFieldsJson = JsonUtil::getOptionalArrayField("form_definitions", aJson);
		if (!formFieldsJson.is_null()) {
			return SettingUtils::deserializeDefinitions(formFieldsJson);
		}

		return ExtensionSettingItem::List();
	}

	void MenuApi::onMenuItemSelected(const string& aMenuId, const json& aSelectedIds, const ContextMenuItemClickData& aClickData, const json& aEntityId) noexcept {
		maybeSend(aMenuId + "_menuitem_selected", [&]() {
			json ret = {
				{ "hook_id", aClickData.hookId },
				{ "menu_id", aMenuId },
				{ "menuitem_id", aClickData.menuItemId },
				{ "selected_ids", aSelectedIds },
				{ "entity_id", aEntityId },
				{ "permissions", Serializer::serializePermissions(aClickData.access) },
				{ "supports", aClickData.supports },
				{ "form_values", aClickData.formValues },
			};

			return ret;
		});
	}

	void MenuApi::on(ContextMenuManagerListener::QueueBundleMenuSelected, const vector<uint32_t>& aSelectedIds, const ContextMenuItemClickData& aClickData) noexcept {
		onMenuItemSelected("queue_bundle", aSelectedIds, aClickData);
	}

	void MenuApi::on(ContextMenuManagerListener::QueueFileMenuSelected, const vector<uint32_t>& aSelectedIds, const ContextMenuItemClickData& aClickData) noexcept {
		onMenuItemSelected("queue_file", aSelectedIds, aClickData);
	}

	void MenuApi::on(ContextMenuManagerListener::TransferMenuSelected, const vector<uint32_t>& aSelectedIds, const ContextMenuItemClickData& aClickData) noexcept {
		onMenuItemSelected("transfer", aSelectedIds, aClickData);
	}

	void MenuApi::on(ContextMenuManagerListener::ShareRootMenuSelected, const vector<TTHValue>& aSelectedIds, const ContextMenuItemClickData& aClickData) noexcept {
		onMenuItemSelected("share_root", aSelectedIds, aClickData);
	}

	void MenuApi::on(ContextMenuManagerListener::FavoriteHubMenuSelected, const vector<uint32_t>& aSelectedIds, const ContextMenuItemClickData& aClickData) noexcept {
		onMenuItemSelected("favorite_hub", aSelectedIds, aClickData);
	}

	void MenuApi::on(ContextMenuManagerListener::UserMenuSelected, const vector<CID>& aSelectedIds, const ContextMenuItemClickData& aClickData) noexcept {
		onMenuItemSelected("user", aSelectedIds, aClickData);
	}

	void MenuApi::on(ContextMenuManagerListener::HintedUserMenuSelected, const vector<HintedUser>& aSelectedIds, const ContextMenuItemClickData& aClickData) noexcept {
		onMenuItemSelected("hinted_user", Serializer::serializeList(aSelectedIds, Serializer::serializeHintedUser), aClickData);
	}

	void MenuApi::on(ContextMenuManagerListener::HubUserMenuSelected, const vector<uint32_t>& aSelectedIds, const ClientPtr& aClient, const ContextMenuItemClickData& aClickData) noexcept {
		onMenuItemSelected("hub_user", aSelectedIds, aClickData, aClient->getToken());
	}

	void MenuApi::on(ContextMenuManagerListener::GroupedSearchResultMenuSelected, const vector<TTHValue>& aSelectedIds, const SearchInstancePtr& aInstance, const ContextMenuItemClickData& aClickData) noexcept {
		onMenuItemSelected("grouped_search_result", aSelectedIds, aClickData, aInstance->getToken());
	}

	void MenuApi::on(ContextMenuManagerListener::FilelistItemMenuSelected, const vector<uint32_t>& aSelectedIds, const DirectoryListingPtr& aList, const ContextMenuItemClickData& aClickData) noexcept {
		onMenuItemSelected("filelist_item", aSelectedIds, aClickData, aList->getToken());
	}

	void MenuApi::on(ContextMenuManagerListener::ExtensionMenuSelected, const vector<string>& aSelectedIds, const ContextMenuItemClickData& aClickData) noexcept {
		onMenuItemSelected("extension", aSelectedIds, aClickData);
	}

	void MenuApi::on(HubMessageHighlightMenuSelected, const vector<uint32_t>& aSelectedIds, const ClientPtr& aClient, const ContextMenuItemClickData& aClickData) noexcept {
		onMenuItemSelected("hub_message_highlight", aSelectedIds, aClickData, aClient->getToken());
	}

	void MenuApi::on(PrivateChatMessageHighlightMenuSelected, const vector<uint32_t>& aSelectedIds, const PrivateChatPtr& aChat, const ContextMenuItemClickData& aClickData) noexcept {
		onMenuItemSelected("private_chat_message_highlight", aSelectedIds, aClickData, aChat->getToken());
	}
}
