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

#ifndef AIRDCPPD_CDMDEBUG_H
#define AIRDCPPD_CDMDEBUG_H

#include <airdcpp/stdinc.h>
#include <airdcpp/DebugManager.h>

namespace airdcppd {

using namespace dcpp;

class CDMDebug : private DebugManagerListener {

public:
	CDMDebug(bool aClientCommands, bool aHubCommands);
	~CDMDebug();
private:
	void on(DebugManagerListener::DebugCommand, const string& aLine, uint8_t aType, uint8_t aDirection, const string& ip) noexcept;
	
	bool showHubCommands = false;
	bool showClientCommands = false;
};

} // namespace airdcppd

#endif //
