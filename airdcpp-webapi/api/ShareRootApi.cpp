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

#include <airdcpp/AirUtil.h>
#include <airdcpp/HashManager.h>
#include <airdcpp/ShareManager.h>

namespace webserver {
	const PropertyList ShareRootApi::properties = {
		{ PROP_PATH, "path", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_VIRTUAL_NAME, "virtual_name", TYPE_TEXT, SERIALIZE_TEXT, SORT_CUSTOM },
		{ PROP_SIZE, "size", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_PROFILES, "profiles", TYPE_LIST_NUMERIC, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_INCOMING, "incoming", TYPE_NUMERIC_OTHER, SERIALIZE_BOOL, SORT_NUMERIC },
		{ PROP_LAST_REFRESH_TIME, "last_refresh_time", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_REFRESH_STATE, "refresh_state", TYPE_NUMERIC_OTHER, SERIALIZE_TEXT_NUMERIC, SORT_NUMERIC },
		{ PROP_TYPE, "type", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
	};

	const PropertyItemHandler<ShareDirectoryInfoPtr> ShareRootApi::itemHandler = {
		properties,
		ShareUtils::getStringInfo, ShareUtils::getNumericInfo, ShareUtils::compareItems, ShareUtils::serializeItem, ShareUtils::filterItem
	};

	ShareRootApi::ShareRootApi(Session* aSession) : SubscribableApiModule(aSession, Access::SETTINGS_VIEW),
		rootView("share_root_view", this, itemHandler, std::bind(&ShareRootApi::getRoots, this)),
		timer(getTimer([this] { onTimer(); }, 5000)) {

		// Maintain the view item listing only when it's needed
		rootView.setActiveStateChangeHandler([&](bool aActive) {
			WLock l(cs);
			if (aActive) {
				roots = ShareManager::getInstance()->getRootInfos();
			} else {
				roots.clear();
			}
		});

		METHOD_HANDLER("roots", Access::SETTINGS_VIEW, ApiRequest::METHOD_GET, (), false, ShareRootApi::handleGetRoots);
		METHOD_HANDLER("root", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("add")), true, ShareRootApi::handleAddRoot);
		METHOD_HANDLER("root", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("update")), true, ShareRootApi::handleUpdateRoot);
		METHOD_HANDLER("root", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (EXACT_PARAM("remove")), true, ShareRootApi::handleRemoveRoot);

		createSubscription("share_root_created");
		createSubscription("share_root_updated");
		createSubscription("share_root_removed");

		ShareManager::getInstance()->addListener(this);
		HashManager::getInstance()->addListener(this);
		timer->start(false);
	}

	ShareRootApi::~ShareRootApi() {
		timer->stop(true);
		HashManager::getInstance()->removeListener(this);
		ShareManager::getInstance()->removeListener(this);
	}

	ShareDirectoryInfoList ShareRootApi::getRoots() const noexcept {
		RLock l(cs);
		return roots;
	}

	api_return ShareRootApi::handleGetRoots(ApiRequest& aRequest) {
		auto j = Serializer::serializeItemList(itemHandler, ShareManager::getInstance()->getRootInfos());
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

		if (ShareManager::getInstance()->isRealPathShared(path)) {
			JsonUtil::throwError("path", JsonUtil::ERROR_INVALID, "Path is shared already");
		}

		auto info = std::make_shared<ShareDirectoryInfo>(path);

		parseRoot(info, reqJson, true);

		ShareManager::getInstance()->addRootDirectory(info);
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

		ShareManager::getInstance()->updateRootDirectory(info);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareRootApi::handleRemoveRoot(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = JsonUtil::getField<string>("path", reqJson, false);
		if (!ShareManager::getInstance()->removeRootDirectory(path)) {
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
		if (!info) {
			dcassert(0);
			return;
		}

		if (rootView.isActive()) {
			RLock l(cs);
			auto i = find_if(roots.begin(), roots.end(), ShareDirectoryInfo::PathCompare(aPath));
			if (i == roots.end()) {
				return;
			}

			// We need to use the same pointer because of listview
			(*i)->merge(info);
			info = *i;
		}

		onRootUpdated(info, toPropertyIdSet(properties));
	}

	void ShareRootApi::onRootUpdated(const ShareDirectoryInfoPtr& aInfo, PropertyIdSet&& aUpdatedProperties) noexcept {
		maybeSend("share_root_updated", [&] { return Serializer::serializeItemProperties(aInfo, aUpdatedProperties, itemHandler); });
		rootView.onItemUpdated(aInfo, aUpdatedProperties);
	}

	void ShareRootApi::on(ShareManagerListener::RootRemoved, const string& aPath) noexcept {
		if (rootView.isActive()) {
			WLock l(cs);
			auto i = find_if(roots.begin(), roots.end(), ShareDirectoryInfo::PathCompare(aPath));
			if (i != roots.end()) {
				rootView.onItemRemoved(*i);
				roots.erase(i);
			} else {
				dcassert(0);
			}
		}

		maybeSend("share_root_removed", [&] {
			return json(
				{ "path", aPath }
			);
		});
	}

	void ShareRootApi::parseRoot(ShareDirectoryInfoPtr& aInfo, const json& j, bool aIsNew) {
		auto virtualName = JsonUtil::getOptionalField<string>("virtual_name", j, false, aIsNew);
		if (virtualName) {
			aInfo->virtualName = *virtualName;
		}

		auto profiles = JsonUtil::getOptionalField<ProfileTokenSet>("profiles", j, false, aIsNew);
		if (profiles) {
			// Only validate added profiles
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

	// Show updates for roots that are being hashed regularly
	void ShareRootApi::onTimer() noexcept {
		ShareDirectoryInfoSet updatedRoots;

		{
			RLock l(cs);
			if (hashedPaths.empty()) {
				return;
			}

			for (const auto& p : hashedPaths) {
				auto i = find_if(roots.begin(), roots.end(), [&](const ShareDirectoryInfoPtr& aInfo) {
					return AirUtil::isParentOrExactLocal(aInfo->path, p);
				});

				if (i != roots.end()) {
					updatedRoots.insert(*i);
				}
			} 
		}

		{
			WLock l(cs);
			hashedPaths.clear();
		}

		for (const auto& root : updatedRoots) {
			// Update with the new information
			auto newInfo = ShareManager::getInstance()->getRootInfo(root->path);
			if (newInfo) {
				WLock l(cs);
				root->merge(newInfo);
				onRootUpdated(root, { PROP_SIZE, PROP_TYPE });
			}
		}
	}

	void ShareRootApi::on(HashManagerListener::FileHashed, const string& aFilePath, HashedFile& aFileInfo) noexcept {
		if (!rootView.isActive() && !subscriptionActive("share_root_updated")) {
			return;
		}

		WLock l(cs);
		hashedPaths.insert(Util::getFilePath(aFilePath));
	}
}