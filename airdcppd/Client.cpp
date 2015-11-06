/*
 * Copyright (C) 2012-2015 AirDC++ Project
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

#include "Client.h"

#include <airdcpp/DCPlusPlus.h>
#include <airdcpp/Util.h>

#include <airdcpp/ConnectivityManager.h>
#include <airdcpp/DirectoryListingManager.h>
#include <airdcpp/ClientManager.h>
#include <airdcpp/FavoriteManager.h>
#include <airdcpp/UpdateManager.h>
#include <airdcpp/TimerManager.h>

#include <web-server/WebServerManager.h>
 
namespace airdcppd {

Client::Client(bool aAsDaemon) : asDaemon(aAsDaemon) {

}

void Client::run() {
	if (!startup()) {
		return;
	}

	if (!asDaemon) {
		auto wsm = webserver::WebServerManager::getInstance();
		printf(".\n%s running, press ctrl-c to exit...\n", shortVersionString.c_str());
		printf("HTTP port: %d, HTTPS port: %d\n", wsm->getPlainServerConfig().getPort(), wsm->getTlsServerConfig().getPort());
	}

	webserver::WebServerManager::getInstance()->join();

	shutdown();
}

void Client::stop() {
	webserver::WebServerManager::getInstance()->stop();
}

bool Client::startup() {
	webserver::WebServerManager::newInstance();
	if (!webserver::WebServerManager::getInstance()->load()) {
		webserver::WebServerManager::deleteInstance();
		printf("%s\n", "No valid configuration found. Run the application with -configure parameter to set up initial configuration.");
		return false;
	}

	dcpp::startup(
		[&](const string& aStr) { printf("Loading %s\n", aStr.c_str()); },
		[&](const string& aStr, bool isQuestion, bool isError) {
				printf("%s\n", aStr.c_str());
				return true;
		},
		nullptr,
		[&](float aProgress) {}
	);

	auto webResourcePath = File::makeAbsolutePath("node_modules/airdcpp-webui/");
	printf("Starting web server (resource path: %s)\n", webResourcePath.c_str());
	webserver::WebServerManager::getInstance()->start([](const string& aError) {
		printf("%s\n", aError.c_str());
	}, webResourcePath);

	DirectoryListingManager::getInstance()->addListener(this);
	ClientManager::getInstance()->addListener(this);


	TimerManager::getInstance()->start();
	UpdateManager::getInstance()->init();

	try {
		ConnectivityManager::getInstance()->setup(true, true);
	} catch (const Exception& e) {

	}

	if (!Util::hasStartupParam("-no-autoconnect")) {
		FavoriteManager::getInstance()->autoConnect();
	}

	started = true;
	return true;
}

void Client::shutdown() {
	if (!started) {
		return;
	}
	
	ClientManager::getInstance()->putClients();
	ConnectivityManager::getInstance()->disconnect();

	DirectoryListingManager::getInstance()->removeListener(this);
	ClientManager::getInstance()->removeListener(this);

	dcpp::shutdown(
			[](const string& aStr) { printf("%s\n", aStr.c_str()); },
			[](float aProgress) {}
	);

	webserver::WebServerManager::getInstance()->save();
	webserver::WebServerManager::deleteInstance();	
}

void Client::on(DirectoryListingManagerListener::OpenListing, const DirectoryListingPtr& aList, const string& aDir, const string& aXML) noexcept {
	if (aList->getPartialList()) {
		aList->addPartialListTask(aXML, aDir, false);
	} else {
		aList->addFullListTask(aDir);
	}
}

void Client::on(ClientManagerListener::ClientCreated, const ClientPtr& aClient) noexcept {
	aClient->connect();
}

}
