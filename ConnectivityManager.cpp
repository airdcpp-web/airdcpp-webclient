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

#include "stdinc.h"
#include "ConnectivityManager.h"

#include "ClientManager.h"
#include "ConnectionManager.h"
#include "format.h"
#include "LogManager.h"
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

	log((string)("Determining the best connectivity settings..."));
	try {
		listen();
	} catch(const Exception& e) {
		autoSettings[SettingsManager::INCOMING_CONNECTIONS] = SettingsManager::INCOMING_FIREWALL_PASSIVE;
		log(str(boost::format("Unable to open %1% port(s); connectivity settings must be configured manually") % e.getError()));
		fire(ConnectivityManagerListener::Finished());
		running = false;
		return;
	}

	autoDetected = true;

	if(!Util::isPrivateIp(AirUtil::getLocalIp())) {
		autoSettings[SettingsManager::INCOMING_CONNECTIONS] = SettingsManager::INCOMING_DIRECT;
		log((string)("Public IP address detected, selecting active mode with direct connection"));
		fire(ConnectivityManagerListener::Finished());
		running = false;
		return;
	}

	autoSettings[SettingsManager::INCOMING_CONNECTIONS] = SettingsManager::INCOMING_FIREWALL_UPNP;
	log((string)("Local network with possible NAT detected, trying to map the ports..."));

	if(!MappingManager::getInstance()->open()) {
		running = false;
	}
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
		} else if(SETTING(INCOMING_CONNECTIONS) == SettingsManager::INCOMING_FIREWALL_UPNP && !MappingManager::getInstance()->getOpened()) {
			// previous mappings had failed; try again
			MappingManager::getInstance()->open();
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
			mode = str(boost::format("Connection behind a router that %1% has configured with %2%") % APPNAME % SETTING(MAPPER));
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

	string ip = CONNSETTING(EXTERNAL_IP);
	if(ip.empty()) {
		ip = "undefined";
	}

	return str(boost::format(
		"Connectivity information:\n\n"
		"Automatic connectivity setup is: %1%\n\n"
		"\t%2%\n"
		"\tExternal IP: %3%\n"
		"\tTransfer port: %4%\n"
		"\tEncrypted transfer port: %5%\n"
		"\tSearch port: %6%") % autoStatus % mode % ip % ConnectionManager::getInstance()->getPort() %
		ConnectionManager::getInstance()->getSecurePort() % SearchManager::getInstance()->getPort());
}

void ConnectivityManager::mappingFinished(const string& mapper) {
	if(BOOLSETTING(AUTO_DETECT_CONNECTION)) {
		if(mapper.empty()) {
			disconnect();
			autoSettings[SettingsManager::INCOMING_CONNECTIONS] = SettingsManager::INCOMING_FIREWALL_PASSIVE;
			log((string)("Active mode could not be achieved; a manual configuration is recommended for better connectivity"));
		} else {
			SettingsManager::getInstance()->set(SettingsManager::MAPPER, mapper);
		}
		fire(ConnectivityManagerListener::Finished());
	}

	running = false;
}

void ConnectivityManager::log(string&& message) {
	if(BOOLSETTING(AUTO_DETECT_CONNECTION)) {
		status = forward<string>(message);
		LogManager::getInstance()->message("Connectivity: " + status);
		fire(ConnectivityManagerListener::Message(), status);
	} else {
		LogManager::getInstance()->message(message);
	}
}

void ConnectivityManager::startSocket() {
	autoDetected = false;

	disconnect();

	if(ClientManager::getInstance()->isActive()) {
		listen();

		// must be done after listen calls; otherwise ports won't be set
		if(SETTING(INCOMING_CONNECTIONS) == SettingsManager::INCOMING_FIREWALL_UPNP)
			MappingManager::getInstance()->open();
	}
}

void ConnectivityManager::listen() {
	try {
		ConnectionManager::getInstance()->listen();
	} catch(const Exception&) {
		throw Exception("Transfer (TCP)");
	}

	try {
		SearchManager::getInstance()->listen();
	} catch(const Exception&) {
		throw Exception("Search (UDP)");
	}
}

void ConnectivityManager::disconnect() {
	SearchManager::getInstance()->disconnect();
	ConnectionManager::getInstance()->disconnect();
}

} // namespace dcpp
