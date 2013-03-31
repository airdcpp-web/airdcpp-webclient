/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#if !defined(CONNECTION_MANAGER_LISTENER_H)
#define CONNECTION_MANAGER_LISTENER_H

namespace dcpp {

class ConnectionQueueItem;

class ConnectionManagerListener {
public:
	virtual ~ConnectionManagerListener() { }
	template<int I>	struct X { enum { TYPE = I };  };

	typedef X<0> Added;
	typedef X<1> Connected;
	typedef X<2> Removed;
	typedef X<3> Failed;
	typedef X<4> StatusChanged;
	typedef X<5> UserUpdated;
	typedef X<6> Forced;

	virtual void on(Added, const ConnectionQueueItem*) noexcept { }
	virtual void on(Connected, const ConnectionQueueItem*) noexcept { }
	virtual void on(Removed, const ConnectionQueueItem*) noexcept { }
	virtual void on(Failed, const ConnectionQueueItem*, const string&) noexcept { }
	virtual void on(StatusChanged, const ConnectionQueueItem*) noexcept { }
	virtual void on(UserUpdated, const ConnectionQueueItem*) noexcept { }
	virtual void on(Forced, const ConnectionQueueItem*) noexcept { }
};

} // namespace dcpp

#endif // !defined(CONNECTION_MANAGER_LISTENER_H)