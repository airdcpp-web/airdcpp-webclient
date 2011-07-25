/* 
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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
#include "SettingsManager.h"

#include "forward.h"
#include "Text.h"
#include "Client.h"
#include "ConnectionManager.h"
#include "UploadManager.h"
#include "StringTokenizer.h"
#include "ZUtils.h"

namespace dcpp {

class ClientManager;

class NmdcHub : public Client, private Flags
{
public:
	using Client::send;
	using Client::connect;

	void connect(const OnlineUser& aUser, const string&);

	void hubMessage(const string& aMessage, bool /*thirdPerson*/ = false);
	void privateMessage(const OnlineUserPtr& aUser, const string& aMessage, bool /*thirdPerson*/ = false);
	void sendUserCmd(const UserCommand& command, const StringMap& params);
	void search(int aSizeType, int64_t aSize, int aFileType, const string& aString, const string& aToken, const StringList& aExtList);
	void password(const string& aPass) { send("$MyPass " + fromUtf8(aPass) + "|"); }
	void info(bool force) { myInfo(force); }

	size_t getUserCount() const { Lock l(cs); return users.size(); }
	
	string escape(string const& str) const { return validateMessage(str, false); }
	static string unescape(const string& str) { return validateMessage(str, true); }

	void send(const AdcCommand&) { dcassert(0); }

	static string validateMessage(string tmp, bool reverse);
	void refreshUserList(bool);

	void getUserList(OnlineUserList& list) const {
		Lock l(cs);
		for(NickIter i = users.begin(); i != users.end(); i++) {
				list.push_back(i->second);
		}
	}
	
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

	string lastMyInfo;
	uint64_t lastUpdate;	
	int64_t lastBytesShared;
	int supportFlags;

	typedef list<pair<string, uint64_t> > FloodMap;
	typedef FloodMap::const_iterator FloodIter;
	FloodMap seekers;
	FloodMap flooders;

	NmdcHub(const string& aHubURL, bool secure);
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
	string toUtf8(const string& str) const { return Text::validateUtf8(str) ? str : Text::toUtf8(str, *getEncoding()); }
	string fromUtf8(const string& str) const { return Text::fromUtf8(str, *getEncoding()); }

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

	string checkNick(const string& aNick);

	// TimerManagerListener
	void on(Second, uint64_t aTick) noexcept;

	void on(Connected) noexcept;
	void on(Line, const string& l) noexcept;
	void on(Failed, const string&) noexcept;

};

} // namespace dcpp

#endif // !defined(NMDC_HUB_H)

/**
 * @file
 * $Id: nmdchub.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
