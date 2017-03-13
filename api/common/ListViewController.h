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

#ifndef DCPLUSPLUS_DCPP_LISTVIEW_H
#define DCPLUSPLUS_DCPP_LISTVIEW_H

#include <web-server/stdinc.h>

#include <web-server/JsonUtil.h>
#include <web-server/SessionListener.h>
#include <web-server/Timer.h>
#include <web-server/WebServerManager.h>

#include <airdcpp/TimerManager.h>

#include <api/base/ApiModule.h>
#include <api/common/PropertyFilter.h>
#include <api/common/Serializer.h>
#include <api/common/ViewTasks.h>

namespace webserver {

	template<class T, int PropertyCount>
	class ListViewController : private SessionListener {
	public:
		typedef typename PropertyItemHandler<T>::ItemList ItemList;
		typedef typename PropertyItemHandler<T>::ItemListFunction ItemListF;
		typedef std::function<void(bool aActive)> StateChangeFunction;

		// Use the short default update interval for lists that can be edited by the users
		// Larger lists with lots of updates and non-critical response times should specify a longer interval
		ListViewController(const string& aViewName, SubscribableApiModule* aModule, const PropertyItemHandler<T>& aItemHandler, ItemListF aItemListF, time_t aUpdateInterval = 200) :
			module(aModule), viewName(aViewName), itemHandler(aItemHandler), itemListF(aItemListF),
			timer(aModule->getTimer([this] { runTasks(); }, aUpdateInterval))
		{
			aModule->getSession()->addListener(this);

			auto access = aModule->getSubscriptionAccess();
			MODULE_METHOD_HANDLER(aModule, access, METHOD_POST, (EXACT_PARAM(viewName), EXACT_PARAM("filter")), ListViewController::handlePostFilter);
			MODULE_METHOD_HANDLER(aModule, access, METHOD_PUT, (EXACT_PARAM(viewName), EXACT_PARAM("filter"), TOKEN_PARAM), ListViewController::handlePutFilter);
			MODULE_METHOD_HANDLER(aModule, access, METHOD_DELETE, (EXACT_PARAM(viewName), EXACT_PARAM("filter"), TOKEN_PARAM), ListViewController::handleDeleteFilter);

			MODULE_METHOD_HANDLER(aModule, access, METHOD_POST, (EXACT_PARAM(viewName), EXACT_PARAM("settings")), ListViewController::handlePostSettings);
			MODULE_METHOD_HANDLER(aModule, access, METHOD_DELETE, (EXACT_PARAM(viewName)), ListViewController::handleReset);

			MODULE_METHOD_HANDLER(aModule, access, METHOD_GET, (EXACT_PARAM(viewName), EXACT_PARAM("items"), RANGE_START_PARAM, RANGE_MAX_PARAM), ListViewController::handleGetItems);
		}

		~ListViewController() {
			module->getSession()->removeListener(this);

			timer->stop(true);
		}

		void stop() noexcept {
			setActive(false);
			timer->stop(false);

			clear();
			currentValues.reset();
		}

		void resetItems() {
			clear();

			currentValues.set(IntCollector::TYPE_RANGE_START, 0);

			initItems();
		}

		void onItemAdded(const T& aItem) {
			if (!active) return;

			tasks.addItem(aItem);
		}

		void onItemRemoved(const T& aItem) {
			if (!active) return;

			tasks.removeItem(aItem);
		}

		void onItemUpdated(const T& aItem, const PropertyIdSet& aUpdatedProperties) {
			if (!active) return;

			tasks.updateItem(aItem, aUpdatedProperties);
		}

		void onItemsUpdated(const ItemList& aItems, const PropertyIdSet& aUpdatedProperties) {
			if (!active) return;

			for (const auto& item : aItems) {
				onItemUpdated(item, aUpdatedProperties);
			}
		}

		void clearFilters() {
			{
				WLock l(cs);
				filters.clear();
			}

			onFilterUpdated();
		}

		bool isActive() const noexcept {
			return active;
		}

		bool hasSourceItem(const T& aItem) const noexcept {
			RLock l(cs);
			return sourceItems.find(aItem) != sourceItems.end();
		}
	private:
		void setActive(bool aActive) {
			active = aActive;
		}

		// FILTERS START
		PropertyFilter::MatcherList getFilterMatcherList() {
			PropertyFilter::MatcherList ret;

			RLock l(cs);
			for (auto& filter : filters) {
				if (!filter->empty()) {
					ret.emplace_back(filter);
				}
			}

			return ret;
		}

		PropertyFilter::List::iterator findFilter(FilterToken aToken) {
			return find_if(filters.begin(), filters.end(), [&](const PropertyFilter::Ptr& aFilter) { return aFilter->getId() == aToken; });
		}

		bool removeFilter(FilterToken aToken) {
			{
				WLock l(cs);

				auto filter = findFilter(aToken);
				if (filter == filters.end()) {
					return false;
				}

				filters.erase(filter);
			}

			onFilterUpdated();
			return true;
		}

		PropertyFilter::Ptr addFilter() {
			auto filter = std::make_shared<PropertyFilter>(itemHandler.properties);

			{
				WLock l(cs);
				filters.push_back(filter);
			}

			return filter;
		}

		template<typename FilterT = PropertyFilter::Ptr, typename MatcherT>
		bool matchesFilter(const T& aItem, const MatcherT& aMatcher) {
			return PropertyFilter::Matcher<FilterT>::match(aMatcher,
				[&](size_t aProperty) { return itemHandler.numberF(aItem, aProperty); },
				[&](size_t aProperty) { return itemHandler.stringF(aItem, aProperty); },
				[&](size_t aProperty, const StringMatch& aStringMatcher, double aNumericMatcher) { return itemHandler.customFilterF(aItem, aProperty, aStringMatcher, aNumericMatcher); }
			);
		}

		void setFilterProperties(const json& aRequestJson, PropertyFilter& aFilter) {
			auto method = JsonUtil::getField<int>("method", aRequestJson);
			auto property = JsonUtil::getField<string>("property", aRequestJson);

			// Pattern can be string or numeric
			string pattern;
			auto patternJson = JsonUtil::getRawField("pattern", aRequestJson);
			if (patternJson.is_number()) {
				pattern = Util::toString(JsonUtil::parseValue<double>("pattern", patternJson));
			} else {
				pattern = JsonUtil::parseValue<string>("pattern", patternJson);
			}

			aFilter.prepare(pattern, method, findPropertyByName(property, itemHandler.properties));
			onFilterUpdated();
		}

		api_return handlePostFilter(ApiRequest& aRequest) {
			const auto& reqJson = aRequest.getRequestBody();

			auto filter = addFilter();
			if (!reqJson.is_null()) {
				setFilterProperties(reqJson, *filter.get());
			}

			aRequest.setResponseBody({ 
				{ "id", filter->getId() }
			});
			return websocketpp::http::status_code::ok;
		}

		api_return handlePutFilter(ApiRequest& aRequest) {
			const auto& reqJson = aRequest.getRequestBody();
			PropertyFilter::Ptr filter = nullptr;

			{
				WLock l(cs);
				auto i = findFilter(aRequest.getTokenParam());
				if (i == filters.end()) {
					aRequest.setResponseErrorStr("Filter not found");
					return websocketpp::http::status_code::bad_request;
				}

				filter = *i;
			}

			setFilterProperties(reqJson, *filter.get());
			return websocketpp::http::status_code::no_content;
		}

		api_return handleDeleteFilter(ApiRequest& aRequest) {
			if (!removeFilter(aRequest.getTokenParam())) {
				aRequest.setResponseErrorStr("Filter not found");
				return websocketpp::http::status_code::bad_request;
			}

			return websocketpp::http::status_code::no_content;
		}

		void onFilterUpdated() {
			ItemList itemsNew;
			auto matchers = getFilterMatcherList();
			{
				RLock l(cs);
				for (const auto& i : sourceItems) {
					if (matchesFilter(i, matchers)) {
						itemsNew.push_back(i);
					}
				}
			}

			{
				WLock l(cs);
				matchingItems.swap(itemsNew);
				itemListChanged = true;
				currentValues.set(IntCollector::TYPE_RANGE_START, 0);
			}
		}

		// FILTERS END


		api_return handlePostSettings(ApiRequest& aRequest) {
			parseProperties(aRequest.getRequestBody());
			if (!active) {
				setActive(true);
				initItems();
				timer->start(true);
			}

			return websocketpp::http::status_code::no_content;
		}

		api_return handleReset(ApiRequest& aRequest) {
			if (!active) {
				aRequest.setResponseErrorStr("The view isn't active");
				return websocketpp::http::status_code::bad_request;
			}

			stop();
			return websocketpp::http::status_code::no_content;
		}

		void parseProperties(const json& j) {
			typename IntCollector::ValueMap updatedValues;

			{
				auto start = JsonUtil::getOptionalField<int>("range_start", j);
				if (start) {
					if (*start < 0) {
						throw std::invalid_argument("Negative range start not allowed");
					}

					updatedValues[IntCollector::TYPE_RANGE_START] = *start;
				}
			}

			{
				auto end = JsonUtil::getOptionalField<int>("max_count", j);
				if (end) {
					updatedValues[IntCollector::TYPE_MAX_COUNT] = *end;
				}
			}

			{
				auto propName = JsonUtil::getOptionalField<string>("sort_property", j);
				if (propName) {
					auto propId = findPropertyByName(*propName, itemHandler.properties);
					if (propId == -1) {
						throw std::invalid_argument("Invalid sort property");
					}

					updatedValues[IntCollector::TYPE_SORT_PROPERTY] = propId;
				}
			}

			{
				auto sortAscending = JsonUtil::getOptionalField<bool>("sort_ascending", j);
				if (sortAscending) {
					updatedValues[IntCollector::TYPE_SORT_ASCENDING] = *sortAscending;
				}
			}

			{
				auto paused = JsonUtil::getOptionalField<bool>("paused", j);
				if (paused) {
					if (*paused && timer->isRunning()) {
						timer->stop(false);
					} else if (!(*paused) && !timer->isRunning()) {
						timer->start(true);
					}
				}
			}

			{
				auto iter = j.find("source_filter");
				if (iter != j.end()) {
					// Reset old filter regardless of the props
					sourceFilter.reset(new PropertyFilter(itemHandler.properties));

					auto filterProps = iter.value();
					if (!filterProps.is_null()) {
						setFilterProperties(filterProps, *sourceFilter.get());
					}
				}
			}

			if (!updatedValues.empty()) {
				WLock l(cs);
				currentValues.set(updatedValues);
			}
		}

		void on(SessionListener::SocketDisconnected) noexcept {
			stop();
		}

		void sendJson(const json& j) {
			if (j.is_null()) {
				return;
			}

			module->send(viewName + "_updated", j);
		}

		int initItems() {
			WLock l(cs);
			matchingItems = itemListF();

			if (sourceFilter) {
				auto matcher = PropertyFilter::Matcher<PropertyFilter*>(sourceFilter.get());
				matchingItems.erase(remove_if(matchingItems.begin(), matchingItems.end(), [&](const T& aItem) {
					return !matchesFilter<PropertyFilter*>(aItem, matcher);
				}), matchingItems.end());
			}

			sourceItems.insert(matchingItems.begin(), matchingItems.end());

			itemListChanged = true;
			return static_cast<int>(matchingItems.size());
		}

		void clear() {
			WLock l(cs);
			tasks.clear();
			currentViewportItems.clear();
			matchingItems.clear();
			sourceItems.clear();
			prevTotalItemCount = -1;
			prevMatchingItemCount = -1;
			filters.clear();
		}

		static bool itemSort(const T& t1, const T& t2, const PropertyItemHandler<T>& aItemHandler, int aSortProperty, int aSortAscending) {
			int res = 0;
			switch (aItemHandler.properties[aSortProperty].sortMethod) {
			case SORT_NUMERIC: {
				res = compare(aItemHandler.numberF(t1, aSortProperty), aItemHandler.numberF(t2, aSortProperty));
				break;
			}
			case SORT_TEXT: {
				res = Util::DefaultSort(aItemHandler.stringF(t1, aSortProperty).c_str(), aItemHandler.stringF(t2, aSortProperty).c_str());
				break;
			}
			case SORT_CUSTOM: {
				res = aItemHandler.customSorterF(t1, t2, aSortProperty);
				break;
			}
			case SORT_NONE: break;
			default: dcassert(0);
			}

			return aSortAscending == 1 ? res < 0 : res > 0;
		}

		api_return handleGetItems(ApiRequest& aRequest) {
			auto start = aRequest.getRangeParam(START_POS);
			auto end = aRequest.getRangeParam(MAX_COUNT);
			decltype(matchingItems) matchingItemsCopy;

			{
				RLock l(cs);
				matchingItemsCopy = matchingItems;
			}

			auto j = Serializer::serializeFromPosition(start, end - start, matchingItemsCopy, [&](const T& i) {
				return Serializer::serializeItem(i, itemHandler);
			});

			aRequest.setResponseBody(j);
			return websocketpp::http::status_code::ok;
		}

		typename ItemList::iterator findItem(const T& aItem, ItemList& aItems) noexcept {
			return find(aItems.begin(), aItems.end(), aItem);
		}

		typename ItemList::const_iterator findItem(const T& aItem, const ItemList& aItems) const noexcept {
			return find(aItems.begin(), aItems.end(), aItem);
		}

		bool isInList(const T& aItem, const ItemList& aItems) const noexcept {
			return findItem(aItem, aItems) != aItems.end();
		}

		int64_t getPosition(const T& aItem, const ItemList& aItems) const noexcept {
			auto i = findItem(aItem, aItems);
			if (i == aItems.end()) {
				return -1;
			}

			return distance(aItems.begin(), i);
		}

		// TASKS START
		void runTasks() {
			typename ItemTasks<T>::TaskMap currentTasks;
			PropertyIdSet updatedProperties;
			tasks.get(currentTasks, updatedProperties);

			// Anything to update?
			if (currentTasks.empty() && !currentValues.hasChanged() && !itemListChanged) {
				return;
			}

			// Get the updated values
			typename IntCollector::ValueMap updateValues;

			{
				WLock l(cs);
				updateValues = currentValues.getAll();
			}

			// Sorting
			auto sortAscending = updateValues[IntCollector::TYPE_SORT_ASCENDING];
			auto sortProperty = updateValues[IntCollector::TYPE_SORT_PROPERTY];
			if (sortProperty < 0) {
				return;
			}

			maybeSort(updatedProperties, sortProperty, sortAscending);

			// Start position
			auto newStart = updateValues[IntCollector::TYPE_RANGE_START];

			json j;

			// Go through the tasks
			auto updatedItems = handleTasks(currentTasks, sortProperty, sortAscending, newStart);

			ItemList nextViewportItems;
			if (newStart >= 0) {
				// Get the new visible items
				updateViewItems(updatedItems, j, newStart, updateValues[IntCollector::TYPE_MAX_COUNT], nextViewportItems);

				// Append other changed properties
				auto startOffset = newStart - updateValues[IntCollector::TYPE_RANGE_START];
				if (startOffset != 0) {
					j["range_offset"] = startOffset;
				}

				j["range_start"] = newStart;
			}

			{
				WLock l(cs);

				// All list operations should possibly be changed to be performed in this thread to avoid things getting out of sync
				if (!active) {
					return;
				}

				// Set cached values
				prevValues.swap(updateValues);
				currentViewportItems.swap(nextViewportItems);

				dcassert((matchingItems.size() != 0 && sourceItems.size() != 0) || currentViewportItems.empty());
			}

			// Counts should be updated even if the list doesn't have valid settings posted
			appendItemCounts(j);

			sendJson(j);
		}

		typedef std::map<T, const PropertyIdSet&> ItemPropertyIdMap;
		ItemPropertyIdMap handleTasks(const typename ItemTasks<T>::TaskMap& aTaskList, int aSortProperty, int aSortAscending, int& rangeStart_) {
			ItemPropertyIdMap updatedItems;
			for (auto& t : aTaskList) {
				switch (t.second.type) {
				case ADD_ITEM: {
					handleAddItem(t.first, aSortProperty, aSortAscending, rangeStart_);
					break;
				}
				case REMOVE_ITEM: {
					handleRemoveItem(t.first, rangeStart_);
					break;
				}
				case UPDATE_ITEM: {
					if (handleUpdateItem(t.first, aSortProperty, aSortAscending, rangeStart_)) {
						updatedItems.emplace(t.first, t.second.updatedProperties);
					}
					break;
				}
				}
			}

			return updatedItems;
		}

		void updateViewItems(const ItemPropertyIdMap& aUpdatedItems, json& json_, int& newStart_, int aMaxCount, ItemList& nextViewportItems_) {
			// Get the new visible items
			ItemList currentItemsCopy;
			{
				RLock l(cs);
				if (newStart_ >= static_cast<int>(sourceItems.size())) {
					newStart_ = 0;
				}

				auto count = min(static_cast<int>(matchingItems.size()) - newStart_, aMaxCount);
				if (count < 0) {
					return;
				}


				auto startIter = matchingItems.begin();
				advance(startIter, newStart_);

				auto endIter = startIter;
				advance(endIter, count);

				std::copy(startIter, endIter, back_inserter(nextViewportItems_));
				currentItemsCopy = currentViewportItems;
			}

			json_["items"] = json::array();

			// List items
			int pos = 0;
			for (const auto& item : nextViewportItems_) {
				if (!isInList(item, currentItemsCopy)) {
					appendItemFull(item, json_, pos);
				} else {
					// append position
					auto props = aUpdatedItems.find(item);
					if (props != aUpdatedItems.end()) {
						appendItemPartial(item, json_, pos, props->second);
					} else {
						appendItemPosition(item, json_, pos);
					}
				}

				pos++;
			}
		}

		void maybeSort(const PropertyIdSet& aUpdatedProperties, int aSortProperty, int aSortAscending) {
			bool needSort = aUpdatedProperties.find(aSortProperty) != aUpdatedProperties.end() ||
				prevValues[IntCollector::TYPE_SORT_ASCENDING] != aSortAscending ||
				prevValues[IntCollector::TYPE_SORT_PROPERTY] != aSortProperty ||
				itemListChanged;

			itemListChanged = false;

			if (needSort) {
				auto start = GET_TICK();

				WLock l(cs);
				std::stable_sort(matchingItems.begin(), matchingItems.end(),
					std::bind(&ListViewController::itemSort,
						std::placeholders::_1,
						std::placeholders::_2,
						itemHandler,
						aSortProperty,
						aSortAscending
						));

				dcdebug("Table %s sorted in " U64_FMT " ms\n", viewName.c_str(), GET_TICK() - start);
			}
		}

		void appendItemCounts(json& json_) {
			int matchingItemCount = 0, totalItemCount = 0;

			{
				RLock l(cs);
				matchingItemCount = matchingItems.size();
				totalItemCount = sourceItems.size();
			}

			if (matchingItemCount != prevMatchingItemCount) {
				prevMatchingItemCount = matchingItemCount;
				json_["matching_items"] = matchingItemCount;
			}

			if (totalItemCount != prevTotalItemCount) {
				prevTotalItemCount = totalItemCount;
				json_["total_items"] = totalItemCount;
			}
		}

		void handleAddItem(const T& aItem, int aSortProperty, int aSortAscending, int& rangeStart_) {
			if (sourceFilter) {
				RLock l(cs);
				PropertyFilter::Matcher<PropertyFilter*> matcher(sourceFilter.get());
				if (!matchesFilter<PropertyFilter*>(aItem, matcher)) {
					return;
				}
			}

			auto matches = matchesFilter(aItem, getFilterMatcherList());

			{
				WLock l(cs);
				sourceItems.emplace(aItem);

				if (matches) {
					auto iter = matchingItems.insert(std::upper_bound(
						matchingItems.begin(),
						matchingItems.end(),
						aItem,
						std::bind(&ListViewController::itemSort, std::placeholders::_1, std::placeholders::_2, itemHandler, aSortProperty, aSortAscending)
					), aItem);

					auto pos = static_cast<int>(std::distance(matchingItems.begin(), iter));
					if (pos < rangeStart_) {
						// Update the range range positions
						rangeStart_++;
					}
				}
			}
		}

		void handleRemoveItem(const T& aItem, int& rangeStart_) {
			WLock l(cs);
			auto iter = findItem(aItem, matchingItems);
			if (iter == matchingItems.end()) {
				//dcassert(0);
				return;
			}

			auto pos = static_cast<int>(std::distance(matchingItems.begin(), iter));

			matchingItems.erase(iter);
			sourceItems.erase(aItem);

			if (rangeStart_ > 0 && pos > rangeStart_) {
				// Update the range range positions
				rangeStart_--;
			}
		}

		// Returns false if the item was added/removed (or the item doesn't exist in any item list)
		bool handleUpdateItem(const T& aItem, int aSortProperty, int aSortAscending, int& rangeStart_) {
			bool inList;

			{
				RLock l(cs);
				inList = isInList(aItem, matchingItems);

				// A delayed update for a removed item?
				if (!inList && sourceItems.find(aItem) == sourceItems.end()) {
					return false;
				}
			}

			auto matchers = getFilterMatcherList();
			if (!matchesFilter(aItem, matchers)) {
				if (inList) {
					handleRemoveItem(aItem, rangeStart_);
				}

				return false;
			} else if (!inList) {
				handleAddItem(aItem, aSortProperty, aSortAscending, rangeStart_);
				return false;
			}

			return true;
		}

		// TASKS END

		// JSON APPEND START

		// Append item with all property values
		void appendItemFull(const T& aItem, json& json_, int pos) {
			appendItemPartial(aItem, json_, pos, toPropertyIdSet(itemHandler.properties));
		}

		// Append item with supplied property values
		void appendItemPartial(const T& aItem, json& json_, int pos, const PropertyIdSet& aPropertyIds) {
			appendItemPosition(aItem, json_, pos);
			json_["items"][pos]["properties"] = Serializer::serializeProperties(aItem, itemHandler, aPropertyIds);
		}

		// Append item without property values
		void appendItemPosition(const T& aItem, json& json_, int pos) {
			json_["items"][pos]["id"] = aItem->getToken();
		}

		// List of dynamically set filters
		PropertyFilter::List filters;

		// This one should be provided when initiating the view
		// Items that don't match the filter won't be added in source items or included in total item count
		unique_ptr<PropertyFilter> sourceFilter;

		// Contains all possible items of this type (excluding ones matching the source item filter)
		std::set<T, std::less<T>> sourceItems;

		const PropertyItemHandler<T>& itemHandler;

		// Items visible in the current viewport
		ItemList currentViewportItems;

		// All items matching the list of dynamic filters
		ItemList matchingItems;

		bool active = false;

		mutable SharedMutex cs;

		SubscribableApiModule* module = nullptr;
		const std::string viewName;

		ItemTasks<T> tasks;

		TimerPtr timer;

		class IntCollector {
		public:
			enum ValueType {
				TYPE_SORT_PROPERTY,
				TYPE_SORT_ASCENDING,
				TYPE_RANGE_START,
				TYPE_MAX_COUNT,
				TYPE_LAST
			};

			typedef std::map<ValueType, int> ValueMap;

			IntCollector() {
				reset();
			}

			void reset() noexcept {
				for (int i = 0; i < TYPE_LAST; i++) {
					values[static_cast<ValueType>(i)] = -1;
				}
			}

			void set(ValueType aType, int aValue) noexcept {
				changed = true;
				values[aType] = aValue;
			}

			void set(const ValueMap& aMap) noexcept {
				changed = true;
				for (const auto& i : aMap) {
					values[i.first] = i.second;
				}
			}

			ValueMap getAll() noexcept {
				changed = false;
				return values;
			}

			bool hasChanged() const noexcept {
				return changed;
			}
		private:
			bool changed = true;
			ValueMap values;
		};

		bool itemListChanged = false;
		IntCollector currentValues;

		int prevMatchingItemCount = -1;
		int prevTotalItemCount = -1;
		ItemListF itemListF;
		typename IntCollector::ValueMap prevValues;
	};
}

#endif
