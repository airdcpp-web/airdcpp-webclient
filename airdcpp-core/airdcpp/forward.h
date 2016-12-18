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

#ifndef DCPLUSPLUS_DCPP_FORWARD_H_
#define DCPLUSPLUS_DCPP_FORWARD_H_

/** @file
 * This file contains forward declarations for the various DC++ classes
 */

#include <boost/intrusive_ptr.hpp>
#include <stdint.h>

namespace dcpp {

class AdcCommand;

class SearchQuery;

class ADLSearch;

class BufferedSocket;

struct BundleDirectoryItemInfo;

struct BundleAddInfo;
struct DirectoryBundleAddInfo;

class Bundle;
typedef boost::intrusive_ptr<Bundle> BundlePtr;
typedef std::vector<BundlePtr> BundleList;

class CID;

typedef std::vector<uint16_t> PartsInfo;

class Client;
typedef std::shared_ptr<Client> ClientPtr;
typedef uint32_t ClientToken;

class ClientManager;

class ConnectionQueueItem;

class DirectoryListing;
typedef boost::intrusive_ptr<DirectoryListing> DirectoryListingPtr;
typedef std::vector<DirectoryListingPtr> DirectoryListingList;

class DirectoryDownload;
typedef std::shared_ptr<DirectoryDownload> DirectoryDownloadPtr;

class Download;
typedef Download* DownloadPtr;
typedef std::vector<DownloadPtr> DownloadList;

class FavoriteHubEntry;
typedef boost::intrusive_ptr<FavoriteHubEntry> FavoriteHubEntryPtr;
typedef std::vector<FavoriteHubEntryPtr> FavoriteHubEntryList;

class FavoriteUser;

class File;

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
typedef boost::intrusive_ptr<QueueItem> QueueItemPtr;
typedef std::vector<QueueItemPtr> QueueItemList;

class RecentHubEntry;
typedef boost::intrusive_ptr<RecentHubEntry> RecentHubEntryPtr;
typedef std::vector<RecentHubEntryPtr> RecentHubEntryList;

class Search;
typedef shared_ptr<Search> SearchPtr;

class SearchResult;
typedef boost::intrusive_ptr<SearchResult> SearchResultPtr;
typedef std::vector<SearchResultPtr> SearchResultList;

class ServerSocket;

class ShareProfile;
typedef std::shared_ptr<ShareProfile> ShareProfilePtr;
typedef vector<ShareProfilePtr> ShareProfileList;
typedef set<string> RefreshPathList;

class SimpleXML;

class Socket;
class SocketException;

class StringSearch;

class TigerHash;

class Transfer;

typedef HashValue<TigerHash> TTHValue;

class UnZFilter;

class Upload;
typedef Upload* UploadPtr;
typedef std::vector<UploadPtr> UploadList;

class UploadBundle;
typedef boost::intrusive_ptr<UploadBundle> UploadBundlePtr;
typedef std::vector<UploadBundlePtr> UploadBundleList;

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

} // namespace dcpp

#endif /*DCPLUSPLUS_CLIENT_FORWARD_H_*/
