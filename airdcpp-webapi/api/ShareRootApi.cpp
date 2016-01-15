/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#include <api/ShareRootApi.h>
#include <api/common/Serializer.h>
#include <api/common/Deserializer.h>
#include <api/ShareUtils.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/ShareManager.h>

namespace webserver {
	ShareRootApi::ShareRootApi(Session* aSession) : ApiModule(aSession, Access::SETTINGS_VIEW), itemHandler(properties,
		ShareUtils::getStringInfo, ShareUtils::getNumericInfo, ShareUtils::compareItems, ShareUtils::serializeItem, ShareUtils::filterItem),
		rootView("share_root_view", this, itemHandler, std::bind(&ShareRootApi::getRoots, this)) {

		// Maintain the view item listing only when it's needed
		rootView.setActiveStateChangeHandler([&](bool aActive) {
			WLock l(cs);
			if (aActive) {
				roots = ShareManager::getInstance()->getRootInfos();
			} else {
				roots.clear();
			}
		});

		ShareManager::getInstance()->addListener(this);

		METHOD_HANDLER("roots", Access::SETTINGS_VIEW, ApiRequest::METHOD_GET, (), false, ShareRootApi::handleGetRoots);
		METHOD_HANDLER("root", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("add")), true, ShareRootApi::handleAddRoot);
		METHOD_HANDLER("root", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("update")), true, ShareRootApi::handleUpdateRoot);
		METHOD_HANDLER("root", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("remove")), true, ShareRootApi::handleRemoveRoot);

		createSubscription("share_root_created");
		createSubscription("share_root_updated");
		createSubscription("share_root_removed");
	}

	ShareRootApi::~ShareRootApi() {
		ShareManager::getInstance()->removeListener(this);
	}

	ShareDirectoryInfoList ShareRootApi::getRoots() const noexcept {
		RLock l(cs);
		return roots;
	}

	api_return ShareRootApi::handleGetRoots(ApiRequest& aRequest) {
		auto j = Serializer::serializeItemList(itemHandler, getRoots());
		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareRootApi::handleAddRoot(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = Util::validatePath(JsonUtil::getField<string>("path", reqJson, false), true);

		// Validate the path
		try {
			ShareManager::getInstance()->validateRootPath(path);
		} catch (ShareException& e) {
			JsonUtil::throwError("path", JsonUtil::ERROR_INVALID, e.what());
		}

		auto info = make_shared<ShareDirectoryInfo>(path);

		parseRoot(info, reqJson, true);

		ShareManager::getInstance()->addDirectory(info);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareRootApi::handleUpdateRoot(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = JsonUtil::getField<string>("path", reqJson, false);

		auto info = ShareManager::getInstance()->getRootInfo(path);
		if (!info) {
			aRequest.setResponseErrorStr("Path not found");
			return websocketpp::http::status_code::not_found;
		}

		parseRoot(info, reqJson, false);

		ShareManager::getInstance()->changeDirectory(info);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareRootApi::handleRemoveRoot(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = JsonUtil::getField<string>("path", reqJson, false);
		if (!ShareManager::getInstance()->removeDirectory(path)) {
			aRequest.setResponseErrorStr("Path not found");
			return websocketpp::http::status_code::not_found;
		}


		return websocketpp::http::status_code::ok;
	}

	void ShareRootApi::on(ShareManagerListener::RootCreated, const string& aPath) noexcept {
		if (!subscriptionActive("share_root_created") && !rootView.isActive()) {
			return;
		}

		auto info = ShareManager::getInstance()->getRootInfo(aPath);

		{
			WLock l(cs);
			roots.push_back(info);
		}

		rootView.onItemAdded(info);

		maybeSend("share_root_created", [&] { return Serializer::serializeItem(info, itemHandler); });
	}

	void ShareRootApi::on(ShareManagerListener::RootUpdated, const string& aPath) noexcept {
		if (!subscriptionActive("share_root_updated") && !rootView.isActive()) {
			return;
		}

		auto info = ShareManager::getInstance()->getRootInfo(aPath);
		if (rootView.isActive()) {
			RLock l(cs);
			auto i = find_if(roots.begin(), roots.end(), ShareDirectoryInfo::PathCompare(aPath));
			if (i != roots.end()) {
				(*i)->merge(info);
				rootView.onItemUpdated(*i, toPropertyIdSet(properties));
			}
		}

		maybeSend("share_root_updated", [&] { return Serializer::serializeItem(info, itemHandler); });
	}

	void ShareRootApi::on(ShareManagerListener::RootRemoved, const string& aPath) noexcept {
		if (rootView.isActive()) {
			WLock l(cs);
			auto i = find_if(roots.begin(), roots.end(), ShareDirectoryInfo::PathCompare(aPath));
			if (i != roots.end()) {
				rootView.onItemRemoved(*i);
				roots.erase(i);
			}
		}

		maybeSend("share_root_removed", [&] {
			return json({ "path", aPath });
		});
	}

	void ShareRootApi::parseRoot(ShareDirectoryInfoPtr& aInfo, const json& j, bool aIsNew) {
		auto virtualName = JsonUtil::getOptionalField<string>("virtual_name", j, false, aIsNew);
		if (virtualName) {
			aInfo->virtualName = *virtualName;
		}

		auto profiles = JsonUtil::getOptionalField<ProfileTokenSet>("profiles", j, false, aIsNew);
		if (profiles) {
			// Only validate added profiles profiles
			ProfileTokenSet diff;

			auto newProfiles = *profiles;
			std::set_difference(newProfiles.begin(), newProfiles.end(),
				aInfo->profiles.begin(), aInfo->profiles.end(), std::inserter(diff, diff.begin()));

			try {
				ShareManager::getInstance()->validateNewRootProfiles(aInfo->path, diff);
			} catch (ShareException& e) {
				JsonUtil::throwError(aIsNew ? "path" : "profiles", JsonUtil::ERROR_INVALID, e.what());
			}

			aInfo->profiles = newProfiles;
		}

		auto incoming = JsonUtil::getOptionalField<bool>("incoming", j, false, false);
		if (incoming) {
			aInfo->incoming = *incoming;
		}
	}
}