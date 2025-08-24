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

#ifndef DCPLUSPLUS_DCPP_HIERARCHIAL_APIMODULE_H
#define DCPLUSPLUS_DCPP_HIERARCHIAL_APIMODULE_H

#include <api/base/SubscribableApiModule.h>

#include <web-server/ApiRequest.h>
#include <web-server/Session.h>
#include <web-server/WebServerManager.h>

#include <airdcpp/core/thread/CriticalSection.h>

namespace webserver {
	class WebSocket;

	// Module class that will forward request to child entities and can be used to manager their subscriptions across all entities
	template<class IdType, class ItemType, class BaseType = SubscribableApiModule>
	class ParentApiModule : public BaseType {
	public:
		using Type = ParentApiModule<IdType, ItemType, BaseType>;
		using IdConvertF = std::function<IdType (const string &)>;
		using ChildSerializeF = std::function<json (const ItemType &)>;

		template<typename... ArgT>
		ParentApiModule(
			const ApiModule::RequestHandler::Param& aParamMatcher, Access aAccess, Session* aSession, 
			IdConvertF aIdConvertF, 
			ChildSerializeF aChildSerializeF, ArgT&&... args
		) :
			BaseType(aSession, aAccess, std::forward<ArgT>(args)...), idConvertF(aIdConvertF), childSerializeF(aChildSerializeF), paramId(aParamMatcher.id) {

			// Get module
			METHOD_HANDLER(aAccess, METHOD_GET, (aParamMatcher), Type::handleGetSubmodule);

			// Delete module
			METHOD_HANDLER(aAccess, METHOD_DELETE, (aParamMatcher), Type::handleDeleteSubmodule);

			// List modules
			METHOD_HANDLER(aAccess, METHOD_GET, (), Type::handleGetSubmodules);

			// Request forwarder
			METHOD_HANDLER(Access::ANY, METHOD_FORWARD, (aParamMatcher), Type::handleSubModuleRequest);

			/*for (const auto& s : aChildSubscription) {
				SubscribableApiModule::createSubscription(s);
			}*/
		}

		~ParentApiModule() {
			// Child modules must always be destoyed first because they depend on the parent for subscription checking 
			// (which can happen via listeners)

			// There can't be references to shared child pointers from other threads because no other requests 
			// can be active at this point (otherwise we wouldn't be destoying the session)

			// Run the destructors outside of WLock
			SubModuleMap subModulesCopy;

			{
				WLock l(cs);
				dcassert(ranges::find_if(subModules | views::values, [](const auto& subModule) {
					return subModule.use_count() != 1;
				}).base() == subModules.end());

				subModules.swap(subModulesCopy);
			}

			subModulesCopy.clear();
		}

		void createSubscriptions(const StringList& aSubscriptions, const StringList& aChildSubscriptions) noexcept {
			BaseType::createSubscriptions(aSubscriptions);
			BaseType::createSubscriptions(aChildSubscriptions);
		}

		void createSubscriptions(const StringList&) noexcept override {
			dcassert(0);
		}

		// Forward request to a submodule
		api_return handleSubModuleRequest(ApiRequest& aRequest) {
			auto sub = getSubModule(aRequest);

			// Remove module ID
			aRequest.popParam();

			return sub->handleRequest(aRequest);
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

		// Parse module ID from the request, throws if the module was not found
		typename ItemType::Ptr getSubModule(ApiRequest& aRequest) {
			const auto& id = aRequest.getStringParam(paramId);

			auto sub = findSubModule(idConvertF(id));
			if (!sub) {
				throw RequestException(http_status::not_found, "Entity " + id + " was not found");
			}

			return sub;
		}
	protected:
		mutable SharedMutex cs;

		void forEachSubModule(std::function<void(const ItemType&)> aAction) {
			RLock l(cs);
			for (const auto& m : subModules | views::values) {
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
			// Avoid destructing the module inside WLock
			auto subModule = findSubModule(aId);

			{
				WLock l(cs);
				subModules.erase(aId);
			}
		}

		api_return handleGetSubmodules(ApiRequest& aRequest) {
			auto retJson = json::array();
			forEachSubModule([this, &retJson](const ItemType& aInfo) {
				retJson.push_back(childSerializeF(aInfo));
			});

			aRequest.setResponseBody(retJson);
			return http_status::ok;
		}

		api_return handleGetSubmodule(ApiRequest& aRequest) {
			auto info = getSubModule(aRequest);

			aRequest.setResponseBody(childSerializeF(*info.get()));
			return http_status::ok;
		}

		virtual api_return handleDeleteSubmodule(ApiRequest& aRequest) = 0;
	private:
		using SubModuleMap = map<IdType, typename ItemType::Ptr>;
		SubModuleMap subModules;

		const IdConvertF idConvertF;
		const ChildSerializeF childSerializeF;
		const string paramId;
	};

	template<class IdType, class ItemType, class IdJsonType, class ParentBaseType = SubscribableApiModule>
	class SubApiModule : public SubscribableApiModule {
	public:
		using ParentType = ParentApiModule<IdType, ItemType, ParentBaseType>;

		// aId = ID of the entity owning this module
		// Will inherit access from the parent module
		SubApiModule(ParentType* aParentModule, const IdJsonType& aJsonId) :
			SubscribableApiModule(aParentModule->getSession(), aParentModule->getSubscriptionAccess()), parentModule(aParentModule), jsonId(aJsonId) { }

		bool send(const string& aSubscription, const json& aJson) override {
			return SubscribableApiModule::send({
				{ "event", aSubscription },
				{ "data", aJson },
				{ "id", jsonId }
			});
		}

		bool maybeSend(const string& aSubscription, const SubscribableApiModule::JsonCallback& aCallback) override {
			if (!subscriptionActive(aSubscription)) {
				return false;
			}

			return send(aSubscription, aCallback());
		}

		bool subscriptionActive(const string& aSubscription) const noexcept override {
			// Enabled across all entities?
			if (parentModule->subscriptionActive(aSubscription)) {
				return true;
			}

			// Enabled for this entity only?
			return SubscribableApiModule::subscriptionActive(aSubscription);
		}


		// Init submodules in a separate call after the module has been constructed and added to the parent
		// Otherwise async calls made in module constructor would fail because they require
		// module exist in the parent
		virtual void init() noexcept = 0;

		virtual IdType getId() const noexcept = 0;

		//void createSubscriptions(const StringList&) noexcept override {
		//	dcassert(0);
		//}

		void addAsyncTask(Callback&& aTask) override {
			SubscribableApiModule::addAsyncTask(getAsyncWrapper(std::move(aTask)));
		}

		TimerPtr getTimer(Callback&& aTask, time_t aIntervalMillis) override {
			return session->getServer()->addTimer(std::move(aTask), aIntervalMillis, 
				std::bind(&SubApiModule::moduleAsyncRunWrapper<ParentType>, std::placeholders::_1, parentModule, getId(), session->getId())
			);
		}

		// All custom async tasks should be run inside this to
		// ensure that the submodule (or the session) won't get deleted
		Callback getAsyncWrapper(Callback&& aTask) noexcept override {
			return [
				this,
				sessionId = session->getId(),
				moduleId = getId(),
				task = std::move(aTask)
			] {
				return moduleAsyncRunWrapper(task, parentModule, moduleId, sessionId);
			};
		}
	private:
		template<class ParentType>
		static void moduleAsyncRunWrapper(const Callback& aTask, ParentType* aParentModule, const IdType& aId, LocalSessionId aSessionId) {
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

		const IdJsonType jsonId;
	};
}

#endif