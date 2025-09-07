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

#ifndef DCPLUSPLUS_DCPP_HOOK_ACTION_HANDLER_H
#define DCPLUSPLUS_DCPP_HOOK_ACTION_HANDLER_H

#include <airdcpp/core/ActionHook.h>
#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/core/thread/Semaphore.h>
#include <airdcpp/core/classes/IncrementingIdCounter.h>

#include <api/base/SubscribableApiModule.h>

namespace webserver {
	struct HookCompletionData;
	using HookCompletionDataPtr = std::shared_ptr<HookCompletionData>;

	class HookActionHandler {
	public:
		HookCompletionDataPtr runHook(const string& aSubscription, int aTimeoutSeconds, const json& aJson, SubscribableApiModule* aModule);
		void stop() noexcept;

		api_return handleResolveHookAction(ApiRequest& aRequest);
		api_return handleRejectHookAction(ApiRequest& aRequest);

		static void reportError(const string& aError, SubscribableApiModule* aModule) noexcept;
	private:
		api_return handleHookAction(ApiRequest& aRequest, bool aRejected);
		mutable SharedMutex cs;

		struct PendingAction {
			Semaphore& semaphore;
			HookCompletionDataPtr completionData;
		};

		using PendingHookActionMap = map<int, PendingAction>;
		PendingHookActionMap pendingHookActions;

		static IncrementingIdCounter<int> hookIdCounter;
	};

	class HookActionHandler;
	struct HookCompletionData {
		HookCompletionData(bool aRejected, const json& aJson);

		json resolveJson;

		string rejectId;
		string rejectMessage;
		const bool rejected;

		template<typename DataT>
		using HookDataGetter = std::function<DataT(const json& aDataJson, const ActionHookResultGetter<DataT>& aResultGetter)>;

		template <typename DataT = nullptr_t>
		static ActionHookResult<DataT> toResult(const HookCompletionDataPtr& aData, const ActionHookResultGetter<DataT>& aResultGetter, SubscribableApiModule* aModule, const HookDataGetter<DataT>& aDataGetter = nullptr) noexcept {
			if (aData) {
				if (aData->rejected) {
					return aResultGetter.getRejection(aData->rejectId, aData->rejectMessage);
				} else if (aDataGetter) {
					try {
						const auto data = aResultGetter.getData(aDataGetter(aData->resolveJson, aResultGetter));
						return data;
					} catch (const ArgumentException& e) {
						dcdebug("Failed to deserialize hook data for subscriber %s: %s (field %s)\n", aResultGetter.getSubscriber().getId().c_str(), e.what(), e.getField().c_str());
						HookActionHandler::reportError("Failed to deserialize hook data for subscriber " + aResultGetter.getSubscriber().getId() + ": " + e.what() + " (field \"" + e.getField() + "\")", aModule);
						return aResultGetter.getDataRejection(e);
					} catch (const std::exception& e) {
						dcdebug("Failed to deserialize hook data for subscriber %s: %s\n", aResultGetter.getSubscriber().getId().c_str(), e.what());
						HookActionHandler::reportError("Failed to deserialize hook data for subscriber " + aResultGetter.getSubscriber().getId() + ": " + e.what(), aModule);
						return aResultGetter.getDataRejection(e);
					}
				}
			}

			return { nullptr, nullptr };
		}
	};
}

#endif