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

#include "ConfigPrompt.h"

#include <airdcpp/stdinc.h>
#include <airdcpp/ScopedFunctor.h>
#include <airdcpp/Util.h>

#include <web-server/WebServerManager.h>
#include <web-server/WebServerSettings.h>
#include <web-server/WebUser.h>

namespace airdcppd {

//using namespace std;
//using dcpp::ScopedFunctor;
//using Util = dcpp::Util;
using namespace dcpp;

std::string ConfigPrompt::toBold(const std::string& aText) {
	return "\e[1m" + aText + "\e[0m";
}

ConfigPrompt::ConfigF ConfigPrompt::checkArgs() {
	std::function<bool(webserver::WebServerManager*)> f = nullptr;
	if (Util::hasStartupParam("--configure")) {
		f = &runConfigure;
	} else if (Util::hasStartupParam("--add-user")) {
		f = &addUser;
	} else if (Util::hasStartupParam("--remove-user")) {
		f = &removeUser;
	} else if (Util::hasStartupParam("--list-users")) {
		f = &listUsers;
	}

	if (!f) {
		return nullptr;
	}

	const auto errorF = [&](const std::string& aError) {
		std::cout << aError << std::endl;
	};

	auto ret = [=] {
		webserver::WebServerManager::newInstance();
		ScopedFunctor([=] { webserver::WebServerManager::deleteInstance(); });

		auto wsm = webserver::WebServerManager::getInstance();
		wsm->load(errorF);

		std::cout << std::endl;
		std::cout << std::endl;

		auto save = f(wsm);

		std::cout << std::endl;
		if (save) {
			if (wsm->save(errorF)) {
				cout << toBold("Configuration was written to " + wsm->getConfigFilePath()) << std::endl;
			}
		}
	};

	return ret;
}

bool ConfigPrompt::runConfigure(webserver::WebServerManager* wsm) {
	auto& plainServerConfig = wsm->getPlainServerConfig();
	auto& tlsServerConfig = wsm->getTlsServerConfig();

	promptPort(plainServerConfig, "HTTP");
	std::cout << std::endl;

	promptPort(tlsServerConfig, "HTTPS");
	std::cout << std::endl;

	if (!wsm->hasUsers()) {
		std::cout << toBold("No existing users were found, adding new one.") << std::endl;

		addUser(wsm);
	} else {
		std::cout << toBold("Configured users were found. Use the separate commands if you want to modify them (see help).") << std::endl;
	}

	std::cout << std::endl;

	if (!wsm->hasValidServerConfig() || !wsm->hasUsers()) {
		std::cout << toBold("No valid configuration was entered. Please re-run the command.") << std::endl;
		return false;
	} else {
		// Set the dirty flag, otherwise we wont save web-server.json
		wsm->setDirty();

		std::cout << toBold("Configuration finished")
			<< std::endl
			<< std::endl
			<< "You may now connect to the client via "
			<< "web browser by using the following address(es): "
			<< std::endl;

		if (plainServerConfig.hasValidConfig()) {
			std::cout << "http://<server address>:" << plainServerConfig.port.num() << std::endl;
		}

		if (tlsServerConfig.hasValidConfig()) {
			std::cout << "https://<server address>:" << tlsServerConfig.port.num() << std::endl;
			std::cout << std::endl;

			std::cout << toBold("NOTE:") << std::endl;
			std::cout << std::endl;
			std::cout << "When connecting to the client via HTTPS, the browser will warn you about a self-signed certificate. "
				<< "If you want the error to go away, you should search for information specific to your operating system about adding the site/certificate as trusted. "
				<< "When browsing within the local network, using HTTPS is generally not needed."
				<< std::endl;
		}
	}

	return true;
}

void ConfigPrompt::setPasswordMode(bool enable) noexcept {
    struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    if( enable )
        tty.c_lflag &= ~ECHO;
    else
        tty.c_lflag |= ECHO;

    (void) tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

bool ConfigPrompt::addUser(webserver::WebServerManager* wsm) {
	auto& um = wsm->getUserManager();

	std::string username, password;
	std::cout << toBold("The user will be created with administrative permissions. Users with restricted permissions can be added from the Web UI.");
	std::cout << std::endl;
	std::cout << std::endl;
	std::cout << "Enter username: ";
	std::getline(std::cin, username);
	std::cout << std::endl;

	auto user = um.getUser(username);
	if (user) {
		std::string input;
		std::cout << "A user with the same name exists. Do you want to change the password? (y/n): ";
		std::getline(std::cin, input);

		if (input != "y") {
			return false;
		}
	} else if (!webserver::WebUser::validateUsername(username)) {
		std::cout << "The username should contain only alphanumeric characters" << std::endl;
		return false;
	}

	setPasswordMode(true);
	ScopedFunctor([=] { setPasswordMode(false); });

	std::cout << "Enter password (input hidden): ";
	std::getline(std::cin, password);
	std::cout << std::endl;

	{
		std::string passwordConfirm;
		std::cout << "Retype password: ";
		std::getline(std::cin, passwordConfirm);

		std::cout << std::endl;
		if (passwordConfirm != password) {
			std::cout << "Passwords didn't match" << std::endl;
			return false;
		}
	}

	if (!user) {
		um.addUser(std::make_shared<webserver::WebUser>(username, password, true));
		std::cout << "The user " << username << " was added" << std::endl;
	} else {
		user->setPassword(password);
		um.updateUser(user, true);
		std::cout << "Password for the user " << username << " was updated" << std::endl;
	}

	return true;
}

bool ConfigPrompt::removeUser(webserver::WebServerManager* wsm) {
	auto& um = wsm->getUserManager();

	std::string username;
	std::cout << "Enter username to remove: ";
	std::getline(std::cin, username);

	auto ret = um.removeUser(username);
	if (ret) {
		std::cout << "The user " << username << " was removed" << std::endl;
	} else {
		std::cout << "The user " << username << " was not found" << std::endl;
	}

	return ret;
}

bool ConfigPrompt::listUsers(webserver::WebServerManager* wsm) {
	auto users = wsm->getUserManager().getUserNames();
	if (users.empty()) {
		std::cout << "No users exist" << std::endl;
	} else {
		std::cout << Util::listToString(users) << std::endl;
	}

	return false;
}

void ConfigPrompt::promptPort(webserver::ServerConfig& config_, const std::string& aProtocol) {
	auto port = config_.port.num();

	std::cout << "Enter " << aProtocol << " port (empty: " << port << ", 0 = disabled): ";

	std::string input;
	std::getline(std::cin, input);

	if (!input.empty()) {
		port = atoi(input.c_str());
		if (port < 0 || port > 65535) {
			std::cout << "Invalid port number\n";
			promptPort(config_, aProtocol);
			return;
		}

		std::cout << std::endl;
	}

	config_.port.setValue(port);
	if (port > 0) {
		std::cout << toBold(aProtocol + " port set to: ") << port << std::endl;
	} else {
		std::cout << toBold(aProtocol + " protocol disabled") << std::endl;
	}

	if (port > 0 && port < 1024) {
		std::cout << toBold("NOTE: Ports under 1024 require you to run the client as root. It's recommended to use ports higher than 1024") << std::endl;
	}
}

}
