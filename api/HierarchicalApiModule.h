/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#include <api/ApiModule.h>

#include <web-server/stdinc.h>
#include <web-server/ApiRequest.h>
#include <web-server/Session.h>
#include <web-server/WebServerManager.h>

#include <airdcpp/CriticalSection.h>

namespace webserver {
	class WebSocket;

	template<class IdType, class ItemType>
	class ParentApiModule : public ApiModule {
	public:
		typedef ParentApiModule<IdType, ItemType> Type;
		typedef std::function<IdType(const string&)> ConvertF;

		ParentApiModule(const string& aSubmoduleSection, const StringMatch& aIdMatcher, Access aAccess, Session* aSession, const StringList& aSubscriptions, const StringList& aChildSubscription, ConvertF aConvertF) :
			ApiModule(aSession, aAccess, &aSubscriptions), convertF(aConvertF) {

			requestHandlers[aSubmoduleSection].push_back(ApiModule::RequestHandler(aIdMatcher, std::bind(&Type::handleSubModuleRequest, this, placeholders::_1)));

			for (const auto& s: aChildSubscription) {
				childSubscriptions.emplace(s, false);
			}
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

		api_return handleSubscribe(ApiRequest& aRequest) {
			if (!socket) {
				aRequest.setResponseErrorStr("Socket required");
				return websocketpp::http::status_code::precondition_required;
			}

			const auto& subscription = aRequest.getStringParam(0);
			if (setChildSubscriptionState(subscription, true)) {
				return websocketpp::http::status_code::ok;
			}

			return ApiModule::handleSubscribe(aRequest);
		}

		api_return handleUnsubscribe(ApiRequest& aRequest) {
			const auto& subscription = aRequest.getStringParam(0);
			if (setChildSubscriptionState(subscription, false)) {
				return websocketpp::http::status_code::ok;
			}

			return ApiModule::handleUnsubscribe(aRequest);
		}

		// Forward request to a submodule
		api_return handleSubModuleRequest(ApiRequest& aRequest) {
			auto sub = getSubModule(aRequest.getStringParam(0));
			if (!sub) {
				aRequest.setResponseErrorStr("Submodule was not found");
				return websocketpp::http::status_code::not_found;
			}

			aRequest.popParam();
			return sub->handleRequest(aRequest);
		}

		bool subscriptionExists(const string& aSubscription) const noexcept {
			if (childSubscriptions.find(aSubscription) != childSubscriptions.end()) {
				return true;
			}

			return ApiModule::subscriptionExists(aSubscription);
		}

		// Change subscription state for all submodules
		bool setChildSubscriptionState(const string& aSubscription, bool aActive) noexcept {
			if (childSubscriptions.find(aSubscription) != childSubscriptions.end()) {
				RLock l(cs);
				for (const auto& m : subModules | map_values) {
					m->setSubscriptionState(aSubscription, aActive);
				}

				childSubscriptions[aSubscription] = aActive;
				return true;
			}

			return false;
		}

		void createChildSubscription(const string& aSubscription) noexcept {
			childSubscriptions[aSubscription];
		}

		bool childSubscriptionActive(const string& aSubscription) const noexcept {
			auto i = childSubscriptions.find(aSubscription);
			dcassert(i != childSubscriptions.end());
			return i->second;
		}

		// Submodules should NEVER be accessed outside of web server threads (e.g. API requests)
		typename ItemType::Ptr getSubModule(IdType aId) {
			RLock l(cs);
			auto m = subModules.find(aId);
			if (m != subModules.end()) {
				return m->second;
			}

			return nullptr;
		}

		// Submodules should NEVER be accessed outside of web server threads (e.g. API requests)
		typename ItemType::Ptr getSubModule(const string& aId) {
			return getSubModule(convertF(aId));
		}
	protected:
		mutable SharedMutex cs;

		void forEachSubModule(std::function<void(const ItemType&)> aAction) {
			RLock l(cs);
			for (const auto& m : subModules | map_values) {
				aAction(*m.get());
			}
		}

		void addSubModule(IdType aId, typename ItemType::Ptr&& aModule) {
			WLock l(cs);
			subModules.emplace(aId, aModule);
		}

		void removeSubModule(IdType aId) {
			WLock l(cs);
			subModules.erase(aId);
		}
	private:
		map<IdType, typename ItemType::Ptr> subModules;

		ApiModule::SubscriptionMap childSubscriptions;
		const ConvertF convertF;
	};

	template<class ParentIdType, class ItemType, class ItemJsonType>
	class SubApiModule : public ApiModule {
	public:
		typedef ParentApiModule<ParentIdType, ItemType> ParentType;

		// aId = ID of the entity owning this module
		// Will inherit access from the parent module
		SubApiModule(ParentType* aParentModule, const ItemJsonType& aId, const StringList& aSubscriptions) :
			ApiModule(aParentModule->getSession(), aParentModule->getSubscriptionAccess(), &aSubscriptions), parentModule(aParentModule), id(aId) { }

		bool send(const string& aSubscription, const json& aJson) {
			json j;
			j["event"] = aSubscription;
			j["data"] = aJson;
			j["id"] = id;
			return ApiModule::send(j);
		}

		bool maybeSend(const string& aSubscription, ApiModule::JsonCallback aCallback) {
			if (!subscriptionActive(aSubscription)) {
				return false;
			}

			return send(aSubscription, aCallback());
		}

		bool subscriptionActive(const string& aSubscription) const noexcept {
			if (parentModule->childSubscriptionActive(aSubscription)) {
				return true;
			}

			return ApiModule::subscriptionActive(aSubscription);
		}

		void createSubscription(const string& aSubscription) noexcept {
			ApiModule::createSubscription(aSubscription);
			parentModule->createChildSubscription(aSubscription);
		}

		void addAsyncTask(CallBack&& aTask) {
			ApiModule::addAsyncTask(getAsyncWrapper(move(aTask)));
		}

		TimerPtr getTimer(CallBack&& aTask, time_t aIntervalMillis) {
			return session->getServer()->addTimer(move(aTask), aIntervalMillis, 
				std::bind(&SubApiModule::moduleAsyncRunWrapper<ItemJsonType, ParentType>, std::placeholders::_1, parentModule, id, session->getId())
			);
		}

		// All custom async tasks should be run inside this to
		// ensure that the submodule (or the session) won't get deleted
		CallBack getAsyncWrapper(CallBack&& aTask) noexcept {
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
			ApiModule::asyncRunWrapper([=] {
				// Ensure that we have a submodule (the parent must exist if we have a session)
				auto m = aParentModule->getSubModule(aId);
				if (!m) {
					dcdebug("Trying to run an async task for a removed submodule %s\n", aId);
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