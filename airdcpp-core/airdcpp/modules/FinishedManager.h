/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_FINISHED_MANAGER_H
#define DCPLUSPLUS_DCPP_FINISHED_MANAGER_H

#include "FinishedManagerListener.h"

#include <airdcpp/queue/QueueManagerListener.h>
#include <airdcpp/transfer/upload/UploadManagerListener.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/user/HintedUser.h>
#include <airdcpp/hash/value/MerkleTree.h>
#include <airdcpp/core/Speaker.h>
#include <airdcpp/core/Singleton.h>
#include <airdcpp/util/Util.h>
#include <airdcpp/user/User.h>

namespace dcpp {

class FinishedManager : public Singleton<FinishedManager>,
	public Speaker<FinishedManagerListener>, private QueueManagerListener, private UploadManagerListener
{
public:
	const FinishedItemList& lockList() { cs.lock(); return uploads; }
	void unlockList() { cs.unlock(); }

	void remove(const FinishedItemPtr& item);
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
