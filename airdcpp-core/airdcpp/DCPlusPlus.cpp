/* 
 * Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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

#include "format.h"
#include "File.h"
#include "StringTokenizer.h"

#include "ActivityManager.h"
#include "ADLSearch.h"
#include "AirUtil.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "ConnectivityManager.h"
#include "CryptoManager.h"
#include "DebugManager.h"
#include "DirectoryListingManager.h"
#include "DownloadManager.h"
#include "FavoriteManager.h"
#include "GeoManager.h"
#include "HashManager.h"
#include "Localization.h"
#include "LogManager.h"
#include "MessageManager.h"
#include "QueueManager.h"
#include "ShareManager.h"
#include "SearchManager.h"
#include "SettingsManager.h"
#include "ShareScannerManager.h"
#include "ThrottleManager.h"
#include "UpdateManager.h"
#include "UploadManager.h"
#include "ViewFileManager.h"

namespace dcpp {

#define RUNNING_FLAG Util::getPath(Util::PATH_USER_LOCAL) + "RUNNING"

void startup(function<void(const string&)> stepF, function<bool(const string& /*Message*/, bool /*isQuestion*/, bool /*isError*/)> messageF, function<void()> runWizard, function<void(float)> progressF, function<void()> moduleInitF) throw(Exception) {
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
	ShareManager::newInstance();
	ClientManager::newInstance();
	ConnectionManager::newInstance();
	MessageManager::newInstance();
	DownloadManager::newInstance();
	UploadManager::newInstance();
	ThrottleManager::newInstance();
	QueueManager::newInstance();
	FavoriteManager::newInstance();
	ADLSearchManager::newInstance();
	ConnectivityManager::newInstance();
	DirectoryListingManager::newInstance();
	DebugManager::newInstance();
	ShareScannerManager::newInstance();
	GeoManager::newInstance();
	UpdateManager::newInstance();
	ViewFileManager::newInstance();
	ActivityManager::newInstance();

	if (moduleInitF) {
		moduleInitF();
	}

	SettingsManager::getInstance()->load(messageF);

	UploadManager::getInstance()->setFreeSlotMatcher();
	Localization::init();
	if(SETTING(WIZARD_RUN) && runWizard) {
		runWizard();
		SettingsManager::getInstance()->set(SettingsManager::WIZARD_RUN, false); //wizard has run on startup
	}


	if(!SETTING(LANGUAGE_FILE).empty()) {
		string languageFile = SETTING(LANGUAGE_FILE);
		if(!File::isAbsolutePath(languageFile))
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

	FavoriteManager::getInstance()->load();

	if(SETTING(GET_USER_COUNTRY)) {
		announce(STRING(COUNTRY_INFORMATION));
		GeoManager::getInstance()->init();
	}
}

void shutdown(function<void (const string&)> stepF, function<void (float)> progressF, function<void()> moduleDestroyF) {
	TimerManager::getInstance()->shutdown();
	auto announce = [&stepF](const string& str) {
		if(stepF) {
			stepF(str);
		}
	};

	ShareManager::getInstance()->abortRefresh();

	announce(STRING(SAVING_HASH_DATA));
	HashManager::getInstance()->shutdown(progressF);

	announce(STRING(SAVING_SHARE));
	ShareManager::getInstance()->shutdown(progressF);

	announce(STRING(CLOSING_CONNECTIONS));
	ConnectionManager::getInstance()->shutdown(progressF);
	ConnectivityManager::getInstance()->close();
	GeoManager::getInstance()->close();
	BufferedSocket::waitShutdown();
	
	announce(STRING(SAVING_SETTINGS));
	QueueManager::getInstance()->shutdown();
	SettingsManager::getInstance()->save();

	if (moduleDestroyF) {
		moduleDestroyF();
	}

	announce(STRING(SHUTTING_DOWN));

	ActivityManager::deleteInstance();
	ViewFileManager::deleteInstance();
	UpdateManager::deleteInstance();
	GeoManager::deleteInstance();
	ConnectivityManager::deleteInstance();
	DebugManager::deleteInstance();
	ADLSearchManager::deleteInstance();
	CryptoManager::deleteInstance();
	ThrottleManager::deleteInstance();
	DirectoryListingManager::deleteInstance();
	QueueManager::deleteInstance();
	DownloadManager::deleteInstance();
	UploadManager::deleteInstance();
	ShareScannerManager::deleteInstance();
	MessageManager::deleteInstance();
	ConnectionManager::deleteInstance();
	SearchManager::deleteInstance();
	FavoriteManager::deleteInstance();
	ClientManager::deleteInstance();
	ShareManager::deleteInstance();
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