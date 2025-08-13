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

#include <api/base/HookActionHandler.h>

namespace webserver {

	IncrementingIdCounter<int> HookActionHandler::hookIdCounter;

	api_return HookActionHandler::handleResolveHookAction(ApiRequest& aRequest) {
		return handleHookAction(aRequest, false);
	}

	api_return HookActionHandler::handleRejectHookAction(ApiRequest& aRequest) {
		return handleHookAction(aRequest, true);
	}

	void HookActionHandler::reportError(const string& aError, SubscribableApiModule* aModule) noexcept {
		aModule->getSession()->reportError(aError);
	}


	HookCompletionDataPtr HookActionHandler::runHook(const string& aSubscription, int aTimeoutSeconds, const json& aJson, SubscribableApiModule* aModule) {
#ifdef _DEBUG
		auto start = std::chrono::system_clock::now();
#endif

		// Add a pending entry
		int id;
		Semaphore completionSemaphore;

		{
			WLock l(cs);
			id = hookIdCounter.next();
			pendingHookActions.try_emplace(id, completionSemaphore, nullptr);
			//dcdebug("Adding action %d for hook %s, total pending count %d\n", id, aSubscription.c_str(), pendingHookActions.size());
		}

		// Notify the subscriber
		if (aModule->send({
			{ "event", aSubscription },
			{ "completion_id", id },
			{ "data", aJson },
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

		if (!completionData) {
			reportError("Action " + aSubscription + " timed out for subscriber " + aModule->getSession()->getUser()->getUserName(), aModule);
			dcdebug("Action %s (id %d) timed out\n", aSubscription.c_str(), id);
#ifdef _DEBUG
		} else {
			std::chrono::duration<double> elapsed = std::chrono::system_clock::now() - start;
			dcdebug("Action %s (id %d) completed in %f s\n", aSubscription.c_str(), id, elapsed.count());
#endif
		}

		return completionData;
	}

	void HookActionHandler::stop() noexcept {
		{
			RLock l(cs);
			for (auto& action : pendingHookActions | views::values) {
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
	}

	api_return HookActionHandler::handleHookAction(ApiRequest& aRequest, bool aRejected) {
		auto id = aRequest.getTokenParam();

		WLock l(cs);
		auto h = pendingHookActions.find(id);
		if (h == pendingHookActions.end()) {
			aRequest.setResponseErrorStr("No pending hook with ID " + std::to_string(id) + " (did the hook time out?)");
			return websocketpp::http::status_code::not_found;
		}

		auto& action = h->second;
		action.completionData = std::make_shared<HookCompletionData>(aRejected, aRequest.getRequestBody());
		action.semaphore.signal();
		return websocketpp::http::status_code::no_content;
	}

	HookCompletionData::HookCompletionData(bool aRejected, const json& aJson) : rejected(aRejected) {
		if (aRejected) {
			rejectId = JsonUtil::getField<string>("reject_id", aJson, false);
			rejectMessage = JsonUtil::getField<string>("message", aJson, false);
		} else {
			resolveJson = aJson;
		}
	}
}