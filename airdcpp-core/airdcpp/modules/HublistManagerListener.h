/*
* Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_HUBLISTMANAGER_LISTENER_H_
#define DCPLUSPLUS_DCPP_HUBLISTMANAGER_LISTENER_H_

#include <airdcpp/forward.h>

namespace dcpp {

	class HublistManagerListener {
	public:
		virtual ~HublistManagerListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> DownloadStarting;
		typedef X<1> DownloadFailed;
		typedef X<2> DownloadFinished;

		typedef X<3> LoadedFromCache;
		typedef X<4> Corrupted;


		virtual void on(DownloadStarting, const string&) noexcept { }
		virtual void on(DownloadFailed, const string&) noexcept { }
		virtual void on(DownloadFinished, const string&) noexcept { }

		virtual void on(LoadedFromCache, const string&, const string&) noexcept { }
		virtual void on(Corrupted, const string&) noexcept { }
	};

} // namespace dcpp

#endif 
