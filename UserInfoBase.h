/* 
* Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef USERINFOBASE_H
#define USERINFOBASE_H

#include "forward.h"

#include "User.h"
#include "Util.h"


namespace dcpp
{

class UserInfoBase {
public:
	UserInfoBase() { }
	
	void getList();
	void browseList();
	void getBrowseList();
	void matchQueue();
	void pm();

	void getList(const string& aUrl);
	void browseList(const string& aUrl);
	void getBrowseList(const string& aUrl);
	void matchQueue(const string& aUrl);
	void pm(const string& aUrl);

	void grant();
	void grantTimeless();
	void grantHour();
	void grantDay();
	void grantWeek();
	void ungrant();
	void handleFav();
	void removeAll();
	void connectFav();
	bool hasReservedSlot();
	
	virtual const UserPtr& getUser() const = 0;
	virtual const string& getHubUrl() const = 0;

	static uint8_t getImage(const Identity& identity, const Client* c);
	enum {
		// base icons
		USER_ICON,
		USER_ICON_AWAY,
		USER_ICON_BOT,

		// modifiers
		USER_ICON_MOD_START,
		USER_ICON_PASSIVE = USER_ICON_MOD_START,
		USER_ICON_OP,
		USER_ICON_NOCONNECT,
		//USER_ICON_FAVORITE,

		USER_ICON_LAST
	};
};

} // namespace dcpp

#endif