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

#ifndef DCPLUSPLUS_DCPP_FILTERABLE_SUBSCRIBABLE_APIMODULE_H
#define DCPLUSPLUS_DCPP_FILTERABLE_SUBSCRIBABLE_APIMODULE_H

#include <web-server/Access.h>

#include <airdcpp/core/thread/CriticalSection.h>

#include <api/base/SubscribableApiModule.h>

namespace webserver {
	template<class IdType, class BaseType = SubscribableApiModule>
	class FilterableSubscribableApiModule : public BaseType {
	public:
		using Type = FilterableSubscribableApiModule<IdType, BaseType>;

		using IdDeserializerF = std::function<IdType(const string& aParam)>;
		using IdSerializerF = std::function<string(const IdType& aParam)>;

		using IdSet = set<IdType>;

		template<typename... ArgT>
		FilterableSubscribableApiModule(IdDeserializerF aIdDeserializerF, IdSerializerF aIdSerializerF, Session* aSession, Access aSubscriptionAccess, ArgT&&... args) :
			BaseType(aSession, aSubscriptionAccess, std::forward<ArgT>(args)...), 
			idDeserializer(std::move(aIdDeserializerF)), 
			idSerializer(std::move(aIdSerializerF)) 
		{
		
#define FILTERABLE_LISTENER_ENTITY_ID "listener_entity_id"
#define FILTERABLE_LISTENER_ENTITY_ID_PARAM STR_PARAM(FILTERABLE_LISTENER_ENTITY_ID)

			METHOD_HANDLER(aSubscriptionAccess, METHOD_POST, (EXACT_PARAM("listeners"), STR_PARAM(LISTENER_PARAM_ID), FILTERABLE_LISTENER_ENTITY_ID_PARAM), Type::handleSubscribeEntity);
			METHOD_HANDLER(aSubscriptionAccess, METHOD_DELETE, (EXACT_PARAM("listeners"), STR_PARAM(LISTENER_PARAM_ID), FILTERABLE_LISTENER_ENTITY_ID_PARAM), Type::handleUnsubscribeEntity);


			METHOD_HANDLER(aSubscriptionAccess, METHOD_POST, (EXACT_PARAM("listeners"), STR_PARAM(LISTENER_PARAM_ID)), Type::handleSubscribe);
		}

		virtual void createFilterableSubscriptions(const StringList& aSubscriptions) noexcept {
			for (const auto& s: aSubscriptions) {
				createFilterableSubscription(s);
			}
		}

		virtual void createFilterableSubscription(const string& aSubscription) noexcept {
			filterableSubscriptions[aSubscription];
			BaseType::createSubscription(aSubscription);
		}

		bool maybeSend(const string& aSubscription, const IdType& aId, const BaseType::JsonCallback& aCallback) {
			if (hasEntitySubscribers(aSubscription, aId)) {
				return BaseType::send(toSubscription(aSubscription, aId), aCallback());
			}

			return BaseType::maybeSend(aSubscription, aCallback);
		}

		virtual bool filterableSubscriptionExists(const string& aSubscription) const noexcept {
			auto i = filterableSubscriptions.find(aSubscription);
			return i != filterableSubscriptions.end();
		}

	protected:
		api_return handleSubscribe(ApiRequest& aRequest) override {
			const auto& subscription = aRequest.getStringParam(LISTENER_PARAM_ID);
			if (hasEntitySubscribers(subscription)) {
				throw RequestException(http_status::conflict, "Global listener can't be added while ID-specific subscriptions are active");
			}

			return BaseType::handleSubscribe(aRequest);
		}

		const string& parseFilterableSubscription(ApiRequest& aRequest) {
			if (!BaseType::getSocket()) {
				throw RequestException(http_status::precondition_required, "Socket required");
			}

			const auto& subscription = aRequest.getStringParam(LISTENER_PARAM_ID);
			if (!filterableSubscriptionExists(subscription)) {
				throw RequestException(http_status::not_found, "No such filterable subscription: " + subscription);
			}

			return subscription;
		}

		void on(SessionListener::SocketDisconnected) noexcept override {
			{
				WLock l(cs);
				for (auto& ids : filterableSubscriptions | views::values) {
					ids.clear();
				}
			}

			BaseType::on(SessionListener::SocketDisconnected());
		}

		const IdType parseEntityIdParam(ApiRequest& aRequest) {
			const auto& entityId = aRequest.getStringParam(FILTERABLE_LISTENER_ENTITY_ID);
			return idDeserializer(entityId);
		}

		virtual api_return handleSubscribeEntity(ApiRequest& aRequest) {
			const auto& subscription = parseFilterableSubscription(aRequest);
			const auto entityId = parseEntityIdParam(aRequest);

			if (BaseType::subscriptionActive(subscription)) {
				throw RequestException(http_status::conflict, "ID-specific subscription can't be added while the listener is globally active");
			}

			subscribeEntity(subscription, entityId);
			return http_status::no_content;
		}

		bool subscribeEntity(const string& aSubscription, const IdType& aEntityId) {
			WLock l(cs);
			if (hasEntitySubscribersUnsafe(aSubscription, aEntityId)) {
				return false;
			}

			filterableSubscriptions[aSubscription].insert(aEntityId);
			return true;
		}

		bool unsubscribeEntity(const string& aSubscription, const IdType& aEntityId) {
			WLock l(cs);
			auto i = filterableSubscriptions.find(aSubscription);
			if (i != filterableSubscriptions.end()) {
				return i->second.erase(aEntityId) == 1;
			}

			dcassert(0);
			return false;
		}

		virtual api_return handleUnsubscribeEntity(ApiRequest& aRequest) {
			const auto& subscription = parseFilterableSubscription(aRequest);
			auto entityId = parseEntityIdParam(aRequest);

			unsubscribeEntity(subscription, entityId);
			return http_status::no_content;
		}

		bool hasEntitySubscribersUnsafe(const string& aSubscription, const IdType& aId) const noexcept {
			auto i = filterableSubscriptions.find(aSubscription);
			if (i != filterableSubscriptions.end()) {
				if (i->second.contains(aId)) {
					return true;
				}

				return false;
			}

			// This should never be used for non-filterable subscriptions
			dcassert(0);
			return false;
		}

		bool hasEntitySubscribers(const string& aSubscription, const IdType& aId) const noexcept {
			RLock l(cs);
			return hasEntitySubscribersUnsafe(aSubscription, aId);
		}

		bool hasEntitySubscribers(const string& aSubscription) const noexcept {
			RLock l(cs);
			auto i = filterableSubscriptions.find(aSubscription);
			if (i != filterableSubscriptions.end()) {
				return !i->second.empty();
			}

			return false;
		}

		string toSubscription(const string& aSubscription, const IdType& aId) {
			return aSubscription + "/" + idSerializer(aId);
		}

		mutable SharedMutex cs;

	private:
		map<string, IdSet> filterableSubscriptions;

		IdDeserializerF idDeserializer;
		IdSerializerF idSerializer;
	};
}

#endif