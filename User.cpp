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

#include "LogManager.h"

namespace dcpp {

FastCriticalSection Identity::cs;

OnlineUser::OnlineUser(const UserPtr& ptr, ClientBase& client_, uint32_t sid_) : identity(ptr, sid_), client(client_), isInList(false) { 
}

bool Identity::isTcpActive(const Client* c) const {
	return isTcp4Active(c) || isTcp6Active();
}

bool Identity::isTcp4Active(const Client* c) const {
	if (!user->isSet(User::NMDC)) {
		return !getIp4().empty() && supports(AdcHub::TCP4_FEATURE);
	} else {
		//we don't want to use the global passive flag for our own user...
		return c && user == ClientManager::getInstance()->getMe() ? c->isActiveV4() : !user->isSet(User::PASSIVE);
	}
}

bool Identity::isTcp6Active() const {
	return !getIp6().empty() && supports(AdcHub::TCP6_FEATURE);
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
	return !allowV6Connections() ? getIp4() : getIp6();
}

string Identity::getConnectionString() const {
	if (user->isNMDC()) {
		return getNmdcConnection();
	} else {
		return Util::toString(getAdcConnectionSpeed(false));
	}
}

int64_t Identity::getAdcConnectionSpeed(bool download) const {
	auto us = Util::toInt64(download ? get("DS") : get("US"));

	const auto& ver = get("VE");
	if (ver.length() >= 12 && strncmp("AirDC++ ", ver.c_str(), 8) == 0) {
		auto version = Util::toDouble(ver.substr(8, 4));

		//workaround for versions older than 2.40 that use wrong units.....
		if (version < 2.40) {
			us = us / 8;
		}

		//convert MiBit/s to Mbit/s...
		if (version <= 2.45 || (version >= 2.50 && version <= 2.59))
			us = us * 0.9765625 * 0.9765625;
	}

	return us;
}

uint8_t Identity::getSlots() const {
	return static_cast<uint8_t>(Util::toInt(get("SL")));
}

void Identity::getParams(ParamMap& sm, const string& prefix, bool compatibility) const {
	{
		FastLock l(cs);
		for(auto& i: info) {
			sm[prefix + string((char*)(&i.first), 2)] = i.second;
		}
	}
	if(user) {
		sm[prefix + "NI"] = getNick();
		sm[prefix + "SID"] = getSIDString();
		sm[prefix + "CID"] = user->getCID().toBase32();
		sm[prefix + "TAG"] = getTag();
		sm[prefix + "CO"] = getNmdcConnection();
		sm[prefix + "DS"] = getDownloadSpeed();
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

	return "<" + getApplication() + ",M:" + getV4ModeString() + getV6ModeString() + 
		",H:" + get("HN") + "/" + get("HR") + "/" + get("HO") + ",S:" + get("SL") + ">";
}

string Identity::getV4ModeString() const {
	if (!getIp4().empty())
		return isTcp4Active() ? "A" : "P";
	else
		return "-";
}

string Identity::getV6ModeString() const {
	if (!getIp6().empty())
		return isTcp6Active() ? "A" : "P";
	else
		return "-";
}

Identity::Identity() : sid(0), connectMode(MODE_UNDEFINED) { }

Identity::Identity(const UserPtr& ptr, uint32_t aSID) : user(ptr), sid(aSID), connectMode(MODE_UNDEFINED) { }

Identity::Identity(const Identity& rhs) : Flags(), sid(0), connectMode(rhs.getConnectMode()) { 
	*this = rhs;  // Use operator= since we have to lock before reading...
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
	for(auto s: st.getTokens()) {
		if(s == name)
			return true;
	}
	return false;
}

std::map<string, string> Identity::getInfo() const {
	std::map<string, string> ret;

	FastLock l(cs);
	for(auto& i: info) {
		ret[string((char*)(&i.first), 2)] = i.second;
	}

	return ret;
}

int Identity::getTotalHubCount() const {
	return Util::toInt(get("HN")) + Util::toInt(get("HR")) + Util::toInt(get("HO"));
}

bool Identity::updateConnectMode(const Identity& me, const Client* aClient) {
	Mode newMode = MODE_NOCONNECT_IP;
	bool meSupports6 = !me.getIp6().empty();

	if (meSupports6 && !getIp6().empty()) {
		// IPv6? active / NAT-T
		if (isTcp6Active())
			newMode = MODE_ACTIVE_V6;
		else if (me.isTcp6Active() || supports(AdcHub::NAT0_FEATURE))
			newMode = MODE_PASSIVE_V6;
	}

	if ((newMode == MODE_NOCONNECT_IP || newMode == MODE_PASSIVE_V6) && !me.getIp4().empty()) {
		if (!getIp4().empty()) {
			auto isActive = isTcp4Active();
			if (isActive || (newMode == MODE_NOCONNECT_IP && (me.isTcp4Active() || supports(AdcHub::NAT0_FEATURE)))) {
				//passive v4 isn't any better than passive v6
				newMode = isActive ? MODE_ACTIVE_V4 : MODE_PASSIVE_V4;
			}
		}
	}

	if (newMode == MODE_NOCONNECT_IP) {
		// the hub doesn't support hybrid connectivity or we weren't able to authenticate the secondary protocol? we are passive via that protocol in that case
		if (isTcp4Active() && aClient->get(HubSettings::Connection) != SettingsManager::INCOMING_DISABLED) {
			newMode = MODE_ACTIVE_V4;
		} else if (isTcp6Active() && aClient->get(HubSettings::Connection6) != SettingsManager::INCOMING_DISABLED) {
			newMode = MODE_ACTIVE_V6;
		} else if (!me.isTcpActive()) {
			//this user is passive with no NAT-T (or the hub is hiding all IP addresses)
			if (!supports(AdcHub::NAT0_FEATURE) && !aClient->isActive()) {
				newMode = MODE_NOCONNECT_PASSIVE;
			}
		} else {
			//could this user still support the same protocol? can't know for sure
			newMode = meSupports6 ? MODE_PASSIVE_V6_UNKNOWN : MODE_PASSIVE_V4_UNKNOWN;
		}
	}

	if (connectMode != newMode) {
		connectMode = newMode;
		return true;
	}
	return false;
}

bool Identity::allowV6Connections() const {
	return connectMode == MODE_PASSIVE_V6 || connectMode == MODE_ACTIVE_V6 || connectMode == MODE_PASSIVE_V6_UNKNOWN;
}

bool Identity::allowV4Connections() const {
	return connectMode == MODE_PASSIVE_V4 || connectMode == MODE_ACTIVE_V4 || connectMode == MODE_PASSIVE_V4_UNKNOWN;
}

const string& OnlineUser::getHubUrl() const { 
	return getClient().getHubUrl();
}

bool OnlineUser::NickSort::operator()(const OnlineUserPtr& left, const OnlineUserPtr& right) const {
	return compare(left->getIdentity().getNick(), right->getIdentity().getNick()) < 0;
}

string OnlineUser::HubName::operator()(const OnlineUserPtr& u) { 
	return u->getClientBase().getHubName(); 
}

void User::addQueued(int64_t inc) {
	queued += inc;
}

void User::removeQueued(int64_t rm) {
	queued -= rm;
	dcassert(queued >= 0);
}

string OnlineUser::getLogPath() {
	ParamMap params;
	params["userNI"] = [this] { return getIdentity().getNick(); };
	params["hubNI"] = [this] { return getClient().getHubName(); };
	params["myNI"] = [this] { return getClient().getMyNick(); };
	params["userCID"] = [this] { return getUser()->getCID().toBase32(); };
	params["hubURL"] = [this] { return getClient().getHubUrl(); };

	return LogManager::getInstance()->getPath(getUser(), params);
}

uint8_t UserInfoBase::getImage(const Identity& identity, const Client* c) {

	bool bot = identity.isBot() && !identity.getUser()->isSet(User::NMDC);
	uint8_t image = bot ? USER_ICON_BOT : identity.isAway() ? USER_ICON_AWAY : USER_ICON;
	image *= (USER_ICON_LAST - USER_ICON_MOD_START) * (USER_ICON_LAST - USER_ICON_MOD_START);

	if (identity.getUser()->isNMDC()) {
		if(!bot && !identity.isTcpActive(c))
		{
			image += 1 << (USER_ICON_PASSIVE - USER_ICON_MOD_START);
		}
	} else {
		const auto cm = identity.getConnectMode();
		if(!bot && (cm == Identity::MODE_PASSIVE_V6 || cm == Identity::MODE_PASSIVE_V4))
		{
			image += 1 << (USER_ICON_PASSIVE - USER_ICON_MOD_START);
		}

		if(!bot && (cm == Identity::MODE_NOCONNECT_PASSIVE || cm == Identity::MODE_NOCONNECT_IP || cm == Identity::MODE_UNDEFINED))
		{
			image += 1 << (USER_ICON_NOCONNECT - USER_ICON_MOD_START);
		}

		//TODO: add icon for unknown (passive) connectivity
		if(!bot && (cm == Identity::MODE_PASSIVE_V4_UNKNOWN || cm == Identity::MODE_PASSIVE_V6_UNKNOWN))
		{
			image += 1 << (USER_ICON_PASSIVE - USER_ICON_MOD_START);
		}
	}

	if(identity.isOp()) {
		image += 1 << (USER_ICON_OP - USER_ICON_MOD_START);
	}
	return image;
}

#ifdef _WIN32
int OnlineUser::compareItems(const OnlineUser* a, const OnlineUser* b, uint8_t col) {
	if (col == COLUMN_NICK) {
		bool a_isOp = a->getIdentity().isOp(),
			b_isOp = b->getIdentity().isOp();
		if (a_isOp && !b_isOp)
			return -1;
		if (!a_isOp && b_isOp)
			return 1;
		if (SETTING(SORT_FAVUSERS_FIRST)) {
			bool a_isFav = a->getUser()->isFavorite(),
				b_isFav = b->getUser()->isFavorite();

			if (a_isFav && !b_isFav)
				return -1;
			if (!a_isFav && b_isFav)
				return 1;
		}
		// workaround for faster hub loading
		// lstrcmpiA(a->identity.getNick().c_str(), b->identity.getNick().c_str());
	} else if (!a->getUser()->isNMDC()) {
		if (col == COLUMN_ULSPEED) {
			return compare(a->identity.getAdcConnectionSpeed(false), b->identity.getAdcConnectionSpeed(false));
		} else if (col == COLUMN_DLSPEED) {
			return compare(a->identity.getAdcConnectionSpeed(true), b->identity.getAdcConnectionSpeed(true));
		}
	}
	switch (col) {
	case COLUMN_SHARED:
	case COLUMN_EXACT_SHARED: return compare(a->identity.getBytesShared(), b->identity.getBytesShared());
	case COLUMN_SLOTS: return compare(Util::toInt(a->identity.get("SL")), Util::toInt(b->identity.get("SL")));
	case COLUMN_HUBS: return compare(a->identity.getTotalHubCount(), b->identity.getTotalHubCount());
	case COLUMN_FILES: return compare(Util::toInt64(a->identity.get("SF")), Util::toInt64(b->identity.get("SF")));
	}
	return Util::DefaultSort(a->getText(col).c_str(), b->getText(col).c_str());
}

bool OnlineUser::update(int sortCol, const tstring& oldText) {
	bool needsSort = ((identity.get("WO").empty() ? false : true) != identity.isOp());

	if (sortCol == -1) {
		isInList = true;
	} else {
		needsSort = needsSort || (oldText != getText(static_cast<uint8_t>(sortCol)));
	}

	return needsSort;
}

tstring OnlineUser::getText(uint8_t col, bool copy /*false*/) const {
	switch (col) {
	case COLUMN_NICK: return Text::toT(identity.getNick());
	case COLUMN_SHARED: return Util::formatBytesW(identity.getBytesShared());
	case COLUMN_EXACT_SHARED: return Util::formatExactSizeW(identity.getBytesShared());
	case COLUMN_DESCRIPTION: return Text::toT(identity.getDescription());
	case COLUMN_TAG: return Text::toT(identity.getTag());
	case COLUMN_ULSPEED: return identity.get("US").empty() ? Text::toT(identity.getNmdcConnection()) : (Util::formatConnectionSpeedW(identity.getAdcConnectionSpeed(false)));
	case COLUMN_DLSPEED: return identity.get("DS").empty() ? Util::emptyStringT : (Util::formatConnectionSpeedW(identity.getAdcConnectionSpeed(true)));
	case COLUMN_IP4: {
		string ip = identity.getIp4();
		if (!copy) {
			string country = ip.empty() ? Util::emptyString : identity.getCountry();
			if (!country.empty())
				ip = country + " (" + ip + ")";
		}
		return Text::toT(ip);
		}
	case COLUMN_IP6: {
		string ip = identity.getIp6();
		if (!copy) {
			string country = ip.empty() ? Util::emptyString : identity.getCountry();
			if (!country.empty())
				ip = country + " (" + ip + ")";
		}
		return Text::toT(ip);
		}
	case COLUMN_EMAIL: return Text::toT(identity.getEmail());
	case COLUMN_VERSION: return Text::toT(identity.get("CL").empty() ? identity.get("VE") : identity.get("CL"));
	case COLUMN_MODE4: return Text::toT(identity.getV4ModeString());
	case COLUMN_MODE6: return Text::toT(identity.getV6ModeString());
	case COLUMN_FILES: return Text::toT(identity.get("SF"));
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
#endif

} // namespace dcpp