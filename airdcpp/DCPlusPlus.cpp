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
#include <airdcpp/DCPlusPlus.h>

#include <airdcpp/format.h>
#include <airdcpp/AppUtil.h>
#include <airdcpp/File.h>
#include <airdcpp/PathUtil.h>
#include <airdcpp/StringTokenizer.h>
#include <airdcpp/ValueGenerator.h>

#include <airdcpp/ActivityManager.h>
#include <airdcpp/ClientManager.h>
#include <airdcpp/ConnectionManager.h>
#include <airdcpp/ConnectivityManager.h>
#include <airdcpp/CryptoManager.h>
#include <airdcpp/ProtocolCommandManager.h>
#include <airdcpp/DirectoryListingManager.h>
#include <airdcpp/DownloadManager.h>
#include <airdcpp/FavoriteManager.h>
#include <airdcpp/FavoriteUserManager.h>
#include <airdcpp/GeoManager.h>
#include <airdcpp/HashManager.h>
#include <airdcpp/IgnoreManager.h>
#include <airdcpp/Localization.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/PartialSharingManager.h>
#include <airdcpp/PrivateChatManager.h>
#include <airdcpp/QueueManager.h>
#include <airdcpp/RecentManager.h>
#include <airdcpp/ShareManager.h>
#include <airdcpp/SearchManager.h>
#include <airdcpp/SettingsManager.h>
#include <airdcpp/TempShareManager.h>
#include <airdcpp/ThrottleManager.h>
#include <airdcpp/TransferInfoManager.h>
#include <airdcpp/UploadBundleManager.h>
#include <airdcpp/UpdateManager.h>
#include <airdcpp/UploadManager.h>
#include <airdcpp/UserCommandManager.h>
#include <airdcpp/ViewFileManager.h>

namespace dcpp {

#define RUNNING_FLAG AppUtil::getPath(AppUtil::PATH_USER_LOCAL) + "RUNNING"

void initializeUtil(const string& aConfigPath) noexcept {
	AppUtil::initialize(aConfigPath);
	ValueGenerator::initialize();
	Text::initialize();
}

void startup(StepFunction aStepF, MessageFunction aMessageF, Callback aRunWizardF, ProgressFunction aProgressF, Callback aModuleInitF /*nullptr*/, StartupLoadCallback aModuleLoadF /*nullptr*/) {
	// "Dedicated to the near-memory of Nev. Let's start remembering people while they're still alive."
	// Nev's great contribution to dc++
	while(1) break;


#ifdef _WIN32
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

	//create the running flag
	if (PathUtil::fileExists(RUNNING_FLAG)) {
		AppUtil::wasUncleanShutdown = true;
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
	PrivateChatManager::newInstance();
	DownloadManager::newInstance();
	UploadManager::newInstance();
	ThrottleManager::newInstance();
	QueueManager::newInstance();
	FavoriteManager::newInstance();
	FavoriteUserManager::newInstance();
	ConnectivityManager::newInstance();
	DirectoryListingManager::newInstance();
	ProtocolCommandManager::newInstance();
	GeoManager::newInstance();
	UpdateManager::newInstance();
	ViewFileManager::newInstance();
	ActivityManager::newInstance();
	RecentManager::newInstance();
	IgnoreManager::newInstance();
	TransferInfoManager::newInstance();
	PartialSharingManager::newInstance();
	UploadBundleManager::newInstance();
	UserCommandManager::newInstance();
	TempShareManager::newInstance();

	if (aModuleInitF) {
		aModuleInitF();
	}

	const auto announce = [&aStepF](const string& str) {
		if (aStepF) {
			aStepF(str);
		}
	};

	auto loader = StartupLoader(
		announce,
		aProgressF,
		aMessageF
	);

	SettingsManager::getInstance()->load(loader);
	FavoriteManager::getInstance()->load();

	UploadManager::getInstance()->setFreeSlotMatcher();
	Localization::init();
	if (SETTING(WIZARD_PENDING) && aRunWizardF) {
		aRunWizardF();
		SettingsManager::getInstance()->set(SettingsManager::WIZARD_PENDING, false); //wizard has run on startup
	}


	if(!SETTING(LANGUAGE_FILE).empty()) {
		ResourceManager::getInstance()->loadLanguage(SETTING(LANGUAGE_FILE));
	}

	CryptoManager::getInstance()->loadCertificates();

	loader.stepF(STRING(HASH_DATABASE));
	try {
		HashManager::getInstance()->startup(loader);
	} catch (const HashException&) {
		throw Exception();
	}

	loader.stepF(STRING(DOWNLOAD_QUEUE));
	QueueManager::getInstance()->loadQueue(loader);

	loader.stepF(STRING(SHARED_FILES));
	ShareManager::getInstance()->startup(loader);

	IgnoreManager::getInstance()->load();
	RecentManager::getInstance()->load();

	if(SETTING(GET_USER_COUNTRY)) {
		loader.stepF(STRING(COUNTRY_INFORMATION));
		GeoManager::getInstance()->init();
	}

	loader.stepF(STRING(CONNECTIVITY));
	ConnectivityManager::getInstance()->startup(loader);

	// Modules may depend on data loaded in other sections
	// Initialization should still be performed before loading SettingsManager as some modules save their config there
	if (aModuleLoadF) {
		aModuleLoadF(loader);
	}

	for (const auto& cb: loader.getPostLoadTasks()) {
		cb();
	}
}

void shutdown(StepFunction stepF, ProgressFunction progressF, ShutdownUnloadCallback aModuleUnloadF, Callback aModuleDestroyF) {
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

	if (aModuleUnloadF) {
		aModuleUnloadF(stepF, progressF);
	}

	QueueManager::getInstance()->shutdown();
	RecentManager::getInstance()->save();
	IgnoreManager::getInstance()->save();
	FavoriteManager::getInstance()->shutdown();
	SettingsManager::getInstance()->save();

	announce(STRING(SHUTTING_DOWN));

	if (aModuleDestroyF) {
		aModuleDestroyF();
	}

	TempShareManager::deleteInstance();
	UserCommandManager::deleteInstance();
	UploadBundleManager::deleteInstance();
	PartialSharingManager::deleteInstance();
	TransferInfoManager::deleteInstance();
	IgnoreManager::deleteInstance();
	RecentManager::deleteInstance();
	ActivityManager::deleteInstance();
	ViewFileManager::deleteInstance();
	UpdateManager::deleteInstance();
	GeoManager::deleteInstance();
	ConnectivityManager::deleteInstance();
	ProtocolCommandManager::deleteInstance();
	CryptoManager::deleteInstance();
	ThrottleManager::deleteInstance();
	DirectoryListingManager::deleteInstance();
	FavoriteUserManager::deleteInstance();
	QueueManager::deleteInstance();
	DownloadManager::deleteInstance();
	UploadManager::deleteInstance();
	PrivateChatManager::deleteInstance();
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