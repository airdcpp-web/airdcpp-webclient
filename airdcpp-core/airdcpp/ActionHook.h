/*
* Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

	// General subscriber config
	class ActionHookSubscriber {
	public:
		ActionHookSubscriber(const string& aId, const string& aName, const void* aIgnoredOwner) noexcept : id(aId), name(aName), ignoredOwner(aIgnoredOwner) {  }

		const string& getId() const noexcept {
			return id;
		}

		const string& getName() const noexcept {
			return name;
		}

		const void* getIgnoredOwner() const noexcept {
			return ignoredOwner;
		}
	private:
		string id;
		string name;
		const void* ignoredOwner;
	};

	typedef std::vector<ActionHookSubscriber> ActionHookSubscriberList;

	// Helper class to be passed to hook handlers for creating result entities
	template<typename DataT>
	class ActionHookDataGetter {
	public:
		ActionHookDataGetter(ActionHookSubscriber&& aSubscriber) noexcept : subscriber(std::move(aSubscriber)) {  }

		ActionHookResult<DataT> getRejection(const string& aRejectId, const string& aMessage) const noexcept {
			auto error = make_shared<ActionHookRejection>(subscriber.getId(), subscriber.getName(), aRejectId, aMessage);
			return { error, nullptr };
		}

		ActionHookResult<DataT> getDataRejection(const std::exception& e) const noexcept {
			auto error = make_shared<ActionHookRejection>(subscriber.getId(), subscriber.getName(), "invalid_hook_data", e.what(), true);
			return { error, nullptr };
		}

		ActionHookResult<DataT> getData(const DataT& aData) const noexcept {
			auto data = make_shared<ActionHookData<DataT>>(subscriber.getId(), subscriber.getName(), aData);
			return { nullptr, data };
		}

		const ActionHookSubscriber& getSubscriber() const noexcept {
			return subscriber;
		}
	protected:
		ActionHookSubscriber subscriber;
	};

	template<typename DataT, typename... ArgT>
	class ActionHook {
	public:
#define HOOK_HANDLER(func) &func, *this

		// Internal hook handler
		class ActionHookHandler {
		public:
			typedef std::function<ActionHookResult<DataT>(ArgT&... aItem, const ActionHookResultGetter<DataT>& aResultGetter)> HookCallback;

			ActionHookHandler(ActionHookSubscriber&& aSubscriber, const HookCallback& aCallback) noexcept: dataGetter(ActionHookDataGetter<DataT>(std::move(aSubscriber))), callback(aCallback) {  }

			const ActionHookSubscriber& getSubscriber() const noexcept {
				return dataGetter.getSubscriber();
			}
		protected:
			friend class ActionHook;

			ActionHookDataGetter<DataT> dataGetter;
			HookCallback callback;
		};

		typedef shared_ptr<ActionHookHandler> ActionHookHandlerPtr;

		using CallbackFunc = std::function<ActionHookResult<DataT>(ArgT&... aArgs, const ActionHookResultGetter<DataT>& aResultGetter)>;
		bool addSubscriber(ActionHookSubscriber&& aSubscriber, CallbackFunc aCallback) noexcept {
			Lock l(cs);
			if (findById(aSubscriber.getId()) != handlers.end()) {
				return false;
			}

			handlers.push_back(ActionHookHandler(std::move(aSubscriber), aCallback));
			return true;
		}

		template<typename CallbackT, typename ObjectT>
		bool addSubscriber(ActionHookSubscriber&& aSubscriber, CallbackT aCallback, ObjectT& aObject) noexcept {
			return addSubscriber(
				std::move(aSubscriber),
				[&, aCallback](ArgT&... aArgs, const ActionHookResultGetter<DataT>& aResultGetter) {
					return (aObject.*aCallback)(aArgs..., aResultGetter);
				}
			);
		}

		bool removeSubscriber(const string& aId) noexcept {
			Lock l(cs);
			auto i = findById(aId);
			if (i == handlers.end()) {
				return false;
			}

			handlers.erase(i);
			return true;
		}

		// Run all validation hooks, returns a rejection object in case of errors
		ActionHookRejectionPtr runHooksError(const void* aOwner, ArgT&... aItem) const noexcept {
			for (const auto& handler: getHookHandlers(aOwner)) {
				auto res = handler.callback(
					aItem..., 
					handler.dataGetter
				);

				if (res.error) {
					dcdebug("Hook rejected by handler %s: %s\n", res.error->hookId.c_str(), res.error->rejectId.c_str());
					return res.error;
				}
			}

			return nullptr;
		}

		// Get data from all hooks, throw in case of rejections
		ActionHookDataList<DataT> runHooksDataThrow(const void* aOwner, ArgT&... aItem) const {
			return runHooksDataImpl(
				aOwner,
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
		ActionHookDataList<DataT> runHooksData(const void* aOwner, ArgT&... aItem) const {
			return runHooksDataImpl(aOwner, nullptr, std::forward<ArgT>(aItem)...);
		}

		// Run all validation hooks, returns false in case of rejections
		bool runHooksBasic(const void* aOwner, ArgT&... aItem) const noexcept {
			auto rejection = runHooksError(aOwner, aItem...);
			return rejection ? false : true;
		}

		bool hasSubscribers() const noexcept {
			Lock l(cs);
			return !handlers.empty();
		}

		ActionHookSubscriberList getSubscribers() const noexcept {
			Lock l(cs);

			ActionHookSubscriberList ret;
			for (const auto& s: handlers) {
				ret.push_back(s.getSubscriber());
			}

			return ret;
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

		static DataT normalizeMap(const ActionHookDataList<DataT>& aResult) noexcept {
			DataT ret;
			for (const auto& i : aResult) {
				for (const auto& s : i->data) {
					ret.emplace(s.first, s.second);
				}
			}

			return ret;
		}

		static vector<DataT> normalizeData(const ActionHookDataList<DataT>& aResult) noexcept {
			vector<DataT> ret;
			for (const auto& i : aResult) {
				ret.push_back(i->data);
			}

			return ret;
		}
	private:
		typedef std::vector<ActionHookHandler> ActionHookHandlerList;

		typename ActionHookHandlerList::iterator findById(const string& aId) noexcept {
			return find_if(handlers.begin(), handlers.end(), [&aId](const ActionHookHandler& aHandler) {
				return aHandler.getSubscriber().getId() == aId;
			});
		}

		ActionHookHandlerList handlers;
		mutable CriticalSection cs;

		ActionHookDataList<DataT> runHooksDataImpl(const void* aOwner, const std::function<void(const ActionHookRejectionPtr&)> aRejectHandler, ArgT&... aItem) const {
			ActionHookDataList<DataT> ret;
			for (const auto& handler : getHookHandlers(aOwner)) {
				auto handlerRes = handler.callback(
					aItem...,
					handler.dataGetter
				);

				if (handlerRes.error) {
					dcdebug("Hook rejected by handler %s: %s\n", handlerRes.error->hookId.c_str(), handlerRes.error->rejectId.c_str());

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

		ActionHookHandlerList getHookHandlers(const void* aOwner) const noexcept {
			ActionHookHandlerList ret;

			{
				Lock l(cs);
				for (const auto& s: handlers) {
					if (!s.dataGetter.getSubscriber().getIgnoredOwner() || s.dataGetter.getSubscriber().getIgnoredOwner() != aOwner) {
						ret.push_back(s);
					}
				}
			}

			return ret;
		}
	};
}

#endif