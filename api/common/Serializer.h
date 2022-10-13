/*
* Copyright (C) 2011-2022 AirDC++ Project
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

#include <api/common/Property.h>

#include <web-server/Access.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/tribool.h>

#include <airdcpp/DirectoryDownload.h>
#include <airdcpp/DupeType.h>
#include <airdcpp/QueueItemBase.h>
#include <airdcpp/TrackableDownloadItem.h>


namespace dcpp {
	struct DirectoryContentInfo;
}

namespace webserver {
	class Serializer {
	public:
		static StringSet getUserFlags(const UserPtr& aUser) noexcept;
		static StringSet getOnlineUserFlags(const OnlineUserPtr& aUser) noexcept;

		static json serializeUser(const UserPtr& aUser) noexcept;
		static json serializeHintedUser(const HintedUser& aUser) noexcept;
		static json serializeOnlineUser(const OnlineUserPtr& aUser) noexcept;

		static string toFileContentType(const string& aExt) noexcept;
		static string getFileTypeId(const string& aName) noexcept;
		static json serializeFileType(const string& aPath) noexcept;
		static json serializeFolderType(const DirectoryContentInfo& aContentInfo) noexcept;

		static json serializeIp(const string& aIP) noexcept;
		static json serializeIp(const string& aIP, const string& aCountryCode) noexcept;

		static json serializeShareProfileSimple(ProfileToken aProfile) noexcept;
		static json serializeEncryption(const string& aInfo, bool aIsTrusted) noexcept;

		static string getDownloadStateId(TrackableDownloadItem::State aState) noexcept;
		static json serializeDownloadState(const TrackableDownloadItem& aItem) noexcept;

		static string getDupeId(DupeType aDupeType) noexcept;
		static json serializeDupe(DupeType aDupeType, StringList&& aPaths) noexcept;
		static json serializeFileDupe(DupeType aDupeType, const TTHValue& aTTH) noexcept;
		static json serializeDirectoryDupe(DupeType aDupeType, const string& aAdcPath) noexcept;
		static json serializeSlots(int aFree, int aTotal) noexcept;
		
		static string getDirectoryDownloadStateId(DirectoryDownload::State aState) noexcept;
		static json serializeDirectoryDownload(const DirectoryDownloadPtr& aDownload) noexcept;
		static json serializeDirectoryBundleAddResult(const DirectoryBundleAddResult& aInfo, const string& aError) noexcept;
		static json serializeBundleAddInfo(const BundleAddInfo& aInfo) noexcept;

		static json serializePriorityId(Priority aPriority) noexcept;
		static json serializePriority(const QueueItemBase& aItem) noexcept;
		static json serializeSourceCount(const QueueItemBase::SourceCount& aCount) noexcept;

		static json serializeGroupedPaths(const pair<string, OrderedStringSet>& aGroupedPair) noexcept;
		static json serializeActionHookError(const ActionHookRejectionPtr& aError) noexcept;

		static json serializeFilesystemItem(const FilesystemItem& aInfo) noexcept;

		static StringList serializePermissions(const AccessList& aPermissions) noexcept;

		// Serialize n items from end by keeping the list order
		// Throws for invalid parameters
		template <class ContainerT, class FuncT>
		static json serializeFromEnd(int aCount, const ContainerT& aList, FuncT aF) {
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

		// Serialize n items from beginning by keeping the list order
		// Throws for invalid parameters
		template <class ContainerT, class FuncT>
		static json serializeFromBegin(int aCount, const ContainerT& aList, FuncT aF) {
			if (aList.empty()) {
				return json::array();
			}

			if (aCount < 0) {
				throw std::domain_error("Invalid range");
			}

			auto listSize = static_cast<int>(std::distance(aList.begin(), aList.end()));
			auto endIter = aList.end();
			if (aCount > 0 && listSize > aCount) {
				endIter = aList.begin();
				std::advance(endIter, aCount);
			}

			return serializeRange(aList.begin(), endIter, aF);
		}

		template <class ContainerT, class FuncT>
		static json serializeList(const ContainerT& aList, FuncT aF) noexcept {
			return serializeRange(aList.begin(), aList.end(), aF);
		}

		// Serialize n messages from position
		// Throws for invalid parameters
		template <class ContainerT, class FuncT>
		static json serializeFromPosition(int aBeginPos, int aCount, const ContainerT& aList, FuncT aF) {
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
		template <class T, class ContainerT>
		static json serializeItemList(int aStart, int aCount, const PropertyItemHandler<T>& aHandler, const ContainerT& aItems) {
			return Serializer::serializeFromPosition(aStart, aCount, aItems, [&aHandler](const T& aItem) {
				return Serializer::serializeItem(aItem, aHandler);
			});
		}

		// Serialize a list of items provider by the handler
		template <class T, class ContainerT>
		static json serializeItemList(const PropertyItemHandler<T>& aHandler, const ContainerT& aItems) {
			return Serializer::serializeRange(aItems.begin(), aItems.end(), [&aHandler](const T& aItem) {
				return Serializer::serializeItem(aItem, aHandler);
			});
		}

		// Serialize item with ID and all properties
		template <class T>
		static json serializeItem(const T& aItem, const PropertyItemHandler<T>& aHandler) noexcept {
			return serializePartialItem(aItem, aHandler, toPropertyIdSet(aHandler.properties));
		}

		// Serialize item with ID and specified properties
		template <class T>
		static json serializePartialItem(const T& aItem, const PropertyItemHandler<T>& aHandler, const PropertyIdSet& aPropertyIds) noexcept {
			auto j = serializeProperties(aItem, aHandler, aPropertyIds);
			j["id"] = aItem->getToken();
			return j;
		}

		// Serialize specified item properties (without the ID)
		template <class T>
		static json serializeProperties(const T& aItem, const PropertyItemHandler<T>& aHandler, const PropertyIdSet& aPropertyIds) noexcept {
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

		static json serializeChangedProperties(const json& aNewProperties, const json& aOldProperties) noexcept;

		template<typename IdT>
		static json defaultArrayValueSerializer(const IdT& aJson) {
			return aJson;
		}

		static json serializeHubSetting(const tribool& aSetting) noexcept;
		static json serializeHubSetting(int aSetting) noexcept;
		static string serializeHubSetting(const string& aSetting) noexcept;
	private:
		static void appendOnlineUserFlags(const OnlineUserPtr& aUser, StringSet& flags_) noexcept;

		template <class IterT, class FuncT>
		static json serializeRange(IterT aBegin, IterT aEnd, FuncT aF) noexcept {
			return std::accumulate(aBegin, aEnd, json::array(), [&](json& list, const typename iterator_traits<IterT>::value_type& elem) {
				list.push_back(aF(elem));
				return list;
			});
		}
	};
}

#endif
