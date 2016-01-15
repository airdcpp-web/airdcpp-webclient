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

#ifndef DCPLUSPLUS_DCPP_VIEWFILE_MANAGER_H_
#define DCPLUSPLUS_DCPP_VIEWFILE_MANAGER_H_

#include "forward.h"
#include "stdinc.h"

#include "ViewFileManagerListener.h"
#include "ViewFile.h"

#include "CriticalSection.h"
#include "QueueManagerListener.h"
#include "Singleton.h"
#include "Speaker.h"


namespace dcpp {
	class ViewFileManager : public Singleton<ViewFileManager>, public Speaker<ViewFileManagerListener>, public QueueManagerListener {
	public:
		typedef unordered_map<TTHValue, ViewFilePtr> ViewFileMap;

		ViewFileManager() noexcept;
		~ViewFileManager() noexcept;

		ViewFileMap getFiles() const noexcept;
		bool removeFile(const TTHValue& aTTH) noexcept;

		ViewFilePtr getFile(const TTHValue& aTTH) const noexcept;
		bool setRead(const TTHValue& aTTH) noexcept;
	private:
		static bool isViewedItem(const QueueItemPtr& aQI) noexcept;

		void on(QueueManagerListener::Added, QueueItemPtr& aQI) noexcept;
		void on(QueueManagerListener::Finished, const QueueItemPtr& qi, const string& dir, const HintedUser& aUser, int64_t aSpeed) noexcept;
		void on(QueueManagerListener::Removed, const QueueItemPtr& qi, bool finished) noexcept;

		void onFileUpdated(const TTHValue& aTTH) noexcept;

		friend class Singleton<ViewFileManager>;

		mutable SharedMutex cs;

		ViewFileMap viewFiles;
	};

}

#endif /*DCPLUSPLUS_DCPP_VIEWFILE_MANAGER_H_ */