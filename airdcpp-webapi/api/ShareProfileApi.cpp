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

#include <api/ShareProfileApi.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/ShareManager.h>

namespace webserver {
	ShareProfileApi::ShareProfileApi(Session* aSession) : SubscribableApiModule(aSession, Access::ANY) {

		ShareManager::getInstance()->addListener(this);

		METHOD_HANDLER("profiles", Access::ANY, ApiRequest::METHOD_GET, (), false, ShareProfileApi::handleGetProfiles);
		METHOD_HANDLER("profile", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (), true, ShareProfileApi::handleAddProfile);
		METHOD_HANDLER("profile", Access::SETTINGS_EDIT, ApiRequest::METHOD_PATCH, (TOKEN_PARAM), true, ShareProfileApi::handleUpdateProfile);
		METHOD_HANDLER("profile", Access::SETTINGS_EDIT, ApiRequest::METHOD_DELETE, (TOKEN_PARAM), false, ShareProfileApi::handleRemoveProfile);
		METHOD_HANDLER("profile", Access::SETTINGS_EDIT, ApiRequest::METHOD_POST, (TOKEN_PARAM, EXACT_PARAM("default")), false, ShareProfileApi::handleDefaultProfile);

		createSubscription("share_profile_added");
		createSubscription("share_profile_updated");
		createSubscription("share_profile_removed");
	}

	ShareProfileApi::~ShareProfileApi() {
		ShareManager::getInstance()->removeListener(this);
	}

	json ShareProfileApi::serializeShareProfile(const ShareProfilePtr& aProfile) noexcept {
		size_t totalFiles = 0;
		int64_t totalSize = 0;
		ShareManager::getInstance()->getProfileInfo(aProfile->getToken(), totalSize, totalFiles);

		return{
			{ "id", aProfile->getToken() },
			{ "name", aProfile->getDisplayName() },
			{ "plain_name", aProfile->getPlainName() },
			{ "default", aProfile->isDefault() },
			{ "size", totalSize },
			{ "files", totalFiles },
		};
	}

	api_return ShareProfileApi::handleDefaultProfile(ApiRequest& aRequest) {
		auto token = aRequest.getTokenParam(0);
		auto profile = ShareManager::getInstance()->getShareProfile(token);
		if (!profile) {
			aRequest.setResponseErrorStr("Profile not found");
			return websocketpp::http::status_code::not_found;
		}

		ShareManager::getInstance()->setDefaultProfile(token);
		return websocketpp::http::status_code::ok;
	}

	void ShareProfileApi::on(ShareManagerListener::ProfileAdded, ProfileToken aProfile) noexcept {
		maybeSend("share_profile_added", [&] {
			return serializeShareProfile(ShareManager::getInstance()->getShareProfile(aProfile));
		});
	}

	void ShareProfileApi::on(ShareManagerListener::ProfileUpdated, ProfileToken aProfile, bool aIsMajorChange) noexcept {
		if (!aIsMajorChange) {
			// Don't spam when files are hashed
			return;
		}

		maybeSend("share_profile_updated", [&] {
			return serializeShareProfile(ShareManager::getInstance()->getShareProfile(aProfile));
		});
	}

	void ShareProfileApi::on(ShareManagerListener::ProfileRemoved, ProfileToken aProfile) noexcept {
		maybeSend("share_profile_removed", [&] {
			return json({ "id", aProfile });
		});
	}

	void ShareProfileApi::parseProfile(ShareProfilePtr& aProfile, const json& j) {
		auto name = JsonUtil::getField<string>("name", j, false);

		auto token = ShareManager::getInstance()->getProfileByName(name);
		if (token && token != aProfile->getToken()) {
			JsonUtil::throwError("name", JsonUtil::ERROR_EXISTS, "Profile with the same name exists");
		}

		aProfile->setPlainName(name);
	}

	api_return ShareProfileApi::handleAddProfile(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto profile = std::make_shared<ShareProfile>();
		parseProfile(profile, reqJson);

		ShareManager::getInstance()->addProfile(profile);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareProfileApi::handleUpdateProfile(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto token = aRequest.getTokenParam(0);
		if (token == SP_HIDDEN) {
			aRequest.setResponseErrorStr("Hidden profile can't be edited");
			return websocketpp::http::status_code::not_found;
		}

		auto profile = ShareManager::getInstance()->getShareProfile(token);
		if (!profile) {
			aRequest.setResponseErrorStr("Profile not found");
			return websocketpp::http::status_code::not_found;
		}

		parseProfile(profile, reqJson);
		ShareManager::getInstance()->updateProfile(profile);
		return websocketpp::http::status_code::ok;
	}

	api_return ShareProfileApi::handleRemoveProfile(ApiRequest& aRequest) {
		auto token = aRequest.getTokenParam(0);
		if (token == SP_HIDDEN) {
			aRequest.setResponseErrorStr("Hidden profile can't be deleted");
			return websocketpp::http::status_code::bad_request;
		}

		if (token == SETTING(DEFAULT_SP)) {
			aRequest.setResponseErrorStr("The default profile can't be deleted (set another profile as default first)");
			return websocketpp::http::status_code::bad_request;
		}

		if (!ShareManager::getInstance()->getShareProfile(token)) {
			aRequest.setResponseErrorStr("Profile not found");
			return websocketpp::http::status_code::not_found;
		}

		ShareManager::getInstance()->removeProfile(token);

		return websocketpp::http::status_code::ok;
	}

	api_return ShareProfileApi::handleGetProfiles(ApiRequest& aRequest) {
		json j;

		auto profiles = ShareManager::getInstance()->getProfiles();

		// Profiles can't be empty
		for (const auto& p : profiles) {
			j.push_back(serializeShareProfile(p));
		}

		aRequest.setResponseBody(j);
		return websocketpp::http::status_code::ok;
	}
}