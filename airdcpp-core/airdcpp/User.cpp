/*
 * Copyright (C) 2001-2018 Jacek Sieka, arnetheduck on gmail point com
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

SharedMutex Identity::cs;

OnlineUser::OnlineUser(const UserPtr& ptr, const ClientPtr& client_, uint32_t sid_) : identity(ptr, sid_), client(client_) {
}

OnlineUser::~OnlineUser() noexcept {

}

bool Identity::isTcpActive(const ClientPtr& c) const noexcept {
	return isTcp4Active(c) || isTcp6Active();
}

bool Identity::isTcp4Active(const ClientPtr& c) const noexcept {
	if (user->isSet(User::NMDC)) {
		// NMDC
		return !user->isSet(User::PASSIVE);
	} else {
		// NMDC flag is not set for our own user (and neither the global User::PASSIVE flag can be used here)
		if (c && user == ClientManager::getInstance()->getMe()) {
			return c->isActiveV4();
		}

		// ADC
		return !getIp4().empty() && supports(AdcHub::TCP4_FEATURE);
	}
}

bool Identity::isTcp6Active() const noexcept {
	return !getIp6().empty() && supports(AdcHub::TCP6_FEATURE);
}

bool Identity::isUdpActive() const noexcept {
	return isUdp4Active() || isUdp6Active();
}

bool Identity::isUdp4Active() const noexcept {
	if(getIp4().empty() || getUdp4Port().empty())
		return false;
	return user->isSet(User::NMDC) ? !user->isSet(User::PASSIVE) : supports(AdcHub::UDP4_FEATURE);
}

bool Identity::isUdp6Active() const noexcept {
	if(getIp6().empty() || getUdp6Port().empty())
		return false;
	return user->isSet(User::NMDC) ? false : supports(AdcHub::UDP6_FEATURE);
}

string Identity::getUdpPort() const noexcept {
	if(getIp6().empty() || getUdp6Port().empty()) {
		return getUdp4Port();
	}

	return getUdp6Port();
}

string Identity::getIp() const noexcept {
	return !allowV6Connections() ? getIp4() : getIp6();
}

string Identity::getConnectionString() const noexcept {
	if (user->isNMDC()) {
		return getNmdcConnection();
	} else {
		return Util::toString(getAdcConnectionSpeed(false));
	}
}

int64_t Identity::getAdcConnectionSpeed(bool download) const noexcept {
	return Util::toInt64(download ? get("DS") : get("US"));
}

uint8_t Identity::getSlots() const noexcept {
	return static_cast<uint8_t>(Util::toInt(get("SL")));
}

void Identity::getParams(ParamMap& sm, const string& prefix, bool compatibility) const noexcept {
	{
		RLock l(cs);
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

bool Identity::isClientType(ClientType ct) const noexcept {
	int type = Util::toInt(get("CT"));
	return (type & ct) == ct;
}

string Identity::getTag() const noexcept {
	if(!get("TA").empty())
		return get("TA");
	if(get("VE").empty() || get("HN").empty() || get("HR").empty() || get("HO").empty() || get("SL").empty())
		return Util::emptyString;

	return "<" + getApplication() + ",M:" + getV4ModeString() + getV6ModeString() + 
		",H:" + get("HN") + "/" + get("HR") + "/" + get("HO") + ",S:" + get("SL") + ">";
}

string Identity::getV4ModeString() const noexcept {
	if (!getIp4().empty())
		return isTcp4Active() ? "A" : "P";
	else
		return "-";
}

string Identity::getV6ModeString() const noexcept {
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

Identity& Identity::operator = (const Identity& rhs) {
	WLock l(cs);
	*static_cast<Flags*>(this) = rhs;
	user = rhs.user;
	sid = rhs.sid;
	info = rhs.info;
	connectMode = rhs.connectMode;
	return *this;
}

string Identity::getApplication() const noexcept {
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
string Identity::getCountry() const noexcept {
	bool v6 = !getIp6().empty();
	return GeoManager::getInstance()->getCountry(v6 ? getIp6() : getIp4());
}

string Identity::get(const char* name) const noexcept {
	RLock l(cs);
	auto i = info.find(*(short*)name);
	return i == info.end() ? Util::emptyString : i->second;
}

bool Identity::isSet(const char* name) const noexcept {
	RLock l(cs);
	auto i = info.find(*(short*)name);
	return i != info.end();
}


void Identity::set(const char* name, const string& val) noexcept {
	WLock l(cs);
	if(val.empty())
		info.erase(*(short*)name);
	else
		info[*(short*)name] = val;
}

StringList Identity::getSupports() const noexcept {
	string su = get("SU");
	return StringTokenizer<string>(su, ',').getTokens();
}

bool Identity::supports(const string& name) const noexcept {
	for (const auto& s: getSupports()) {
		if (s == name)
			return true;
	}

	return false;
}

std::map<string, string> Identity::getInfo() const noexcept {
	std::map<string, string> ret;

	RLock l(cs);
	for(const auto& i: info) {
		ret[string((char*)(&i.first), 2)] = i.second;
	}

	return ret;
}

int Identity::getTotalHubCount() const noexcept {
	return Util::toInt(get("HN")) + Util::toInt(get("HR")) + Util::toInt(get("HO"));
}

bool Identity::updateConnectMode(const Identity& me, const Client* aClient) noexcept {
	Mode newMode = MODE_NOCONNECT_IP;
	bool meSupports6 = !me.getIp6().empty();

	if (meSupports6 && !getIp6().empty()) {
		// IPv6? active / NAT-T
		if (isTcp6Active()) {
			newMode = MODE_ACTIVE_V6;
		} else if (me.isTcp6Active() || supports(AdcHub::NAT0_FEATURE)) {
			newMode = MODE_PASSIVE_V6;
		}
	}

	if (!me.getIp4().empty() && !getIp4().empty()) {
		if (isTcp4Active()) {
			newMode = newMode == MODE_ACTIVE_V6 ? MODE_ACTIVE_DUAL : MODE_ACTIVE_V4;
		} else if (newMode == MODE_NOCONNECT_IP && (me.isTcp4Active() || supports(AdcHub::NAT0_FEATURE))) { //passive v4 isn't any better than passive v6
			newMode = MODE_PASSIVE_V4;
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

bool Identity::allowV6Connections() const noexcept {
	return connectMode == MODE_PASSIVE_V6 || connectMode == MODE_ACTIVE_V6 || connectMode == MODE_PASSIVE_V6_UNKNOWN || connectMode == MODE_ACTIVE_DUAL;
}

bool Identity::allowV4Connections() const noexcept {
	return connectMode == MODE_PASSIVE_V4 || connectMode == MODE_ACTIVE_V4 || connectMode == MODE_PASSIVE_V4_UNKNOWN || connectMode == MODE_ACTIVE_DUAL;
}

const string& OnlineUser::getHubUrl() const noexcept {
	return getClient()->getHubUrl();
}

bool OnlineUser::NickSort::operator()(const OnlineUserPtr& left, const OnlineUserPtr& right) const {
	return compare(left->getIdentity().getNick(), right->getIdentity().getNick()) < 0;
}

string OnlineUser::HubName::operator()(const OnlineUserPtr& u) { 
	return u->getClient()->getHubName(); 
}

void User::addQueued(int64_t aBytes) noexcept {
	queued += aBytes;
}

void User::removeQueued(int64_t aBytes) noexcept {
	queued -= aBytes;
	dcassert(queued >= 0);
}

string OnlineUser::getLogPath() const noexcept {
	ParamMap params;
	params["userNI"] = [this] { return getIdentity().getNick(); };
	params["hubNI"] = [this] { return getClient()->getHubName(); };
	params["myNI"] = [this] { return getClient()->getMyNick(); };
	params["userCID"] = [this] { return getUser()->getCID().toBase32(); };
	params["hubURL"] = [this] { return getClient()->getHubUrl(); };

	return LogManager::getInstance()->getPath(getUser(), params);
}

bool OnlineUser::supportsCCPM() const noexcept {
	return getIdentity().supports(AdcHub::CCPM_FEATURE);
}

} // namespace dcpp