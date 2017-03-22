/*
* Copyright (C) 2011-2017 AirDC++ Project
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

#include <web-server/stdinc.h>
#include <web-server/JsonUtil.h>

#include <api/base/HookApiModule.h>

namespace webserver {
	HookApiModule::HookApiModule(Session* aSession, Access aSubscriptionAccess, const StringList* aSubscriptions, Access aHookAccess) :
		SubscribableApiModule(aSession, aSubscriptionAccess, aSubscriptions) 
	{
		METHOD_HANDLER(aHookAccess, METHOD_POST, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID)), HookApiModule::handleAddHook);
		METHOD_HANDLER(aHookAccess, METHOD_DELETE, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID)), HookApiModule::handleRemoveHook);
		METHOD_HANDLER(aHookAccess, METHOD_POST, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID), TOKEN_PARAM, EXACT_PARAM("resolve")), HookApiModule::handleResolveHookAction);
		METHOD_HANDLER(aHookAccess, METHOD_POST, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID), TOKEN_PARAM, EXACT_PARAM("reject")), HookApiModule::handleRejectHookAction);
	}

	void HookApiModule::on(SessionListener::SocketDisconnected) noexcept {
		for (auto& h : hooks | map_values) {
			h.disable();
		}

		{
			RLock l(cs);
			for (auto& action : pendingHookActions | map_values) {
				action.semaphore.signal();
			}
		}

		// Wait for the pending action hooks to be cancelled
		while (true) {
			{
				RLock l(cs);
				if (pendingHookActions.empty()) {
					break;
				}
			}

			std::this_thread::sleep_for(chrono::milliseconds(100));
		}

		SubscribableApiModule::on(SessionListener::SocketDisconnected());
	}

	bool HookApiModule::hookActive(const string& aSubscription) const noexcept {
		dcassert(hooks.find(aSubscription) != hooks.end());
		return hooks.at(aSubscription).isActive();
	}

	HookApiModule::HookSubscriber& HookApiModule::getHookSubscriber(ApiRequest& aRequest) {
		const auto& hook = aRequest.getStringParam(LISTENER_PARAM_ID);
		auto i = hooks.find(hook);
		if (i == hooks.end()) {
			throw RequestException(websocketpp::http::status_code::not_found, "No such hook: " + hook);
		}

		return i->second;
	}

	bool HookApiModule::HookSubscriber::enable(const json& aJson) {
		if (active) {
			return true;
		}

		auto id = JsonUtil::getField<string>("id", aJson, false);
		if (!addHandler(id, JsonUtil::getField<string>("name", aJson, false))) {
			return false;
		}

		subscriberId = id;
		active = true;
		return true;
	}

	void HookApiModule::HookSubscriber::disable() {
		if (!active) {
			return;
		}

		removeHandler(subscriberId);
		active = false;
	}

	api_return HookApiModule::handleAddHook(ApiRequest& aRequest) {
		if (!SubscribableApiModule::getSocket()) {
			aRequest.setResponseErrorStr("Socket required");
			return websocketpp::http::status_code::precondition_required;
		}

		auto& hook = getHookSubscriber(aRequest);
		if (!hook.enable(aRequest.getRequestBody())) {
			aRequest.setResponseErrorStr("Subscription ID exists already for this hook event");
			return websocketpp::http::status_code::conflict;
		}

		return websocketpp::http::status_code::no_content;
	}

	api_return HookApiModule::handleRemoveHook(ApiRequest& aRequest) {
		auto& hook = getHookSubscriber(aRequest);
		hook.disable();

		return websocketpp::http::status_code::not_found;
	}

	HookApiModule::HookCompletionData::HookCompletionData(bool aRejected, const json& aJson) : rejected(aRejected) {
		if (aRejected) {
			rejectId = JsonUtil::getField<string>("reject_id", aJson, false);
			rejectMessage = JsonUtil::getField<string>("message", aJson, false);
		} else {
			resolveJson = aJson;
		}
	}

	ActionHookRejectionPtr HookApiModule::HookCompletionData::toResult(const HookCompletionData::Ptr& aData, const HookRejectionGetter& aRejectionGetter) noexcept {
		if (!aData || !aData->rejected) {
			return nullptr;
		}

		return aRejectionGetter(aData->rejectId, aData->rejectMessage);
	}

	void HookApiModule::createHook(const string& aSubscription, HookAddF&& aAddHandler, HookRemoveF&& aRemoveF) noexcept {
		hooks.emplace(aSubscription, HookSubscriber(std::move(aAddHandler), std::move(aRemoveF)));
	}

	api_return HookApiModule::handleResolveHookAction(ApiRequest& aRequest) {
		return handleHookAction(aRequest, false);
	}

	api_return HookApiModule::handleRejectHookAction(ApiRequest& aRequest) {
		return handleHookAction(aRequest, true);
	}

	api_return HookApiModule::handleHookAction(ApiRequest& aRequest, bool aRejected) {
		auto id = aRequest.getTokenParam();

		WLock l(cs);
		auto h = pendingHookActions.find(id);
		if (h == pendingHookActions.end()) {
			aRequest.setResponseErrorStr("No pending hook with this ID");
			return websocketpp::http::status_code::not_found;
		}

		auto& action = h->second;
		action.completionData = std::make_shared<HookCompletionData>(aRejected, aRequest.getRequestBody());
		action.semaphore.signal();
		return websocketpp::http::status_code::no_content;
	}

	HookApiModule::HookCompletionDataPtr HookApiModule::fireHook(const string& aSubscription, int aTimeoutSeconds, JsonCallback&& aJsonCallback) {
		if (!hookActive(aSubscription)) {
			return nullptr;
		}

#ifdef _DEBUG
		auto start = std::chrono::system_clock::now();
#endif

		// Add a pending entry
		auto id = pendingHookIdCounter++;
		Semaphore completionSemaphore;

		{
			WLock l(cs);
			pendingHookActions.emplace(id, PendingAction({ completionSemaphore, nullptr }));
		}

		// Notify the subscriber
		if (send({
			{ "event", aSubscription },
			{ "completion_id", id },
			{ "data", aJsonCallback() },
		})) {
			completionSemaphore.wait(aTimeoutSeconds * 1000);
		}

		// Clean up
		HookCompletionDataPtr completionData = nullptr;

		{
			WLock l(cs);
			completionData = pendingHookActions.at(id).completionData;
			pendingHookActions.erase(id);
		}

#ifdef _DEBUG
		std::chrono::duration<double> ellapsed = std::chrono::system_clock::now() - start;
		dcdebug("Action %s completed in %f s\n", aSubscription.c_str(), ellapsed.count());

		if (!completionData) {
			dcdebug("API hook %s timed out\n", aSubscription.c_str());
		}
#endif

		return completionData;
	}
}