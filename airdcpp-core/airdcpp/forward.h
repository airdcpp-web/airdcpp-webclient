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

#ifndef DCPLUSPLUS_DCPP_FORWARD_H_
#define DCPLUSPLUS_DCPP_FORWARD_H_

/** @file
 * This file contains forward declarations for the various DC++ classes
 */

#include <stdint.h>

namespace dcpp {

using StringHashToken = size_t;
using RandomNumericToken = uint32_t;
using IncrementToken = uint32_t;

template<typename DataT>
class ActionHookDataGetter;

struct ActionHookRejection;
using ActionHookRejectionPtr = std::shared_ptr<ActionHookRejection>;

template<typename DataT = nullptr_t>
struct ActionHookData;

template<typename DataT = nullptr_t>
using ActionHookDataPtr = std::shared_ptr<ActionHookData<DataT>>;

template<typename DataT = nullptr_t>
struct ActionHookResult;

template<typename DataT = nullptr_t>
using ActionHookDataList = vector<ActionHookDataPtr<DataT>>;

template<typename DataT = nullptr_t>
using ActionHookResultGetter = ActionHookDataGetter<DataT>;
using CallerPtr = const void*;

struct AdapterInfo;
using AdapterInfoList = vector<AdapterInfo>;

class AdcCommand;

class SearchQuery;

class ADLSearch;

class BufferedSocket;

struct BundleFileInfo;

struct BundleAddInfo;
struct DirectoryBundleAddResult;

class Bundle;
using BundlePtr = std::shared_ptr<Bundle>;
using BundleList = std::vector<BundlePtr>;

class CID;

typedef std::vector<uint16_t> PartsInfo;

class Client;
using ClientPtr = std::shared_ptr<Client>;
typedef uint32_t ClientToken;

class ClientManager;

class ConnectionQueueItem;

struct DirectoryContentInfo;

class DirectoryListing;
using DirectoryListingPtr = std::shared_ptr<DirectoryListing>;
using DirectoryListingList = std::vector<DirectoryListingPtr>;
using DirectoryListingItemToken = StringHashToken;

class DirectoryDownload;
using DirectoryDownloadPtr = std::shared_ptr<DirectoryDownload>;
using DirectoryDownloadList = vector<DirectoryDownloadPtr>;

class Download;
using DownloadPtr = Download *;
using DownloadList = std::vector<DownloadPtr>;

class FavoriteHubEntry;
using FavoriteHubEntryPtr = std::shared_ptr<FavoriteHubEntry>;
using FavoriteHubEntryList = std::vector<FavoriteHubEntryPtr>;
using FavoriteHubToken = RandomNumericToken;

class FavoriteUser;

class File;
struct FilesystemItem;
using FilesystemItemList = vector<FilesystemItem>;

class FinishedManager;

template<class Hasher>
struct HashValue;

class HashedFile;

struct HintedUser;
using HintedUserList = std::vector<HintedUser>;

class HttpConnection;

struct HttpDownload;

class Identity;

class InputStream;

class LogManager;

struct Message;
struct OutgoingChatMessage;

class MessageHighlight;
using MessageHighlightPtr = shared_ptr<MessageHighlight>;
using MessageHighlightList = vector<MessageHighlightPtr>;
using MessageHighlightToken = uint32_t;

class ChatMessage;
using ChatMessagePtr = std::shared_ptr<ChatMessage>;
using ChatMessageList = std::deque<ChatMessagePtr>;

class LogMessage;
using LogMessagePtr = std::shared_ptr<LogMessage>;
using LogMessageList = std::deque<LogMessagePtr>;

class OnlineUser;
using OnlineUserPtr = std::shared_ptr<OnlineUser>;
using OnlineUserList = std::vector<OnlineUserPtr>;
using SID = uint32_t;

class OutputStream;

class PrivateChat;
using PrivateChatPtr = std::shared_ptr<PrivateChat>;

using QueueToken = uint32_t;
using QueueTokenSet = unordered_set<QueueToken>;
class QueueItemBase;

class QueueItem;
using QueueItemPtr = std::shared_ptr<QueueItem>;
using QueueItemList = std::vector<QueueItemPtr>;

class Search;
using SearchPtr = shared_ptr<Search>;

class SearchInstance;
using SearchInstancePtr = shared_ptr<SearchInstance>;
using SearchInstanceList = vector<SearchInstancePtr>;
using SearchInstanceToken = uint32_t;

class SearchResult;
using SearchResultPtr = std::shared_ptr<SearchResult>;
using SearchResultList = std::vector<SearchResultPtr>;

class SearchType;
using SearchTypePtr = shared_ptr<SearchType>;
using SearchTypeList = vector<SearchTypePtr>;

class GroupedSearchResult;
using GroupedSearchResultPtr = std::shared_ptr<GroupedSearchResult>;
using GroupedSearchResultList = std::vector<GroupedSearchResultPtr>;

class ServerSocket;

class ShareProfile;
using ShareProfilePtr = std::shared_ptr<ShareProfile>;
using ShareProfileList = vector<ShareProfilePtr>;

using RefreshPathList = set<string>;
struct ShareRefreshStats;

struct ShareRefreshTask;
using ShareRefreshTaskToken = uint32_t;

struct TempShareInfo;

class SimpleXML;

class Socket;
class SocketException;

class StartupLoader;
class StringSearch;

class TigerHash;

class Transfer;
using TransferToken = uint32_t;

using TTHValue = HashValue<TigerHash>;

class UnZFilter;

class Upload;
using UploadPtr = Upload *;
using UploadList = std::vector<UploadPtr>;

class UploadQueueItem;

class User;
using UserPtr = std::shared_ptr<User>;
using UserList = std::vector<UserPtr>;

class UserCommand;

class UserConnection;
using UserConnectionPtr = UserConnection *;
using UserConnectionList = std::vector<UserConnectionPtr>;
using UserConnectionToken = uint32_t;

class ViewFile;
using ViewFilePtr = shared_ptr<ViewFile>;
using ViewFileList = vector<ViewFilePtr>;

// Generic callbacks
using Callback = function<void ()>;
using MessageCallback = function<void (const string &)>;

// Startup callbacks
using StepFunction = std::function<void (const string &)>;
using MessageFunction = std::function<bool (const string &, bool, bool)>;
using ProgressFunction = std::function<void (float)>;

} // namespace dcpp

#endif /*DCPLUSPLUS_CLIENT_FORWARD_H_*/
