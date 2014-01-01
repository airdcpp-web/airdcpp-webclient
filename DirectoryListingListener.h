/*
 * Copyright (C) 2011-2014 AirDC++ Project
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

#ifndef DIRECTORYLISTING_LISTENER_H
#define DIRECTORYLISTING_LISTENER_H

#include "forward.h"

namespace dcpp {

class DirectoryListingListener {
public:
	virtual ~DirectoryListingListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> LoadingFinished;
	typedef X<1> LoadingFailed;
	typedef X<2> LoadingStarted;
	typedef X<3> QueueMatched;
	typedef X<4> Close;
	typedef X<5> SearchStarted;
	typedef X<6> SearchFailed;
	typedef X<7> ChangeDirectory;
	typedef X<8> UpdateStatusMessage;
	typedef X<9> RemovedQueue;
	typedef X<10> SetActive;
	typedef X<11> HubChanged;

	virtual void on(LoadingFinished, int64_t /*start*/, const string& /*aDir*/, bool /*reloadList*/, bool /*changeDir*/, bool /*load in gui thread*/) noexcept { }
	virtual void on(LoadingFailed, const string&) noexcept { }
	virtual void on(LoadingStarted, bool /* changeDir */) noexcept { }
	virtual void on(QueueMatched, const string&) noexcept { }
	virtual void on(Close) noexcept { }
	virtual void on(SearchStarted) noexcept { }
	virtual void on(SearchFailed, bool) noexcept { }
	virtual void on(ChangeDirectory, const string&, bool) noexcept { }
	virtual void on(UpdateStatusMessage, const string&) noexcept { }
	virtual void on(RemovedQueue, const string&) noexcept { }
	virtual void on(SetActive) noexcept {}
	virtual void on(HubChanged) noexcept {}
};

} // namespace dcpp

#endif // !defined(DIRECTORYLISTING_LISTENER_H)