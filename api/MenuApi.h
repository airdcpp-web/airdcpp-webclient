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

#ifndef DCPLUSPLUS_DCPP_MENUAPI_H
#define DCPLUSPLUS_DCPP_MENUAPI_H

#include <api/base/HookApiModule.h>

#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>

#include <web-server/ApiRequest.h>
#include <web-server/ContextMenuItem.h>
#include <web-server/ContextMenuManagerListener.h>
#include <web-server/JsonUtil.h>
#include <web-server/Session.h>
#include <web-server/WebUser.h>

#include <airdcpp/core/header/typedefs.h>


namespace webserver {
	class ContextMenuManager;
	class MenuApi : public HookApiModule, private ContextMenuManagerListener {
	public:
		explicit MenuApi(Session* aSession);
		~MenuApi() override;
	private:
		ContextMenuManager& cmm;

		static string toHookId(const string& aMenuId) noexcept {
			return aMenuId + "_list_menuitems";
		}

		using MenuActionHookResultGetter = ActionHookResultGetter<GroupedContextMenuItemPtr>;

		static StringMap deserializeIconInfo(const json& aJson);

		static ContextMenuItemPtr toMenuItem(const json& aData, const MenuActionHookResultGetter& aResultGetter, int aLevel = 0);
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
				this,
				MenuApi::deserializeMenuItems
			);
		}

		ActionHookResult<GroupedContextMenuItemPtr> menuListHookHandler(const ContextMenuItemListData& aListData, const MenuActionHookResultGetter& aResultGetter, const string& aMenuId) {
			return HookCompletionData::toResult<GroupedContextMenuItemPtr>(
				fireMenuHook(aMenuId, nullptr, aListData, nullptr),
				aResultGetter,
				this,
				MenuApi::deserializeMenuItems
			);
		}

		HookCompletionDataPtr fireMenuHook(const string& aMenuId, const json& aSelectedIds, const ContextMenuItemListData& aListData, const json& aEntityId);

		template<typename IdT>
		static vector<IdT> deserializeItemIds(const ApiRequest& aRequest, const Deserializer::ArrayDeserializerFunc<IdT>& aIdDeserializerFunc) {
			return Deserializer::deserializeList<IdT>("selected_ids", aRequest.getRequestBody(), aIdDeserializerFunc, false);
		}

		template<typename IdT>
		using IdClickHandlerFunc = std::function<void(const vector<IdT>& aId, const ContextMenuItemClickData& aClickData)>;

		using ClickHandlerFunc = std::function<void(const ContextMenuItemClickData& aClickData)>;

		template<typename IdT>
		api_return handleClickItem(ApiRequest& aRequest, const string& aMenuId, const IdClickHandlerFunc<IdT>& aHandler, const Deserializer::ArrayDeserializerFunc<IdT>& aIdDeserializerFunc) {
			const auto selectedIds = deserializeItemIds<IdT>(aRequest, aIdDeserializerFunc);
			return handleClickItem(aRequest, aMenuId, [&](const ContextMenuItemClickData& aClickData) {
				aHandler(selectedIds, aClickData);
			});
		}

		api_return handleClickItem(ApiRequest& aRequest, const string& aMenuId, const ClickHandlerFunc& aHandler) {
			const auto accessList = aRequest.getSession()->getUser()->getPermissions();
			const auto clickData = deserializeClickData(aRequest.getRequestBody(), accessList);
			aHandler(clickData);
			return http_status::no_content;
		}

		static ContextMenuItemClickData deserializeClickData(const json& aJson, const AccessList& aPermissions);

		template<typename IdT>
		using IdGroupedListHandlerFunc = std::function<GroupedContextMenuItemList(const vector<IdT>& aId, const ContextMenuItemListData& aListData)>;

		using GroupedListHandlerFunc = std::function<GroupedContextMenuItemList(const ContextMenuItemListData& aListData)>;

		api_return handleListItemsGrouped(ApiRequest& aRequest, const GroupedListHandlerFunc& aHandlerHooked) {
			addAsyncTask([
				supports = JsonUtil::getOptionalFieldDefault<StringList>("supports", aRequest.getRequestBody(), StringList()),
				accessList = aRequest.getSession()->getUser()->getPermissions(),
				ownerPtr = aRequest.getOwnerPtr(),
				complete = aRequest.defer(),
				aHandlerHooked
			] {
				const auto items = aHandlerHooked(ContextMenuItemListData(supports, accessList, ownerPtr));
				complete(
					http_status::ok,
					Serializer::serializeList(items, MenuApi::serializeGroupedMenuItem),
					nullptr
				);
			});

			return CODE_DEFERRED;
		}

		template<typename IdT>
		api_return handleListItemsGrouped(ApiRequest& aRequest, const IdGroupedListHandlerFunc<IdT>& aHandlerHooked, const Deserializer::ArrayDeserializerFunc<IdT>& aIdDeserializerFunc) {
			auto selectedIds = deserializeItemIds<IdT>(aRequest, aIdDeserializerFunc);
			return handleListItemsGrouped(aRequest, [=](const ContextMenuItemListData& aListData) {
				return aHandlerHooked(selectedIds, aListData);
			});
		}


		void on(ContextMenuManagerListener::QueueBundleMenuSelected, const vector<QueueToken>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::QueueFileMenuSelected, const vector<QueueToken>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::TransferMenuSelected, const vector<TransferToken>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::ShareRootMenuSelected, const vector<TTHValue>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::FavoriteHubMenuSelected, const vector<FavoriteHubToken>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::ExtensionMenuSelected, const vector<string>&, const ContextMenuItemClickData& aClickData) noexcept override;

		void on(ContextMenuManagerListener::UserMenuSelected, const vector<CID>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::HintedUserMenuSelected, const vector<HintedUser>&, const ContextMenuItemClickData& aClickData) noexcept override;

		// Sessions
		void on(ContextMenuManagerListener::HubMenuSelected, const vector<ClientToken>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::PrivateChatMenuSelected, const vector<CID>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::FilelistMenuSelected, const vector<CID>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::ViewedFileMenuSelected, const vector<TTHValue>&, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::SearchInstanceMenuSelected, const vector<SearchInstanceToken>&, const ContextMenuItemClickData& aClickData) noexcept override;

		// Entities
		void on(ContextMenuManagerListener::GroupedSearchResultMenuSelected, const vector<TTHValue>& aSelectedIds, const SearchInstancePtr& aInstance, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::FilelistItemMenuSelected, const vector<DirectoryListingItemToken>& aSelectedIds, const DirectoryListingPtr& aList, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::HubUserMenuSelected, const vector<dcpp::SID>&, const ClientPtr& aClient, const ContextMenuItemClickData& aClickData) noexcept override;

		void on(HubMessageHighlightMenuSelected, const vector<MessageHighlightToken>&, const ClientPtr& aClient, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(PrivateChatMessageHighlightMenuSelected, const vector<MessageHighlightToken>&, const PrivateChatPtr& aChat, const ContextMenuItemClickData& aClickData) noexcept override;

		// Common
		void on(ContextMenuManagerListener::QueueMenuSelected, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::EventsMenuSelected, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::TransfersMenuSelected, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::ShareRootsMenuSelected, const ContextMenuItemClickData& aClickData) noexcept override;
		void on(ContextMenuManagerListener::FavoriteHubsMenuSelected, const ContextMenuItemClickData& aClickData) noexcept override;

		void onMenuItemSelected(const string& aMenuId, const json& aSelectedIds, const ContextMenuItemClickData& aClickData, const json& aEntityId = nullptr) noexcept;
	};
}

#endif