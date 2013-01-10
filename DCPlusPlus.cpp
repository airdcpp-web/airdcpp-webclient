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
#include "MappingManager.h"
#include "ConnectivityManager.h"
#include "WebShortcuts.h"
#include "Localization.h"
#include "DirectoryListingManager.h"
#include "UpdateManager.h"
#include "ThrottleManager.h"

#include "StringTokenizer.h"

#include "DebugManager.h"
#include "File.h"

#include "../windows/IgnoreManager.h"
#include "../windows/PopupManager.h"
#include "../windows/Wizard.h"
#include "HighlightManager.h"
#include "AutoSearchManager.h"
#include "ShareScannerManager.h"

#include "format.h"
namespace dcpp {

void startup(function<void (const string&)> splashF, function<void (const string&)> messageF) {
	// "Dedicated to the near-memory of Nev. Let's start remembering people while they're still alive."
	// Nev's great contribution to dc++
	while(1) break;


#ifdef _WIN32
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

	Util::initialize();
	AirUtil::init();

	ResourceManager::newInstance();
	SettingsManager::newInstance();

	LogManager::newInstance();
	TimerManager::newInstance();
	HashManager::newInstance();
	CryptoManager::newInstance();
	SearchManager::newInstance();
	ClientManager::newInstance();
	ConnectionManager::newInstance();
	DownloadManager::newInstance();
	UploadManager::newInstance();
	ThrottleManager::newInstance();
	QueueManager::newInstance();
	ShareManager::newInstance();
	FavoriteManager::newInstance();
	FinishedManager::newInstance();
	ADLSearchManager::newInstance();
	MappingManager::newInstance();
	ConnectivityManager::newInstance();
	DebugManager::newInstance();
	PopupManager::newInstance();
	IgnoreManager::newInstance();
	WebShortcuts::newInstance();
	AutoSearchManager::newInstance();
	HighlightManager::newInstance();
	ShareScannerManager::newInstance();
	GeoManager::newInstance();
	DirectoryListingManager::newInstance();
	UpdateManager::newInstance();

	SettingsManager::getInstance()->load(messageF);


	AutoSearchManager::getInstance()->AutoSearchLoad();
	UploadManager::getInstance()->setFreeSlotMatcher();
	Localization::init();
	if(SETTING(WIZARD_RUN_NEW)) {
		WizardDlg dlg;
		dlg.DoModal(/*m_hWnd*/);
		SettingsManager::getInstance()->set(SettingsManager::WIZARD_RUN_NEW, false); //wizard has run on startup
	}


	if(!SETTING(LANGUAGE_FILE).empty()) {
		string languageFile = SETTING(LANGUAGE_FILE);
		if(!File::isAbsolute(languageFile))
			languageFile = Util::getPath(Util::PATH_LOCALE) + languageFile;
		ResourceManager::getInstance()->loadLanguage(languageFile);
	}

	CryptoManager::getInstance()->loadCertificates();

	auto announce = [&splashF](const string& str) {
		if(splashF) {
			splashF(str);
		}
	};

	announce(STRING(HASH_DATABASE));
	HashManager::getInstance()->startup();

	announce(STRING(DOWNLOAD_QUEUE));
	QueueManager::getInstance()->loadQueue();

	announce(STRING(SHARED_FILES));
	ShareManager::getInstance()->startup(); 

	FavoriteManager::getInstance()->load();

	if(SETTING(GET_USER_COUNTRY)) {
		announce(STRING(COUNTRY_INFORMATION));
		GeoManager::getInstance()->init();
	}
	announce(STRING(LOADING_GUI));
}

void shutdown(function<void (const string&)> f) {
	TimerManager::getInstance()->shutdown();
	auto announce = [&f](const string& str) {
		if(f) {
			f(str);
		}
	};

	ShareManager::getInstance()->abortRefresh();

	announce(STRING(SAVING_HASH_DATA));
	HashManager::getInstance()->shutdown();

	ThrottleManager::getInstance()->shutdown();

	announce(STRING(SAVING_SHARE));
	ShareManager::getInstance()->shutdown();

	announce(STRING(CLOSING_CONNECTIONS));
	ConnectionManager::getInstance()->shutdown();
	MappingManager::getInstance()->close();
	GeoManager::getInstance()->close();
	BufferedSocket::waitShutdown();
	
	announce(STRING(SAVING_SETTINGS));
	AutoSearchManager::getInstance()->AutoSearchSave();
	QueueManager::getInstance()->saveQueue(true);
	SettingsManager::getInstance()->save();

	announce(STRING(SHUTTING_DOWN));

	UpdateManager::deleteInstance();
	GeoManager::deleteInstance();
	MappingManager::deleteInstance();
	ConnectivityManager::deleteInstance();
	DebugManager::deleteInstance();
	HighlightManager::deleteInstance();
	AutoSearchManager::deleteInstance();
	IgnoreManager::deleteInstance();
	WebShortcuts::deleteInstance();
	PopupManager::deleteInstance();
	ADLSearchManager::deleteInstance();
	FinishedManager::deleteInstance();
	ShareManager::deleteInstance();
	CryptoManager::deleteInstance();
	ThrottleManager::deleteInstance();
	DirectoryListingManager::deleteInstance();
	QueueManager::deleteInstance();
	DownloadManager::deleteInstance();
	UploadManager::deleteInstance();
	ShareScannerManager::deleteInstance();
	ConnectionManager::deleteInstance();
	SearchManager::deleteInstance();
	FavoriteManager::deleteInstance();
	ClientManager::deleteInstance();
	HashManager::deleteInstance();
	LogManager::deleteInstance();
	SettingsManager::deleteInstance();
	TimerManager::deleteInstance();
	ResourceManager::deleteInstance();

#ifdef _WIN32	
	::WSACleanup();
#endif
}

} // namespace dcpp