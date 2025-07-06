/*
 * Copyright (C) 2012-2021 AirDC++ Project
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
#include "Client.h"

#include <airdcpp/DCPlusPlus.h>
#include <airdcpp/util/AppUtil.h>

#include <airdcpp/core/timer/TimerManager.h>
#include <airdcpp/core/update/UpdateManager.h>

#include <airdcpp/connectivity/ConnectivityManager.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/favorites/FavoriteManager.h>
#include <airdcpp/filelist/DirectoryListing.h>
#include <airdcpp/filelist/DirectoryListingManager.h>
#include <airdcpp/hub/activity/ActivityManager.h>
#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/settings/SettingsManager.h>

#include <web-server/FileServer.h>
#include <web-server/HttpManager.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>

namespace airdcppd {

Client::Client(bool aAsDaemon) : asDaemon(aAsDaemon) {

}

void Client::run(const dcpp::StartupParams& aStartupParams) {
	if (!startup(aStartupParams)) {
		return;
	}

	if (!asDaemon) {
		auto wsm = webserver::WebServerManager::getInstance();
		printf(".\n\n%s running, press ctrl-c to exit...\n\n", shortVersionString.c_str());
		printf("HTTP port: %d, HTTPS port: %d\n", WEBCFG(PLAIN_PORT).num(), WEBCFG(TLS_PORT).num());
		printf("Config path: %s\n", AppUtil::getPath(AppUtil::PATH_USER_CONFIG).c_str());
		printf("Web resources path: %s\n", wsm->getHttpManager().getFileServer().getResourcePath().c_str());
	}

	shutdownSemaphore.wait();

	shutdown();
}

void Client::stop() {
	if (!running) {
		if (!asDaemon) {
			printf("Shutdown request ignored, operation in progress\n");
		}

		return;
	}

	running = false;
	if (!asDaemon) {
		printf("Shutdown requested...\n");
	}

	// FreeBSD would fail with "Fatal error 'thread 0x807616000 was already on queue."
	// if signaling from a system thread
	webserver::WebServerManager::getInstance()->addAsyncTask([this] {
		shutdownSemaphore.signal();
	});
}

void webErrorF(const string& aError) {
	printf("%s\n", aError.c_str());
};

bool messageF(const string& aStr, bool isQuestion, bool isError) {
	printf("%s\n", aStr.c_str());
	return true;
}

void stepF(const string& aStr) {
	printf("Loading %s\n", aStr.c_str());
}

void progressF(float aProgress) {
	// Not implemented
}

bool Client::startup(const dcpp::StartupParams& aStartupParams) {
	webserver::WebServerManager::newInstance();

	auto wsm = webserver::WebServerManager::getInstance();
	if (!wsm->load(webErrorF) || !wsm->hasUsers()) {
		webserver::WebServerManager::deleteInstance();
		printf("%s\n", "No valid configuration found. Run the application with --configure parameter to set up initial configuration.");
		return false;
	}

	bool serverStarted = false;
	dcpp::startup(
		stepF,
		messageF,
		nullptr, // wizard
		progressF,
		[&] {
			// Add the command listeners here so that we won't miss any messages while loading
			auto cdmHub = aStartupParams.hasParam("--cdm-hub");
			auto cdmClient = aStartupParams.hasParam("--cdm-client");
			auto cdmWeb = aStartupParams.hasParam("--cdm-web");
			if (cdmHub || cdmClient || cdmWeb) {
				cdmDebug.reset(new CDMDebug(cdmClient, cdmHub, cdmWeb));
			}
		}, // module init
		[&](StartupLoader& aLoader) { // module load
			auto webResources = aStartupParams.getValue("--web-resources");
			aLoader.stepF(STRING(WEB_SERVER));
			serverStarted = wsm->startup(
				webErrorF,
				webResources ? *webResources : "",
				[this]() { stop(); }
			);

			wsm->waitExtensionsLoaded();
		} // module load
	);

	if (!serverStarted) {
		return false;
	}

	ActivityManager::getInstance()->setAway(AWAY_IDLE);
	SettingsManager::getInstance()->setDefault(SettingsManager::LOG_IGNORED, false);

	// The client is often run on slow system and this would cause high CPU usage
	SettingsManager::getInstance()->setDefault(SettingsManager::REFRESH_THREADING, static_cast<int>(SettingsManager::MULTITHREAD_NEVER));

	DirectoryListingManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);


	TimerManager::getInstance()->start();
	UpdateManager::getInstance()->init();

	if (!aStartupParams.hasParam("--no-autoconnect")) {
		FavoriteManager::getInstance()->autoConnect();
	}

	running = true;
	return true;
}


void unloadModules(StepFunction& aStepF, ProgressFunction&) {
	aStepF("Stopping web server");
	webserver::WebServerManager::getInstance()->stop();
	webserver::WebServerManager::getInstance()->save(webErrorF);
}

void destroyModules() {
	webserver::WebServerManager::deleteInstance();
}

void Client::shutdown() {
	cdmDebug.reset(nullptr);

	ClientManager::getInstance()->putClients();
	ConnectivityManager::getInstance()->disconnect();

	DirectoryListingManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);

	dcpp::shutdown(
			[](const string& aStr) { printf("%s\n", aStr.c_str()); },
			progressF,
			unloadModules,
			destroyModules
	);
}

void Client::on(DirectoryListingManagerListener::OpenListing, const DirectoryListingPtr& aList, const string& aDir, const string& aXML) noexcept {
	if (aList->getPartialList()) {
		aList->addPartialListLoadTask(aXML, aDir);
	} else {
		aList->addFullListTask(aDir);
	}
}

void Client::on(ClientManagerListener::ClientCreated, const ClientPtr& aClient) noexcept {
	aClient->connect();
}

void Client::on(ClientManagerListener::ClientRedirected, const ClientPtr& /*aOldClient*/, const ClientPtr& aNewClient) noexcept {
	aNewClient->connect();
}

}
