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

#include "stdinc.h"
#include <airdcpp/connectivity/ConnectivityManager.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/connection/ConnectionManager.h>
#include <airdcpp/DCPlusPlus.h>
#include <airdcpp/favorites/FavoriteManager.h>
#include <airdcpp/core/header/format.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/connectivity/MappingManager.h>
#include <airdcpp/util/NetworkUtil.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/search/SearchManager.h>
#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/core/version.h>

namespace dcpp {

const SettingsManager::SettingKeyList ConnectivityManager::commonIncomingSettings = { 
	SettingsManager::TCP_PORT, SettingsManager::UDP_PORT, SettingsManager::TLS_PORT, SettingsManager::MAPPER 
};

const SettingsManager::SettingKeyList ConnectivityManager::incomingV4Settings = {
	SettingsManager::INCOMING_CONNECTIONS, SettingsManager::BIND_ADDRESS, SettingsManager::AUTO_DETECT_CONNECTION,
};

const SettingsManager::SettingKeyList ConnectivityManager::incomingV6Settings = {
	SettingsManager::INCOMING_CONNECTIONS6, SettingsManager::BIND_ADDRESS6, SettingsManager::AUTO_DETECT_CONNECTION6,
};

ConnectivityManager::ConnectivityManager() : mapperV6(true), mapperV4(false) { }

void ConnectivityManager::startup(StartupLoader& aLoader) noexcept {
	try {
		ConnectivityManager::getInstance()->setup(true, true);
	} catch (const Exception& e) {
		aLoader.messageF(STRING_F(PORT_BYSY, e.getError()), false, true);
	}

	if (CONNSETTING(OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SOCKS5) {
		try {
			Socket::socksUpdated();
		} catch (const SocketException& e) {
			aLoader.messageF(e.getError(), false, true);
		}
	}

	SettingsManager::SettingKeyList outgoingSettings = {
		SettingsManager::OUTGOING_CONNECTIONS,
		SettingsManager::SOCKS_SERVER, SettingsManager::SOCKS_PORT, SettingsManager::SOCKS_USER, SettingsManager::SOCKS_PASSWORD
	};

	auto incomingSettings = commonIncomingSettings;
	Util::concatenate(incomingSettings, incomingV4Settings);
	Util::concatenate(incomingSettings, incomingV6Settings);

	SettingsManager::getInstance()->registerChangeHandler(incomingSettings, onIncomingSettingsChanged);
	SettingsManager::getInstance()->registerChangeHandler(outgoingSettings, onProxySettingsChanged);
}

void ConnectivityManager::onIncomingSettingsChanged(const MessageCallback& errorF, const SettingsManager::SettingKeyList& aSettings) {
	auto commonChanged = Util::hasCommonElements(aSettings, commonIncomingSettings);
	auto v4Changed = commonChanged || Util::hasCommonElements(aSettings, incomingV4Settings);
	auto v6Changed = commonChanged || Util::hasCommonElements(aSettings, incomingV6Settings);

	try {
		ConnectivityManager::getInstance()->setup(v4Changed, v6Changed);
	} catch (const Exception& e) {
		errorF(STRING_F(PORT_BYSY, e.getError()));
	}
}

void ConnectivityManager::onProxySettingsChanged(const MessageCallback& errorF, const SettingsManager::SettingKeyList&) noexcept {
	try {
		Socket::socksUpdated();
	} catch (const SocketException& e) {
		errorF(e.getError());
	}
}

bool ConnectivityManager::get(SettingsManager::BoolSetting setting) const {
	if(SETTING(AUTO_DETECT_CONNECTION) || SETTING(AUTO_DETECT_CONNECTION6)) {
		RLock l(cs);
		auto i = autoSettings.find(setting);
		if(i != autoSettings.end()) {
			return boost::get<bool>(i->second);
		}
	}
	return SettingsManager::getInstance()->get(setting);
}

int ConnectivityManager::get(SettingsManager::IntSetting setting) const {
	if(SETTING(AUTO_DETECT_CONNECTION) || SETTING(AUTO_DETECT_CONNECTION6)) {
		RLock l(cs);
		auto i = autoSettings.find(setting);
		if(i != autoSettings.end()) {
			return boost::get<int>(i->second);
		}
	}
	return SettingsManager::getInstance()->get(setting);
}

const string& ConnectivityManager::get(SettingsManager::StrSetting setting) const {
	if(SETTING(AUTO_DETECT_CONNECTION) || SETTING(AUTO_DETECT_CONNECTION6)) {
		RLock l(cs);
		auto i = autoSettings.find(setting);
		if(i != autoSettings.end()) {
			return boost::get<string>(i->second);
		}
	}
	return SettingsManager::getInstance()->get(setting);
}

void ConnectivityManager::set(SettingsManager::StrSetting setting, const string& str) {
	if(SETTING(AUTO_DETECT_CONNECTION) || SETTING(AUTO_DETECT_CONNECTION6)) {
		WLock l(cs);
		autoSettings[setting] = str;
	} else {
		SettingsManager::getInstance()->set(setting, str);
	}
}

void ConnectivityManager::clearAutoSettings(bool v6, bool resetDefaults) {
	int settings6[] = { SettingsManager::EXTERNAL_IP6, SettingsManager::BIND_ADDRESS6, 
		SettingsManager::NO_IP_OVERRIDE6, SettingsManager::INCOMING_CONNECTIONS6 };

	int settings4[] = { SettingsManager::EXTERNAL_IP, SettingsManager::NO_IP_OVERRIDE,
		SettingsManager::BIND_ADDRESS, SettingsManager::INCOMING_CONNECTIONS };

	int portSettings[] = { SettingsManager::TCP_PORT, SettingsManager::UDP_PORT,
		SettingsManager::TLS_PORT };


	WLock l(cs);

	//erase the old settings first
	for(const auto setting: v6 ? settings6 : settings4) {
		autoSettings.erase(setting);
	}


	if (resetDefaults) {
		for(const auto setting: v6 ? settings6 : settings4) {
			if(setting >= SettingsManager::STR_FIRST && setting < SettingsManager::STR_LAST) {
				autoSettings[setting] = SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::StrSetting>(setting));
			} else if(setting >= SettingsManager::INT_FIRST && setting < SettingsManager::INT_LAST) {
				autoSettings[setting] = SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::IntSetting>(setting));
			} else if(setting >= SettingsManager::BOOL_FIRST && setting < SettingsManager::BOOL_LAST) {
				autoSettings[setting] = SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::BoolSetting>(setting));
			} else {
				dcassert(0);
			}
		}
	}


	if ((!SETTING(AUTO_DETECT_CONNECTION) && SETTING(INCOMING_CONNECTIONS) != SettingsManager::INCOMING_DISABLED) || (!SETTING(AUTO_DETECT_CONNECTION6) && SETTING(INCOMING_CONNECTIONS6) != SettingsManager::INCOMING_DISABLED)) {
		//we must prefer the configured port instead of default now...
		for(const auto setting: portSettings)
			autoSettings[setting] = SettingsManager::getInstance()->get(static_cast<SettingsManager::IntSetting>(setting));
	} else if (resetDefaults) {
		for(const auto setting: portSettings)
			autoSettings[setting] = SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::IntSetting>(setting));
	}
}

void ConnectivityManager::detectConnection() {
	if (isRunning()) {
		return;
	}

	bool detectV4 = false;
	if (SETTING(AUTO_DETECT_CONNECTION) && SETTING(INCOMING_CONNECTIONS) != SettingsManager::INCOMING_DISABLED) {
		detectV4 = true;
		runningV4 = true;
	}

	bool detectV6 = false;
	if (SETTING(AUTO_DETECT_CONNECTION6) && SETTING(INCOMING_CONNECTIONS6) != SettingsManager::INCOMING_DISABLED) {
		detectV6 = true;
		runningV6 = true;
	}

	if (!detectV6 && !detectV4) {
		return;
	}

	if (detectV4) {
		statusV4.clear();
		fire(ConnectivityManagerListener::Started(), false);
	}

	if (detectV6) {
		statusV6.clear();
		fire(ConnectivityManagerListener::Started(), true);
	}

	if (detectV4 && mapperV4.getOpened()) {
		mapperV4.close();
	}

	if (detectV6 && mapperV6.getOpened()) {
		mapperV6.close();
	}

	disconnect();

	// restore auto settings to their default value.
	if (detectV6)
		clearAutoSettings(true, true);
	if (detectV4)
		clearAutoSettings(false, true);

	log(STRING(CONN_DETERMINING), LogMessage::SEV_INFO, TYPE_BOTH);

	try {
		listen();
	} catch(const Exception& e) {
		{
			WLock l(cs);
			autoSettings[SettingsManager::INCOMING_CONNECTIONS] = SettingsManager::INCOMING_PASSIVE;
			autoSettings[SettingsManager::INCOMING_CONNECTIONS6] = SettingsManager::INCOMING_PASSIVE;
		}

		log(STRING_F(CONN_PORT_X_FAILED, e.getError()), LogMessage::SEV_ERROR, TYPE_NORMAL);
		fire(ConnectivityManagerListener::Finished(), false, true);
		fire(ConnectivityManagerListener::Finished(), true, true);
		if (detectV4)
			runningV4 = false;
		if (detectV6)
			runningV6 = false;
		return;
	}

	autoDetectedV4 = detectV4;
	autoDetectedV6 = detectV6;

	if (detectV4) {
		if (NetworkUtil::isPublicIp(NetworkUtil::getLocalIp(false), false)) {
			// Direct connection
			{
				WLock l(cs);
				autoSettings[SettingsManager::INCOMING_CONNECTIONS] = SettingsManager::INCOMING_ACTIVE;
			}

			log(STRING(CONN_DIRECT_DETECTED), LogMessage::SEV_INFO, TYPE_V4);
			fire(ConnectivityManagerListener::Finished(), false, false);
			runningV4 = false;
			detectV4 = false;
		} else {
			// Private IP, enable UPNP
			WLock l(cs);
			autoSettings[SettingsManager::INCOMING_CONNECTIONS] = SettingsManager::INCOMING_ACTIVE_UPNP;
		}
	}

	if (detectV6) {
		if (NetworkUtil::isPublicIp(NetworkUtil::getLocalIp(true), true)) {
			// Direct connection
			{
				WLock l(cs);
				autoSettings[SettingsManager::INCOMING_CONNECTIONS6] = SettingsManager::INCOMING_ACTIVE;
			}

			log(STRING(CONN_DIRECT_DETECTED), LogMessage::SEV_INFO, TYPE_V6);
		} else {
			// Disable IPv6 connectivity if no public IP address is available
			{
				WLock l(cs);
				autoSettings[SettingsManager::INCOMING_CONNECTIONS6] = SettingsManager::INCOMING_DISABLED;
			}

			log(STRING(IPV6_NO_PUBLIC_IP), LogMessage::SEV_INFO, TYPE_V6);
		}

		fire(ConnectivityManagerListener::Finished(), true, false);
		runningV6 = false;
		detectV6 = false;
	}

	if (!detectV6 && !detectV4)
		return;

	log(STRING(CONN_NAT_DETECTED), LogMessage::SEV_INFO, (detectV4 && detectV6 ? TYPE_BOTH : detectV4 ? TYPE_V4 : TYPE_V6));

	// UPNP mapping is not being used with v6 for now
	//if (detectV6)
	//	startMapping(true);
	if (detectV4)
		startMapping(false);
}

void ConnectivityManager::setup(bool v4SettingsChanged, bool v6SettingsChanged) {
	auto settingsChanged = v4SettingsChanged || v6SettingsChanged;

	bool autoDetect4 = SETTING(AUTO_DETECT_CONNECTION) && SETTING(INCOMING_CONNECTIONS) != SettingsManager::INCOMING_DISABLED;
	bool autoDetect6 = SETTING(AUTO_DETECT_CONNECTION6) && SETTING(INCOMING_CONNECTIONS6) != SettingsManager::INCOMING_DISABLED;

	// whether automatic detection is enabled.
	auto autoDetect = autoDetect4 || autoDetect6;

	// whether automatic detection has run before.
	auto autoDetected = autoDetectedV4 || autoDetectedV6;

	if (v4SettingsChanged || (autoDetectedV4 && !autoDetect4)) {
		mapperV4.close();
		autoDetectedV4 = false;
	}

	if (v6SettingsChanged || (autoDetectedV6 && !autoDetect6)) {
		mapperV6.close();
		autoDetectedV6 = false;
	}


	if (!autoDetect6)
		clearAutoSettings(true, false);
		
	if (!autoDetect4) {
		clearAutoSettings(false, false);
	}

	if (autoDetect) {
		if ((!autoDetectedV4 && autoDetect4) || (!autoDetectedV6 && autoDetect6) || autoSettings.empty()) {
			detectConnection();
			autoDetect = true;
		}
	}

	// reset listening connections when:
	// - auto-detection is disabled.
	// - settings have changed.
	if (!autoDetect && (autoDetected || settingsChanged)) {
		startSocket();
	}

	if(!autoDetect4 && SETTING(INCOMING_CONNECTIONS) == SettingsManager::INCOMING_ACTIVE_UPNP && !runningV4) // previous mappings had failed; try again
		startMapping(false);

	if(!autoDetect6 && SETTING(INCOMING_CONNECTIONS6) == SettingsManager::INCOMING_ACTIVE_UPNP && !runningV6) // previous mappings had failed; try again
		startMapping(true);
}

void ConnectivityManager::close() {
	mapperV4.close();
	mapperV6.close();
}

void ConnectivityManager::editAutoSettings() {
	SettingsManager::getInstance()->set(SettingsManager::AUTO_DETECT_CONNECTION, false);

	auto sm = SettingsManager::getInstance();
	for(auto i = autoSettings.cbegin(), iend = autoSettings.cend(); i != iend; ++i) {
		if(i->first >= SettingsManager::STR_FIRST && i->first < SettingsManager::STR_LAST) {
			sm->set(static_cast<SettingsManager::StrSetting>(i->first), boost::get<string>(i->second));
		} else if(i->first >= SettingsManager::INT_FIRST && i->first < SettingsManager::INT_LAST) {
			sm->set(static_cast<SettingsManager::IntSetting>(i->first), boost::get<int>(i->second));
		}
	}
	autoSettings.clear();

	fire(ConnectivityManagerListener::SettingChanged());
}

string ConnectivityManager::getInformation() const {
	if(isRunning()) {
		return "Connectivity settings are being configured; try again later";
	}

	string autoStatusV4 = ok(false) ? str(boost::format("enabled - %1%") % getStatus(false)) : "disabled";
	string autoStatusV6 = ok(true) ? str(boost::format("enabled - %1%") % getStatus(true)) : "disabled";

	auto getMode = [&](bool v6) -> string { 
		switch(v6 ? CONNSETTING(INCOMING_CONNECTIONS6) : CONNSETTING(INCOMING_CONNECTIONS)) {
		case SettingsManager::INCOMING_ACTIVE:
			{
				return "Direct connection to the Internet (no router or manual router configuration)";
				break;
			}
		case SettingsManager::INCOMING_ACTIVE_UPNP:
			{
				return str(boost::format("Active mode behind a router that %1% can configure; port mapping status: %2%") % APPNAME % (v6 ? mapperV6.getStatus() : mapperV4.getStatus()));
				break;
			}
		case SettingsManager::INCOMING_PASSIVE:
			{
				return "Passive mode";
				break;
			}
		default:
			return "Disabled";
		}
	};

	auto field = [](const string& s) { return s.empty() ? "undefined" : s; };

	return str(boost::format(
		"Connectivity information:\n\n"
		"Automatic connectivity setup (v4) is: %1%\n\n"
		"Automatic connectivity setup (v6) is: %2%\n\n"
		"\tMode (v4): %3%\n"
		"\tMode (v6): %4%\n"
		"\tExternal IP (v4): %5%\n"
		"\tExternal IP (v6): %6%\n"
		"\tBound interface (v4): %7%\n"
		"\tBound interface (v6): %8%\n"
		"\tTransfer port: %9%\n"
		"\tSearch port: %11%\n"
		"\tEncrypted transfer port: %10%") % autoStatusV4 % autoStatusV6 % getMode(false) % getMode(true) %
		field(CONNSETTING(EXTERNAL_IP)) % field(CONNSETTING(EXTERNAL_IP6)) %
		field(CONNSETTING(BIND_ADDRESS)) % field(CONNSETTING(BIND_ADDRESS6)) %
		field(ConnectionManager::getInstance()->getPort()) % field(ConnectionManager::getInstance()->getSecurePort()) %
		field(SearchManager::getInstance()->getPort()));
}

void ConnectivityManager::startMapping(bool v6) {
	if (v6) {
		runningV6 = true;
		if(!mapperV6.open()) {
			runningV6 = false;
		}
	} else {
		runningV4 = true;
		if(!mapperV4.open()) {
			runningV4 = false;
		}
	}
}

void ConnectivityManager::mappingFinished(const string& mapper, bool v6) {
	if((SETTING(AUTO_DETECT_CONNECTION) && !v6) || (SETTING(AUTO_DETECT_CONNECTION6) && v6)) {
		if(mapper.empty()) {
			//disconnect();
			{
				WLock l(cs);
				autoSettings[v6 ? SettingsManager::INCOMING_CONNECTIONS6 : SettingsManager::INCOMING_CONNECTIONS] = SettingsManager::INCOMING_PASSIVE;
			}
			log(STRING(CONN_ACTIVE_FAILED), LogMessage::SEV_WARNING, v6 ? TYPE_V6 : TYPE_V4);
		} else {
			SettingsManager::getInstance()->set(SettingsManager::MAPPER, mapper);
		}
		fire(ConnectivityManagerListener::Finished(), v6, mapper.empty());
	}

	if (v6)
		runningV6 = false;
	else
		runningV4 = false;
}

void ConnectivityManager::log(const string& aMessage, LogMessage::Severity sev, LogType aType) {
	if (aType == TYPE_NORMAL) {
		LogManager::getInstance()->message(aMessage, sev, STRING(CONNECTIVITY));
	} else {
		string proto;
		if (aType == TYPE_BOTH && runningV4 && runningV6) {
			statusV6 = aMessage;
			statusV4 = aMessage;
			proto = "IPv4 & IPv6";
		} else if (aType == TYPE_V4 || (aType == TYPE_BOTH && runningV4)) {
			proto = "IPv4";
			statusV4 = aMessage;
		} else if (aType == TYPE_V6 || (aType == TYPE_BOTH && runningV6)) {
			proto = "IPv6";
			statusV6 = aMessage;
		}

		LogManager::getInstance()->message(aMessage, sev, STRING(CONNECTIVITY) + " (" + proto + ")");
		fire(ConnectivityManagerListener::Message(), proto + ": " + aMessage);
	}
}

const string& ConnectivityManager::getStatus(bool v6) const { 
	return v6 ? statusV6 : statusV4; 
}

StringList ConnectivityManager::getMappers(bool v6) const {
	if (v6) {
		return mapperV6.getMappers();
	} else {
		return mapperV4.getMappers();
	}
}

bool ConnectivityManager::isActive() const noexcept {
	if (CONNSETTING(INCOMING_CONNECTIONS) != SettingsManager::INCOMING_PASSIVE && CONNSETTING(INCOMING_CONNECTIONS) != SettingsManager::INCOMING_DISABLED)
		return true;

	if (CONNSETTING(INCOMING_CONNECTIONS6) != SettingsManager::INCOMING_PASSIVE && CONNSETTING(INCOMING_CONNECTIONS6) != SettingsManager::INCOMING_DISABLED)
		return true;

	return FavoriteManager::getInstance()->hasActiveHubs();
}

void ConnectivityManager::startSocket() {
	autoDetectedV4 = false;
	autoDetectedV6 = false;

	disconnect();

	if (isActive()) {
		listen();

		// must be done after listen calls; otherwise ports won't be set
		startMapping();
	}
}

void ConnectivityManager::startMapping() {
	if(SETTING(INCOMING_CONNECTIONS) == SettingsManager::INCOMING_ACTIVE_UPNP && !runningV4)
		startMapping(false);

	if(SETTING(INCOMING_CONNECTIONS6) == SettingsManager::INCOMING_ACTIVE_UPNP && !runningV6)
		startMapping(true);
}

void ConnectivityManager::listen() {
	try {
		ConnectionManager::getInstance()->listen();
	} catch(const Exception&) {
		throw Exception(STRING(TRANSFER_PORT));
	}

	try {
		SearchManager::getInstance()->listen();
	} catch(const Exception&) {
		throw Exception(STRING(SEARCH_PORT));
	}
}

void ConnectivityManager::disconnect() {
	SearchManager::getInstance()->disconnect();
	ConnectionManager::getInstance()->disconnect();
}

} // namespace dcpp
