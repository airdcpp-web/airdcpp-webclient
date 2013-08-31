/*
 * Copyright (C) 2012 AirDC++ Project
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

namespace dcpp {

class DirectoryListingManagerListener {
public:
	virtual ~DirectoryListingManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	/*typedef X<0> Loading;
	typedef X<1> LoadingFinished;
	typedef X<2> LoadingError;
	typedef X<3> ScanFailed;*/

	typedef X<0> OpenListing;
	typedef X<1> PromptAction;

	typedef std::function<void (bool)> completionF;

	virtual void on(OpenListing, DirectoryListing*, const string& /*aDir*/, const string& /*aXML*/) noexcept { }
	virtual void on(PromptAction, completionF aF, const string & /*msg*/) noexcept { aF(true); }
	/*virtual void on(Loading, const string) noexcept { }
	virtual void on(LoadingFinished, const AutoSearchPtr&) noexcept { }
	virtual void on(LoadingError, const AutoSearchPtr&, int) noexcept { }
	virtual void on(ScanFailed, const AutoSearchPtr&, int) noexcept { }*/
};

} // namespace dcpp

#endif // !defined(DIRECTORYLISTING_MANAGER_LISTENER_H)