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

#include <boost/intrusive_ptr.hpp>
#include <stdint.h>

namespace dcpp {


template<typename DataT>
class ActionHookDataGetter;

struct ActionHookRejection;
typedef std::shared_ptr<ActionHookRejection> ActionHookRejectionPtr;

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

struct AdapterInfo;
typedef vector<AdapterInfo> AdapterInfoList;

class AdcCommand;

class SearchQuery;

class ADLSearch;

class BufferedSocket;

struct BundleFileInfo;

struct BundleAddInfo;
struct DirectoryBundleAddResult;

class Bundle;
typedef std::shared_ptr<Bundle> BundlePtr;
typedef std::vector<BundlePtr> BundleList;

class CID;

typedef std::vector<uint16_t> PartsInfo;

class Client;
typedef std::shared_ptr<Client> ClientPtr;
typedef uint32_t ClientToken;

class ClientManager;

class ConnectionQueueItem;

struct DirectoryContentInfo;

class DirectoryListing;
typedef std::shared_ptr<DirectoryListing> DirectoryListingPtr;
typedef std::vector<DirectoryListingPtr> DirectoryListingList;

class DirectoryDownload;
typedef std::shared_ptr<DirectoryDownload> DirectoryDownloadPtr;
typedef vector<DirectoryDownloadPtr> DirectoryDownloadList;

class Download;
typedef Download* DownloadPtr;
typedef std::vector<DownloadPtr> DownloadList;

class FavoriteHubEntry;
typedef boost::intrusive_ptr<FavoriteHubEntry> FavoriteHubEntryPtr;
typedef std::vector<FavoriteHubEntryPtr> FavoriteHubEntryList;

class FavoriteUser;

class File;
struct FilesystemItem;
typedef vector<FilesystemItem> FilesystemItemList;

class FinishedManager;

template<class Hasher>
struct HashValue;

class HashedFile;

struct HintedUser;
typedef std::vector<HintedUser> HintedUserList;

class HttpConnection;

struct HttpDownload;

class Identity;

class InputStream;

class LogManager;

struct Message;
struct OutgoingChatMessage;

class MessageHighlight;
typedef shared_ptr<MessageHighlight> MessageHighlightPtr;
typedef vector<MessageHighlightPtr> MessageHighlightList;

class ChatMessage;
typedef std::shared_ptr<ChatMessage> ChatMessagePtr;
typedef std::deque<ChatMessagePtr> ChatMessageList;

class LogMessage;
typedef std::shared_ptr<LogMessage> LogMessagePtr;
typedef std::deque<LogMessagePtr> LogMessageList;

class OnlineUser;
typedef boost::intrusive_ptr<OnlineUser> OnlineUserPtr;
typedef std::vector<OnlineUserPtr> OnlineUserList;

class OutputStream;

class PrivateChat;
typedef std::shared_ptr<PrivateChat> PrivateChatPtr;

class QueueItemBase;

class QueueItem;
typedef std::shared_ptr<QueueItem> QueueItemPtr;
typedef std::vector<QueueItemPtr> QueueItemList;

class Search;
typedef shared_ptr<Search> SearchPtr;

class SearchInstance;
typedef shared_ptr<SearchInstance> SearchInstancePtr;
typedef vector<SearchInstancePtr> SearchInstanceList;
typedef uint32_t SearchInstanceToken;

class SearchResult;
typedef std::shared_ptr<SearchResult> SearchResultPtr;
typedef std::vector<SearchResultPtr> SearchResultList;

class SearchType;
typedef shared_ptr<SearchType> SearchTypePtr;
typedef vector<SearchTypePtr> SearchTypeList;

class GroupedSearchResult;
typedef std::shared_ptr<GroupedSearchResult> GroupedSearchResultPtr;
typedef std::vector<GroupedSearchResultPtr> GroupedSearchResultList;

class ServerSocket;

class ShareProfile;
typedef std::shared_ptr<ShareProfile> ShareProfilePtr;
typedef vector<ShareProfilePtr> ShareProfileList;

typedef set<string> RefreshPathList;
struct ShareRefreshStats;

struct ShareRefreshTask;
typedef uint32_t ShareRefreshTaskToken;

struct TempShareInfo;
typedef vector<TempShareInfo> TempShareInfoList;

class SimpleXML;

class Socket;
class SocketException;

class StartupLoader;
class StringSearch;

class TigerHash;

class Transfer;

typedef HashValue<TigerHash> TTHValue;

class UnZFilter;

class Upload;
typedef Upload* UploadPtr;
typedef std::vector<UploadPtr> UploadList;

class UploadQueueItem;

class User;
typedef boost::intrusive_ptr<User> UserPtr;
typedef std::vector<UserPtr> UserList;

class UserCommand;

class UserConnection;
typedef UserConnection* UserConnectionPtr;
typedef std::vector<UserConnectionPtr> UserConnectionList;

class ViewFile;
typedef shared_ptr<ViewFile> ViewFilePtr;
typedef vector<ViewFilePtr> ViewFileList;

// Generic callbacks
typedef function<void()> Callback;
typedef function<void(const string&)> MessageCallback;

// Startup callbacks
typedef std::function<void(const string&)> StepFunction;
typedef std::function<bool(const string& /*aMessage*/, bool /*aIsQuestion*/, bool /*aIsError*/)> MessageFunction;
typedef std::function<void(float)> ProgressFunction;

} // namespace dcpp

#endif /*DCPLUSPLUS_CLIENT_FORWARD_H_*/
