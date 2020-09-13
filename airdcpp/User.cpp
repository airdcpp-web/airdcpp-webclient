/*
 * Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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

bool Identity::hasActiveTcpConnectivity(const ClientPtr& c) const noexcept {
	if (user == ClientManager::getInstance()->getMe()) {
		return isTcp4Active(c) || isTcp6Active();
	}

	return isActiveMode(tcpConnectMode);
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

string Identity::getTcpConnectIp() const noexcept {
	return !allowV6Connections(tcpConnectMode) ? getIp4() : getIp6();
}

string Identity::getUdpIp() const noexcept {
	return !allowV6Connections(udpConnectMode) ? getIp4() : getIp6();
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

Identity::Identity() : sid(0) { }

Identity::Identity(const UserPtr& ptr, uint32_t aSID) : user(ptr), sid(aSID) { }

Identity::Identity(const Identity& rhs) : Flags(), sid(0) { 
	*this = rhs;  // Use operator= since we have to lock before reading...
}

Identity& Identity::operator = (const Identity& rhs) {
	WLock l(cs);
	*static_cast<Flags*>(this) = rhs;
	user = rhs.user;
	sid = rhs.sid;
	info = rhs.info;
	tcpConnectMode = rhs.tcpConnectMode;
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

Identity::Mode Identity::detectConnectMode(const Identity& aMe, const Identity& aOther, bool aMeActive4, bool aMeActive6, bool aOtherActive4, bool aOtherActive6, bool aNatTravelsal, const Client* aClient) noexcept {
	auto mode = MODE_NOCONNECT_IP;

	if (!aMe.getIp6().empty() && !aOther.getIp6().empty()) {
		// IPv6? active / NAT-T
		if (aOtherActive6) {
			mode = MODE_ACTIVE_V6;
		} else if (aMeActive6 || aNatTravelsal) {
			mode = MODE_PASSIVE_V6;
		}
	}

	if (!aMe.getIp4().empty() && !aOther.getIp4().empty()) {
		if (aOtherActive4) {
			mode = mode == MODE_ACTIVE_V6 ? MODE_ACTIVE_DUAL : MODE_ACTIVE_V4;
		} else if (mode == MODE_NOCONNECT_IP && (aMeActive4 || aNatTravelsal)) { //passive v4 isn't any better than passive v6
			mode = MODE_PASSIVE_V4;
		}
	}

	if (mode == MODE_NOCONNECT_IP) {
		// The hub doesn't support hybrid connectivity or we weren't able to authenticate the secondary protocol? We are passive via that protocol in that case
		if (aOtherActive4 && aClient->get(HubSettings::Connection) != SettingsManager::INCOMING_DISABLED) {
			mode = MODE_ACTIVE_V4;
		} else if (aOtherActive6 && aClient->get(HubSettings::Connection6) != SettingsManager::INCOMING_DISABLED) {
			mode = MODE_ACTIVE_V6;
		} else if (!aMeActive4 && !aMeActive6) {
			// Other user is passive with no NAT-T (or the hub is hiding all IP addresses)
			if (!aNatTravelsal && !aClient->isActive()) {
				mode = MODE_NOCONNECT_PASSIVE;
			}
		} else {
			// Could this user still support the same protocol? Can't know for sure
			mode = !aMe.getIp6().empty() ? MODE_PASSIVE_V6_UNKNOWN : MODE_PASSIVE_V4_UNKNOWN;
		}
	}

	return mode;
}

Identity::Mode Identity::detectConnectModeTcp(const Identity& aMe, const Identity& aOther, const Client* aClient) noexcept {
	return detectConnectMode(aMe, aOther, aMe.isTcp4Active(), aMe.isTcp6Active(), aOther.isTcp4Active(), aOther.isTcp6Active(), aOther.supports(AdcHub::NAT0_FEATURE), aClient);
}

Identity::Mode Identity::detectConnectModeUdp(const Identity& aMe, const Identity& aOther, const Client* aClient) noexcept {
	return detectConnectMode(aMe, aOther, aMe.isUdp4Active(), aMe.isUdp6Active(), aOther.isUdp4Active(), aOther.isUdp6Active(), false, aClient);
}

bool Identity::updateConnectMode(const Identity& me, const Client* aClient) noexcept {
	bool updated = false;

	{
		auto newModeTcp = detectConnectModeTcp(me, *this, aClient);
		if (tcpConnectMode != newModeTcp) {
			tcpConnectMode = newModeTcp;
			updated = true;
		}
	}

	{
		auto newModeUdp = detectConnectModeUdp(me, *this, aClient);
		if (udpConnectMode != newModeUdp) {
			udpConnectMode = newModeUdp;
			updated = true;
		}
	}

	return updated;
}


bool Identity::allowV4Connections(Mode aConnectMode) noexcept {
	return aConnectMode == MODE_PASSIVE_V4 || aConnectMode == MODE_ACTIVE_V4 || aConnectMode == MODE_PASSIVE_V4_UNKNOWN || aConnectMode == MODE_ACTIVE_DUAL;
}

bool Identity::allowV6Connections(Mode aConnectMode) noexcept {
	return aConnectMode == MODE_PASSIVE_V6 || aConnectMode == MODE_ACTIVE_V6 || aConnectMode == MODE_PASSIVE_V6_UNKNOWN || aConnectMode == MODE_ACTIVE_DUAL;
}

bool Identity::isActiveMode(Mode aConnectMode) noexcept {
	return aConnectMode == MODE_ACTIVE_V6 || aConnectMode == MODE_ACTIVE_V4 || aConnectMode == MODE_ACTIVE_DUAL;
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