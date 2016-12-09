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

#include "CDMDebug.h"

#include <web-server/WebServerManager.h>

namespace airdcppd {

CDMDebug::CDMDebug(bool aClientCommands, bool aHubCommands, bool aWebCommands) : showHubCommands(aHubCommands), showClientCommands(aClientCommands), showWebCommands(aWebCommands) {
	DebugManager::getInstance()->addListener(this);
	WebServerManager::getInstance()->addListener(this);
}

CDMDebug::~CDMDebug() {
	DebugManager::getInstance()->removeListener(this);
	WebServerManager::getInstance()->removeListener(this);
}

void CDMDebug::printMessage(const string& aType, bool aIncoming, const string& aData, const string& aIP) noexcept {
	string cmd(aType + ":\t");

	if (aIncoming) {
		cmd += "[Incoming]";
	} else {
		cmd += "[Outgoing]";
	}

	cmd += "[" + aIP + "]\t" + aData;
	
	printf("%s\n", cmd.c_str());
}

void CDMDebug::on(WebServerManagerListener::Data, const string& aData, TransportType aType, Direction aDirection, const string& aIP) noexcept {
	if (!showWebCommands) {
		return;
	}

	string type;
	switch (aType) {
		case TransportType::TYPE_HTTP_API:
			type = "API (HTTP)";
			break;
		case TransportType::TYPE_SOCKET:
			type = "API (socket)";
			break;
		case TransportType::TYPE_HTTP_FILE:
			type = "HTTP file request";
			break;
		default: dcassert(0);
	}

	printMessage(type, aDirection == Direction::INCOMING, aData, aIP);
}

void CDMDebug::on(DebugManagerListener::DebugCommand, const string& aLine, uint8_t aType, uint8_t aDirection, const string& aIP) noexcept{
	string type;
	switch (aType) {
	case DebugManager::TYPE_HUB:
		if (!showHubCommands)
			return;
		type = "Hub";
		break;
	case DebugManager::TYPE_CLIENT:
		if (!showClientCommands)
			return;
		type = "Client (TCP)";
		break;
	case DebugManager::TYPE_CLIENT_UDP:
		if (!showClientCommands)
			return;
		type = "Client (UDP)";
		break;
	default: dcassert(0);
	}

	printMessage(type, aDirection == DebugManager::INCOMING, aLine, aIP);
}

}
