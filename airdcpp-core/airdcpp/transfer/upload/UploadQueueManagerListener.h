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

#ifndef DCPLUSPLUS_DCPP_UPLOADQUEUEMANAGERLISTENER_H_
#define DCPLUSPLUS_DCPP_UPLOADQUEUEMANAGERLISTENER_H_

#include <airdcpp/forward.h>
#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/transfer/upload/UploadQueueItem.h>

namespace dcpp {

class UploadQueueManagerListener {
public:
	virtual ~UploadQueueManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<1> QueueAdd;
	typedef X<2> QueueUserRemove;
	typedef X<3> QueueItemRemove;
	typedef X<4> QueueUpdate;

	virtual void on(QueueAdd, const UploadQueueItemPtr&) noexcept { }
	virtual void on(QueueUserRemove, const UserPtr&) noexcept { }
	virtual void on(QueueItemRemove, const UploadQueueItemPtr&) noexcept { }
	virtual void on(QueueUpdate) noexcept { }
};

} // namespace dcpp

#endif /*UPLOADMANAGERLISTENER_H_*/
