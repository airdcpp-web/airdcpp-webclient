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

#ifndef DCPLUSPLUS_DCPP_HIERARCHIAL_APIMODULE_H
#define DCPLUSPLUS_DCPP_HIERARCHIAL_APIMODULE_H

#include <api/base/ApiModule.h>

#include <web-server/stdinc.h>
#include <web-server/ApiRequest.h>
#include <web-server/Session.h>
#include <web-server/WebServerManager.h>

#include <airdcpp/CriticalSection.h>

namespace webserver {
	class WebSocket;

	template<class IdType, class ItemType, class BaseType = SubscribableApiModule>
	class ParentApiModule : public BaseType {
	public:
		typedef ParentApiModule<IdType, ItemType, BaseType> Type;
		typedef std::function<IdType(const string&)> IdConvertF;
		typedef std::function<json(const ItemType&)> ChildSerializeF;

		template<typename... ArgT>
		ParentApiModule(const string& aSubmoduleSection, ApiModule::RequestHandler::Param&& aParamMatcher, Access aAccess, Session* aSession, const StringList& aSubscriptions, const StringList& aChildSubscription, IdConvertF aIdConvertF, ChildSerializeF aChildSerializeF, ArgT&&... args) :
			BaseType(aSession, aAccess, &aSubscriptions, std::forward<ArgT>(args)...), idConvertF(aIdConvertF), childSerializeF(aChildSerializeF), paramId(aParamMatcher.id), childSubscriptions(aChildSubscription) {

			// Get module
			METHOD_HANDLER(aAccess, METHOD_GET, (EXACT_PARAM(aSubmoduleSection), aParamMatcher), Type::handleGetSubmodule);

			// List modules
			METHOD_HANDLER(aAccess, METHOD_GET, (EXACT_PARAM(aSubmoduleSection)), Type::handleGetSubmodules);

			// Request forwarder
			METHOD_HANDLER(Access::ANY, METHOD_FORWARD, (EXACT_PARAM(aSubmoduleSection), aParamMatcher), Type::handleSubModuleRequest);
		}

		~ParentApiModule() {
			// Child modules must always be destoyed first because they depend on the parent for subscription checking 
			// (which can happen via listeners)

			// There can't be references to shared child pointers from other threads because no other requests 
			// can be active at this point (otherwise we wouldn't be destoying the session)

			WLock l(cs);
			dcassert(boost::find_if(subModules | map_values, [](const typename ItemType::Ptr& subModule) {  
				return !subModule.unique();
			}).base() == subModules.end());

			subModules.clear();
		}

		api_return handleSubscribe(ApiRequest& aRequest) override {
			if (!SubscribableApiModule::getSocket()) {
				aRequest.setResponseErrorStr("Socket required");
				return websocketpp::http::status_code::precondition_required;
			}

			const auto& subscription = aRequest.getStringParam(LISTENER_PARAM_ID);
			if (setChildSubscriptionState(subscription, true)) {
				return websocketpp::http::status_code::no_content;
			}

			return SubscribableApiModule::handleSubscribe(aRequest);
		}

		api_return handleUnsubscribe(ApiRequest& aRequest) override {
			const auto& subscription = aRequest.getStringParam(LISTENER_PARAM_ID);
			if (setChildSubscriptionState(subscription, false)) {
				return websocketpp::http::status_code::no_content;
			}

			return SubscribableApiModule::handleUnsubscribe(aRequest);
		}

		// Forward request to a submodule
		api_return handleSubModuleRequest(ApiRequest& aRequest) {
			auto sub = getSubModule(aRequest);

			// Remove section and module ID
			aRequest.popParam(2);

			return sub->handleRequest(aRequest);
		}

		bool subscriptionExists(const string& aSubscription) const noexcept override {
			if (hasChildSubscription(aSubscription)) {
				return true;
			}

			return SubscribableApiModule::subscriptionExists(aSubscription);
		}

		// Change subscription state for all submodules
		bool setChildSubscriptionState(const string& aSubscription, bool aActive) noexcept {
			if (hasChildSubscription(aSubscription)) {
				RLock l(cs);
				for (const auto& m : subModules | map_values) {
					m->setSubscriptionState(aSubscription, aActive);
				}

				return true;
			}

			return false;
		}

		// Submodules should NEVER be accessed outside of web server threads (e.g. API requests)
		typename ItemType::Ptr findSubModule(IdType aId) {
			RLock l(cs);
			auto m = subModules.find(aId);
			if (m != subModules.end()) {
				return m->second;
			}

			return nullptr;
		}

		// Submodules should NEVER be accessed outside of web server threads (e.g. API requests)
		typename ItemType::Ptr findSubModule(const string& aId) {
			return findSubModule(idConvertF(aId));
		}

		// Parse module ID from the request, throws if the module was not found
		typename ItemType::Ptr getSubModule(ApiRequest& aRequest) {
			auto id = aRequest.getStringParam(paramId);

			auto sub = findSubModule(id);
			if (!sub) {
				throw RequestException(websocketpp::http::status_code::not_found, "Entity " + id + " was not found");
			}

			return sub;
		}

		api_return handleGetSubmodules(ApiRequest& aRequest) {
			auto retJson = json::array();
			forEachSubModule([&](const ItemType& aInfo) {
				retJson.push_back(childSerializeF(aInfo));
			});

			aRequest.setResponseBody(retJson);
			return websocketpp::http::status_code::ok;
		}

		api_return handleGetSubmodule(ApiRequest& aRequest) {
			auto info = getSubModule(aRequest);

			aRequest.setResponseBody(childSerializeF(*info.get()));
			return websocketpp::http::status_code::ok;
		}
	protected:
		mutable SharedMutex cs;

		bool hasChildSubscription(const string& aName) const noexcept {
			return find(childSubscriptions.begin(), childSubscriptions.end(), aName) != childSubscriptions.end();
		}

		void forEachSubModule(std::function<void(const ItemType&)> aAction) {
			RLock l(cs);
			for (const auto& m : subModules | map_values) {
				aAction(*m.get());
			}
		}

		void addSubModule(IdType aId, const typename ItemType::Ptr& aModule) {
			{
				WLock l(cs);
				subModules.emplace(aId, aModule);
			}

			aModule->init();
		}

		void removeSubModule(IdType aId) {
			WLock l(cs);
			subModules.erase(aId);
		}
	private:
		map<IdType, typename ItemType::Ptr> subModules;

		const StringList& childSubscriptions;
		const IdConvertF idConvertF;
		const ChildSerializeF childSerializeF;
		const string paramId;
	};

	template<class ParentIdType, class ItemType, class ItemJsonType, class ParentBaseType = SubscribableApiModule>
	class SubApiModule : public SubscribableApiModule {
	public:
		typedef ParentApiModule<ParentIdType, ItemType, ParentBaseType> ParentType;

		// aId = ID of the entity owning this module
		// Will inherit access from the parent module
		SubApiModule(ParentType* aParentModule, const ItemJsonType& aId, const StringList& aSubscriptions) :
			SubscribableApiModule(aParentModule->getSession(), aParentModule->getSubscriptionAccess(), &aSubscriptions), parentModule(aParentModule), id(aId) { }

		bool send(const string& aSubscription, const json& aJson) override {
			return SubscribableApiModule::send({
				{ "event", aSubscription },
				{ "data", aJson },
				{ "id", id }
			});
		}

		bool maybeSend(const string& aSubscription, SubscribableApiModule::JsonCallback aCallback) override {
			if (!subscriptionActive(aSubscription)) {
				return false;
			}

			return send(aSubscription, aCallback());
		}


		// Init submodules in a separate call after the module has been constructed and added to the parent
		// Otherwise async calls made in module constructor would fail because they require
		// module exist in the parent
		virtual void init() noexcept = 0;

		void createSubscription(const string& aSubscription) noexcept override {
			dcassert(0);
		}

		void addAsyncTask(CallBack&& aTask) override {
			SubscribableApiModule::addAsyncTask(getAsyncWrapper(move(aTask)));
		}

		TimerPtr getTimer(CallBack&& aTask, time_t aIntervalMillis) override {
			return session->getServer()->addTimer(move(aTask), aIntervalMillis, 
				std::bind(&SubApiModule::moduleAsyncRunWrapper<ItemJsonType, ParentType>, std::placeholders::_1, parentModule, id, session->getId())
			);
		}

		// All custom async tasks should be run inside this to
		// ensure that the submodule (or the session) won't get deleted
		CallBack getAsyncWrapper(CallBack&& aTask) noexcept override {
			auto sessionId = session->getId();
			auto moduleId = id;
			return [=] {
				return moduleAsyncRunWrapper(aTask, parentModule, moduleId, sessionId);
			};
		}
	private:
		template<class IdType, class ParentType>
		static void moduleAsyncRunWrapper(const CallBack& aTask, ParentType* aParentModule, const IdType& aId, LocalSessionId aSessionId) {
			// Ensure that we have a session
			SubscribableApiModule::asyncRunWrapper([=] {
				// Ensure that we have a submodule (the parent must exist if we have a session)
				auto m = aParentModule->findSubModule(aId);
				if (!m) {
					dcdebug("Trying to run an async task for a removed submodule\n");
					return;
				}

				aTask();
			}, aSessionId);
		}


		ParentType* parentModule;

		const ItemJsonType id;
	};
}

#endif