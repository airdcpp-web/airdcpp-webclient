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

#ifndef DCPLUSPLUS_DCPP_HOOK_APIMODULE_H
#define DCPLUSPLUS_DCPP_HOOK_APIMODULE_H

#include <web-server/Access.h>
#include <web-server/SessionListener.h>

#include <airdcpp/ActionHook.h>
#include <airdcpp/CriticalSection.h>
#include <airdcpp/Semaphore.h>

#include <api/base/ApiModule.h>
#include <api/common/Deserializer.h>

namespace webserver {
	class HookApiModule : public SubscribableApiModule {
	public:
		using HookAddF = std::function<bool (ActionHookSubscriber &&)>;
		using HookRemoveF = std::function<void (const string &)>;
		using HookListF = std::function<ActionHookSubscriberList ()>;

		class HookSubscriber {
		public:
			HookSubscriber(const string& aHookId, HookAddF&& aAddHandler, HookRemoveF&& aRemoveF, HookListF&& aListF) : 
				addHandler(std::move(aAddHandler)), removeHandler(std::move(aRemoveF)), listHandler(std::move(aListF)), hookId(aHookId) {}

			bool enable(ActionHookSubscriber&& aHookSubscriber);
			void disable();

			bool isActive() const noexcept {
				return active;
			}

			const string& getSubscriberId() const noexcept {
				return subscriberId;
			}

			const string& getHookId() const noexcept {
				return hookId;
			}

			ActionHookSubscriberList getSubscribers() const noexcept {
				return listHandler();
			}
		private:
			bool active = false;

			const HookAddF addHandler;
			const HookRemoveF removeHandler;
			const HookListF listHandler;
			const string hookId;

			string subscriberId;
		};

		struct HookCompletionData {
			HookCompletionData(bool aRejected, const json& aJson);

			json resolveJson;

			string rejectId;
			string rejectMessage;
			const bool rejected;

			using Ptr = std::shared_ptr<HookCompletionData>;

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
		using HookCompletionDataPtr = HookCompletionData::Ptr;

		HookApiModule(Session* aSession, Access aSubscriptionAccess, const StringList& aSubscriptions, Access aHookAccess);

		virtual void createHook(const string& aSubscription, HookAddF&& aAddHandler, HookRemoveF&& aRemoveF, HookListF&& aListF) noexcept;
		virtual bool hookActive(const string& aSubscription) const noexcept;

		virtual HookCompletionDataPtr fireHook(const string& aSubscription, int aTimeoutSeconds, const JsonCallback& aJsonCallback);
	protected:
		HookSubscriber& getHookSubscriber(ApiRequest& aRequest);

		void on(SessionListener::SocketDisconnected) noexcept override;

		virtual bool addHook(HookSubscriber& aApiSubscriber, ActionHookSubscriber&& aHookSubscriber, const json& aJson);

		virtual api_return handleAddHook(ApiRequest& aRequest);
		virtual api_return handleRemoveHook(ApiRequest& aRequest);
		virtual api_return handleListHooks(ApiRequest& aRequest);
		virtual api_return handleResolveHookAction(ApiRequest& aRequest);
		virtual api_return handleRejectHookAction(ApiRequest& aRequest);

		static ActionHookSubscriber deserializeSubscriber(CallerPtr aOwner, const json& aJson);
	protected:
		mutable SharedMutex cs;
	private:
		api_return handleHookAction(ApiRequest& aRequest, bool aRejected);

		struct PendingAction {
			Semaphore& semaphore;
			HookCompletionDataPtr completionData;
		};

		using PendingHookActionMap = map<int, PendingAction>;

		PendingHookActionMap pendingHookActions;

		map<string, HookSubscriber> hooks;

		int pendingHookIdCounter = 1;

		int getActionId() noexcept;
	};

	template<class IdType>
	class FilterableHookApiModule : public HookApiModule {
	public:
		using IdDeserializerF = Deserializer::ArrayDeserializerFunc<IdType>;
		using IdSet = set<IdType>;

		FilterableHookApiModule(Session* aSession, Access aSubscriptionAccess, const StringList& aSubscriptions, Access aHookAccess, IdDeserializerF aIdDeserializerF) : 
			HookApiModule(aSession, aSubscriptionAccess, aSubscriptions, aHookAccess), idDeserializer(std::move(aIdDeserializerF)) {}

		bool isIdActive(const string& aSubscription, const IdType& aId) const noexcept {
			RLock l(cs);
			auto idMapIter = ids.find(aSubscription);
			if (idMapIter != ids.end()) {
				if (!idMapIter->second.contains(aId)) {
					return false;
				}
			}

			return true;
		}

		bool maybeSend(const string& aSubscription, const IdType& aId, const JsonCallback& aCallback) {
			if (!isIdActive(aSubscription, aId)) {
				return false;
			}

			return HookApiModule::maybeSend(aSubscription, aCallback);
		}

		HookCompletionDataPtr maybeFireHook(const string& aSubscription, const IdType& aId, int aTimeoutSeconds, const JsonCallback& aJsonCallback) {
			if (!isIdActive(aSubscription, aId)) {
				return nullptr;
			}

			return HookApiModule::fireHook(aSubscription, aTimeoutSeconds, aJsonCallback);
		}

		void setIds(const string& aSubscription, const IdSet& aIds) {
			if (aIds.empty()) {
				return;
			}

			WLock l(cs);
			ids[aSubscription] = aIds;
		}

		void removeIds(const string& aSubscription) {
			WLock l(cs);
			ids.erase(aSubscription);
		}

		IdSet deserializeIds(const json& aJson) {
			auto lst = Deserializer::deserializeList("ids", aJson, idDeserializer, true);
			return set(lst.begin(), lst.end());
		}

		void setSubscriptionState(const string& aSubscription, bool aActive, const json& aJson) noexcept {
			{
				if (aActive) {
					auto entityIds = deserializeIds(aJson);
					setIds(aSubscription, entityIds);
				} else {
					removeIds(aSubscription);
				}
			}

			HookApiModule::setSubscriptionState(aSubscription, aActive);
		}

		bool addHook(HookSubscriber& aApiSubscriber, ActionHookSubscriber&& aHookSubscriber, const json& aJson) override {
			auto entityIds = deserializeIds(aJson);
			setIds(aApiSubscriber.getHookId(), entityIds);

			return HookApiModule::addHook(aApiSubscriber, std::move(aHookSubscriber), aJson);
		}
	private:
		map<string, IdSet> ids;

		IdDeserializerF idDeserializer;
	};
}

#endif