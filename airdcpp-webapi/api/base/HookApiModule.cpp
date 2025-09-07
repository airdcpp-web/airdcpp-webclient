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

#include <web-server/JsonUtil.h>
#include <web-server/Session.h>
#include <web-server/WebUser.h>

#include <api/base/HookApiModule.h>

namespace webserver {
	bool HookApiModule::APIHook::enable(ActionHookSubscriber&& aActionHookSubscriber) noexcept {
		hookSubscriberId = aActionHookSubscriber.getId();
		if (!addHandlerF(std::move(aActionHookSubscriber))) {
			hookSubscriberId = "";
			return false;
		}

		return true;
	}

	void HookApiModule::APIHook::disable(const Session* aSession) noexcept {
		removeHandlerF(hookSubscriberId);
		hookSubscriberId = "";
	}

	//const string& HookApiModule::APIHook::getSubscriberId(const Session* aSession) noexcept {
	//	return aSession->getUser()->getUserName();
	//}

	HookApiModule::HookApiModule(Session* aSession, Access aSubscriptionAccess, Access aHookAccess) :
		SubscribableApiModule(aSession, aSubscriptionAccess) 
	{
		METHOD_HANDLER(aHookAccess, METHOD_GET, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID)), HookApiModule::handleListHooks);
		METHOD_HANDLER(aHookAccess, METHOD_POST, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID)), HookApiModule::handleSubscribeHook);
		METHOD_HANDLER(aHookAccess, METHOD_DELETE, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID)), HookApiModule::handleUnsubscribeHook);

		// VARIABLE_METHOD_HANDLER(aHookAccess, METHOD_POST, (EXACT_PARAM("hook_actions"), TOKEN_PARAM, EXACT_PARAM("resolve")), HookActionHandler::handleResolveHookAction, actionHandler);
		// VARIABLE_METHOD_HANDLER(aHookAccess, METHOD_POST, (EXACT_PARAM("hook_actions"), TOKEN_PARAM, EXACT_PARAM("reject")), HookActionHandler::handleRejectHookAction, actionHandler);

		METHOD_HANDLER(aHookAccess, METHOD_POST, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID), TOKEN_PARAM, EXACT_PARAM("resolve")), HookApiModule::handleResolveHookAction);
		METHOD_HANDLER(aHookAccess, METHOD_POST, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID), TOKEN_PARAM, EXACT_PARAM("reject")), HookApiModule::handleRejectHookAction);
	}

	api_return HookApiModule::handleResolveHookAction(ApiRequest& aRequest) {
		return actionHandler.handleResolveHookAction(aRequest);
	}

	api_return HookApiModule::handleRejectHookAction(ApiRequest& aRequest) {
		return actionHandler.handleRejectHookAction(aRequest);
	}

	void HookApiModule::createHook(const string& aSubscription, HookAddF&& aAddHandler, HookRemoveF&& aRemoveF, HookListF&& aListF) noexcept {
		addHook(aSubscription, APIHook(aSubscription, std::move(aAddHandler), std::move(aRemoveF), std::move(aListF)));
		createSubscription(aSubscription);
	}

	void HookApiModule::addHook(const string& aSubscription, APIHook&& aHook) noexcept {
		hooks.emplace(aSubscription, std::move(aHook));
	}

	void HookApiModule::on(SessionListener::SocketDisconnected) noexcept {
		for (auto& h : hooks | views::values) {
			h.disable(session);
		}
		actionHandler.stop();

		SubscribableApiModule::on(SessionListener::SocketDisconnected());
	}
	
	HookApiModule::APIHook& HookApiModule::getAPIHook(ApiRequest& aRequest) {
		if (!SubscribableApiModule::getSocket()) {
			throw RequestException(http_status::precondition_required, "Socket required");
		}

		const auto& hook = aRequest.getStringParam(LISTENER_PARAM_ID);
		auto i = hooks.find(hook);
		if (i == hooks.end()) {
			throw RequestException(http_status::not_found, "No such hook: " + hook);
		}

		return i->second;
	}

	api_return HookApiModule::handleListHooks(ApiRequest& aRequest) {
		const auto& hook = getAPIHook(aRequest);

		auto ret = json::array();
		for (const auto& h : hook.getSubscribers()) {
			ret.push_back({
				{ "id", h.getId() },
				{ "name", h.getName() },
			});
		}

		aRequest.setResponseBody(ret);
		return http_status::ok;
	}

	ActionHookSubscriber HookApiModule::deserializeActionHookSubscriber(CallerPtr aOwner, Session* aSession, const json& aJson) {
		auto id = JsonUtil::getField<string>("id", aJson, false);
		auto name = JsonUtil::getField<string>("name", aJson, false);
		return ActionHookSubscriber(id, name, aOwner);
	}

	api_return HookApiModule::handleSubscribeHook(ApiRequest& aRequest) {
		auto& apiHook = getAPIHook(aRequest);
		auto actionHookSubscriber = deserializeActionHookSubscriber(aRequest.getOwnerPtr(), session, aRequest.getRequestBody());

		handleSubscribe(aRequest);
		apiHook.enable(std::move(actionHookSubscriber));

		return http_status::no_content;
	}

	api_return HookApiModule::handleUnsubscribeHook(ApiRequest& aRequest) {
		auto& apiHook = getAPIHook(aRequest);

		apiHook.disable(session);
		handleUnsubscribe(aRequest);

		return http_status::no_content;
	}

	HookCompletionDataPtr HookApiModule::maybeFireHook(const string& aSubscription, int aTimeoutSeconds, const JsonCallback& aJsonCallback) {
		if (!subscriptionActive(aSubscription)) {
			return nullptr;
		}

		return fireHook(aSubscription, aTimeoutSeconds, aJsonCallback());
	}

	HookCompletionDataPtr HookApiModule::fireHook(const string& aSubscription, int aTimeoutSeconds, const json& aJson) {
		return actionHandler.runHook(aSubscription, aTimeoutSeconds, aJson, this);
	}
}