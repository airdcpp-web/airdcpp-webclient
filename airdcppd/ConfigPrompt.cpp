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

#include "ConfigPrompt.h"

#include <airdcpp/stdinc.h>
#include <airdcpp/ScopedFunctor.h>
#include <airdcpp/Util.h>

#include <web-server/WebServerManager.h>

namespace airdcppd {

std::string ConfigPrompt::toBold(const std::string& aText) {
	return "\e[1m" + aText + "\e[0m";
}

ConfigPrompt::ConfigF ConfigPrompt::checkArgs() {
	function<bool(webserver::WebServerManager*)> f = nullptr;
	if (Util::hasStartupParam("-configure")) {
		f = &runConfigure;
	} else if (Util::hasStartupParam("-add-user")) {
		f = &addUser;
	} else if (Util::hasStartupParam("-remove-user")) {
		f = &removeUser;
	} else if (Util::hasStartupParam("-list-users")) {
		f = &listUsers;
	}

	if (!f) {
		return nullptr;
	}

	auto ret = [=] {
		webserver::WebServerManager::newInstance();
		ScopedFunctor([=] { webserver::WebServerManager::deleteInstance(); });

		auto wsm = webserver::WebServerManager::getInstance();
		wsm->load();

		cout << std::endl;
		cout << std::endl;

		auto save = f(wsm);

		cout << std::endl;
		if (save) {
			if (wsm->save([&](const string& aError) {
				cout << toBold("Failed to save the configuration to " + wsm->getConfigPath()) << ": " << aError << std::endl;
			})) {
				cout << toBold("Configuration was written to " + wsm->getConfigPath()) << std::endl;
			}
		}
	};

	return ret;
}

bool ConfigPrompt::runConfigure(webserver::WebServerManager* wsm) {
	auto& plainServerConfig = wsm->getPlainServerConfig();
	auto& tlsServerConfig = wsm->getTlsServerConfig();

	promptPort(plainServerConfig, "HTTP", 5334);
	cout << std::endl;

	promptPort(tlsServerConfig, "HTTPS", 5336);
	cout << std::endl;

	if (!wsm->getUserManager().hasUsers()) {
		cout << toBold("No existing users were found, adding new one.") << std::endl;

		addUser(wsm);
	} else {
		cout << toBold("Configured users were found. Use the separate commands if you want to modify them (see help).") << std::endl;
	}

	cout << std::endl;

	if (!wsm->hasValidConfig()) {
		cout << toBold("No valid configuration was entered. Please re-run the command.") << std::endl;
		return false;
	} else {
		cout << toBold("Configuration finished")
			<< std::endl
			<< std::endl
			<< "You may now connect to the client via "
			<< "web browser by using the following address(es): "
			<< std::endl;

		if (plainServerConfig.hasValidConfig()) {
			cout << "http://<server address>:" << plainServerConfig.getPort() << std::endl;
		}

		if (tlsServerConfig.hasValidConfig()) {
			cout << "https://<server address>:" << tlsServerConfig.getPort() << std::endl;
			cout << std::endl;

			cout << toBold("NOTE:") << std::endl;
			cout << std::endl;
			cout << "When connecting to the client via HTTPS, the browser will warn you about a self-signed certificate. "
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
	cout << "Enter username: ";
	cin >> username;
	cout << std::endl;

	if (um.hasUser(username)) {
		string input;
		cout << "A user with the same name exists. Do you want to change the password? (y/n): ";
		cin >> input;

		if (input != "y") {
			return false;
		}
	}

	setPasswordMode(true);
	ScopedFunctor([=] { setPasswordMode(false); });

	cout << "Enter password (input hidden): ";
	cin >> password;
	cout << std::endl;

	{
		string passwordConfirm;
		cout << "Retype password: ";
		cin >> passwordConfirm;

		cout << std::endl;
		if (passwordConfirm != password) {
			cout << "Passwords didn't match" << std::endl;
			return false;
		}
	}

	auto added = um.addUser(username, password);
	if (added) {
		cout << "The user " << username << " was added" << std::endl;
	} else {
		cout << "Password for the user " << username << " was updated" << std::endl;
	}

	return true;
}

bool ConfigPrompt::removeUser(webserver::WebServerManager* wsm) {
	auto& um = wsm->getUserManager();

	std::string username;
	cout << "Enter username to remove" << std::endl;
	cin >> username;
	cout << std::endl;

	auto ret = um.removeUser(username);
	if (ret) {
		cout << "The user " << username << " was removed" << std::endl;
	} else {
		cout << "The user " << username << " was not found" << std::endl;
	}

	return ret;
}

bool ConfigPrompt::listUsers(webserver::WebServerManager* wsm) {
	auto users = wsm->getUserManager().getUserNames();
	if (users.empty()) {
		cout << "No users exist" << std::endl;
	} else {
		cout << Util::listToString(users) << std::endl;
	}

	return false;
}

void ConfigPrompt::promptPort(webserver::ServerConfig& config_, const std::string& aProtocol, int aDefaultPort) {
	auto port = config_.getPort();
	if (port > 0) {
		aDefaultPort = port;
	}

	cout << "Enter " << aProtocol << " port (empty: " << aDefaultPort << ", 0 = disabled): ";

	string input;
	std::getline(std::cin, input);

	if (input.empty()) {
		port = aDefaultPort;
	} else {
		port = atoi(input.c_str());
		if (port < 0 || port > 65535) {
			cout << "Invalid port number\n";
			promptPort(config_, aProtocol, aDefaultPort);
			return;
		}

		cout << std::endl;
	}

	config_.setPort(port);
	if (port > 0) {
		cout << toBold(aProtocol + " port set to: ") << port << std::endl;
	} else {
		cout << toBold(aProtocol + " protocol disabled") << std::endl;
	}

	if (port > 0 && port < 1024) {
		cout << toBold("NOTE: Ports under 1024 require you to run the client as root. It's recommended to use ports higher than 1024") << std::endl;
	}
}

}
