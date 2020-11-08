/*
* Copyright (C) 2011-2021 AirDC++ Project
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

#include <web-server/WebServerSettings.h>

#include <airdcpp/TimerManager.h>

namespace webserver {
	WebServerSettings::WebServerSettings(): 
		settings({
			{ "web_plain_port", ResourceManager::WEB_CFG_PORT, 5600, ApiSettingItem::TYPE_NUMBER, false, { 0, 65535 } },
			{ "web_plain_bind_address", ResourceManager::WEB_CFG_BIND_ADDRESS, "", ApiSettingItem::TYPE_STRING, true },

			{ "web_tls_port", ResourceManager::WEB_CFG_PORT, 5601, ApiSettingItem::TYPE_NUMBER, false, { 0, 65535 } },
			{ "web_tls_bind_address", ResourceManager::WEB_CFG_BIND_ADDRESS, "", ApiSettingItem::TYPE_STRING, true },

			{ "web_tls_certificate_path", ResourceManager::WEB_CFG_CERT_PATH, "", ApiSettingItem::TYPE_EXISTING_FILE_PATH, true },
			{ "web_tls_certificate_key_path", ResourceManager::WEB_CFG_CERT_KEY_PATH, "", ApiSettingItem::TYPE_EXISTING_FILE_PATH, true },

			{ "web_server_threads", ResourceManager::WEB_CFG_SERVER_THREADS, 4, ApiSettingItem::TYPE_NUMBER, false, { 1, 100 } },

			{ "default_idle_timeout", ResourceManager::WEB_CFG_IDLE_TIMEOUT, 20, ApiSettingItem::TYPE_NUMBER, false, { 0, MAX_INT_VALUE }, ResourceManager::MINUTES_LOWER },
			{ "ping_interval", ResourceManager::WEB_CFG_PING_INTERVAL, 30, ApiSettingItem::TYPE_NUMBER, false, { 1, 10000 }, ResourceManager::SECONDS_LOWER },
			{ "ping_timeout", ResourceManager::WEB_CFG_PING_TIMEOUT, 10, ApiSettingItem::TYPE_NUMBER, false, { 1, 10000 }, ResourceManager::SECONDS_LOWER },

			{ "extensions_debug_mode", ResourceManager::WEB_CFG_EXTENSIONS_DEBUG_MODE, false, ApiSettingItem::TYPE_BOOLEAN, false },
			{ "extensions_init_timeout", ResourceManager::WEB_CFG_EXTENSIONS_INIT_TIMEOUT, 5, ApiSettingItem::TYPE_NUMBER, false, { 1, 60 }, ResourceManager::SECONDS_LOWER },
		}) {}
}
