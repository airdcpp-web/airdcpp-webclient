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

#ifndef DCPLUSPLUS_DCPP_SERIALIZER_H
#define DCPLUSPLUS_DCPP_SERIALIZER_H

#include <web-server/stdinc.h>

#include <api/common/Property.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/MessageCache.h>
#include <airdcpp/QueueItemBase.h>
#include <airdcpp/TrackableDownloadItem.h>


namespace webserver {
	class Serializer {
	public:
		static StringSet getUserFlags(const UserPtr& aUser) noexcept;
		static StringSet getOnlineUserFlags(const OnlineUserPtr& aUser) noexcept;
		static string getSeverity(LogMessage::Severity aSeverity) noexcept;

		static json serializeMessage(const Message& aMessage) noexcept;
		static json serializeChatMessage(const ChatMessagePtr& aMessage) noexcept;
		static json serializeLogMessage(const LogMessagePtr& aMessageData) noexcept;

		typedef std::function<json(const MessageCache& aCache)> UnreadSerializerF;
		static void serializeCacheInfo(json& json_, const MessageCache& aCache, UnreadSerializerF unreadF) noexcept;
		static json serializeUnreadChat(const MessageCache& aCache) noexcept;
		static json serializeUnreadLog(const MessageCache& aCache) noexcept;

		static json serializeUser(const UserPtr& aUser) noexcept;
		static json serializeHintedUser(const HintedUser& aUser) noexcept;
		static json serializeOnlineUser(const OnlineUserPtr& aUser) noexcept;

		static json serializeFileType(const string& aPath) noexcept;
		static json serializeFolderType(int aFiles, int aDirectories) noexcept;

		static json serializeIp(const string& aIP) noexcept;
		static json serializeIp(const string& aIP, const string& aCountryCode) noexcept;

		static string getDownloadStateId(TrackableDownloadItem::State aState) noexcept;
		static string getDownloadStateStr(TrackableDownloadItem::State aState) noexcept;
		static json serializeDownloadState(TrackableDownloadItem::State aState) noexcept;


		// Serialize n messages from end by keeping the list order
		// Throws for invalid parameters
		template <class ContainerT, class FuncT>
		static json serializeFromEnd(int aCount, const ContainerT& aList, FuncT aF) throw(std::exception) {
			if (aList.empty()) {
				return json::array();
			}

			if (aCount < 0) {
				throw std::domain_error("Invalid range");
			}

			auto listSize = static_cast<int>(std::distance(aList.begin(), aList.end()));
			auto beginIter = aList.begin();
			if (aCount > 0 && listSize > aCount) {
				std::advance(beginIter, listSize - aCount);
			}

			return serializeRange(beginIter, aList.end(), aF);
		}

		// Serialize n messages from position
		// Throws for invalid parameters
		template <class ContainerT, class FuncT>
		static json serializeFromPosition(int aBeginPos, int aCount, const ContainerT& aList, FuncT aF) throw(std::exception) {
			auto listSize = static_cast<int>(std::distance(aList.begin(), aList.end()));
			if (listSize == 0) {
				return json::array();
			}

			if (aBeginPos >= listSize || aCount <= 0) {
				throw std::domain_error("Invalid range");
			}

			auto beginIter = aList.begin();
			std::advance(beginIter, aBeginPos);

			auto endIter = beginIter;
			std::advance(endIter, min(listSize - aBeginPos, aCount));

			return serializeRange(beginIter, endIter, aF);
		}

		// Serialize a list of items provider by the handler with a custom range
		// Throws for invalid range parameters
		template <class T>
		static json serializeItemList(int aStart, int aCount, const PropertyItemHandler<T>& aHandler, const vector<T>& aItems) throw(std::exception) {
			if (aItems.empty()) {
				return json::array();
			}

			return Serializer::serializeFromPosition(aStart, aCount, aItems, [&aHandler](const T& aItem) {
				return Serializer::serializeItem(aItem, aHandler);
			});
		}

		// Serialize a list of items provider by the handler
		template <class T>
		static json serializeItemList(const PropertyItemHandler<T>& aHandler, const vector<T>& aItems) throw(std::exception) {
			if (aItems.empty()) {
				return json::array();
			}

			return Serializer::serializeRange(aItems.begin(), aItems.end(), [&aHandler](const T& aItem) {
				return Serializer::serializeItem(aItem, aHandler);
			});
		}

		template <class T>
		static json serializeItem(const T& aItem, const PropertyItemHandler<T>& aHandler) noexcept {
			auto j = serializeItemProperties(aItem, toPropertyIdSet(aHandler.properties), aHandler);
			j["id"] = aItem->getToken();
			return j;
		}

		template <class T>
		static json serializeItemProperties(const T& aItem, const PropertyIdSet& aPropertyIds, const PropertyItemHandler<T>& aHandler) noexcept {
			json j;
			for (auto id : aPropertyIds) {
				const auto& prop = aHandler.properties[id];
				switch (prop.serializationMethod) {
				case SERIALIZE_NUMERIC: {
					j[prop.name] = aHandler.numberF(aItem, id);
					break;
				}
				case SERIALIZE_TEXT: {
					j[prop.name] = aHandler.stringF(aItem, id);
					break;
				}
				case SERIALIZE_TEXT_NUMERIC: {
					j[prop.name]["id"] = aHandler.numberF(aItem, id);
					j[prop.name]["str"] = aHandler.stringF(aItem, id);
					break;
				}
				case SERIALIZE_BOOL: {
					j[prop.name] = aHandler.numberF(aItem, id) == 0 ? false : true;
					break;
				}
				case SERIALIZE_CUSTOM: {
					j[prop.name] = aHandler.jsonF(aItem, id);
					break;
				}
				}
			}

			return j;
		}
	private:
		static void appendOnlineUserFlags(const OnlineUserPtr& aUser, StringSet& flags_) noexcept;

		template <class IterT, class FuncT>
		static json serializeRange(IterT aBegin, IterT aEnd, FuncT aF) noexcept {
			return std::accumulate(aBegin, aEnd, json(), [&](json& list, const typename iterator_traits<IterT>::value_type& elem) {
				list.push_back(aF(elem));
				return list;
			});
		}
	};
}

#endif
