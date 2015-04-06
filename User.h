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

#ifndef DCPLUSPLUS_DCPP_USER_H
#define DCPLUSPLUS_DCPP_USER_H

#include "forward.h"
#include "Pointer.h"
#include "CID.h"
#include "FastAlloc.h"
#include "Flags.h"
#include "GetSet.h"

namespace dcpp {

/** A user connected to one or more hubs. */
class User : public FastAlloc<User>, public intrusive_ptr_base<User>, public Flags, private boost::noncopyable
{
public:
	/** Each flag is set if it's true in at least one hub */
	enum UserFlags {
		ONLINE					= 0x01,
		AIRDCPLUSPLUS			= 0x02,
		PASSIVE					= 0x04,
		NMDC					= 0x08,
		BOT						= 0x10,
		TLS						= 0x20,	//< Client supports TLS
		OLD_CLIENT				= 0x40, //< Can't download - old client
		NO_ADC_1_0_PROTOCOL		= 0x80,	//< Doesn't support "ADC/1.0" (dc++ <=0.703)
		NO_ADCS_0_10_PROTOCOL	= 0x100,	//< Doesn't support "ADCS/0.10"
		NAT_TRAVERSAL			= 0x200,	//< Client supports NAT Traversal
		FAVORITE				= 0x400,
		ASCH					= 0x800,
		IGNORED					= 0x1000
	};

	struct Hash {
		size_t operator()(const UserPtr& x) const { return ((size_t)(&(*x)))/sizeof(User); }
	};

	User(const CID& aCID) : cid(aCID) {}

	~User() { }

	const CID& getCID() const { return cid; }
	operator const CID&() const { return cid; }

	bool isOnline() const { return isSet(ONLINE); }
	bool isNMDC() const { return isSet(NMDC); }
	bool isFavorite() const { return isSet(FAVORITE); }
	bool isIgnored() const { return isSet(IGNORED); }

	struct UserHubInfo {
		UserHubInfo(const string& aHubUrl, const string& aHubName, int64_t aShared) : hubName(aHubName), hubUrl(aHubUrl), shared(aShared) { }

		string hubName;
		string hubUrl;
		int64_t shared;
	};
	typedef vector<UserHubInfo> UserInfoList;

	void addQueued(int64_t inc);
	void removeQueued(int64_t inc);
	int64_t getQueued() const { return queued; }

	IGETSET(int64_t, speed, Speed, 0);
private:
	int64_t queued = 0;
	const CID cid;
};

} // namespace dcpp

#endif // !defined(USER_H)