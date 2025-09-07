/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#ifndef DCPLUSPLUS_WEBSERVER_FORWARD_H
#define DCPLUSPLUS_WEBSERVER_FORWARD_H

#include <stdint.h>
#include <functional>
#include <memory>

#include <nlohmann/json_fwd.hpp>

#define CODE_UNPROCESSABLE_ENTITY 422

namespace webserver {
	class ApiRequest;

	class ContextMenuItem;
	using ContextMenuItemPtr = std::shared_ptr<ContextMenuItem>;
	using ContextMenuItemList = std::vector<ContextMenuItemPtr>;
	struct ContextMenuItemClickData;

	class GroupedContextMenuItem;
	using GroupedContextMenuItemPtr = std::shared_ptr<GroupedContextMenuItem>;
	using GroupedContextMenuItemList = std::vector<GroupedContextMenuItemPtr>;

	class Extension;
	using ExtensionPtr = std::shared_ptr<Extension>;
	using ExtensionList = std::vector<ExtensionPtr>;

	class Session;
	using SessionPtr = std::shared_ptr<Session>;
	using SessionList = std::vector<SessionPtr>;
	using SessionCallback = std::function<void(const SessionPtr&)>;
	using LocalSessionId = uint32_t;

	class Timer;
	using TimerPtr = std::shared_ptr<Timer>;

	class WebSocket;
	using WebSocketPtr = std::shared_ptr<WebSocket>;

	class WebServerManager;

	using Callback = std::function<void ()>;
	using MessageCallback = std::function<void (const std::string &)>;

	class WebUser;
	using WebUserPtr = std::shared_ptr<WebUser>;
	using WebUserList = std::vector<WebUserPtr>;
}

#endif // !defined(DCPLUSPLUS_WEBSERVER_FORWARD_H)
