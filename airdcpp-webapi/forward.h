/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#ifndef DCPLUSPLUS_WEBSERVER_FORWARD_H
#define DCPLUSPLUS_WEBSERVER_FORWARD_H

#include <stdint.h>
#include <functional>
#include <memory>
#include <boost/detail/container_fwd.hpp>

#include <nlohmann/json_fwd.hpp>

#define CODE_UNPROCESSABLE_ENTITY 422

namespace webserver {
	class ApiRequest;

	class ContextMenuItem;
	typedef std::shared_ptr<ContextMenuItem> ContextMenuItemPtr;
	typedef std::vector<ContextMenuItemPtr> ContextMenuItemList;
	struct ContextMenuItemClickData;

	class GroupedContextMenuItem;
	typedef std::shared_ptr<GroupedContextMenuItem> GroupedContextMenuItemPtr;
	typedef std::vector<GroupedContextMenuItemPtr> GroupedContextMenuItemList;

	class Extension;
	typedef std::shared_ptr<Extension> ExtensionPtr;
	typedef std::vector<ExtensionPtr> ExtensionList;

	class Session;
	typedef std::shared_ptr<Session> SessionPtr;
	typedef std::vector<SessionPtr> SessionList;
	typedef uint32_t LocalSessionId;

	// typedef std::map<string, json> SettingValueMap;

	class Timer;
	typedef std::shared_ptr<Timer> TimerPtr;

	class WebSocket;
	typedef std::shared_ptr<WebSocket> WebSocketPtr;

	class WebServerManager;

	typedef std::function<void()> Callback;
	typedef std::function<void(const std::string&)> MessageCallback;

	using json = nlohmann::json;
}

#endif // !defined(DCPLUSPLUS_WEBSERVER_FORWARD_H)
