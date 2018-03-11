/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#include <api/ShareProfileApi.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/ShareManager.h>

namespace webserver {
	ShareProfileApi::ShareProfileApi(Session* aSession) : SubscribableApiModule(aSession, Access::ANY) {

		ShareManager::getInstance()->addListener(this);

		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(),										ShareProfileApi::handleGetProfiles);

		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(TOKEN_PARAM),							ShareProfileApi::handleGetProfile);
		METHOD_HANDLER(Access::ANY,				METHOD_GET,		(EXACT_PARAM("default")),				ShareProfileApi::handleGetDefaultProfile);

		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(),										ShareProfileApi::handleAddProfile);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_PATCH,	(TOKEN_PARAM),							ShareProfileApi::handleUpdateProfile);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_DELETE,	(TOKEN_PARAM),							ShareProfileApi::handleRemoveProfile);
		METHOD_HANDLER(Access::SETTINGS_EDIT,	METHOD_POST,	(TOKEN_PARAM, EXACT_PARAM("default")),	ShareProfileApi::handleSetDefaultProfile);

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
			{ "name", aProfile->getPlainName() },
			{ "str", aProfile->getDisplayName() },
			{ "default", aProfile->isDefault() },
			{ "size", totalSize },
			{ "files", totalFiles },
		};
	}

	api_return ShareProfileApi::handleGetProfile(ApiRequest& aRequest) {
		auto profile = ShareManager::getInstance()->getShareProfile(aRequest.getTokenParam());
		if (!profile) {
			aRequest.setResponseErrorStr("Profile not found");
			return websocketpp::http::status_code::not_found;
		}

		aRequest.setResponseBody(serializeShareProfile(profile));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareProfileApi::handleGetDefaultProfile(ApiRequest& aRequest) {
		auto profile = ShareManager::getInstance()->getShareProfile(SETTING(DEFAULT_SP));
		if (!profile) {
			return websocketpp::http::status_code::internal_server_error;
		}

		aRequest.setResponseBody(serializeShareProfile(profile));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareProfileApi::handleSetDefaultProfile(ApiRequest& aRequest) {
		auto token = aRequest.getTokenParam();
		auto profile = ShareManager::getInstance()->getShareProfile(token);
		if (!profile) {
			aRequest.setResponseErrorStr("Profile not found");
			return websocketpp::http::status_code::not_found;
		}

		ShareManager::getInstance()->setDefaultProfile(token);
		return websocketpp::http::status_code::no_content;
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

		aRequest.setResponseBody(serializeShareProfile(profile));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareProfileApi::handleUpdateProfile(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto token = aRequest.getTokenParam();
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

		aRequest.setResponseBody(serializeShareProfile(profile));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareProfileApi::handleRemoveProfile(ApiRequest& aRequest) {
		auto token = aRequest.getTokenParam();
		if (token == SP_HIDDEN) {
			aRequest.setResponseErrorStr("Hidden profile can't be deleted");
			return websocketpp::http::status_code::bad_request;
		}

		if (static_cast<int>(token) == SETTING(DEFAULT_SP)) {
			aRequest.setResponseErrorStr("The default profile can't be deleted (set another profile as default first)");
			return websocketpp::http::status_code::bad_request;
		}

		if (!ShareManager::getInstance()->getShareProfile(token)) {
			aRequest.setResponseErrorStr("Profile not found");
			return websocketpp::http::status_code::not_found;
		}

		ShareManager::getInstance()->removeProfile(token);
		return websocketpp::http::status_code::no_content;
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