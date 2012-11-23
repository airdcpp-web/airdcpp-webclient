/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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

#include "stdinc.h"
#include "User.h"

#include "AdcHub.h"
#include "Client.h"
#include "StringTokenizer.h"
#include "FavoriteUser.h"
#include "GeoManager.h"

#include "ClientManager.h"
#include "UserCommand.h"
#include "ResourceManager.h"
#include "FavoriteManager.h"

namespace dcpp {

FastCriticalSection Identity::cs;

OnlineUser::OnlineUser(const UserPtr& ptr, ClientBase& client_, uint32_t sid_) : identity(ptr, sid_), client(client_), isInList(false) { 
}

bool Identity::isTcpActive(const Client* c) const {
	return isTcp4Active(c) || isTcp6Active(c);
}

bool Identity::isTcp4Active(const Client* c) const {
	if(c != NULL && user == ClientManager::getInstance()->getMe()) {
		return c->isActive(); // userlist should display our real mode
	} else {
		return (!user->isSet(User::NMDC)) ?
			!getIp4().empty() && supports(AdcHub::TCP4_FEATURE) :
			!user->isSet(User::PASSIVE);
	}
}

bool Identity::isTcp6Active(const Client* c) const {
	if(c != NULL && user == ClientManager::getInstance()->getMe()) {
		return c->isActive(); // userlist should display our real mode
	} else {
		return !getIp6().empty() && supports(AdcHub::TCP6_FEATURE);
	}
}

bool Identity::isUdpActive() const {
	return isUdp4Active() || isUdp6Active();
}

bool Identity::isUdp4Active() const {
	if(getIp4().empty() || getUdp4Port().empty())
		return false;
	return user->isSet(User::NMDC) ? !user->isSet(User::PASSIVE) : supports(AdcHub::UDP4_FEATURE);
}

bool Identity::isUdp6Active() const {
	if(getIp6().empty() || getUdp6Port().empty())
		return false;
	return user->isSet(User::NMDC) ? false : supports(AdcHub::UDP6_FEATURE);
}

string Identity::getUdpPort() const {
	if(getIp6().empty() || getUdp6Port().empty()) {
		return getUdp4Port();
	}

	return getUdp6Port();
}

string Identity::getIp() const {
	return getIp6().empty() ? getIp4() : getIp6();
}

void Identity::getParams(ParamMap& sm, const string& prefix, bool compatibility) const {
	{
		FastLock l(cs);
		for(auto i = info.begin(); i != info.end(); ++i) {
			sm[prefix + string((char*)(&i->first), 2)] = i->second;
		}
	}
	if(user) {
		sm[prefix + "NI"] = getNick();
		sm[prefix + "SID"] = getSIDString();
		sm[prefix + "CID"] = user->getCID().toBase32();
		sm[prefix + "TAG"] = getTag();
		sm[prefix + "CO"] = getConnection();
		sm[prefix + "DS"] = getDLSpeed();
		sm[prefix + "SSshort"] = Util::formatBytes(get("SS"));

		if(compatibility) {
			if(prefix == "my") {
				sm["mynick"] = getNick();
				sm["mycid"] = user->getCID().toBase32();
			} else {
				sm["nick"] = getNick();
				sm["cid"] = user->getCID().toBase32();
				sm["ip"] = get("I4");
				sm["tag"] = getTag();
				sm["description"] = get("DE");
				sm["email"] = get("EM");
				sm["share"] = get("SS");
				sm["shareshort"] = Util::formatBytes(get("SS"));
				sm["realshareformat"] = Util::formatBytes(get("RS"));
			}
		}
	}
}

bool Identity::isClientType(ClientType ct) const {
	int type = Util::toInt(get("CT"));
	return (type & ct) == ct;
}

string Identity::getTag() const {
	if(!get("TA").empty())
		return get("TA");
	if(get("VE").empty() || get("HN").empty() || get("HR").empty() || get("HO").empty() || get("SL").empty())
		return Util::emptyString;
	return "<" + getApplication() + ",M:" + string(isTcpActive() ? "A" : "P") + 
		",H:" + get("HN") + "/" + get("HR") + "/" + get("HO") + ",S:" + get("SL") + ">";
}

string Identity::getApplication() const {
	auto application = get("AP");
	auto version = get("VE");

	if(version.empty()) {
		return application;
	}

	if(application.empty()) {
		// AP is an extension, so we can't guarantee that the other party supports it, so default to VE.
		return version;
	}

	return application + ' ' + version;
}
const string& Identity::getCountry() const {
	bool v6 = !getIp6().empty();
	return GeoManager::getInstance()->getCountry(v6 ? getIp6() : getIp4(), v6 ? GeoManager::V6 : GeoManager::V4);
}

string Identity::get(const char* name) const {
	FastLock l(cs);
	auto i = info.find(*(short*)name);
	return i == info.end() ? Util::emptyString : i->second;
}

bool Identity::isSet(const char* name) const {
	FastLock l(cs);
	auto i = info.find(*(short*)name);
	return i != info.end();
}


void Identity::set(const char* name, const string& val) {
	FastLock l(cs);
	if(val.empty())
		info.erase(*(short*)name);
	else
		info[*(short*)name] = val;
}

bool Identity::supports(const string& name) const {
	string su = get("SU");
	StringTokenizer<string> st(su, ',');
	for(auto i = st.getTokens().begin(); i != st.getTokens().end(); ++i) {
		if(*i == name)
			return true;
	}
	return false;
}

std::map<string, string> Identity::getInfo() const {
	std::map<string, string> ret;

	FastLock l(cs);
	for(auto i = info.begin(); i != info.end(); ++i) {
		ret[string((char*)(&i->first), 2)] = i->second;
	}

	return ret;
}

void FavoriteUser::update(const OnlineUser& info) { 
	setNick(info.getIdentity().getNick()); 
	setUrl(info.getClient().getHubUrl()); 
}
int OnlineUser::compareItems(const OnlineUser* a, const OnlineUser* b, uint8_t col)  {
	if(col == COLUMN_NICK) {
		bool a_isOp = a->getIdentity().isOp(),
			b_isOp = b->getIdentity().isOp();
		if(a_isOp && !b_isOp)
			return -1;
		if(!a_isOp && b_isOp)
			return 1;
		if(BOOLSETTING(SORT_FAVUSERS_FIRST)) {
			bool a_isFav = FavoriteManager::getInstance()->isFavoriteUser(a->getIdentity().getUser()),
				b_isFav = FavoriteManager::getInstance()->isFavoriteUser(b->getIdentity().getUser());
			if(a_isFav && !b_isFav)
				return -1;
			if(!a_isFav && b_isFav)
				return 1;
		}
		// workaround for faster hub loading
		// lstrcmpiA(a->identity.getNick().c_str(), b->identity.getNick().c_str());
	}
	switch(col) {
		case COLUMN_SHARED:
		case COLUMN_EXACT_SHARED: return compare(a->identity.getBytesShared(), b->identity.getBytesShared());
		case COLUMN_SLOTS: return compare(Util::toInt(a->identity.get("SL")), Util::toInt(b->identity.get("SL")));
		case COLUMN_HUBS: return compare(Util::toInt(a->identity.get("HN"))+Util::toInt(a->identity.get("HR"))+Util::toInt(a->identity.get("HO")), Util::toInt(b->identity.get("HN"))+Util::toInt(b->identity.get("HR"))+Util::toInt(b->identity.get("HO")));
	}
	return Util::DefaultSort(a->getText(col).c_str(), b->getText(col).c_str());
}

tstring OnlineUser::getText(uint8_t col, bool copy /*false*/) const {
	switch(col) {
		case COLUMN_NICK: return Text::toT(identity.getNick());
		case COLUMN_SHARED: return Util::formatBytesW(identity.getBytesShared());
		case COLUMN_EXACT_SHARED: return Util::formatExactSize(identity.getBytesShared());
		case COLUMN_DESCRIPTION: return Text::toT(identity.getDescription());
		case COLUMN_TAG: return Text::toT(identity.getTag());
		case COLUMN_ULSPEED: return identity.get("US").empty() ? Text::toT(identity.getConnection()) : (Text::toT(Util::formatBytes(identity.get("US"))) + _T("/s"));
		case COLUMN_DLSPEED: return identity.get("DS").empty() ? Util::emptyStringT : (Text::toT(Util::formatBytes(identity.get("DS"))) + _T("/s"));
		case COLUMN_IP: {
			string ip = identity.getIp();
			if (!copy) {
				string country = ip.empty() ? Util::emptyString : identity.getCountry();
				if (!country.empty())
					ip = country + " (" + ip + ")";
			}
			return Text::toT(ip);
		}
		case COLUMN_EMAIL: return Text::toT(identity.getEmail());
		case COLUMN_VERSION: return Text::toT(identity.get("CL").empty() ? identity.get("VE") : identity.get("CL"));
		case COLUMN_MODE: return identity.isTcpActive(&getClient()) ? _T("A") : _T("P");
		case COLUMN_HUBS: {
			const tstring hn = Text::toT(identity.get("HN"));
			const tstring hr = Text::toT(identity.get("HR"));
			const tstring ho = Text::toT(identity.get("HO"));
			return (hn.empty() || hr.empty() || ho.empty()) ? Util::emptyStringT : (hn + _T("/") + hr + _T("/") + ho);
		}
		case COLUMN_SLOTS: return Text::toT(identity.get("SL"));
		case COLUMN_CID: return Text::toT(identity.getUser()->getCID().toBase32());
		default: return Util::emptyStringT;
	}
}

tstring old = Util::emptyStringT;
bool OnlineUser::update(int sortCol, const tstring& oldText) {
	bool needsSort = ((identity.get("WO").empty() ? false : true) != identity.isOp());
	
	if(sortCol == -1) {
		isInList = true;
	} else {
		needsSort = needsSort || (oldText != getText(static_cast<uint8_t>(sortCol)));
	}

	return needsSort;
}

const string& OnlineUser::getHubUrl() const { 
	//return HintedUser(getIdentity().getUser(), (&getClient())->getHubUrl());
	return getClient().getHubUrl();
}

uint8_t UserInfoBase::getImage(const Identity& identity, const Client* c) {

	uint8_t image = identity.isBot() ? USER_ICON_BOT : identity.isAway() ? USER_ICON_AWAY : USER_ICON;
	image *= (USER_ICON_LAST - USER_ICON_MOD_START) * (USER_ICON_LAST - USER_ICON_MOD_START);

	if(!identity.isBot() && !identity.isTcpActive())
	{
		image += 1 << (USER_ICON_PASSIVE - USER_ICON_MOD_START);
	}
	/*
	if(identity.getUser()->isSet(User::AIRDCPLUSPLUS)) {
		image += 1 << (USER_ICON_AIRDC - USER_ICON_MOD_START);
	}*/

	if(identity.isOp()) {
		image += 1 << (USER_ICON_OP - USER_ICON_MOD_START);
	}
	return image;
}

} // namespace dcpp