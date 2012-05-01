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
#include "ConnectivityManager.h"

#include "ClientManager.h"
#include "ConnectionManager.h"
#include "format.h"
#include "MappingManager.h"
#include "SearchManager.h"
#include "SettingsManager.h"
#include "version.h"
#include "AirUtil.h"

namespace dcpp {

ConnectivityManager::ConnectivityManager() :
autoDetected(false),
running(false)
{
}

const string& ConnectivityManager::get(SettingsManager::StrSetting setting) const {
	if(BOOLSETTING(AUTO_DETECT_CONNECTION)) {
		auto i = autoSettings.find(setting);
		if(i != autoSettings.end()) {
			return boost::get<const string&>(i->second);
		}
	}
	return SettingsManager::getInstance()->get(setting);
}

int ConnectivityManager::get(SettingsManager::IntSetting setting) const {
	if(BOOLSETTING(AUTO_DETECT_CONNECTION)) {
		auto i = autoSettings.find(setting);
		if(i != autoSettings.end()) {
			return boost::get<int>(i->second);
		}
	}
	return SettingsManager::getInstance()->get(setting);
}

void ConnectivityManager::set(SettingsManager::StrSetting setting, const string& str) {
	if(BOOLSETTING(AUTO_DETECT_CONNECTION)) {
		autoSettings[setting] = str;
	} else {
		SettingsManager::getInstance()->set(setting, str);
	}
}

void ConnectivityManager::detectConnection() {
	if(running)
		return;
	running = true;

	status.clear();
	fire(ConnectivityManagerListener::Started());

	if(MappingManager::getInstance()->getOpened()) {
		MappingManager::getInstance()->close();
	}

	disconnect();

	// restore auto settings to their default value.
	int settings[] = { SettingsManager::TCP_PORT, SettingsManager::TLS_PORT, SettingsManager::UDP_PORT,
		SettingsManager::EXTERNAL_IP, SettingsManager::EXTERNAL_IP6, SettingsManager::NO_IP_OVERRIDE,
		SettingsManager::BIND_ADDRESS, SettingsManager::BIND_ADDRESS6,
		SettingsManager::INCOMING_CONNECTIONS, SettingsManager::OUTGOING_CONNECTIONS };
	std::for_each(settings, settings + sizeof(settings) / sizeof(settings[0]), [this](int setting) {
		if(setting >= SettingsManager::STR_FIRST && setting < SettingsManager::STR_LAST) {
			autoSettings[setting] = SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::StrSetting>(setting));
		} else if(setting >= SettingsManager::INT_FIRST && setting < SettingsManager::INT_LAST) {
			autoSettings[setting] = SettingsManager::getInstance()->getDefault(static_cast<SettingsManager::IntSetting>(setting));
		}
	});

	log((string)("Determining the best connectivity settings..."), LogManager::LOG_INFO);
	try {
		listen();
	} catch(const Exception& e) {
		autoSettings[SettingsManager::INCOMING_CONNECTIONS] = SettingsManager::INCOMING_FIREWALL_PASSIVE;
		log(str(boost::format("Unable to open %1% port(s); connectivity settings must be configured manually") % e.getError()), LogManager::LOG_ERROR);
		fire(ConnectivityManagerListener::Finished());
		running = false;
		return;
	}

	autoDetected = true;

	if(!Util::isPrivateIp(AirUtil::getLocalIp())) {
		autoSettings[SettingsManager::INCOMING_CONNECTIONS] = SettingsManager::INCOMING_DIRECT;
		log((string)("Public IP address detected, selecting active mode with direct connection"), LogManager::LOG_INFO);
		fire(ConnectivityManagerListener::Finished());
		running = false;
		return;
	}

	autoSettings[SettingsManager::INCOMING_CONNECTIONS] = SettingsManager::INCOMING_FIREWALL_UPNP;
	log((string)("Local network with possible NAT detected, trying to map the ports..."), LogManager::LOG_INFO);

	startMapping();
}

void ConnectivityManager::setup(bool settingsChanged) {
	if(BOOLSETTING(AUTO_DETECT_CONNECTION)) {
		if(!autoDetected) {
			detectConnection();
		}
	} else {
		if(autoDetected) {
			autoSettings.clear();
		}
		if(autoDetected || settingsChanged) {
			if(settingsChanged || (SETTING(INCOMING_CONNECTIONS) != SettingsManager::INCOMING_FIREWALL_UPNP)) {
				MappingManager::getInstance()->close();
			}
			startSocket();
		} else if(SETTING(INCOMING_CONNECTIONS) == SettingsManager::INCOMING_FIREWALL_UPNP && !running) {
			// previous mappings had failed; try again
			startMapping();
		}
	}
}

void ConnectivityManager::editAutoSettings() {
	SettingsManager::getInstance()->set(SettingsManager::AUTO_DETECT_CONNECTION, false);

	auto sm = SettingsManager::getInstance();
	for(auto i = autoSettings.cbegin(), iend = autoSettings.cend(); i != iend; ++i) {
		if(i->first >= SettingsManager::STR_FIRST && i->first < SettingsManager::STR_LAST) {
			sm->set(static_cast<SettingsManager::StrSetting>(i->first), boost::get<const string&>(i->second));
		} else if(i->first >= SettingsManager::INT_FIRST && i->first < SettingsManager::INT_LAST) {
			sm->set(static_cast<SettingsManager::IntSetting>(i->first), boost::get<int>(i->second));
		}
	}
	autoSettings.clear();

	fire(ConnectivityManagerListener::SettingChanged());
}

string ConnectivityManager::getInformation() const {
	if(running) {
		return "Connectivity settings are being configured; try again later";
	}

	string autoStatus = ok() ? str(boost::format("enabled - %1%") % getStatus()) : "disabled";

	string mode;

	switch(CONNSETTING(INCOMING_CONNECTIONS)) {
	case SettingsManager::INCOMING_DIRECT:
		{
			mode = "Direct connection to the Internet (no router)";
			break;
		}
	case SettingsManager::INCOMING_FIREWALL_UPNP:
		{
			mode = str(boost::format("Active mode behind a router that %1% can configure; port mapping status: %2%") %
				APPNAME % MappingManager::getInstance()->getStatus());
			break;
		}
	case SettingsManager::INCOMING_FIREWALL_NAT:
		{
			mode = "Active mode behind a router";
			break;
		}
	case SettingsManager::INCOMING_FIREWALL_PASSIVE:
		{
			mode = "Passive mode";
			break;
		}
	}

	auto field = [](const string& s) { return s.empty() ? "undefined" : s; };

	return str(boost::format(
		"Connectivity information:\n\n"
		"Automatic connectivity setup is: %1%\n\n"
		"\t%2%\n"
		"\tExternal IP (v4): %3%\n"
		"\tExternal IP (v6): %4%\n"
		"\tBound interface (v4): %5%\n"
		"\tBound interface (v6): %6%\n"
		"\tTransfer port: %7%\n"
		"\tEncrypted transfer port: %8%\n"
		"\tSearch port: %9%") % autoStatus % mode %
		field(CONNSETTING(EXTERNAL_IP)) % field(CONNSETTING(EXTERNAL_IP6)) %
		field(CONNSETTING(BIND_ADDRESS)) % field(CONNSETTING(BIND_ADDRESS6)) %
		field(ConnectionManager::getInstance()->getPort()) % field(ConnectionManager::getInstance()->getSecurePort()) %
		field(SearchManager::getInstance()->getPort()));
}

void ConnectivityManager::startMapping() {
	running = true;
	if(!MappingManager::getInstance()->open()) {
		running = false;
	}
}

void ConnectivityManager::mappingFinished(const string& mapper) {
	if(BOOLSETTING(AUTO_DETECT_CONNECTION)) {
		if(mapper.empty()) {
			disconnect();
			autoSettings[SettingsManager::INCOMING_CONNECTIONS] = SettingsManager::INCOMING_FIREWALL_PASSIVE;
			log((string)("Active mode could not be achieved; a manual configuration is recommended for better connectivity"), LogManager::LOG_WARNING);
		} else {
			SettingsManager::getInstance()->set(SettingsManager::MAPPER, mapper);
		}
		fire(ConnectivityManagerListener::Finished());
	}

	running = false;
}

void ConnectivityManager::log(string&& message, LogManager::Severity sev) {
	if(BOOLSETTING(AUTO_DETECT_CONNECTION)) {
		status = forward<string>(message);
		LogManager::getInstance()->message("Connectivity: " + status, LogManager::LOG_INFO);
		fire(ConnectivityManagerListener::Message(), status);
	} else {
		LogManager::getInstance()->message(message, LogManager::LOG_INFO);
	}
}

void ConnectivityManager::startSocket() {
	autoDetected = false;

	disconnect();

	if(ClientManager::getInstance()->isActive()) {
		listen();

		// must be done after listen calls; otherwise ports won't be set
		if(SETTING(INCOMING_CONNECTIONS) == SettingsManager::INCOMING_FIREWALL_UPNP && !running)
			startMapping();
	}
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
