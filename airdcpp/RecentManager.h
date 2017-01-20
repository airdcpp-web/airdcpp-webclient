/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#ifndef RECENT_MANAGER_H
#define RECENT_MANAGER_H

#include "RecentEntry.h"
#include "Singleton.h"
#include "Speaker.h"
#include "DelayedEvents.h"

#include "RecentManagerListener.h"


namespace dcpp {
	class RecentManager : public Speaker<RecentManagerListener>, public Singleton<RecentManager>
	{
	public:
		// Recent Hubs
		RecentEntryList getRecents() noexcept {  RLock l(cs); return recents; };

		void addRecent(const string& aEntry) noexcept;
		void removeRecent(const string& aEntry) noexcept;
		void updateRecent(const ClientPtr& aClient) noexcept;

		RecentEntryPtr getRecentEntry(const string& aServer) const noexcept;
		RecentEntryList searchRecents(const string& aPattern, size_t aMaxResults) const noexcept;

		void clearRecents() noexcept;
		void saveRecents() const noexcept;

		void load() noexcept;

		mutable SharedMutex cs;
	private:
		RecentEntryList recents;
		friend class Singleton<RecentManager>;
		
		enum Events {
			SAVE = 0
		};
		RecentManager();
		~RecentManager();

		DelayedEvents<int> delayEvents;

		void loadRecents(SimpleXML& aXml);
	};

} // namespace dcpp

#endif // !defined(RECENT_MANAGER_H)