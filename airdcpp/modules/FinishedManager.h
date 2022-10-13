/*
 * Copyright (C) 2001-2022 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_FINISHED_MANAGER_H
#define DCPLUSPLUS_DCPP_FINISHED_MANAGER_H

#include "FinishedManagerListener.h"

#include <airdcpp/QueueManagerListener.h>
#include <airdcpp/UploadManagerListener.h>

#include <airdcpp/ClientManager.h>
#include <airdcpp/HintedUser.h>
#include <airdcpp/MerkleTree.h>
#include <airdcpp/Speaker.h>
#include <airdcpp/Singleton.h>
#include <airdcpp/Util.h>
#include <airdcpp/User.h>

namespace dcpp {

class FinishedItem
{
public:
	enum {
		COLUMN_FIRST,
		COLUMN_FILE = COLUMN_FIRST,
		COLUMN_DONE,
		COLUMN_PATH,
		COLUMN_NICK,
		COLUMN_HUB,
		COLUMN_SIZE,
		COLUMN_SPEED,
		COLUMN_TYPE,
		COLUMN_LAST
	};

	FinishedItem(string const& aTarget, const HintedUser& aUser,  int64_t aSize, int64_t aSpeed, time_t aTime, const string& aTTH = Util::emptyString) : 
		target(aTarget), user(aUser), size(aSize), avgSpeed(aSpeed), time(aTime), tth(aTTH)
	{
	}

#ifdef _WIN32
	const tstring getText(uint8_t col) const;
	static int compareItems(const FinishedItem* a, const FinishedItem* b, uint8_t col) {
		switch (col) {
		case COLUMN_SPEED:	return compare(a->getAvgSpeed(), b->getAvgSpeed());
		case COLUMN_SIZE:	return compare(a->getSize(), b->getSize());
		default:			return Util::stricmp(a->getText(col).c_str(), b->getText(col).c_str());
		}
	}
#endif
	int getImageIndex() const;

	GETSET(string, target, Target);
	GETSET(HintedUser, user, User);
	GETSET(int64_t, size, Size);
	GETSET(int64_t, avgSpeed, AvgSpeed);
	GETSET(time_t, time, Time);
	GETSET(string, tth, TTH);
private:
	friend class FinishedManager;

};

class FinishedManager : public Singleton<FinishedManager>,
	public Speaker<FinishedManagerListener>, private QueueManagerListener, private UploadManagerListener
{
public:
	const FinishedItemList& lockList() { cs.lock(); return uploads; }
	void unlockList() { cs.unlock(); }

	void remove(FinishedItemPtr item);
	void removeAll();

private:
	friend class Singleton<FinishedManager>;
	
	FinishedManager();
	~FinishedManager();

	void on(UploadManagerListener::Complete, const Upload*) noexcept;

	CriticalSection cs;
	FinishedItemList uploads;
};

} // namespace dcpp

#endif // !defined(FINISHED_MANAGER_H)
