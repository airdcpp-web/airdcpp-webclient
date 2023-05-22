/*
 * Copyright (C) 2011-2023 AirDC++ Project
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
#include "Priority.h"

namespace dcpp {

using std::string;

typedef uint32_t QueueToken;
typedef unordered_set<QueueToken> QueueTokenSet;
class QueueItemBase : public Flags {
public:
	enum DownloadType {
		TYPE_NONE,
		TYPE_ANY,
		TYPE_SMALL,
		TYPE_MCN_NORMAL
	};

	QueueItemBase(const string& aTarget, int64_t aSize, Priority aPriority, time_t aAdded, QueueToken aToken, Flags::MaskType aFlags);

	const DownloadList& getDownloads() { return downloads; }

	GETSET(Priority, priority, Priority);
	GETSET(bool, autoPriority, AutoPriority);
	GETSET(time_t, timeAdded, TimeAdded);
	IGETSET(time_t, timeFinished, TimeFinished, 0);
	GETSET(DownloadList, downloads, Downloads);
	GETSET(int64_t, size, Size);

	virtual bool isPausedPrio() const noexcept { return priority == Priority::PAUSED_FORCE || priority == Priority::PAUSED; }

	QueueToken getToken() const noexcept {
		return token;
	}

	string getStringToken() const noexcept;

	const string& getTarget() const noexcept {
		return target;
	}

	double getPercentage(int64_t aDownloadedBytes) const noexcept;

	struct SourceCount {
		const size_t online;
		const size_t total;

		string format() const noexcept;
		static int compare(const SourceCount& a, const SourceCount& b) noexcept;
	};
protected:
	QueueToken token;
	const string target;
};

}

#endif /* DCPLUSPLUS_DCPP_QUEUEITEM_BASE_H_ */