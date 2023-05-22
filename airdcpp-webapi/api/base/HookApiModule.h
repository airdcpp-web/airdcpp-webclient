/*
* Copyright (C) 2011-2023 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_HOOK_APIMODULE_H
#define DCPLUSPLUS_DCPP_HOOK_APIMODULE_H

#include <web-server/Access.h>
#include <web-server/SessionListener.h>

#include <airdcpp/ActionHook.h>
#include <airdcpp/CriticalSection.h>
#include <airdcpp/Semaphore.h>

#include <api/base/ApiModule.h>

namespace webserver {
	class HookApiModule : public SubscribableApiModule {
	public:
		typedef std::function<bool(ActionHookSubscriber&& aSubscriber)> HookAddF;
		typedef std::function<void(const string& aSubscriberId)> HookRemoveF;
		typedef std::function<ActionHookSubscriberList()> HookListF;

		class HookSubscriber {
		public:
			HookSubscriber(HookAddF&& aAddHandler, HookRemoveF&& aRemoveF, HookListF&& aListF) : addHandler(std::move(aAddHandler)), removeHandler(aRemoveF), listHandler(aListF) {}

			bool enable(const void* aOwner, const json& aJson);
			void disable();

			bool isActive() const noexcept {
				return active;
			}

			const string& getSubscriberId() const noexcept {
				return subscriberId;
			}

			ActionHookSubscriberList getSubscribers() const noexcept {
				return listHandler();
			}
		private:
			bool active = false;

			const HookAddF addHandler;
			const HookRemoveF removeHandler;
			const HookListF listHandler;
			string subscriberId;
		};

		struct HookCompletionData {
			HookCompletionData(bool aRejected, const json& aJson);

			json resolveJson;

			string rejectId;
			string rejectMessage;
			const bool rejected;

			typedef std::shared_ptr<HookCompletionData> Ptr;

			template<typename DataT>
			using HookDataGetter = std::function<DataT(const json& aDataJson, const ActionHookResultGetter<DataT>& aResultGetter)>;

			template <typename DataT = nullptr_t>
			static ActionHookResult<DataT> toResult(const HookCompletionData::Ptr& aData, const ActionHookResultGetter<DataT>& aResultGetter, const HookDataGetter<DataT>& aDataGetter = nullptr) noexcept {
				if (aData) {
					if (aData->rejected) {
						return aResultGetter.getRejection(aData->rejectId, aData->rejectMessage);
					} else if (aDataGetter) {
						try {
							const auto data = aResultGetter.getData(aDataGetter(aData->resolveJson, aResultGetter));
							return data;
						} catch (const std::exception& e) {
							dcdebug("Failed to deserialize hook data for subscriber %s: %s\n", aResultGetter.getSubscriber().getId().c_str(), e.what());
							return aResultGetter.getDataRejection(e);
						}
					}
				}

				return { nullptr, nullptr };
			}
		};
		typedef HookCompletionData::Ptr HookCompletionDataPtr;

		HookApiModule(Session* aSession, Access aSubscriptionAccess, const StringList& aSubscriptions, Access aHookAccess);

		virtual void createHook(const string& aSubscription, HookAddF&& aAddHandler, HookRemoveF&& aRemoveF, HookListF&& aListF) noexcept;
		virtual bool hookActive(const string& aSubscription) const noexcept;

		virtual HookCompletionDataPtr fireHook(const string& aSubscription, int aTimeoutSeconds, JsonCallback&& aJsonCallback);
	protected:
		HookSubscriber& getHookSubscriber(ApiRequest& aRequest);

		virtual void on(SessionListener::SocketDisconnected) noexcept override;

		virtual api_return handleAddHook(ApiRequest& aRequest);
		virtual api_return handleRemoveHook(ApiRequest& aRequest);
		virtual api_return handleListHooks(ApiRequest& aRequest);
		virtual api_return handleResolveHookAction(ApiRequest& aRequest);
		virtual api_return handleRejectHookAction(ApiRequest& aRequest);
	private:
		api_return handleHookAction(ApiRequest& aRequest, bool aRejected);

		struct PendingAction {
			Semaphore& semaphore;
			HookCompletionDataPtr completionData;
		};

		typedef map<int, PendingAction> PendingHookActionMap;

		PendingHookActionMap pendingHookActions;

		map<string, HookSubscriber> hooks;

		int pendingHookIdCounter = 1;
		mutable SharedMutex cs;

		int getActionId() noexcept;
	};

	typedef std::unique_ptr<ApiModule> HandlerPtr;
}

#endif