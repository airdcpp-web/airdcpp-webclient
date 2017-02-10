/*
* Copyright (C) 2011-2017 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_RECENT_MANAGER_H
#define DCPLUSPLUS_DCPP_RECENT_MANAGER_H

#include "RecentEntry.h"
#include "Singleton.h"
#include "Speaker.h"

#include "ClientManagerListener.h"
#include "DirectoryListingManagerListener.h"
#include "MessageManagerListener.h"
#include "RecentManagerListener.h"
#include "TimerManagerListener.h"


namespace dcpp {
	class RecentManager : public Speaker<RecentManagerListener>, public Singleton<RecentManager>, private TimerManagerListener,
		private ClientManagerListener, private MessageManagerListener, private DirectoryListingManagerListener
	{
	public:
		RecentUserEntryList getRecentChats() const noexcept;
		RecentUserEntryList getRecentFilelists() const noexcept;

		// Recent Hubs
		RecentHubEntryList getRecentHubs() const noexcept;

		void addRecentHub(const string& aEntry) noexcept;
		void removeRecentHub(const string& aEntry) noexcept;
		void updateRecentHub(const ClientPtr& aClient) noexcept;

		void clearRecentHubs() noexcept;

		RecentHubEntryPtr getRecentHub(const string& aServer) const noexcept;
		RecentHubEntryList searchRecentHubs(const string& aPattern, size_t aMaxResults) const noexcept;

		void load() noexcept;
		void save() const noexcept;
	private:
		RecentUserEntryPtr getRecentUser(const CID& aCid, const RecentUserEntryList& aUsers) noexcept;
		void setDirty() noexcept { xmlDirty = true; }

		RecentHubEntryList recentHubs;
		RecentUserEntryList recentChats, recentFilelists;

		friend class Singleton<RecentManager>;

		mutable SharedMutex cs;

		void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;

		void on(ClientManagerListener::ClientCreated, const ClientPtr& c) noexcept;
		void on(ClientManagerListener::ClientRedirected, const ClientPtr& aOldClient, const ClientPtr& aNewClient) noexcept;
		void on(ClientManagerListener::ClientUpdated, const ClientPtr& c) noexcept;
		void on(ClientManagerListener::ClientRemoved, const ClientPtr& c) noexcept;

		void on(MessageManagerListener::ChatCreated, const PrivateChatPtr&, bool /* received message */) noexcept;

		void on(DirectoryListingManagerListener::ListingCreated, const DirectoryListingPtr&) noexcept;


		RecentManager();
		~RecentManager();

		void loadRecentHubs(SimpleXML& aXml);
		void loadRecentUsers(SimpleXML& aXml, const string& aRootTag, RecentUserEntryList& users_);

		void saveRecentHubs(SimpleXML& aXml) const noexcept;
		void saveRecentUsers(SimpleXML& aXml, const string& aRootTag, const RecentUserEntryList& users_) const noexcept;

		bool xmlDirty = false;
	};

} // namespace dcpp

#endif // !defined(RECENT_MANAGER_H)