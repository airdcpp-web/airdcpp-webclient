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

#ifndef DCPLUSPLUS_DCPP_USER_COMMAND_MANAGER_H
#define DCPLUSPLUS_DCPP_USER_COMMAND_MANAGER_H

#include "FavoriteManagerListener.h"

#include "CriticalSection.h"
#include "Singleton.h"
#include "UserCommand.h"

namespace dcpp {

class UserCommandManager : public Singleton<UserCommandManager>, public FavoriteManagerListener
{
public:
	UserCommand addUserCommand(int type, int ctx, Flags::MaskType flags, const string& name, const string& command, const string& to, const string& hub) noexcept;
	bool getUserCommand(int cid, UserCommand& uc) noexcept;
	int findUserCommand(const string& aName, const string& aUrl) noexcept;
	bool moveUserCommand(int cid, int pos) noexcept;
	void updateUserCommand(const UserCommand& uc) noexcept;
	void removeUserCommand(int cid) noexcept;
	void removeUserCommand(const string& srv) noexcept;
	void removeHubUserCommands(int ctx, const string& hub) noexcept;

	UserCommand::List getUserCommands() noexcept { RLock l(cs); return userCommands; }
	UserCommand::List getUserCommands(int ctx, const StringList& hub, bool& op) noexcept;

	void setDirty() noexcept;

	void loadUserCommands(SimpleXML& aXml);
	void saveUserCommands(SimpleXML& aXml) const noexcept;
private:
	mutable SharedMutex cs;

	UserCommand::List userCommands;
	int lastId = 0;

	friend class Singleton<UserCommandManager>;
	
	UserCommandManager();
	~UserCommandManager();


	void on(FavoriteManagerListener::Load, SimpleXML& xml) noexcept override;
	void on(FavoriteManagerListener::Save, SimpleXML& xml) noexcept override;
};

} // namespace dcpp

#endif // !defined(FAVORITE_MANAGER_H)