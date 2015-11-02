/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#include <airdcpp/CriticalSection.h>

namespace webserver {
	class WebSocket;

	template<class IdType, class ItemType>
	class ParentApiModule : public ApiModule {
	public:
		typedef ParentApiModule<IdType, ItemType> Type;
		typedef std::function<IdType(const string&)> ConvertF;

		ParentApiModule(const string& aSubmoduleSection, const StringMatch& aIdMatcher, Session* aSession, const StringList& aSubscriptions, const StringList& aChildSubscription, ConvertF aConvertF) : 
			ApiModule(aSession, &aSubscriptions), convertF(aConvertF) {

			requestHandlers[aSubmoduleSection].push_back(ApiModule::RequestHandler(aIdMatcher, std::bind(&Type::handleSubModuleRequest, this, placeholders::_1)));

			for (const auto& s: aChildSubscription) {
				childSubscriptions.emplace(s, false);
			}
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

		api_return handleSubModuleRequest(ApiRequest& aRequest) {
			auto chat = getSubModule(aRequest.getStringParam(0));
			if (!chat) {
				aRequest.setResponseErrorStr("Submodule was not found");
				return websocketpp::http::status_code::not_found;
			}

			aRequest.popParam();
			return chat->handleRequest(aRequest);
		}

		bool subscriptionExists(const string& aSubscription) const noexcept {
			if (childSubscriptions.find(aSubscription) != childSubscriptions.end()) {
				return true;
			}

			return ApiModule::subscriptionExists(aSubscription);
		}

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
	protected:
		mutable SharedMutex cs;

		map<IdType, typename ItemType::Ptr> subModules;

		typename ItemType::Ptr getSubModule(const string& aId) {
			RLock l(cs);
			auto m = subModules.find(convertF(aId));
			if (m != subModules.end()) {
				return m->second;
			}

			return nullptr;
		}
	private:
		ApiModule::SubscriptionMap childSubscriptions;
		ConvertF convertF;
	};

	template<class ParentIdType, class ItemType, class ItemJsonType>
	class SubApiModule : public ApiModule {
	public:
		typedef ParentApiModule<ParentIdType, ItemType> ParentType;

		// aId = ID of the entity owning this module
		SubApiModule(ParentType* aParentModule, const ItemJsonType& aId, const StringList& aSubscriptions) :
			ApiModule(aParentModule->getSession(), &aSubscriptions), parentModule(aParentModule), id(aId) { }

		bool send(const string& aSubscription, const json& aJson) {
			json j;
			j["event"] = aSubscription;
			j["data"] = aJson;
			j["id"] = id;
			return ApiModule::send(j);
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
	protected:
		ParentType* parentModule;

		ItemJsonType id;
	};
}

#endif