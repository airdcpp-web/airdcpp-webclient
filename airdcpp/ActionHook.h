/*
* Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_ACTION_HOOK_H
#define DCPLUSPLUS_DCPP_ACTION_HOOK_H

#include "stdinc.h"
#include "forward.h"
#include "typedefs.h"

#include "CriticalSection.h"

#include <vector>


namespace dcpp {
	struct ActionHookError {
		ActionHookError(const string& aHookId, const string& aHookName, const string& aErrorId, const string& aErrorMessage) :
			hookId(aHookId), hookName(aHookName), errorId(aErrorId), errorMessage(aErrorMessage) {}

		const string hookId;
		const string hookName;
		const string errorId;
		const string errorMessage;

		static bool matches(const ActionHookErrorPtr& aError, const string& aHookId, const string& aErrorId) noexcept {
			if (!aError) return false;

			return aError->hookId == aHookId && aError->errorId == aErrorId;
		}
	};

	template<typename ItemT>
	class ActionHook {
	public:
#define HOOK_HANDLER(func) std::bind(&func, this, placeholders::_1, placeholders::_2)

		typedef std::function<ActionHookErrorPtr(ItemT& aItem, const HookErrorGetter& aErrorGetter)> HookCallback;
		struct Subscriber {
			string id;
			string name;

			HookCallback callback;

			ActionHookErrorPtr getError(const string& aErrorId, const string& aErrorMessage) noexcept {
				return make_shared<ActionHookError>(id, name, aErrorId, aErrorMessage);
			}
		};

		void addSubscriber(const string& aId, const string& aName, HookCallback aCallback) noexcept {
			Lock l(cs);
			removeSubscriber(aId);
			subscribers.push_back({ aId, aName, std::move(aCallback) });
		}

		bool removeSubscriber(const string& aId) noexcept {
			Lock l(cs);
			auto i = findById(aId);
			if (i == subscribers.end()) {
				return false;
			}

			subscribers.erase(i);
			return true;
		}

		ActionHookErrorPtr runHooks(ItemT& aItem) const noexcept {
			SubscriberList subscribersCopy;

			{
				Lock l(cs);
				subscribersCopy = subscribers;
			}

			for (const auto& handler : subscribersCopy) {
				auto error = handler.callback(aItem, std::bind(&Subscriber::getError, handler, std::placeholders::_1, std::placeholders::_2));
				if (error) {
					return error;
				}
			}

			return nullptr;
		}

		bool hasSubscribers() const noexcept {
			Lock l(cs);
			return !subscribers.empty();
		}
	private:
		typedef std::vector<Subscriber> SubscriberList;

		typename SubscriberList::iterator findById(const string& aId) noexcept {
			return find_if(subscribers.begin(), subscribers.end(), [&aId](const Subscriber& aSubscriber) {
				return aSubscriber.id == aId;
			});
		}

		SubscriberList subscribers;
		mutable CriticalSection cs;
	};
}

#endif