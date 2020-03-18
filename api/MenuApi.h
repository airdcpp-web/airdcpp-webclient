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

#ifndef DCPLUSPLUS_DCPP_MENUAPI_H
#define DCPLUSPLUS_DCPP_MENUAPI_H

#include <api/base/HookApiModule.h>

#include <api/common/Deserializer.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/ContextMenuManager.h>


namespace webserver {
	class MenuApi : public HookApiModule, private ContextMenuManagerListener {
	public:
		MenuApi(Session* aSession);
		~MenuApi();
	private:
		static ContextMenuItemPtr toMenuItem(const json& aData, const ActionHookResultGetter<ContextMenuItemList>& aResultGetter);
		static ContextMenuItemList deserializeMenuItems(const json& aData, const ActionHookResultGetter<ContextMenuItemList>& aResultGetter);

		static json toHookData(const json& aSelectedIds, const json& aData = json());
		static json serializeMenuItem(const ContextMenuItemPtr& aMenuItem);


		template<typename IdT>
		using IdSerializer = std::function<json(const IdT& aId)>;

		template<typename IdT>
		ActionHookResult<ContextMenuItemList> menuListHookHandler(const vector<IdT>& aSelections, const ActionHookResultGetter<ContextMenuItemList>& aResultGetter, const string& aMenuId, const IdSerializer<IdT>& aIdSerializer, const json& aData = nullptr) {
			return HookCompletionData::toResult<ContextMenuItemList>(
				fireHook(aMenuId, 1, [&]() {
					return toHookData(
					 	Serializer::serializeList(aSelections, aIdSerializer),
						aData
					);
				}),
				aResultGetter,
				MenuApi::deserializeMenuItems
			);
		}

		template<typename IdT>
		vector<IdT> deserializeItemIds(ApiRequest& aRequest, const Deserializer::ArrayDeserializerFunc<IdT>& aIdDeserializerFunc) {
			return Deserializer::deserializeList<IdT>("selected_ids", aRequest.getRequestBody(), aIdDeserializerFunc, false);
		}

		template<typename IdT>
		using ClickHandlerFunc = std::function<void(const vector<IdT>& aId, const string& aHookId, const string& aMenuItemId)>;

		template<typename IdT>
		api_return handleClickItem(ApiRequest& aRequest, const string& aMenuId, const ClickHandlerFunc<IdT>& aHandler, const Deserializer::ArrayDeserializerFunc<IdT>& aIdDeserializerFunc) {
			const auto selectedIds = deserializeItemIds<IdT>(aRequest, aIdDeserializerFunc);
			const auto hookId = JsonUtil::getField<string>("hook_id", aRequest.getRequestBody(), false);
			const auto menuItemId = JsonUtil::getField<string>("menuitem_id", aRequest.getRequestBody(), false);

			aHandler(selectedIds, hookId, menuItemId);
			return websocketpp::http::status_code::no_content;
		}

		template<typename IdT>
		using ListHandlerFunc = std::function<ContextMenuItemList(const vector<IdT>& aId)>;

		template<typename IdT>
		api_return handleListItems(ApiRequest& aRequest, const ListHandlerFunc<IdT>& aHandler, const Deserializer::ArrayDeserializerFunc<IdT>& aIdDeserializerFunc) {
			const auto selectedIds = deserializeItemIds<IdT>(aRequest, aIdDeserializerFunc);
			const auto complete = aRequest.defer();

			addAsyncTask([=] {
				const auto items = aHandler(selectedIds);
				complete(
					websocketpp::http::status_code::ok,
					Serializer::serializeList(items, MenuApi::serializeMenuItem),
					nullptr
				);
			});

			return websocketpp::http::status_code::see_other;
		}

		template<typename IdT>
		using EntityListHandlerFunc = std::function<ContextMenuItemList(const vector<IdT> & aId)>;

		static TTHValue tthArrayValueParser(const json& aJson, const string& aFieldName);
		static CID cidArrayValueParser(const json& aJson, const string& aFieldName);
		static HintedUser hintedUserArrayValueParser(const json& aJson, const string& aFieldName);

		template<typename IdT>
		static IdT defaultArrayValueParser(const json& aJson, const string& aFieldName) {
			return JsonUtil::parseValue<IdT>(aFieldName, aJson, false);
		}

		template<typename IdT>
		static json defaultArrayValueSerializer(const IdT& aJson) {
			return aJson;
		}

		void on(ContextMenuManagerListener::QueueBundleMenuSelected, const vector<uint32_t>&, const string& aHookId, const string& aMenuItemId) noexcept override;
		void on(ContextMenuManagerListener::QueueFileMenuSelected, const vector<uint32_t>&, const string& aHookId, const string& aMenuItemId) noexcept override;
		void on(ContextMenuManagerListener::TransferMenuSelected, const vector<uint32_t>&, const string& aHookId, const string& aMenuItemId) noexcept override;
		void on(ContextMenuManagerListener::ShareRootMenuSelected, const vector<TTHValue>&, const string& aHookId, const string& aMenuItemId) noexcept override;
		void on(ContextMenuManagerListener::FavoriteHubMenuSelected, const vector<uint32_t>&, const string& aHookId, const string& aMenuItemId) noexcept override;
		void on(ContextMenuManagerListener::UserMenuSelected, const vector<CID>&, const string& aHookId, const string& aMenuItemId) noexcept override;
		void on(ContextMenuManagerListener::HintedUserMenuSelected, const vector<HintedUser>&, const string& aHookId, const string& aMenuItemId) noexcept override;

		void on(ContextMenuManagerListener::GroupedSearchResultMenuSelected, const vector<TTHValue>& aSelectedIds, const SearchInstancePtr& aInstance, const string& aHookId, const string& aMenuItemId) noexcept override;
		void on(ContextMenuManagerListener::FilelistItemMenuSelected, const vector<uint32_t>& aSelectedIds, const DirectoryListingPtr& aList, const string& aHookId, const string& aMenuItemId) noexcept override;
		void on(ContextMenuManagerListener::HubUserMenuSelected, const vector<uint32_t>&, const ClientPtr& aClient, const string& aHookId, const string& aMenuItemId) noexcept override;

		void onMenuItemSelected(const string& aMenuId, const json& aSelectedIds, const string& aHookId, const string& aMenuItemId, const json& aEntityId = nullptr) noexcept;
	};
}

#endif