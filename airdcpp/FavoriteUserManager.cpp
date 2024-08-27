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

#include "stdinc.h"
#include "FavoriteUserManager.h"

#include "ClientManager.h"
#include "ConnectionManager.h"
#include "Download.h"
#include "DownloadManager.h"
#include "FavoriteManager.h"
#include "HintedUser.h"
#include "Message.h"
#include "ReservedSlotManager.h"
#include "ResourceManager.h"
#include "SimpleXML.h"
#include "UploadManager.h"

namespace dcpp {

using ranges::find_if;

#define FAVORITE_USERS_HOOK_ID "favorite_users"
FavoriteUserManager::FavoriteUserManager() : 
	reservedSlots(make_unique<ReservedSlotManager>([this](const UserPtr& aUser) { 
		fire(FavoriteUserManagerListener::SlotsUpdated(), aUser); 
	})) 
{
	ClientManager::getInstance()->addListener(this);
	FavoriteManager::getInstance()->addListener(this);

	ConnectionManager::getInstance()->addListener(this);
	DownloadManager::getInstance()->addListener(this);

	ClientManager::getInstance()->incomingPrivateMessageHook.addSubscriber(ActionHookSubscriber(FAVORITE_USERS_HOOK_ID, STRING(FAVORITE_USERS), nullptr), HOOK_HANDLER(FavoriteUserManager::onPrivateMessage));
	ClientManager::getInstance()->incomingHubMessageHook.addSubscriber(ActionHookSubscriber(FAVORITE_USERS_HOOK_ID, STRING(FAVORITE_USERS), nullptr), HOOK_HANDLER(FavoriteUserManager::onHubMessage));

	UploadManager::getInstance()->slotTypeHook.addSubscriber(ActionHookSubscriber(FAVORITE_USERS_HOOK_ID, STRING(FAVORITE_USERS), nullptr), HOOK_HANDLER(FavoriteUserManager::onSlotType));
}

FavoriteUserManager::~FavoriteUserManager() {
	ClientManager::getInstance()->removeListener(this);
	FavoriteManager::getInstance()->removeListener(this);

	ConnectionManager::getInstance()->removeListener(this);
	DownloadManager::getInstance()->removeListener(this);
}

FavoriteUser FavoriteUserManager::createUser(const UserPtr& aUser, const string& aUrl) {
	string nick;
	int64_t seen = 0;
	string hubUrl = aUrl;

	//prefer to use the add nick
	auto ou = ClientManager::getInstance()->findOnlineUser(aUser->getCID(), aUrl);
	if (!ou) {
		//offline
		auto ofu = ClientManager::getInstance()->getOfflineUser(aUser->getCID());
		if (ofu) {
			nick = ofu->getNick();
			seen = ofu->getLastSeen();
			hubUrl = ofu->getUrl();
		}
	}
	else {
		nick = ou->getIdentity().getNick();
	}

	auto fu = FavoriteUser(aUser, nick, hubUrl, aUser->getCID().toBase32());
	fu.setLastSeen(seen);
	return fu;
}

void FavoriteUserManager::addFavoriteUser(const HintedUser& aUser) noexcept {
	if(aUser.user == ClientManager::getInstance()->getMe()) // we cant allow adding ourself as a favorite user :P
		return;

	{
		RLock l(cs);
		if(users.find(aUser.user->getCID()) != users.end()) {
			return;
		}
	}

	auto fu = createUser(aUser.user, aUser.hint);
	{
		WLock l (cs);
		users.emplace(aUser.user->getCID(), fu);
	}

	aUser.user->setFlag(User::FAVORITE);
	fire(FavoriteUserManagerListener::FavoriteUserAdded(), fu);
}

void FavoriteUserManager::addSavedUser(const UserPtr& aUser) noexcept {
	if (aUser == ClientManager::getInstance()->getMe()) // no reason saving ourself
		return;

	{
		RLock l(cs);
		if (savedUsers.find(aUser) != savedUsers.end()) {
			return;
		}
	}

	{
		WLock l(cs);
		savedUsers.emplace(aUser);
	}
	setDirty();
}

void FavoriteUserManager::removeFavoriteUser(const UserPtr& aUser) noexcept {
	{
		WLock l(cs);
		auto i = users.find(aUser->getCID());
		if(i != users.end()) {
			aUser->unsetFlag(User::FAVORITE);
			fire(FavoriteUserManagerListener::FavoriteUserRemoved(), i->second);
			users.erase(i);
		}
	}

	setDirty();
}

optional<FavoriteUser> FavoriteUserManager::getFavoriteUser(const UserPtr &aUser) const noexcept {
	RLock l(cs);
	auto i = users.find(aUser->getCID());
	return i == users.end() ? optional<FavoriteUser>() : i->second;
}


void FavoriteUserManager::changeLimiterOverride(const UserPtr& aUser) noexcept {
	RLock l(cs);
	auto i = users.find(aUser->getCID());
	if (i != users.end()) {
		if (!i->second.isSet(FavoriteUser::FLAG_SUPERUSER))
			i->second.setFlag(FavoriteUser::FLAG_SUPERUSER);
		else
			i->second.unsetFlag(FavoriteUser::FLAG_SUPERUSER);
	}
}

void FavoriteUserManager::saveFavoriteUsers(SimpleXML& aXml) noexcept {
	aXml.addTag("Users");
	aXml.stepIn();

	{
		RLock l(cs);
		for (const auto& i : users) {
			aXml.addTag("User");
			aXml.addChildAttrib("LastSeen", i.second.getLastSeen());
			aXml.addChildAttrib("GrantSlot", i.second.isSet(FavoriteUser::FLAG_GRANTSLOT));
			aXml.addChildAttrib("SuperUser", i.second.isSet(FavoriteUser::FLAG_SUPERUSER));
			aXml.addChildAttrib("UserDescription", i.second.getDescription());
			aXml.addChildAttrib("Nick", i.second.getNick());
			aXml.addChildAttrib("URL", i.second.getUrl());
			aXml.addChildAttrib("CID", i.first.toBase32());
			aXml.addChildAttrib("Favorite", true);
		}

		for (auto& s : savedUsers) {
			auto u = createUser(s, Util::emptyString);
			aXml.addTag("User");
			aXml.addChildAttrib("LastSeen", u.getLastSeen());
			aXml.addChildAttrib("Nick", u.getNick());
			aXml.addChildAttrib("URL", u.getUrl());
			aXml.addChildAttrib("CID", s->getCID().toBase32());
			aXml.addChildAttrib("Favorite", false);
		}
	}

	aXml.stepOut();
}

void FavoriteUserManager::loadFavoriteUsers(SimpleXML& aXml) {
	if (aXml.findChild("Users")) {
		aXml.stepIn();
		while (aXml.findChild("User")) {
			const string& cid = aXml.getChildAttrib("CID");
			const string& nick = aXml.getChildAttrib("Nick");
			const string& hubUrl = aXml.getChildAttrib("URL");
			bool isFavorite = Util::toBool(Util::toInt(aXml.getChildAttrib("Favorite", "1")));
			auto lastSeen = (uint32_t)aXml.getIntChildAttrib("LastSeen");
			auto u = ClientManager::getInstance()->loadUser(cid, hubUrl, nick, lastSeen);
			if(!u || !isFavorite)
				continue;

			u->setFlag(User::FAVORITE);
			auto i = users.emplace(u->getCID(), FavoriteUser(u, nick, hubUrl, u->getCID().toBase32())).first;

			if (aXml.getBoolChildAttrib("GrantSlot"))
				i->second.setFlag(FavoriteUser::FLAG_GRANTSLOT);
			if (aXml.getBoolChildAttrib("SuperUser"))
				i->second.setFlag(FavoriteUser::FLAG_SUPERUSER);

			i->second.setLastSeen(lastSeen);
			i->second.setDescription(aXml.getChildAttrib("UserDescription"));
			
		}
		aXml.stepOut();
	}

	aXml.resetCurrentChild();
}

bool FavoriteUserManager::hasSlot(const UserPtr& aUser) const noexcept {
	{
		RLock l(cs);
		auto i = users.find(aUser->getCID());
		if (i == users.end())
			return false;
		if (i->second.isSet(FavoriteUser::FLAG_GRANTSLOT)) {
			return true;
		}
	}

	return reservedSlots->hasReservedSlot(aUser);
}

time_t FavoriteUserManager::getLastSeen(const UserPtr& aUser) const noexcept {
	RLock l(cs);
	auto i = users.find(aUser->getCID());
	if(i == users.end())
		return 0;
	return i->second.getLastSeen();
}

void FavoriteUserManager::setAutoGrant(const UserPtr& aUser, bool grant) noexcept {
	{
		RLock l(cs);
		auto i = users.find(aUser->getCID());
		if(i == users.end())
			return;
		if(grant)
			i->second.setFlag(FavoriteUser::FLAG_GRANTSLOT);
		else
			i->second.unsetFlag(FavoriteUser::FLAG_GRANTSLOT);
	}

	setDirty();
}

void FavoriteUserManager::setUserDescription(const UserPtr& aUser, const string& description) noexcept {
	{
		RLock l(cs);
		auto i = users.find(aUser->getCID());
		if(i == users.end())
			return;
		i->second.setDescription(description);
	}

	setDirty();
}

void FavoriteUserManager::on(ClientManagerListener::UserDisconnected, const UserPtr& user, bool wentOffline) noexcept {
	bool isFav = false;
	{
		RLock l(cs);
		auto i = users.find(user->getCID());
		if(i != users.end()) {
			isFav = true;
			if (wentOffline) {
				i->second.setLastSeen(GET_TIME());
			}
		}
	}

	if (isFav) {
		fire(FavoriteUserManagerListener::FavoriteUserUpdated(), user);
	}
}

void FavoriteUserManager::on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool /*wasOffline*/) noexcept {
	UserPtr user = aUser.getUser();

	if(user->isSet(User::FAVORITE))
		fire(FavoriteUserManagerListener::FavoriteUserUpdated(), user);
}

void FavoriteUserManager::on(FavoriteManagerListener::Save, SimpleXML& xml) noexcept {
	saveFavoriteUsers(xml);
}

void FavoriteUserManager::on(FavoriteManagerListener::Load, SimpleXML& xml) noexcept {
	loadFavoriteUsers(xml);
}

void FavoriteUserManager::setDirty() noexcept {
	FavoriteManager::getInstance()->setDirty();
}

void FavoriteUserManager::on(ConnectionManagerListener::UserSet, UserConnection* aUserConnection) noexcept {
	auto user = aUserConnection->getUser();
	if (user->isSet(User::FAVORITE)) {
		auto favoriteUser = getFavoriteUser(user);
		if (favoriteUser && favoriteUser->isSet(FavoriteUser::FLAG_SUPERUSER)) {
			aUserConnection->setUseLimiter(false);
		}
	}
}

void FavoriteUserManager::on(DownloadManagerListener::Tick, const DownloadList& aDownloads, uint64_t aTick) noexcept {
	if (SETTING(FAV_DL_SPEED) == 0) {
		return;
	}

	for (const auto d : aDownloads) {
		auto fstusr = d->getHintedUser();
		auto speed = d->getAverageSpeed();
		if (speed > Util::convertSize(SETTING(FAV_DL_SPEED), Util::KB) && (aTick - d->getStart()) > 7000 && !fstusr.user->isFavorite()) {
			addFavoriteUser(fstusr);
			setUserDescription(fstusr, ("!fast user! (" + Util::toString(speed / 1000) + "KB/s)"));
		}
	}
}

ActionHookResult<MessageHighlightList> FavoriteUserManager::formatFavoriteUsers(const ChatMessagePtr& aMessage, const ActionHookResultGetter<MessageHighlightList>& aResultGetter) noexcept {
	MessageHighlightList highlights;

	{
		RLock l(cs);
		for (const auto& favUser: users | views::values) {
			decltype(auto) nick = favUser.getNick();
			if (nick.empty()) continue;

			size_t start = string::npos;
			size_t pos = 0;
			while ((start = (long)aMessage->getText().find(nick, pos)) != tstring::npos) {
				auto lMyNickEnd = start + nick.size();
				pos = lMyNickEnd;

				highlights.emplace_back(make_shared<MessageHighlight>(start, nick, MessageHighlight::HighlightType::TYPE_USER, MessageHighlight::TAG_FAVORITE));
			}
		}
	}

	return aResultGetter.getData(highlights);
}

ActionHookResult<MessageHighlightList> FavoriteUserManager::onPrivateMessage(const ChatMessagePtr& aMessage, const ActionHookResultGetter<MessageHighlightList>& aResultGetter) noexcept {
	return formatFavoriteUsers(aMessage, aResultGetter);
}

ActionHookResult<MessageHighlightList> FavoriteUserManager::onHubMessage(const ChatMessagePtr& aMessage, const ActionHookResultGetter<MessageHighlightList>& aResultGetter) noexcept {
	return formatFavoriteUsers(aMessage, aResultGetter);
}

ActionHookResult<uint8_t> FavoriteUserManager::onSlotType(const HintedUser& aUser, const ParsedUpload&, const ActionHookResultGetter<uint8_t>& aResultGetter) noexcept {
	auto autoGrant = hasSlot(aUser);
	return aResultGetter.getData(autoGrant ? UserConnection::STDSLOT : UserConnection::NOSLOT);
}

} // namespace dcpp
