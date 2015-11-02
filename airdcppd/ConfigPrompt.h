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

#ifndef AIRDCPPD_CONFIG_PROMPT_H
#define AIRDCPPD_CONFIG_PROMPT_H

#include <functional>
#include <string>

namespace webserver {
	struct ServerConfig;
	class WebServerManager;
}

namespace airdcppd {

class ConfigPrompt {

public:
	typedef std::function<void()> ConfigF;
	static ConfigF checkArgs();
	
	static void setPasswordMode(bool enabled) noexcept;
private:
	static bool runConfigure(webserver::WebServerManager* wsm);
	
	static bool addUser(webserver::WebServerManager* wsm);
	static bool removeUser(webserver::WebServerManager* wsm);
	static bool listUsers(webserver::WebServerManager* wsm);

	static std::string toBold(const std::string& aText);
	static void promptPort(webserver::ServerConfig& config_, const std::string& aText, int aDefaultPort);
};

} // namespace airdcppd

#endif //