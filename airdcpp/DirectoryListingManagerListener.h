/*
 * Copyright (C) 2012-2021 AirDC++ Project
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

#ifndef DIRECTORYLISTING_MANAGER_LISTENER_H
#define DIRECTORYLISTING_MANAGER_LISTENER_H

#include "forward.h"
#include "BundleInfo.h"

namespace dcpp {

class DirectoryListingManagerListener {
public:
	virtual ~DirectoryListingManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> ListingCreated;
	typedef X<1> OpenListing;
	typedef X<2> ListingClosed;

	typedef X<3> DirectoryDownloadAdded;
	typedef X<4> DirectoryDownloadRemoved;
	typedef X<5> DirectoryDownloadProcessed;
	typedef X<6> DirectoryDownloadFailed;

	virtual void on(ListingCreated, const DirectoryListingPtr&) noexcept { }
	virtual void on(OpenListing, const DirectoryListingPtr&, const string& /*aDir*/, const string& /*aXML*/) noexcept { }
	virtual void on(ListingClosed, const DirectoryListingPtr&) noexcept { }

	virtual void on(DirectoryDownloadAdded, const DirectoryDownloadPtr&) noexcept { }
	virtual void on(DirectoryDownloadRemoved, const DirectoryDownloadPtr&) noexcept { }
	virtual void on(DirectoryDownloadProcessed, const DirectoryDownloadPtr&, const DirectoryBundleAddInfo&, const string&) noexcept { }
	virtual void on(DirectoryDownloadFailed, const DirectoryDownloadPtr&, const string&) noexcept { }
};

} // namespace dcpp

#endif // !defined(DIRECTORYLISTING_MANAGER_LISTENER_H)