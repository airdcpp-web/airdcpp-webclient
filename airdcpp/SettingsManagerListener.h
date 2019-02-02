/*
* Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SETTINGSMANAGER_LISTENER_H_
#define DCPLUSPLUS_DCPP_SETTINGSMANAGER_LISTENER_H_


#include "forward.h"

namespace dcpp {

class SettingsManagerListener {
public:
	virtual ~SettingsManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> Load; // Called when the XML file is being loaded. Note that the event is not fired on initial run (when no setting file exist)
	typedef X<1> LoadCompleted; // Called when loading of settings has completed (even if no XML file existed)
	typedef X<2> Save;
	typedef X<3> ReloadPages; // if a settingspage modifies properties in another page, fire this to update the pages.
	typedef X<4> Cancel; // PropertiesDialog fires this when CANCEL is hit

	virtual void on(Load, SimpleXML&) noexcept { }
	virtual void on(LoadCompleted, bool) noexcept { }
	virtual void on(Save, SimpleXML&) noexcept { }
	virtual void on(ReloadPages, int) noexcept { }
	virtual void on(Cancel, int) noexcept { }
};

}

#endif