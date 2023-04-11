/*
* Copyright (C) 2011-2023 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_MENUAPI_H
#define DCPLUSPLUS_DCPP_MENUAPI_H

#include <api/base/HookApiModule.h>

#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>

#include <web-server/ApiRequest.h>
#include <web-server/ContextMenuManager.h>
#include <web-server/JsonUtil.h>
#include <web-server/Session.h>

#include <airdcpp/typedefs.h>


namespace webserver {
	class MenuApi : public HookApiModule, private ContextMenuManagerListener {
	public:
		MenuApi(Session* aSession);
		~MenuApi();
	private:
		ContextMenuManager& cmm;

		static string toHookId(const string& aMenuId) noexcept {
			return aMenuId + "_list_menuitems";
		}

		using MenuActionHookResultGetter = ActionHookResultGetter<GroupedContextMenuItemPtr>;

		static StringMap deserializeIconInfo(const json& aJson);

		static ContextMenuItemPtr toMenuItem(const json& aData, const MenuActionHookResultGetter& aResultGetter);
		static GroupedContextMenuItemPtr deserializeMenuItems(const json& aData, const MenuActionHookResultGetter& aResultGetter);

		static ExtensionSettingItem::List deserializeFormFieldDefinitions(const json& aJson);

		static json serializeMenuItem(const ContextMenuItemPtr& aMenuItem);
		static json serializeGroupedMenuItem(const GroupedContextMenuItemPtr& aMenuItem);

		template<typename IdT>
		using IdSerializer = std::function<json(const IdT& aId)>;

		template<typename IdT>
		ActionHookResult<GroupedContextMenuItemPtr> menuListHookHandler(const vector<IdT>& aSelections, const ContextMenuItemListData& aListData, const MenuActionHookResultGetter& aResultGetter, const string& aMenuId, const IdSerializer<IdT>& aIdSerializer, const json& aEntityId = nullptr) {
			return HookCompletionData::toResult<GroupedContextMenuItemPtr>(
				fireMenuHook(aMenuId, Serializer::serializeList(aSelections, aIdSerializer), aListData, aEntityId),
				aResultGetter,
				MenuApi::deserializeMenuItems
			);
		}

		HookCompletionDataPtr fireMenuHook(const string& aMenuId, const json& aSelectedIds, const ContextMenuItemListData& aListData, const json& aEntityId);

		template<typename IdT>
		static vector<IdT> deserializeItemIds(ApiRequest& aRequest, const Deserializer::ArrayDeserializerFunc<IdT>& aIdDeserializerFunc) {
			return Deserializer::deserializeList<IdT>("selected_ids", aRequest.getRequestBody(), aIdDeserializerFunc, false);
		}

		template<typename IdT>
		using ClickHandlerFunc = std::function<void(const vector<IdT>& aId, const ContextMenuItemClickData& aClickData)>;

		template<typename IdT>
		api_return handleClickItem(ApiRequest& aRequest, const string& aMenuId, const ClickHandlerFunc<IdT>& aHandler, const Deserializer::ArrayDeserializerFunc<IdT>& aIdDeserializerFunc) {
			const auto selectedIds = deserializeItemIds<IdT>(aRequest, aIdDeserializerFunc);

			const auto accessList = aRequest.getSession()->getUser()->getPermissions();
			const auto clickData = deserializeClickData(aRequest.getRequestBody(), accessList);
			aHandler(selectedIds, clickData);
			return websocketpp::http::status_code::no_content;
		}

		ContextMenuItemClickData deserializeClickData(const json& aJson, const AccessList& aPermissions);

		template<typename IdT>
		using GroupedListHandlerFunc = std::function<GroupedContextMenuItemList(const vector<IdT>& aId, const ContextMenuItemListData& aListData)>;

		// DEPRECATED
		template<typename IdT>
		api_return handleListItems(ApiRequest& aRequest, const GroupedListHandlerFunc<IdT>& aHandlerHooked, const Deserializer::ArrayDeserializerFunc<IdT>& aIdDeserializerFunc) {
			addAsyncTask([
				selectedIds = deserializeItemIds<IdT>(aRequest, aIdDeserializerFunc),
				supports = JsonUtil::getOptionalFieldDefault<StringList>("supports", aRequest.getRequestBody(), StringList()),
				accessList = aRequest.getSession()->getUser()->getPermissions(),
				ownerPtr = aRequest.getOwnerPtr(),
				complete = aRequest.defer(),
				aHandlerHooked
			] {
				const auto groupedItems = aHandlerHooked(selectedIds, ContextMenuItemListData(supports, accessList, ownerPtr));

				auto serializedItems = json::array();
				for (const auto& groupedItem : groupedItems) {
					for (const auto& item : groupedItem->getItems()) {
						serializedItems.push_back(MenuApi::serializeMenuItem(item));
					}
				}

				complete(
					websocketpp::http::status_code::ok,
					serializedItems,
					nullptr
				);
			});

			return CODE_DEFERRED;
		}

		template<typename IdT>
		api_return handleListItemsGrouped(ApiRequest& aRequest, const GroupedListHandlerFunc<IdT>& aHandlerHooked, const Deserializer::ArrayDeserializerFunc<IdT>& aIdDeserializerFunc) {
			addAsyncTask([
				selectedIds = deserializeItemIds<IdT>(aRequest, aIdDeserializerFunc),
				supports = JsonUtil::getOptionalFieldDefault<StringList>("supports", aRequest.getRequestBody(), StringList()),
				accessList = aRequest.getSession()->getUser()->getPermissions(),
				ownerPtr = aRequest.getOwnerPtr(),
				complete = aRequest.defer(),
				aHandlerHooked
			] {
				const auto items = aHandlerHooked(selectedIds, ContextMenuItemListData(supports, accessList, ownerPtr));
				complete(
					websocketpp::http::status_code::ok,
					Serializer::serializeList(items, MenuApi::serializeGroupedMenuItem),
					nullptr
				);
			});

			return CODE_DEFERRED;
		}

		void on(ContextMenuManagerListener::QueueBundleMenuSelected, const vector<uint32_t>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::QueueFileMenuSelected, const vector<uint32_t>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::TransferMenuSelected, const vector<uint32_t>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::ShareRootMenuSelected, const vector<TTHValue>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::FavoriteHubMenuSelected, const vector<uint32_t>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::UserMenuSelected, const vector<CID>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::HintedUserMenuSelected, const vector<HintedUser>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::ExtensionMenuSelected, const vector<string>&, const ContextMenuItemClickData& aClickData) noexcept override;

		void on(ContextMenuManagerListener::GroupedSearchResultMenuSelected, const vector<TTHValue>& aSelectedIds, const SearchInstancePtr& aInstance, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::FilelistItemMenuSelected, const vector<uint32_t>& aSelectedIds, const DirectoryListingPtr& aList, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::HubUserMenuSelected, const vector<uint32_t>&, const ClientPtr& aClient, const ContextMenuItemClickData& aClickData) noexcept override;

		void onMenuItemSelected(const string& aMenuId, const json& aSelectedIds, const ContextMenuItemClickData& aClickData, const json& aEntityId = nullptr) noexcept;
	};
}

#endif