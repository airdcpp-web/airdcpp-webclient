/*
* Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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
#include "Exception.h"
#include "GetSet.h"
#include "debug.h"

#include <vector>


namespace dcpp {
	struct ActionHookRejection {
		ActionHookRejection(const string& aHookId, const string& aHookName, const string& aRejectId, const string& aMessage, bool aIsDataError = false) :
			hookId(aHookId), hookName(aHookName), rejectId(aRejectId), message(aMessage), isDataError(aIsDataError) {}

		const string hookId;
		const string hookName;
		const string rejectId;
		const string message;
		const bool isDataError;

		static string formatError(const ActionHookRejectionPtr& aRejection) noexcept {
			if (!aRejection) return "";
			return aRejection->hookName + ": " + aRejection->message;
		}

		static bool matches(const ActionHookRejectionPtr& aRejection, const string& aHookId, const string& aRejectId) noexcept {
			if (!aRejection) return false;

			return aRejection->hookId == aHookId && aRejection->rejectId == aRejectId;
		}
	};

	class HookRejectException : public Exception {
	public:
		HookRejectException(const ActionHookRejectionPtr& aRejection) : Exception(ActionHookRejection::formatError(aRejection)), rejection(aRejection) {

		}

		const ActionHookRejectionPtr& getRejection() const noexcept {
			return rejection;
		}
	private:
		ActionHookRejectionPtr rejection;
	};


	template<typename DataT>
	struct ActionHookData {
		ActionHookData(const string& aHookId, const string& aHookName, const DataT& aData) :
			hookId(aHookId), hookName(aHookName), data(aData) {}

		const string hookId;
		const string hookName;

		DataT data;
	};

	template<typename DataT>
	struct ActionHookResult {
		ActionHookRejectionPtr error = nullptr;
		ActionHookDataPtr<DataT> data = nullptr;
	};

	template<typename DataT>
	class ActionHookSubscriber {
	public:
		ActionHookSubscriber(const string& aId, const string& aName) noexcept : id(aId), name(aName) {  }

		GETSET(string, id, Id);
		GETSET(string, name, Name);

		ActionHookResult<DataT> getRejection(const string& aRejectId, const string& aMessage) const noexcept {
			auto error = make_shared<ActionHookRejection>(id, name, aRejectId, aMessage);
			return { error, nullptr };
		}

		ActionHookResult<DataT> getDataRejection(const std::exception& e) const noexcept {
			auto error = make_shared<ActionHookRejection>(id, name, "invalid_hook_data", e.what(), true);
			return { error, nullptr };
		}

		ActionHookResult<DataT> getData(const DataT& aData) const noexcept {
			auto data = make_shared<ActionHookData<DataT>>(id, name, aData);
			return { nullptr, data };
		}
	};


	template<typename DataT, typename... ArgT>
	class ActionHook {
	public:
#define HOOK_HANDLER(func) &func, *this
		class ActionHookHandler: public ActionHookSubscriber<DataT> {
		public:
			typedef std::function<ActionHookResult<DataT>(ArgT &... aItem, const ActionHookResultGetter<DataT> & aResultGetter)> HookCallback;

			ActionHookHandler(const string& aId, const string& aName, const HookCallback& aCallback) noexcept: ActionHookSubscriber<DataT>(aId, aName), callback(aCallback) {  }
		protected:
			friend class ActionHook;

			HookCallback callback;
		};

		using CallbackFunc = std::function<ActionHookResult<DataT>(ArgT &... aArgs, const ActionHookResultGetter<DataT> & aResultGetter)>;
		bool addSubscriber(const string& aId, const string& aName, CallbackFunc aCallback) noexcept {
			Lock l(cs);
			if (findById(aId) != subscribers.end()) {
				return false;
			}

			subscribers.push_back(ActionHookHandler(aId, aName, aCallback));
			return true;
		}

		template<typename CallbackT, typename ObjectT>
		bool addSubscriber(const string& aId, const string& aName, CallbackT aCallback, ObjectT& aObject) noexcept {
			return addSubscriber(
				aId,
				aName,
				[&, aCallback](ArgT&... aArgs, const ActionHookResultGetter<DataT>& aResultGetter) {
					return (aObject.*aCallback)(aArgs..., aResultGetter);
				}
			);
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

		// Run all validation hooks, returns a rejection object in case of errors
		ActionHookRejectionPtr runHooksError(ArgT&... aItem) const noexcept {
			SubscriberList subscribersCopy;

			{
				Lock l(cs);
				subscribersCopy = subscribers;
			}

			for (const auto& handler : subscribersCopy) {
				auto res = handler.callback(
					aItem..., 
					handler
				);

				if (res.error) {
					dcdebug("Hook rejected by handler %s: %s (%s)\n", res.error->hookId.c_str(), res.error->rejectId.c_str(), res.error->message.c_str());
					return res.error;
				}
			}

			return nullptr;
		}

		// Get data from all hooks, throw in case of rejections
		ActionHookDataList<DataT> runHooksDataThrow(ArgT&... aItem) const {
			return runHooksDataImpl(
				[](const ActionHookRejectionPtr& aRejection) {
					if (aRejection->isDataError) {
						// Ignore data deserialization failures...
						return;
					}

					throw HookRejectException(aRejection);
				},
				std::forward<ArgT>(aItem)...
			);
		}

		// Get data from all hooks, ignore errors
		ActionHookDataList<DataT> runHooksData(ArgT&... aItem) const {
			return runHooksDataImpl(nullptr, std::forward<ArgT>(aItem)...);
		}

		// Run all validation hooks, returns false in case of rejections
		bool runHooksBasic(ArgT&... aItem) const noexcept {
			auto rejection = runHooksError(aItem...);
			return rejection ? false : true;
		}

		bool hasSubscribers() const noexcept {
			Lock l(cs);
			return !subscribers.empty();
		}

		static DataT normalizeListItems(const ActionHookDataList<DataT>& aResult) noexcept {
			DataT ret;
			for (const auto& i: aResult) {
				for (const auto& s : i->data) {
					ret.push_back(s);
				}
			}

			return ret;
		}
	private:
		typedef std::vector<ActionHookHandler> SubscriberList;

		typename SubscriberList::iterator findById(const string& aId) noexcept {
			return find_if(subscribers.begin(), subscribers.end(), [&aId](const ActionHookHandler& aSubscriber) {
				return aSubscriber.getId() == aId;
			});
		}

		SubscriberList subscribers;
		mutable CriticalSection cs;

		ActionHookDataList<DataT> runHooksDataImpl(const std::function<void(const ActionHookRejectionPtr&)> aRejectHandler, ArgT&... aItem) const {
			SubscriberList subscribersCopy;

			{
				Lock l(cs);
				subscribersCopy = subscribers;
			}

			ActionHookDataList<DataT> ret;
			for (const auto& handler : subscribersCopy) {
				auto handlerRes = handler.callback(
					aItem...,
					handler
				);

				if (handlerRes.error) {
					dcdebug("Hook rejected by handler %s: %s (%s)\n", handlerRes.error->hookId.c_str(), handlerRes.error->rejectId.c_str(), handlerRes.error->message.c_str());
					if (aRejectHandler) {
						aRejectHandler(handlerRes.error);
					}
				}

				if (handlerRes.data) {
					ret.push_back(handlerRes.data);
				}
			}

			return ret;
		}
	};
}

#endif