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

#include <airdcpp/core/ActionHook.h>

#include <api/base/SubscribableApiModule.h>
#include <api/base/HookActionHandler.h>
#include <api/common/Deserializer.h>

namespace webserver {

#define MODULE_HOOK_HANDLER(func, name, hook, callback) \
	func(name, [this](ActionHookSubscriber&& aSubscriber) { \
		return hook.addSubscriber(std::move(aSubscriber), HOOK_CALLBACK(callback)); \
	}, [](const string& aId) { \
		hook.removeSubscriber(aId); \
	}, [] { \
		return hook.getSubscribers(); \
	});


#define HOOK_HANDLER(name, hook, callback) MODULE_HOOK_HANDLER(HookApiModule::createHook, name, hook, callback)

	class HookApiModule : public SubscribableApiModule {
	public:
		using HookAddF = std::function<bool (ActionHookSubscriber &&)>;
		using HookRemoveF = std::function<void (const string &)>;
		using HookListF = std::function<ActionHookSubscriberList ()>;

		class APIHook {
		public:
			APIHook(const string& aHookId, HookAddF&& aAddHandlerF, HookRemoveF&& aRemoveF, HookListF&& aListF) :
				addHandlerF(std::move(aAddHandlerF)), removeHandlerF(std::move(aRemoveF)), listHandlerF(std::move(aListF)), hookId(aHookId) {}

			bool enable(ActionHookSubscriber&& aHookSubscriber) noexcept;
			void disable(const Session* aSession) noexcept;

			ActionHookSubscriberList getSubscribers() const noexcept {
				return listHandlerF();
			}

			GETPROP(string, hookId, HookId);
			GETPROP(string, hookSubscriberId, HookSubscriberId);
		private:
			const HookAddF addHandlerF;
			const HookRemoveF removeHandlerF;
			const HookListF listHandlerF;
		};

		HookApiModule(Session* aSession, Access aSubscriptionAccess, Access aHookAccess);

		virtual void createHook(const string& aSubscription, HookAddF&& aAddHandler, HookRemoveF&& aRemoveF, HookListF&& aListF) noexcept;

		virtual HookCompletionDataPtr maybeFireHook(const string& aSubscription, int aTimeoutSeconds, const JsonCallback& aJsonCallback);
		virtual HookCompletionDataPtr fireHook(const string& aSubscription, int aTimeoutSeconds, const json& aJson);
	protected:
		void addHook(const string& aSubscription, APIHook&& aHook) noexcept;

		HookActionHandler actionHandler;

		APIHook& getAPIHook(ApiRequest& aRequest);

		void on(SessionListener::SocketDisconnected) noexcept override;

		virtual api_return handleSubscribeHook(ApiRequest& aRequest);
		virtual api_return handleUnsubscribeHook(ApiRequest& aRequest);
		virtual api_return handleListHooks(ApiRequest& aRequest);

		api_return handleResolveHookAction(ApiRequest& aRequest);
		api_return handleRejectHookAction(ApiRequest& aRequest);

		static ActionHookSubscriber deserializeActionHookSubscriber(CallerPtr aOwner, Session* aSession, const json& aJson);
	private:
		map<string, APIHook> hooks;
	};
}

#endif