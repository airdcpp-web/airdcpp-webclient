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

#ifndef DCPLUSPLUS_DCPP_HUBLIST_MANAGER_H
#define DCPLUSPLUS_DCPP_HUBLIST_MANAGER_H

#include <airdcpp/forward.h>

#include "HublistManagerListener.h"
#include "HublistEntry.h"

#include <airdcpp/HttpConnection.h>
#include <airdcpp/Singleton.h>
#include <airdcpp/Speaker.h>


namespace dcpp {

	/**
	* Assumed to be called only by UI thread.
	*/
	class HublistManager : public Speaker<HublistManagerListener>, private HttpConnectionListener, public Singleton<HublistManager>
	{
	public:
		HublistManager();
		~HublistManager();

		// Public Hubs
		enum HubTypes {
			TYPE_NORMAL,
			TYPE_BZIP2
		};

		StringList getHubLists() noexcept;
		void setHubList(int aHubList) noexcept;
		int getSelectedHubList() const noexcept { return lastServer; }
		void refresh(bool forceDownload = false) noexcept;
		HubTypes getHubListType() const noexcept { return listType; }
		HublistEntry::List getPublicHubs() noexcept;
		bool isDownloading() const noexcept { return (useHttp && running); }

		mutable SharedMutex cs;
	private:
		static string getHublistPath() noexcept;

		// Public Hubs
		typedef unordered_map<string, HublistEntry::List> PubListMap;
		PubListMap publicListMatrix;
		string publicListServer;
		bool useHttp = false, running = false;
		HttpConnection* c = nullptr;
		int lastServer = 0;
		HubTypes listType = TYPE_NORMAL;
		string downloadBuf;

		// HttpConnectionListener
		void on(Data, HttpConnection*, const uint8_t*, size_t) noexcept;
		void on(Failed, HttpConnection*, const string&) noexcept;
		void on(Complete, HttpConnection*, const string&) noexcept;
		void on(Redirected, HttpConnection*, const string&) noexcept;
		void on(Retried, HttpConnection*, bool) noexcept;

		bool onHttpFinished(bool fromHttp) noexcept;
	};

} // namespace dcpp

#endif // !defined(FAVORITE_MANAGER_H)