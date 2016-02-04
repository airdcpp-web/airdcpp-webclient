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

namespace airdcppd {

CDMDebug::CDMDebug(bool aClientCommands, bool aHubCommands) : showHubCommands(aHubCommands), showClientCommands(aClientCommands) {
	DebugManager::getInstance()->addListener(this);
}

CDMDebug::~CDMDebug() {
	DebugManager::getInstance()->removeListener(this);
}

void CDMDebug::on(DebugManagerListener::DebugCommand, const string& aLine, uint8_t aType, uint8_t aDirection, const string& ip) noexcept{
	string cmd;
	switch (aType) {
	case DebugManager::TYPE_HUB:
		if (!showHubCommands)
			return;
		cmd = "Hub:";
		break;
	case DebugManager::TYPE_CLIENT:
		if (!showClientCommands)
			return;
		cmd = "Client (TCP):";
		break;
	case DebugManager::TYPE_CLIENT_UDP:
		if (!showClientCommands)
			return;
		cmd = "Client (UDP):";
		break;
	default: dcassert(0);
	}

	cmd += "\t";

	if (aDirection == DebugManager::INCOMING) {
		cmd += "[Incoming]";
	} else {
		cmd += "[Outgoing]";
	}

	cmd += "[" + ip + "]\t" + aLine;

	printf("%s\n", cmd.c_str());
}

}
