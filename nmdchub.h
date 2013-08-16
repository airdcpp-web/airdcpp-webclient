/* 
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_NMDC_HUB_H
#define DCPLUSPLUS_DCPP_NMDC_HUB_H

#include "TimerManager.h"

#include "forward.h"
#include "Client.h"
#include "Search.h"

namespace dcpp {

class ClientManager;

class NmdcHub : public Client, private Flags
{
public:
	using Client::send;
	using Client::connect;

	int connect(const OnlineUser& aUser, const string& token, string& lastError_);

	bool hubMessage(const string& aMessage, string& error_, bool /*thirdPerson*/ = false);
	bool privateMessage(const OnlineUserPtr& aUser, const string& aMessage, string& error_, bool /*thirdPerson*/ = false);
	void sendUserCmd(const UserCommand& command, const ParamMap& params);
	void search(const SearchPtr& aSearch);
	void password(const string& aPass) { send("$MyPass " + fromUtf8(aPass) + "|"); }
	void infoImpl() { myInfo(false); }

	size_t getUserCount() const;
	
	string escape(string const& str) const { return validateMessage(str, false); }
	static string unescape(const string& str) { return validateMessage(str, true); }

	bool send(const AdcCommand&) { dcassert(0); return false; }

	static string validateMessage(string tmp, bool reverse);
	void refreshUserList(bool);

	void getUserList(OnlineUserList& list) const;
private:
	friend class ClientManager;
	enum SupportFlags {
		SUPPORTS_USERCOMMAND	= 0x01,
		SUPPORTS_NOGETINFO		= 0x02,
		SUPPORTS_USERIP2		= 0x04
	};	

	mutable CriticalSection cs;

	typedef unordered_map<string, OnlineUser*, noCaseStringHash, noCaseStringEq> NickMap;
	typedef NickMap::const_iterator NickIter;

	NickMap users;

	string localIp;
	string lastMyInfo;
	uint64_t lastUpdate;	
	int64_t lastBytesShared;
	int supportFlags;

	typedef list<pair<string, uint64_t> > FloodMap;
	typedef FloodMap::const_iterator FloodIter;
	FloodMap seekers;
	FloodMap flooders;

	NmdcHub(const string& aHubURL);
	~NmdcHub();

	// Dummy
	NmdcHub(const NmdcHub&);
	NmdcHub& operator=(const NmdcHub&);

	void clearUsers();
	void onLine(const string& aLine) noexcept;

	OnlineUser& getUser(const string& aNick);
	OnlineUserPtr findUser(const string& aNick) const;
	void putUser(const string& aNick);
	
	// don't convert to UTF-8 if string is already in this encoding
	string toUtf8(const string& str) const;
	string fromUtf8(const string& str) const;

	void privateMessage(const string& nick, const string& aMessage, bool thirdPerson);
	void validateNick(const string& aNick) { send("$ValidateNick " + fromUtf8(aNick) + "|"); }
	void key(const string& aKey) { send("$Key " + aKey + "|"); }
	void version() { send("$Version 1,0091|"); }
	void getNickList() { send("$GetNickList|"); }
	void connectToMe(const OnlineUser& aUser);
	void revConnectToMe(const OnlineUser& aUser);
	void myInfo(bool alwaysSend);
	void supports(const StringList& feat);
	void clearFlooders(uint64_t tick);

	void updateFromTag(Identity& id, const string& tag);
	void refreshLocalIp() noexcept;

	string checkNick(const string& aNick);
	virtual bool v4only() const { return true; }

	// TimerManagerListener
	virtual void on(Second, uint64_t aTick) noexcept;
	virtual void on(Minute, uint64_t aTick) noexcept;

	void on(Connected) noexcept;
	void on(Line, const string& l) noexcept;
	void on(Failed, const string&) noexcept;
};

} // namespace dcpp

#endif // !defined(NMDC_HUB_H)