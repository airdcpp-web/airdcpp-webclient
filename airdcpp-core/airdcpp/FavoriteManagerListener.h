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

#ifndef DCPLUSPLUS_DCPP_FAVORITEMANAGERLISTENER_H_
#define DCPLUSPLUS_DCPP_FAVORITEMANAGERLISTENER_H_

#include "forward.h"

namespace dcpp {

class FavoriteManagerListener {
public:
	virtual ~FavoriteManagerListener() { }
	template<int I>	struct X { enum { TYPE = I };  };

	typedef X<0> DownloadStarting;
	typedef X<1> DownloadFailed;
	typedef X<2> DownloadFinished;

	typedef X<3> FavoriteHubAdded;
	typedef X<4> FavoriteHubRemoved;
	typedef X<5> FavoriteHubUpdated;
	typedef X<6> FavoriteHubsUpdated;

	typedef X<7> FavoriteUserAdded;
	typedef X<8> FavoriteUserRemoved;
	typedef X<9> FavoriteUserUpdated;

	typedef X<10> LoadedFromCache;
	typedef X<11> Corrupted;	

	typedef X<12> RecentAdded;
	typedef X<13> RecentRemoved;
	typedef X<14> RecentUpdated;

	typedef X<15> FavoriteDirectoriesUpdated;

	virtual void on(DownloadStarting, const string&) noexcept { }
	virtual void on(DownloadFailed, const string&) noexcept { }
	virtual void on(DownloadFinished, const string&, bool) noexcept { }

	virtual void on(FavoriteHubAdded, const FavoriteHubEntryPtr&) noexcept { }
	virtual void on(FavoriteHubRemoved, const FavoriteHubEntryPtr&) noexcept {}
	virtual void on(FavoriteHubUpdated, const FavoriteHubEntryPtr&) noexcept { }
	virtual void on(FavoriteHubsUpdated) noexcept { }

	virtual void on(FavoriteUserAdded, const FavoriteUser&) noexcept { }
	virtual void on(FavoriteUserRemoved, const FavoriteUser&) noexcept { }
	virtual void on(FavoriteUserUpdated, const UserPtr&) noexcept { }

	virtual void on(LoadedFromCache, const string&, const string&) noexcept { }
	virtual void on(Corrupted, const string&) noexcept { }

	virtual void on(RecentAdded, const RecentHubEntryPtr&) noexcept {}
	virtual void on(RecentRemoved, const RecentHubEntryPtr&) noexcept {}
	virtual void on(RecentUpdated, const RecentHubEntryPtr&) noexcept {}

	virtual void on(FavoriteDirectoriesUpdated) noexcept { }
};

} // namespace dcpp

#endif 
