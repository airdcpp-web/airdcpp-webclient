/*
* Copyright (C) 2011-2016 AirDC++ Project
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
#include "ViewFile.h"

#include "AirUtil.h"
#include "File.h"
#include "TimerManager.h"

namespace dcpp {
	ViewFile::ViewFile(const string& aTarget, const TTHValue& aTTH, bool aIsText, bool aIsLocalFile, UpdateF&& aUpdateFunction) noexcept :
		TrackableDownloadItem(aIsLocalFile), path(aTarget), tth(aTTH), timeCreated(GET_TIME()),
		updateFunction(aUpdateFunction), text(aIsText), localFile(aIsLocalFile) {

	}

	ViewFile::~ViewFile() noexcept {
		if (!localFile) {
			File::deleteFile(path);
		}
	}

	string ViewFile::getDisplayName() const noexcept {
		return localFile ? Util::getFileName(path) : AirUtil::fromOpenFileName(Util::getFileName(path));
	}

	void ViewFile::onStateChanged() noexcept {
		updateFunction(tth);
	}

} //dcpp