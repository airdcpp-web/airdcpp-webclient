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

#include <api/ShareRootApi.h>
#include <api/common/Serializer.h>
#include <api/common/Deserializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/hash/HashManager.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/share/ShareManager.h>

namespace webserver {
	ShareRootApi::ShareRootApi(Session* aSession) : 
		SubscribableApiModule(aSession, Access::SHARE_VIEW),
		roots(ShareManager::getInstance()->getRootInfos()),
		rootView("share_root_view", this, ShareUtils::propertyHandler, std::bind(&ShareRootApi::getRoots, this)),
		timer(getTimer([this] { onTimer(); }, 5000)) 
	{
		createSubscriptions({ "share_root_created", "share_root_updated", "share_root_removed" });

		METHOD_HANDLER(Access::SHARE_VIEW, METHOD_GET,		(),				ShareRootApi::handleGetRoots);

		METHOD_HANDLER(Access::SHARE_EDIT, METHOD_POST,		(),				ShareRootApi::handleAddRoot);
		METHOD_HANDLER(Access::SHARE_VIEW, METHOD_GET,		(TTH_PARAM),	ShareRootApi::handleGetRoot);
		METHOD_HANDLER(Access::SHARE_EDIT, METHOD_PATCH,	(TTH_PARAM),	ShareRootApi::handleUpdateRoot);
		METHOD_HANDLER(Access::SHARE_EDIT, METHOD_DELETE,	(TTH_PARAM),	ShareRootApi::handleRemoveRoot);

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

	api_return ShareRootApi::handleGetRoot(ApiRequest& aRequest) {
		auto info = getRoot(aRequest);
		aRequest.setResponseBody(Serializer::serializeItem(info, ShareUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareRootApi::handleGetRoots(ApiRequest& aRequest) {
		auto j = Serializer::serializeItemList(ShareUtils::propertyHandler, ShareManager::getInstance()->getRootInfos());
		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareRootApi::handleAddRoot(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto path = PathUtil::validateDirectoryPath(JsonUtil::getField<string>("path", reqJson, false));

		// Validate the path
		try {
			ShareManager::getInstance()->validateRootPath(path);
		} catch (ShareException& e) {
			JsonUtil::throwError("path", JsonException::ERROR_INVALID, e.what());
		}

		auto info = std::make_shared<ShareDirectoryInfo>(path);

		parseRoot(info, reqJson);

		ShareManager::getInstance()->addRootDirectory(info);

		aRequest.setResponseBody(Serializer::serializeItem(info, ShareUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareRootApi::handleUpdateRoot(ApiRequest& aRequest) {
		auto info = getRoot(aRequest);

		parseRoot(info, aRequest.getRequestBody());
		ShareManager::getInstance()->updateRootDirectory(info);

		aRequest.setResponseBody(Serializer::serializeItem(info, ShareUtils::propertyHandler));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareRootApi::handleRemoveRoot(ApiRequest& aRequest) {
		auto info = getRoot(aRequest);
		ShareManager::getInstance()->removeRootDirectory(info->path);
		return websocketpp::http::status_code::no_content;
	}

	void ShareRootApi::on(ShareManagerListener::RootCreated, const string& aPath) noexcept {
		auto info = ShareManager::getInstance()->getRootInfo(aPath);

		{
			WLock l(cs);
			roots.push_back(info);
		}

		rootView.onItemAdded(info);

		maybeSend("share_root_created", [&] { 
			return Serializer::serializeItem(info, ShareUtils::propertyHandler); 
		});
	}

	void ShareRootApi::on(ShareManagerListener::RootUpdated, const string& aPath) noexcept {
		onRootUpdated(aPath, toPropertyIdSet(ShareUtils::properties));
	}

	void ShareRootApi::on(ShareManagerListener::RootRefreshState, const string& aPath) noexcept {
		onRootUpdated(aPath, { ShareUtils::PROP_LAST_REFRESH_TIME, ShareUtils::PROP_SIZE, ShareUtils::PROP_STATUS });
	}

	void ShareRootApi::onRootUpdated(const string& aPath, PropertyIdSet&& aUpdatedProperties) noexcept {
		auto info = ShareManager::getInstance()->getRootInfo(aPath);
		if (!info) {
			dcassert(0);
			return;
		}

		auto localInfo = findRoot(aPath);
		if (!localInfo) {
			dcassert(0);
			return;
		}

		localInfo->merge(info);
		info = localInfo;  // We need to use the same pointer because of listview

		onRootUpdated(localInfo, std::move(aUpdatedProperties));
	}


	void ShareRootApi::onRootUpdated(const ShareDirectoryInfoPtr& aInfo, PropertyIdSet&& aUpdatedProperties) noexcept {
		maybeSend("share_root_updated", [&] { 
			// Always serialize the full item
			return Serializer::serializeItem(aInfo, ShareUtils::propertyHandler);
		});

		//dcassert(rootView.hasSourceItem(aInfo));
		rootView.onItemUpdated(aInfo, aUpdatedProperties);
	}

	void ShareRootApi::on(ShareManagerListener::RootRemoved, const string& aPath) noexcept {
		auto root = findRoot(aPath);
		if (!root) {
			dcassert(0);
			return;
		}

		rootView.onItemRemoved(root);

		{
			WLock l(cs);
			roots.erase(remove(roots.begin(), roots.end(), root), roots.end());
		}

		maybeSend("share_root_removed", [&] {
			return Serializer::serializeItem(root, ShareUtils::propertyHandler);
		});
	}

	ShareDirectoryInfoPtr ShareRootApi::getRoot(const ApiRequest& aRequest) {
		auto rootId = aRequest.getTTHParam();

		RLock l(cs);
		auto i = ranges::find_if(roots, ShareDirectoryInfo::IdCompare(rootId));
		if (i == roots.end()) {
			throw RequestException(websocketpp::http::status_code::not_found, "Root " + rootId.toBase32() + " not found");
		}

		return *i;
	}

	ShareDirectoryInfoPtr ShareRootApi::findRoot(const string& aPath) noexcept {
		RLock l(cs);
		auto i = ranges::find_if(roots, ShareDirectoryInfo::PathCompare(aPath));
		if (i == roots.end()) {
			return nullptr;
		}

		return *i;
	}

	void ShareRootApi::parseRoot(ShareDirectoryInfoPtr& aInfo, const json& j) {
		auto virtualName = JsonUtil::getOptionalField<string>("virtual_name", j);
		if (virtualName) {
			aInfo->virtualName = *virtualName;
		}

		// Default profile is added for new roots if profiles are not specified
		auto profiles = JsonUtil::getOptionalField<ProfileTokenSet>("profiles", j);
		if (profiles) {
			auto newProfiles = *profiles;
			for (const auto& p : newProfiles) {
				if (!ShareManager::getInstance()->getShareProfile(p)) {
					JsonUtil::throwError("profiles", JsonException::ERROR_INVALID, "Share profile " +  Util::toString(p)  + " was not found");
				}
			}

			aInfo->profiles = newProfiles;
		}

		auto incoming = JsonUtil::getOptionalField<bool>("incoming", j);
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
				auto i = ranges::find_if(roots, [&](const ShareDirectoryInfoPtr& aInfo) {
					return PathUtil::isParentOrExactLocal(aInfo->path, p);
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
				onRootUpdated(root, { ShareUtils::PROP_SIZE, ShareUtils::PROP_TYPE });
			}
		}
	}

	void ShareRootApi::on(HashManagerListener::FileHashed, const string& aFilePath, HashedFile&, int) noexcept {
		WLock l(cs);
		hashedPaths.insert(PathUtil::getFilePath(aFilePath));
	}
}