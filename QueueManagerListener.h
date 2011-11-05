/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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
#include "noexcept.h"

namespace dcpp {

class QueueManagerListener {
public:
	virtual ~QueueManagerListener() { }
	template<int I>	struct X { enum { TYPE = I };  };

	typedef X<0> Added;
	typedef X<1> Finished;
	typedef X<2> Removed;
	typedef X<3> Moved;
	typedef X<4> SourcesUpdated;
	typedef X<5> StatusUpdated;
	typedef X<6> PartialList;

	typedef X<8> RecheckStarted;
	typedef X<9> RecheckNoFile;
	typedef X<10> RecheckFileTooSmall;
	typedef X<11> RecheckDownloadsRunning;
	typedef X<12> RecheckNoTree;
	typedef X<13> RecheckAlreadyFinished;
	typedef X<14> RecheckDone;
	
	typedef X<15> FileMoved;

	typedef X<16> BundleFinished;
	typedef X<17> BundleWaiting;
	typedef X<18> BundleRemoved;
	typedef X<19> BundleRenamed;
	typedef X<20> BundleTick;
	typedef X<21> BundleFilesMoved;
	typedef X<22> BundleUser;

	virtual void on(Added, QueueItem*) noexcept { }
	virtual void on(Finished, const QueueItem*, const string&, const Download*) noexcept { }
	virtual void on(Removed, const QueueItem*) noexcept { }
	virtual void on(Moved, const QueueItem*, const string&) noexcept { }
	virtual void on(SourcesUpdated, const QueueItem*) noexcept { }
	virtual void on(StatusUpdated, const QueueItem*) noexcept { }
	virtual void on(PartialList, const HintedUser&, const string&) noexcept { }

	virtual void on(BundleFinished, const BundlePtr) noexcept { }
	virtual void on(BundleWaiting, const BundlePtr) noexcept { }
	virtual void on(BundleRemoved, const BundlePtr) noexcept { }
	virtual void on(BundleRenamed, const BundlePtr) noexcept { }
	virtual void on(BundleTick, const BundlePtr) noexcept { }
	virtual void on(BundleFilesMoved, const BundlePtr) noexcept { }
	virtual void on(BundleUser, const string&, const HintedUser&) noexcept { }
	
	virtual void on(RecheckStarted, const string&) noexcept { }
	virtual void on(RecheckNoFile, const string&) noexcept { }
	virtual void on(RecheckFileTooSmall, const string&) noexcept { }
	virtual void on(RecheckDownloadsRunning, const string&) noexcept { }
	virtual void on(RecheckNoTree, const string&) noexcept { }
	virtual void on(RecheckAlreadyFinished, const string&) noexcept { }
	virtual void on(RecheckDone, const string&) noexcept { }

	virtual void on(FileMoved, const string&) noexcept { }
};

} // namespace dcpp

#endif // !defined(QUEUE_MANAGER_LISTENER_H)

/**
 * @file
 * $Id: QueueManagerListener.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
