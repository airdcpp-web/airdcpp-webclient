/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_ONLINEUSER_H_
#define DCPLUSPLUS_DCPP_ONLINEUSER_H_

#include <map>

#include <boost/noncopyable.hpp>

#include "forward.h"

#include "CriticalSection.h"
#include "Flags.h"
#include "FastAlloc.h"
#include "GetSet.h"
#include "Util.h"
#include "User.h"
#include "UserInfoBase.h"

namespace dcpp {

class ClientBase;
class NmdcHub;

/** One of possibly many identities of a user, mainly for UI purposes */
class Identity : public Flags {
public:
	enum ClientType {
		CT_BOT = 1,
		CT_REGGED = 2,
		CT_OP = 4,
		CT_SU = 8,
		CT_OWNER = 16,
		CT_HUB = 32,
		CT_HIDDEN = 64
	};
	
	enum StatusFlags {
		NORMAL		= 0x01,
		AWAY		= 0x02,
		TLS			= 0x10,
		NAT			= 0x20,
		AIRDC		= 0x40
	};

	enum Mode {
		MODE_UNDEFINED,
		MODE_ME,
		MODE_NOCONNECT_IP,
		MODE_NOCONNECT_PASSIVE,
		MODE_ACTIVE_V4,
		MODE_ACTIVE_V6,
		MODE_PASSIVE_V4,
		MODE_PASSIVE_V6,
		MODE_PASSIVE_V4_UNKNOWN,
		MODE_PASSIVE_V6_UNKNOWN,
	};

	Identity();
	Identity(const UserPtr& ptr, uint32_t aSID);
	Identity(const Identity& rhs);

	Identity& operator=(const Identity& rhs) {
		FastLock l(cs);
		*static_cast<Flags*>(this) = rhs;
		user = rhs.user;
		sid = rhs.sid;
		info = rhs.info;
		return *this;
	}


#define GETSET_FIELD(n, x) string get##n() const { return get(x); } void set##n(const string& v) { set(x, v); }
	GETSET_FIELD(Nick, "NI")
	GETSET_FIELD(Description, "DE")
	GETSET_FIELD(Ip4, "I4")
	GETSET_FIELD(Ip6, "I6")
	GETSET_FIELD(Udp4Port, "U4")
	GETSET_FIELD(Udp6Port, "U6")
	GETSET_FIELD(Email, "EM")
	GETSET_FIELD(NmdcConnection, "CO")
	GETSET_FIELD(DownloadSpeed, "DS")
	//GETSET_FIELD(UploadSpeed, "US")
	GETSET_FIELD(SharedFiles, "SF")
	GETSET_FIELD(ShareSize, "SS")
#undef GETSET_FIELD
	uint8_t getSlots() const;
	void setBytesShared(const string& bs) { set("SS", bs); }
	int64_t getBytesShared() const { return Util::toInt64(get("SS")); }
	
	void setStatus(const string& st) { set("ST", st); }
	StatusFlags getStatus() const { return static_cast<StatusFlags>(Util::toInt(get("ST"))); }

	void setOp(bool op) { set("OP", op ? "1" : Util::emptyString); }
	void setHub(bool hub) { set("HU", hub ? "1" : Util::emptyString); }
	void setBot(bool bot) { set("BO", bot ? "1" : Util::emptyString); }
	void setHidden(bool hidden) { set("HI", hidden ? "1" : Util::emptyString); }
	string getTag() const;
	string getApplication() const;
	int getTotalHubCount() const;
	const string& getCountry() const;
	bool supports(const string& name) const;
	bool isHub() const { return isClientType(CT_HUB) || isSet("HU"); }
	bool isOp() const { return isClientType(CT_OP) || isClientType(CT_SU) || isClientType(CT_OWNER) || isSet("OP"); }
	bool isRegistered() const { return isClientType(CT_REGGED) || isSet("RG"); }
	bool isHidden() const { return isClientType(CT_HIDDEN) || isSet("HI"); }
	bool isBot() const { return isClientType(CT_BOT) || isSet("BO"); }
	bool isAway() const { return (getStatus() & AWAY) || isSet("AW"); }
	bool isTcpActive(const Client* = NULL) const;
	bool isTcp4Active(const Client* = NULL) const;
	bool isTcp6Active() const;
	bool isUdpActive() const;
	bool isUdp4Active() const;
	bool isUdp6Active() const;
	string getIp() const;
	string getUdpPort() const;
	string getV4ModeString() const;
	string getV6ModeString() const;

	string getConnectionString() const;
	int64_t getAdcConnectionSpeed(bool download) const;

	std::map<string, string> getInfo() const;
	string get(const char* name) const;
	void set(const char* name, const string& val);
	bool isSet(const char* name) const;
	string getSIDString() const { return string((const char*)&sid, 4); }
	
	bool isClientType(ClientType ct) const;
		
	string setCheat(const ClientBase& c, const string& aCheatDescription, bool aBadClient);
	map<string, string> getReport() const;
	string updateClientType(const OnlineUser& ou);
	bool matchProfile(const string& aString, ProfileToken aProfile) const;

	static string getVersion(const string& aExp, string aTag);
	static string splitVersion(const string& aExp, string aTag, size_t part);
	
	void getParams(ParamMap& map, const string& prefix, bool compatibility) const;
	const UserPtr& getUser() const { return user; }
	UserPtr& getUser() { return user; }
	uint32_t getSID() const { return sid; }

	//this caches the mode, taking into account what we support too
	GETSET(Mode, connectMode, ConnectMode);
	bool updateConnectMode(const Identity& me, const Client* aClient);

	bool allowV4Connections() const;
	bool allowV6Connections() const;
private:
	UserPtr user;
	uint32_t sid;

	typedef map<short, string> InfMap;
	InfMap info;

	static FastCriticalSection cs;
};

class OnlineUser :  public FastAlloc<OnlineUser>, public intrusive_ptr_base<OnlineUser>, public UserInfoBase, private boost::noncopyable {
public:
	enum {
		COLUMN_FIRST,
		COLUMN_NICK = COLUMN_FIRST, 
		COLUMN_SHARED, 
		COLUMN_EXACT_SHARED, 
		COLUMN_DESCRIPTION, 
		COLUMN_TAG,
		COLUMN_ULSPEED,
		COLUMN_DLSPEED,
		COLUMN_IP4,
		COLUMN_IP6,
		COLUMN_EMAIL, 
		COLUMN_VERSION, 
		COLUMN_MODE4,
		COLUMN_MODE6,
		COLUMN_FILES, 
		COLUMN_HUBS, 
		COLUMN_SLOTS,
		COLUMN_CID,
		COLUMN_LAST
	};

	struct Hash {
		size_t operator()(const OnlineUserPtr& x) const { return ((size_t)(&(*x)))/sizeof(OnlineUser); }
	};

	struct NickSort {
		bool operator()(const OnlineUserPtr& left, const OnlineUserPtr& right) const;
	};

	struct Nick {
		string operator()(const OnlineUserPtr& u) { return u->getIdentity().getNick(); }
	};

	struct HubName {
		string operator()(const OnlineUserPtr& u);
	};

	class UrlCompare {
	public:
		UrlCompare(const string& aUrl) : url(aUrl) { }
		bool operator()(const OnlineUserPtr& ou) { return ou->getHubUrl() == url; }
	private:
		UrlCompare& operator=(const UrlCompare&);
		const string& url;
	};

	OnlineUser(const UserPtr& ptr, ClientBase& client_, uint32_t sid_);
	virtual ~OnlineUser() noexcept { }

	operator UserPtr&() { return getUser(); }
	operator const UserPtr&() const { return getUser(); }

	UserPtr& getUser() { return getIdentity().getUser(); }
	const UserPtr& getUser() const { return getIdentity().getUser(); }
	const string& getHubUrl() const;
	Identity& getIdentity() { return identity; }
	Client& getClient() { return (Client&)client; }
	const Client& getClient() const { return (const Client&)client; }
	
	ClientBase& getClientBase() { return client; }	
	const ClientBase& getClientBase() const { return client; }

	/* UserInfo */
	uint8_t getImageIndex() const { return UserInfoBase::getImage(identity, &getClient()); }
	bool isHidden() const { return identity.isHidden(); }

#ifdef _WIN32
	static int compareItems(const OnlineUser* a, const OnlineUser* b, uint8_t col);
	bool update(int sortCol, const tstring& oldText = Util::emptyStringT);
	tstring getText(uint8_t col, bool copy = false) const;
#endif

	string getLogPath();

	bool isInList;
	GETSET(Identity, identity, Identity);
private:

	ClientBase& client;
};

}

#endif /* ONLINEUSER_H_ */
