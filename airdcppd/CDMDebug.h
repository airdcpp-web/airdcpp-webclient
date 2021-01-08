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

#ifndef AIRDCPPD_CDMDEBUG_H
#define AIRDCPPD_CDMDEBUG_H

// #include "stdinc.h"

#include <airdcpp/DebugManager.h>

namespace dcpp {
        class SimpleXML;
}

#include <web-server/WebServerManagerListener.h>

namespace airdcppd {

class CDMDebug : private DebugManagerListener, private WebServerManagerListener {

public:
	CDMDebug(bool aClientCommands, bool aHubCommands, bool aWebCommands);
	~CDMDebug();
private:
	void on(DebugManagerListener::DebugCommand, const string& aLine, uint8_t aType, uint8_t aDirection, const string& ip) noexcept override;
	void on(WebServerManagerListener::Data, const string& aData, TransportType aType, Direction aDirection, const string& aIP) noexcept override;

	bool showHubCommands = false;
	bool showClientCommands = false;
	bool showWebCommands = false;

	static void printMessage(const string& aType, bool aIncoming, const string& aData, const string& aIP) noexcept;
};

} // namespace airdcppd

#endif //
