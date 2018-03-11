/*
* Copyright (C) 2011-2018 AirDC++ Project
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
#include "PrivateChatManagerListener.h"
#include "RecentManagerListener.h"
#include "SettingsManager.h"
#include "TimerManagerListener.h"


namespace dcpp {
	class RecentManager : public Speaker<RecentManagerListener>, public Singleton<RecentManager>, private TimerManagerListener,
		private ClientManagerListener, private PrivateChatManagerListener, private DirectoryListingManagerListener
	{
	public:
		RecentEntryList getRecents(RecentEntry::Type aType) const noexcept;

		void removeRecent(RecentEntry::Type aType, const RecentEntryPtr& aEntry) noexcept;
		void clearRecents(RecentEntry::Type aType) noexcept;

		RecentEntryList searchRecents(RecentEntry::Type aType, const string& aPattern, size_t aMaxResults) const noexcept;

		void load() noexcept;
		void save() noexcept;
	private:
		void checkCount(RecentEntry::Type aType) noexcept;
		void onHubOpened(const ClientPtr& aClient) noexcept;
		void onRecentOpened(RecentEntry::Type aType, const string& aName, const string& aDescription, const string& aUrl, const UserPtr& aUser, const RecentEntryPtr& aEntry) noexcept;
		void onRecentUpdated(RecentEntry::Type aType, const RecentEntryPtr& aEntry) noexcept;

		template<typename CompareT>
		RecentEntryPtr getRecent(RecentEntry::Type aType, const CompareT& aCompare) const noexcept {
			RLock l(cs);
			auto i = find_if(recents[aType], aCompare);
			return i != recents[aType].end() ? *i : nullptr;
		}

		void setDirty() noexcept { xmlDirty = true; }

		RecentEntryList recents[RecentEntry::TYPE_LAST];

		friend class Singleton<RecentManager>;

		mutable SharedMutex cs;

		void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;

		void on(ClientManagerListener::ClientCreated, const ClientPtr& c) noexcept;
		void on(ClientManagerListener::ClientRedirected, const ClientPtr& aOldClient, const ClientPtr& aNewClient) noexcept;
		void on(ClientManagerListener::ClientUpdated, const ClientPtr& c) noexcept;

		void on(PrivateChatManagerListener::ChatCreated, const PrivateChatPtr&, bool /* received message */) noexcept;

		void on(DirectoryListingManagerListener::ListingCreated, const DirectoryListingPtr&) noexcept;


		RecentManager();
		~RecentManager();

		void loadRecents(SimpleXML& aXml, RecentEntry::Type aType);
		void saveRecents(SimpleXML& aXml, RecentEntry::Type aType) const noexcept;

		static string rootTags[RecentEntry::TYPE_LAST];
		static string itemTags[RecentEntry::TYPE_LAST];
		static SettingsManager::IntSetting maxLimits[RecentEntry::TYPE_LAST];

		bool xmlDirty = false;
	};

} // namespace dcpp

#endif // !defined(RECENT_MANAGER_H)