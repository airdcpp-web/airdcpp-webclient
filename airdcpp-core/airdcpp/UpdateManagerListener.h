/*
 * Copyright (C) 2006-2011 Crise, crise<at>mail.berlios.de
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

#ifndef UPDATEMANAGER_LISTENER_H
#define UPDATEMANAGER_LISTENER_H

#include "forward.h"
#include "typedefs.h"
#include <string>

// #include "UpdateVersion.h"

namespace dcpp {

struct UpdateVersion;

class UpdateManagerListener {
public:
	virtual ~UpdateManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> UpdateAvailable;
	typedef X<1> BadVersion;
	typedef X<2> UpdateFailed;
	typedef X<3> UpdateComplete;
	typedef X<4> VersionFileDownloaded;

	typedef X<5> SettingUpdated;

	typedef X<6> LanguageDownloading;
	typedef X<7> LanguageFinished;
	typedef X<8> LanguageFailed;

	virtual void on(UpdateAvailable, const string& /*title*/, const string& /*message*/, const UpdateVersion&) noexcept { }
	virtual void on(BadVersion, const string& /*message*/, const UpdateVersion&) noexcept { }
	virtual void on(UpdateFailed, const string& /*line*/) noexcept { }
	virtual void on(UpdateComplete, const string& /*updater*/) noexcept { }
	virtual void on(VersionFileDownloaded, SimpleXML&) noexcept { }

	virtual void on(SettingUpdated, size_t /*key*/, const string& /*value*/) noexcept { }

	virtual void on(LanguageDownloading) noexcept { }
	virtual void on(LanguageFinished) noexcept { }
	virtual void on(LanguageFailed, const string& /*updater*/) noexcept { }
};

}

#endif // !defined(UPDATEMANAGER_LISTENER_H)