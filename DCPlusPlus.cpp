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
#include "DCPlusPlus.h"

#include "ConnectionManager.h"
#include "DownloadManager.h"
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

#include "StringTokenizer.h"

#include "DebugManager.h"
#include "File.h"

#include "../windows/IgnoreManager.h"
#include "../windows/PopupManager.h"
#include "../windows/Wizard.h"
#include "HighlightManager.h"
#include "AutoSearchManager.h"
#include "ShareScannerManager.h"
namespace dcpp {

void startup(void (*f)(void*, const tstring&), void* p) {
	// "Dedicated to the near-memory of Nev. Let's start remembering people while they're still alive."
	// Nev's great contribution to dc++
	while(1) break;


#ifdef _WIN32
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

	Util::initialize();

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
	QueueManager::newInstance();
	ShareManager::newInstance();
	FavoriteManager::newInstance();
	FinishedManager::newInstance();
	ADLSearchManager::newInstance();
	ConnectivityManager::newInstance();
	MappingManager::newInstance();
	DebugManager::newInstance();
	PopupManager::newInstance();
	IgnoreManager::newInstance();
	AutoSearchManager::newInstance();
	HighlightManager::newInstance();
	ShareScannerManager::newInstance();


	SettingsManager::getInstance()->load();	
	AutoSearchManager::getInstance()->AutosearchLoad();

	if(BOOLSETTING(WIZARD_RUN_NEW)) {
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

	FavoriteManager::getInstance()->load();
	CryptoManager::getInstance()->loadCertificates();


	if(f != NULL)
		(*f)(p, TSTRING(HASH_DATABASE));
	HashManager::getInstance()->startup();
	if(f != NULL)
		(*f)(p, TSTRING(SHARED_FILES));

	ShareManager::getInstance()->Startup(); 

	if(f != NULL)
		(*f)(p, TSTRING(DOWNLOAD_QUEUE));
	QueueManager::getInstance()->loadQueue();
}

void shutdown() {
	TimerManager::getInstance()->shutdown();
	HashManager::getInstance()->shutdown();
	//sharemanager needs to save things too now. -Night
	ShareManager::getInstance()->shutdown();
	ConnectionManager::getInstance()->shutdown();
	MappingManager::getInstance()->close();
	BufferedSocket::waitShutdown();
	
	AutoSearchManager::getInstance()->AutosearchSave();
	QueueManager::getInstance()->saveQueue(true);
	SettingsManager::getInstance()->save();
	MappingManager::deleteInstance();
	ConnectivityManager::deleteInstance();
	DebugManager::deleteInstance();
	HighlightManager::deleteInstance();
	AutoSearchManager::deleteInstance();
	IgnoreManager::deleteInstance();
	PopupManager::deleteInstance();
	ADLSearchManager::deleteInstance();
	FinishedManager::deleteInstance();
	ShareManager::deleteInstance();
	CryptoManager::deleteInstance();
	DownloadManager::deleteInstance();
	UploadManager::deleteInstance();
	QueueManager::deleteInstance();
	ConnectionManager::deleteInstance();
	SearchManager::deleteInstance();
	FavoriteManager::deleteInstance();
	ClientManager::deleteInstance();
	HashManager::deleteInstance();
	ShareScannerManager::deleteInstance();
	LogManager::deleteInstance();
	SettingsManager::deleteInstance();
	TimerManager::deleteInstance();
	ResourceManager::deleteInstance();
	

#ifdef _WIN32	
	::WSACleanup();
#endif
}

} // namespace dcpp

/**
 * @file
 * $Id: DCPlusPlus.cpp 500 2010-06-25 22:08:18Z bigmuscle $
 */
