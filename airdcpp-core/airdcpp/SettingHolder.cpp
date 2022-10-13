/*
* Copyright (C) 2013-2022 AirDC++ Project
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

#include "ClientManager.h"
#include "ConnectivityManager.h"
#include "GeoManager.h"
#include "QueueManager.h"
#include "SettingHolder.h"
#include "ShareManager.h"
#include "UpdateManager.h"
#include "UploadManager.h"


namespace dcpp {

SettingHolder::SettingHolder(MessageCallback aErrorF) : errorF(aErrorF)  {

}

SettingHolder::~SettingHolder() {

}

void SettingHolder::apply() {
	if (SETTING(DISCONNECT_SPEED) < 1) {
		SettingsManager::getInstance()->set(SettingsManager::DISCONNECT_SPEED, 1);
	}

	bool v4Changed = SETTING(INCOMING_CONNECTIONS) != prevConn4 ||
		SETTING(TCP_PORT) != prevTCP || SETTING(UDP_PORT) != prevUDP || SETTING(TLS_PORT) != prevTLS ||
		SETTING(MAPPER) != prevMapper || SETTING(BIND_ADDRESS) != prevBind || SETTING(BIND_ADDRESS6) != prevBind6;

	bool v6Changed = SETTING(INCOMING_CONNECTIONS6) != prevConn6 ||
		SETTING(TCP_PORT) != prevTCP || SETTING(UDP_PORT) != prevUDP || SETTING(TLS_PORT) != prevTLS ||
		/*SETTING(MAPPER) != prevMapper ||*/ SETTING(BIND_ADDRESS6) != prevBind6;

	try {
		ConnectivityManager::getInstance()->setup(v4Changed, v6Changed);
	} catch (const Exception& e) {
		showError(STRING_F(PORT_BYSY, e.getError()));
	}

	auto outConnsChanged = 
		SETTING(OUTGOING_CONNECTIONS) != prevOutConn ||
		SETTING(SOCKS_SERVER) != prevSocksServer ||
		SETTING(SOCKS_PORT) != prevSocksPort ||
		SETTING(SOCKS_USER) != prevSocksUser ||
		SETTING(SOCKS_PASSWORD) != prevSocksPassword;

	if (outConnsChanged) {
		try {
			Socket::socksUpdated();
		} catch (const SocketException& e) {
			showError(e.getError());
		}
	}

	ClientManager::getInstance()->infoUpdated();


	if (prevHighPrio != SETTING(HIGH_PRIO_FILES) || prevHighPrioRegex != SETTING(HIGHEST_PRIORITY_USE_REGEXP) || prevDownloadSkiplist != SETTING(SKIPLIST_DOWNLOAD) ||
		prevDownloadSkiplistRegex != SETTING(DOWNLOAD_SKIPLIST_USE_REGEXP)) {

		QueueManager::getInstance()->setMatchers();
	}

	if (prevShareSkiplist != SETTING(SKIPLIST_SHARE) || prevShareSkiplistRegex != SETTING(SHARE_SKIPLIST_USE_REGEXP)) {
		ShareManager::getInstance()->reloadSkiplist();
	}

	if (prevFreeSlotMatcher != SETTING(FREE_SLOTS_EXTENSIONS)) {
		UploadManager::getInstance()->setFreeSlotMatcher();
	}

	if (SETTING(GET_USER_COUNTRY) != prevGeo) {
		if (SETTING(GET_USER_COUNTRY)) {
			GeoManager::getInstance()->init();
			UpdateManager::getInstance()->checkGeoUpdate();
		} else {
			GeoManager::getInstance()->close();
		}
	}

	if (prevUpdateChannel != SETTING(UPDATE_CHANNEL)) {
		UpdateManager::getInstance()->checkVersion(false);
	} else if (prevTranslation != SETTING(LANGUAGE_FILE)) {
		UpdateManager::getInstance()->checkLanguage();
	}
}

void SettingHolder::showError(const string& aError) const noexcept{
	if (errorF)
		errorF(aError);
}

}