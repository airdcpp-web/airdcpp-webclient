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

#include <api/ShareProfileApi.h>
#include <api/common/Serializer.h>

#include <web-server/JsonUtil.h>

#include <airdcpp/share/ShareManager.h>
#include <airdcpp/share/profiles/ShareProfileManager.h>

namespace webserver {
	ShareProfileApi::ShareProfileApi(Session* aSession) : 
		SubscribableApiModule(aSession, Access::ANY),
		mgr(ShareManager::getInstance()->getProfileMgr())
	{
		createSubscriptions({
			"share_profile_added",
			"share_profile_updated",
			"share_profile_removed"
		});

		METHOD_HANDLER(Access::ANY,			METHOD_GET,		(),										ShareProfileApi::handleGetProfiles);

		METHOD_HANDLER(Access::ANY,			METHOD_GET,		(TOKEN_PARAM),							ShareProfileApi::handleGetProfile);
		METHOD_HANDLER(Access::ANY,			METHOD_GET,		(EXACT_PARAM("default")),				ShareProfileApi::handleGetDefaultProfile);

		METHOD_HANDLER(Access::SHARE_EDIT,	METHOD_POST,	(),										ShareProfileApi::handleAddProfile);
		METHOD_HANDLER(Access::SHARE_EDIT,	METHOD_PATCH,	(TOKEN_PARAM),							ShareProfileApi::handleUpdateProfile);
		METHOD_HANDLER(Access::SHARE_EDIT,	METHOD_DELETE,	(TOKEN_PARAM),							ShareProfileApi::handleRemoveProfile);
		METHOD_HANDLER(Access::SHARE_EDIT,	METHOD_POST,	(TOKEN_PARAM, EXACT_PARAM("default")),	ShareProfileApi::handleSetDefaultProfile);

		mgr.addListener(this);
	}

	ShareProfileApi::~ShareProfileApi() {
		mgr.removeListener(this);
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

	ShareProfilePtr ShareProfileApi::parseProfileToken(ApiRequest& aRequest, bool aAllowHidden) {
		auto profileId = aRequest.getTokenParam();
		auto profile = mgr.getShareProfile(profileId);
		if (!profile) {
			throw RequestException(websocketpp::http::status_code::not_found, "Share profile " + Util::toString(profileId) + " was not found");
		}

		if (!aAllowHidden && profile->isHidden()) {
			throw RequestException(websocketpp::http::status_code::bad_request, "Hidden share profile isn't valid for this API method");
		}

		return profile;
	}

	api_return ShareProfileApi::handleGetProfile(ApiRequest& aRequest) {
		auto profile = parseProfileToken(aRequest, true);
		aRequest.setResponseBody(serializeShareProfile(profile));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareProfileApi::handleGetDefaultProfile(ApiRequest& aRequest) {
		auto profile = mgr.getShareProfile(SETTING(DEFAULT_SP));
		if (!profile) {
			return websocketpp::http::status_code::internal_server_error;
		}

		aRequest.setResponseBody(serializeShareProfile(profile));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareProfileApi::handleSetDefaultProfile(ApiRequest& aRequest) {
		auto profile = parseProfileToken(aRequest, true);

		mgr.setDefaultProfile(profile->getToken());
		return websocketpp::http::status_code::no_content;
	}

	void ShareProfileApi::on(ShareProfileManagerListener::ProfileAdded, ProfileToken aProfile) noexcept {
		maybeSend("share_profile_added", [&] {
			return serializeShareProfile(mgr.getShareProfile(aProfile));
		});
	}

	void ShareProfileApi::on(ShareProfileManagerListener::ProfileUpdated, ProfileToken aProfile, bool aIsMajorChange) noexcept {
		if (!aIsMajorChange) {
			// Don't spam when files are hashed
			return;
		}

		maybeSend("share_profile_updated", [&] {
			return serializeShareProfile(mgr.getShareProfile(aProfile));
		});
	}

	void ShareProfileApi::on(ShareProfileManagerListener::ProfileRemoved, ProfileToken aProfile) noexcept {
		maybeSend("share_profile_removed", [&] {
			return json({ "id", aProfile });
		});
	}

	void ShareProfileApi::updateProfileProperties(ShareProfilePtr& aProfile, const json& j) {
		auto name = JsonUtil::getField<string>("name", j, false);

		auto token = mgr.getProfileByName(name);
		if (token && token != aProfile->getToken()) {
			JsonUtil::throwError("name", JsonException::ERROR_EXISTS, "Profile with the same name exists");
		}

		aProfile->setPlainName(name);
	}

	api_return ShareProfileApi::handleAddProfile(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto profile = std::make_shared<ShareProfile>();
		updateProfileProperties(profile, reqJson);

		mgr.addProfile(profile);

		aRequest.setResponseBody(serializeShareProfile(profile));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareProfileApi::handleUpdateProfile(ApiRequest& aRequest) {
		const auto& reqJson = aRequest.getRequestBody();

		auto profile = parseProfileToken(aRequest, false);

		updateProfileProperties(profile, reqJson);
		mgr.updateProfile(profile);

		aRequest.setResponseBody(serializeShareProfile(profile));
		return websocketpp::http::status_code::ok;
	}

	api_return ShareProfileApi::handleRemoveProfile(ApiRequest& aRequest) {
		auto profile = parseProfileToken(aRequest, false);
		if (profile->isDefault()) {
			aRequest.setResponseErrorStr("The default profile can't be deleted (set another profile as default first)");
			return websocketpp::http::status_code::bad_request;
		}

		mgr.removeProfile(profile->getToken());
		return websocketpp::http::status_code::no_content;
	}

	api_return ShareProfileApi::handleGetProfiles(ApiRequest& aRequest) {
		auto profiles = mgr.getProfiles();

		auto j = Serializer::serializeList(profiles, serializeShareProfile);
		aRequest.setResponseBody(j);

		return websocketpp::http::status_code::ok;
	}
}