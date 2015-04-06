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

#include "stdinc.h"
#include "DCPlusPlus.h"

#include "ConnectionManager.h"
#include "DownloadManager.h"
#include "GeoManager.h"
#include "UploadManager.h"
#include "CryptoManager.h"
#include "ShareManager.h"
#include "SearchManager.h"
#include "QueueManager.h"
#include "ClientManager.h"
#include "HashManager.h"
#include "LogManager.h"
#include "FavoriteManager.h"
#include "SettingsManager.h"
#include "FinishedManager.h"
#include "ADLSearch.h"
#include "ConnectivityManager.h"
#include "WebShortcuts.h"
#include "Localization.h"
#include "DirectoryListingManager.h"
#include "UpdateManager.h"
#include "ThrottleManager.h"
#include "MessageManager.h"
#include "HighlightManager.h"

#include "StringTokenizer.h"

#include "DebugManager.h"
#include "File.h"

#include "AutoSearchManager.h"
#include "ShareScannerManager.h"

#include "format.h"
namespace dcpp {

#define RUNNING_FLAG Util::getPath(Util::PATH_USER_LOCAL) + "RUNNING"

void startup(function<void(const string&)> stepF, function<bool(const string& /*Message*/, bool /*isQuestion*/, bool /*isError*/)> messageF, function<void()> runWizard, function<void(float)> progressF) throw(Exception) {
	// "Dedicated to the near-memory of Nev. Let's start remembering people while they're still alive."
	// Nev's great contribution to dc++
	while(1) break;


#ifdef _WIN32
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
	AirUtil::init();

	//create the running flag
	if (Util::fileExists(RUNNING_FLAG)) {
		Util::wasUncleanShutdown = true;
	} else {
		File::createFile(RUNNING_FLAG);
	}

	ResourceManager::newInstance();
	SettingsManager::newInstance();

	LogManager::newInstance();
	TimerManager::newInstance();
	HashManager::newInstance();
	CryptoManager::newInstance();
	SearchManager::newInstance();
	ClientManager::newInstance();
	ConnectionManager::newInstance();
	MessageManager::newInstance();
	DownloadManager::newInstance();
	UploadManager::newInstance();
	ThrottleManager::newInstance();
	QueueManager::newInstance();
	ShareManager::newInstance();
	FavoriteManager::newInstance();
	FinishedManager::newInstance();
	ADLSearchManager::newInstance();
	ConnectivityManager::newInstance();
	DebugManager::newInstance();
	WebShortcuts::newInstance();
	AutoSearchManager::newInstance();
	ShareScannerManager::newInstance();
	GeoManager::newInstance();
	DirectoryListingManager::newInstance();
	UpdateManager::newInstance();
	HighlightManager::newInstance();

	SettingsManager::getInstance()->load(messageF);

	UploadManager::getInstance()->setFreeSlotMatcher();
	Localization::init();
	if(SETTING(WIZARD_RUN_NEW) && runWizard) {
		runWizard();
		SettingsManager::getInstance()->set(SettingsManager::WIZARD_RUN_NEW, false); //wizard has run on startup
	}


	if(!SETTING(LANGUAGE_FILE).empty()) {
		string languageFile = SETTING(LANGUAGE_FILE);
		if(!File::isAbsolute(languageFile))
			languageFile = Util::getPath(Util::PATH_LOCALE) + languageFile;
		ResourceManager::getInstance()->loadLanguage(languageFile);
	}

	CryptoManager::getInstance()->loadCertificates();

	auto announce = [&stepF](const string& str) {
		if(stepF) {
			stepF(str);
		}
	};

	announce(STRING(HASH_DATABASE));
	try {
		HashManager::getInstance()->startup(stepF, progressF, messageF);
	} catch (const HashException&) {
		throw Exception();
	}

	announce(STRING(DOWNLOAD_QUEUE));
	QueueManager::getInstance()->loadQueue(progressF);

	announce(STRING(SHARED_FILES));
	ShareManager::getInstance()->startup(stepF, progressF); 

	AutoSearchManager::getInstance()->AutoSearchLoad();
	FavoriteManager::getInstance()->load();

	if(SETTING(GET_USER_COUNTRY)) {
		announce(STRING(COUNTRY_INFORMATION));
		GeoManager::getInstance()->init();
	}
}

void shutdown(function<void (const string&)> stepF, function<void (float)> progressF) {
	TimerManager::getInstance()->shutdown();
	auto announce = [&stepF](const string& str) {
		if(stepF) {
			stepF(str);
		}
	};

	ShareManager::getInstance()->abortRefresh();

	announce(STRING(SAVING_HASH_DATA));
	HashManager::getInstance()->shutdown(progressF);

	ThrottleManager::getInstance()->shutdown();

	announce(STRING(SAVING_SHARE));
	ShareManager::getInstance()->shutdown(progressF);

	announce(STRING(CLOSING_CONNECTIONS));
	ConnectionManager::getInstance()->shutdown(progressF);
	ConnectivityManager::getInstance()->close();
	GeoManager::getInstance()->close();
	BufferedSocket::waitShutdown();
	
	announce(STRING(SAVING_SETTINGS));
	AutoSearchManager::getInstance()->AutoSearchSave();
	QueueManager::getInstance()->shutdown();
	SettingsManager::getInstance()->save();

	announce(STRING(SHUTTING_DOWN));

	HighlightManager::deleteInstance();
	UpdateManager::deleteInstance();
	GeoManager::deleteInstance();
	ConnectivityManager::deleteInstance();
	DebugManager::deleteInstance();
	AutoSearchManager::deleteInstance();
	WebShortcuts::deleteInstance();
	ADLSearchManager::deleteInstance();
	FinishedManager::deleteInstance();
	CryptoManager::deleteInstance();
	ThrottleManager::deleteInstance();
	DirectoryListingManager::deleteInstance();
	ShareManager::deleteInstance();
	QueueManager::deleteInstance();
	DownloadManager::deleteInstance();
	UploadManager::deleteInstance();
	ShareScannerManager::deleteInstance();
	MessageManager::deleteInstance();
	ConnectionManager::deleteInstance();
	SearchManager::deleteInstance();
	FavoriteManager::deleteInstance();
	ClientManager::deleteInstance();
	HashManager::deleteInstance();
	LogManager::deleteInstance();
	SettingsManager::deleteInstance();
	TimerManager::deleteInstance();
	ResourceManager::deleteInstance();

	File::deleteFile(RUNNING_FLAG);
#ifdef _WIN32	
	::WSACleanup();
#endif
}

} // namespace dcpp