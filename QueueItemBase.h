/*
 * Copyright (C) 2011-2014 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_QUEUEITEM_BASE_H_
#define DCPLUSPLUS_DCPP_QUEUEITEM_BASE_H_

#include <string>
#include <set>

#include "Flags.h"
#include "forward.h"
#include "GetSet.h"

namespace dcpp {

using std::string;

class QueueItemBase : public Flags {
public:
	enum DownloadType {
		TYPE_NONE,
		TYPE_ANY,
		TYPE_SMALL,
		TYPE_MCN_NORMAL
	};

	enum Priority {
		DEFAULT = -1,
		PAUSED_FORCE = 0,
		PAUSED,
		LOWEST,
		LOW,
		NORMAL,
		HIGH,
		HIGHEST,
		LAST
	};

	QueueItemBase(const string& aTarget, int64_t aSize, Priority aPriority, time_t aAdded, Flags::MaskType aFlags = 0);

	virtual void setTarget(const string& aTarget) = 0;
	const DownloadList& getDownloads() { return downloads; }

	GETSET(Priority, priority, Priority);
	GETSET(bool, autoPriority, AutoPriority);
	GETSET(time_t, added, Added);
	IGETSET(time_t, fileFinished, FileFinished, 0);
	GETSET(string, target, Target);
	GETSET(DownloadList, downloads, Downloads);
	GETSET(int64_t, size, Size);

	bool isPausedPrio() const { return priority == PAUSED_FORCE || priority == PAUSED; }

};

}

#endif /* DCPLUSPLUS_DCPP_QUEUEITEM_BASE_H_ */