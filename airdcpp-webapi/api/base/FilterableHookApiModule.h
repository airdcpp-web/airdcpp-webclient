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

#ifndef DCPLUSPLUS_DCPP_FILTERABLE_HOOK_APIMODULE_H
#define DCPLUSPLUS_DCPP_FILTERABLE_HOOK_APIMODULE_H

#include <web-server/Access.h>

#include <airdcpp/core/thread/CriticalSection.h>

#include <api/base/HookApiModule.h>
#include <api/base/FilterableSubscribableApiModule.h>

namespace webserver {

#define FILTERABLE_HOOK_HANDLER(name, hook, callback) MODULE_HOOK_HANDLER(Type::createFilterableHook, name, hook, callback)

	template<class IdType>
	class FilterableHookApiModule : public FilterableSubscribableApiModule<IdType, HookApiModule> {
	public:
		using Type = FilterableHookApiModule<IdType>;
		using BaseType = FilterableSubscribableApiModule<IdType, HookApiModule>;

		FilterableHookApiModule(
			Session* aSession, 
			Access aSubscriptionAccess,
			Access aHookAccess, 
			BaseType::IdDeserializerF aIdDeserializerF,
			BaseType::IdSerializerF aIdSerializerF
		) :
			BaseType(std::move(aIdDeserializerF), std::move(aIdSerializerF), aSession, aSubscriptionAccess, aHookAccess) {

			METHOD_HANDLER(aHookAccess, METHOD_POST, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID), FILTERABLE_LISTENER_ENTITY_ID_PARAM), Type::handleAddHookEntity);
			METHOD_HANDLER(aHookAccess, METHOD_DELETE, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID), FILTERABLE_LISTENER_ENTITY_ID_PARAM), Type::handleRemoveHookEntity);

			METHOD_HANDLER(aHookAccess, METHOD_POST, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID)), Type::handleSubscribeHook);

			METHOD_HANDLER(aHookAccess, METHOD_POST, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID), FILTERABLE_LISTENER_ENTITY_ID_PARAM, TOKEN_PARAM, EXACT_PARAM("resolve")), Type::handleResolveHookAction);
			METHOD_HANDLER(aHookAccess, METHOD_POST, (EXACT_PARAM("hooks"), STR_PARAM(LISTENER_PARAM_ID), FILTERABLE_LISTENER_ENTITY_ID_PARAM, TOKEN_PARAM, EXACT_PARAM("reject")), Type::handleRejectHookAction);
		}

		api_return handleSubscribeHook(ApiRequest& aRequest) {
			const auto& subscription = aRequest.getStringParam(LISTENER_PARAM_ID);
			if (BaseType::hasEntitySubscribers(subscription)) {
				throw RequestException(http_status::conflict, "Global hook subscription can't be added while ID-specific subscriptions are active");
			}

			return HookApiModule::handleSubscribeHook(aRequest);
		}

		void createFilterableHook(const string& aSubscription, HookApiModule::HookAddF&& aAddHandler, HookApiModule::HookRemoveF&& aRemoveF, HookApiModule::HookListF&& aListF) noexcept {
			BaseType::createFilterableSubscription(aSubscription);
			HookApiModule::addHook(aSubscription, HookApiModule::APIHook(aSubscription, std::move(aAddHandler), std::move(aRemoveF), std::move(aListF)));
		}

		api_return handleAddHookEntity(ApiRequest& aRequest) {
			auto& apiHook = HookApiModule::getAPIHook(aRequest);
			auto actionHookSubscriber = HookApiModule::deserializeActionHookSubscriber(aRequest.getOwnerPtr(), aRequest.getSession().get(), aRequest.getRequestBody());
			auto entityId = BaseType::parseEntityIdParam(aRequest);

			if (!BaseType::filterableSubscriptionExists(apiHook.getHookId())) {
				throw RequestException(http_status::not_found, "No such filterable hook: " + apiHook.getHookId());
			}

			if (BaseType::subscriptionActive(apiHook.getHookId())) {
				throw RequestException(http_status::conflict, "ID-specific subscription can't be added while the hook is globally active");
			}

			if (!BaseType::hasEntitySubscribers(apiHook.getHookId())) {
				apiHook.enable(std::move(actionHookSubscriber));
				dcdebug("Subscriber %s: hook %s was enabled\n", actionHookSubscriber.getId().c_str(), apiHook.getHookId().c_str());
			} else {
				dcdebug("Subscriber %s: hook %s is already active\n", actionHookSubscriber.getId().c_str(), apiHook.getHookId().c_str());
			}

			BaseType::subscribeEntity(apiHook.getHookId(), entityId);
			return http_status::no_content;
		}

		api_return handleRemoveHookEntity(ApiRequest& aRequest) {
			auto& apiHook = BaseType::getAPIHook(aRequest);
			auto entityId = BaseType::parseEntityIdParam(aRequest);
			auto subscriberId = apiHook.getHookSubscriberId();

			BaseType::unsubscribeEntity(apiHook.getHookId(), entityId);
			if (!BaseType::hasEntitySubscribers(apiHook.getHookId())) {
				apiHook.disable(aRequest.getSession().get());
				dcdebug("Subscriber %s: hook %s was disabled\n", subscriberId.c_str(), apiHook.getHookId().c_str());
			} else {
				dcdebug("Subscriber %s: hook %s has other subscribers, not disabling\n", subscriberId.c_str(), apiHook.getHookId().c_str());
			}

			return http_status::no_content;
		}

		HookCompletionDataPtr maybeFireHook(const string& aSubscription, const IdType& aId, int aTimeoutSeconds, const BaseType::JsonCallback& aJsonCallback) {
			if (BaseType::hasEntitySubscribers(aSubscription, aId)) {
				return HookApiModule::fireHook(BaseType::toSubscription(aSubscription, aId), aTimeoutSeconds, aJsonCallback());
			}

			return HookApiModule::maybeFireHook(aSubscription, aTimeoutSeconds, aJsonCallback);
		}
	};
}

#endif