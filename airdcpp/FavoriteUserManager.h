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

#ifndef DCPLUSPLUS_DCPP_FAVORITE_USER_MANAGER_H
#define DCPLUSPLUS_DCPP_FAVORITE_USER_MANAGER_H

#include "ClientManagerListener.h"
#include "ConnectionManagerListener.h"
#include "DownloadManagerListener.h"
#include "FavoriteManagerListener.h"
#include "FavoriteUserManagerListener.h"

#include "FavoriteUser.h"
#include "Singleton.h"
#include "Speaker.h"
#include "UploadSlot.h"

namespace dcpp {

class ReservedSlotManager;
struct ParsedUpload;

class FavoriteUserManager : public Speaker<FavoriteUserManagerListener>, public Singleton<FavoriteUserManager>,
	private ClientManagerListener, private FavoriteManagerListener, private ConnectionManagerListener, private DownloadManagerListener
{
public:
	using FavoriteMap = unordered_map<CID, FavoriteUser>;

	void addFavoriteUser(const HintedUser& aUser) noexcept;
	void removeFavoriteUser(const UserPtr& aUser) noexcept;
	optional<FavoriteUser> getFavoriteUser(const UserPtr& aUser) const noexcept;

	bool hasSlot(const UserPtr& aUser) const noexcept;
	void setUserDescription(const UserPtr& aUser, const string& description) noexcept;
	void setAutoGrant(const UserPtr& aUser, bool grant) noexcept;
	time_t getLastSeen(const UserPtr& aUser) const noexcept;
	void changeLimiterOverride(const UserPtr& aUser) noexcept;

	void addSavedUser(const UserPtr& aUser) noexcept;

	void setDirty() noexcept;

	ReservedSlotManager& getReservedSlots() noexcept {
		return *reservedSlots.get();
	}
private:

	mutable SharedMutex cs;
	unique_ptr<ReservedSlotManager> reservedSlots;

	//Favorite users
	FavoriteMap users;

	//Saved users
	unordered_set<UserPtr, User::Hash> savedUsers;

	static FavoriteUser createUser(const UserPtr& aUser, const string& aUrl);
	
	friend class Singleton<FavoriteUserManager>;
	
	FavoriteUserManager();
	~FavoriteUserManager() override;

	ActionHookResult<MessageHighlightList> onPrivateMessage(const ChatMessagePtr& aMessage, const ActionHookResultGetter<MessageHighlightList>& aResultGetter) noexcept;
	ActionHookResult<MessageHighlightList> onHubMessage(const ChatMessagePtr& aMessage, const ActionHookResultGetter<MessageHighlightList>& aResultGetter) noexcept;

	ActionHookResult<OptionalUploadSlot> onSlotType(const UserConnection& aUser, const ParsedUpload& aUploadInfo, const ActionHookResultGetter<OptionalUploadSlot>& aResultGetter) const noexcept;

	ActionHookResult<MessageHighlightList> formatFavoriteUsers(const ChatMessagePtr& msg, const ActionHookResultGetter<MessageHighlightList>& aResultGetter) noexcept;

	// ClientManagerListener
	void on(ClientManagerListener::UserConnected, const OnlineUser& user, bool wasOffline) noexcept override;
	void on(ClientManagerListener::UserDisconnected, const UserPtr& user, bool wentOffline) noexcept override;

	void on(FavoriteManagerListener::Load, SimpleXML& xml) noexcept override;
	void on(FavoriteManagerListener::Save, SimpleXML& xml) noexcept override;

	void on(ConnectionManagerListener::UserSet, UserConnection* aCqi) noexcept override;

	void on(DownloadManagerListener::Tick, const DownloadList& aDownloads, uint64_t aTick) noexcept override;

	void loadFavoriteUsers(SimpleXML& aXml);
	void saveFavoriteUsers(SimpleXML& aXml) noexcept;
};

} // namespace dcpp

#endif // !defined(FAVORITE_MANAGER_H)