/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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

#include "QueueManagerListener.h"
#include "UploadManagerListener.h"

#include "Speaker.h"
#include "Singleton.h"
#include "FinishedManagerListener.h"
#include "Util.h"
#include "User.h"
#include "MerkleTree.h"
#include "ClientManager.h"

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

	const tstring getText(uint8_t col) const {
		dcassert(col >= 0 && col < COLUMN_LAST);
		switch(col) {
			case COLUMN_FILE: return Text::toT(Util::getFileName(getTarget()));
			case COLUMN_DONE: return Text::toT(Util::formatTime("%Y-%m-%d %H:%M:%S", getTime()));
			case COLUMN_PATH: return Text::toT(Util::getFilePath(getTarget()));
			case COLUMN_NICK: return Text::toT(Util::toString(ClientManager::getInstance()->getNicks(getUser())));
			case COLUMN_HUB: return Text::toT(Util::toString(ClientManager::getInstance()->getHubNames(getUser()))); 
			case COLUMN_SIZE: return Util::formatBytesW(getSize());
			case COLUMN_SPEED: return Util::formatBytesW(getAvgSpeed()) + _T("/s");
			case COLUMN_TYPE: {
			tstring filetype = Text::toT(Util::getFileExt(Text::fromT(getText(COLUMN_FILE))));
						if(!filetype.empty() && filetype[0] == _T('.'))
							filetype.erase(0, 1);
						return filetype;
							  }
			default: return Util::emptyStringT;
		}
	}

	static int compareItems(const FinishedItem* a, const FinishedItem* b, uint8_t col) {
		switch(col) {
			case COLUMN_SPEED:	return compare(a->getAvgSpeed(), b->getAvgSpeed());
			case COLUMN_SIZE:	return compare(a->getSize(), b->getSize());
			default:			return lstrcmpi(a->getText(col).c_str(), b->getText(col).c_str());
		}
	}
	int getImageIndex() const;

	GETSET(string, target, Target);
	GETSET(string, tth, TTH);

	GETSET(int64_t, size, Size);
	GETSET(int64_t, avgSpeed, AvgSpeed);
	GETSET(time_t, time, Time);
	GETSET(HintedUser, user, User);

private:
	friend class FinishedManager;

};

class FinishedManager : public Singleton<FinishedManager>,
	public Speaker<FinishedManagerListener>, private QueueManagerListener, private UploadManagerListener
{
public:
	const FinishedItemList& lockList(bool upload = false) { cs.lock(); return upload ? uploads : downloads; }
	void unlockList() { cs.unlock(); }

	void remove(FinishedItemPtr item, bool upload = false);
	void removeAll(bool upload = false);

	/** Get file full path by tth to share */
	bool getTarget(const string& aTTH, string& target);
	bool handlePartialRequest(const TTHValue& tth, vector<uint16_t>& outPartialInfo);

private:
	friend class Singleton<FinishedManager>;
	
	FinishedManager();
	~FinishedManager();

	void on(QueueManagerListener::Finished, const QueueItemPtr, const string&, const HintedUser& aUser, int64_t aSpeed) noexcept;
	void on(UploadManagerListener::Complete, const Upload*) noexcept;

	CriticalSection cs;
	FinishedItemList downloads, uploads;
};

} // namespace dcpp

#endif // !defined(FINISHED_MANAGER_H)