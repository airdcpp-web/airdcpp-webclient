/*
 * Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_QUEUE_MANAGER_LISTENER_H
#define DCPLUSPLUS_DCPP_QUEUE_MANAGER_LISTENER_H

#include "forward.h"
#include "QueueItem.h"
#include "HashedFile.h"

namespace dcpp {

class QueueManagerListener {
public:
	virtual ~QueueManagerListener() { }
	template<int I>	struct X { enum { TYPE = I };  };

	typedef X<0> Added;
	typedef X<1> Finished;
	typedef X<2> Removed;
	typedef X<3> SourcesUpdated;
	typedef X<4> StatusUpdated;
	typedef X<5> PartialList;
	typedef X<6> SourceFilesUpdated;

	typedef X<7> FileRecheckStarted;
	typedef X<8> FileRecheckFailed;
	typedef X<9> FileRecheckDone;
	
	typedef X<15> BundleSources;

	typedef X<16> BundleRemoved;
	typedef X<17> BundleSize;
	typedef X<18> BundleUser;
	typedef X<19> BundlePriority;
	typedef X<20> BundleAdded;

	typedef X<21> FileHashed;
	typedef X<22> BundleStatusChanged;

	virtual void on(Added, QueueItemPtr&) noexcept { }
	virtual void on(Finished, const QueueItemPtr&, const string&, const HintedUser&, int64_t) noexcept { }
	virtual void on(Removed, const QueueItemPtr&, bool) noexcept { }
	virtual void on(SourcesUpdated, const QueueItemPtr&) noexcept { }
	virtual void on(StatusUpdated, const QueueItemPtr&) noexcept { }
	virtual void on(PartialList, const HintedUser&, const string&, const string&) noexcept { }
	virtual void on(SourceFilesUpdated, const UserPtr&) noexcept { }

	virtual void on(BundleSources, const BundlePtr&) noexcept { }
	virtual void on(BundleRemoved, const BundlePtr&) noexcept { }
	virtual void on(BundleSize, const BundlePtr&) noexcept { }
	virtual void on(BundlePriority, const BundlePtr&) noexcept { }
	virtual void on(BundleAdded, const BundlePtr&) noexcept { }
	virtual void on(FileHashed, const string& /* fileName */, HashedFile& /* fileInfo */) noexcept { }
	virtual void on(BundleStatusChanged, const BundlePtr&) noexcept { }
	
	virtual void on(FileRecheckStarted, const string&) noexcept { }
	virtual void on(FileRecheckFailed, const QueueItemPtr&, const string&) noexcept{ }
	virtual void on(FileRecheckDone, const string&) noexcept { }
};

} // namespace dcpp

#endif // !defined(QUEUE_MANAGER_LISTENER_H)