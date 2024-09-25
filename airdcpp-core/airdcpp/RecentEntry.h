/*
* Copyright (C) 2011-2024 AirDC++ Project
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
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

#ifndef DCPLUSPLUS_DCPP_RECENT_ENTRY_H_
#define DCPLUSPLUS_DCPP_RECENT_ENTRY_H_

#include <airdcpp/GetSet.h>
#include <airdcpp/typedefs.h>

#include <airdcpp/HintedUser.h>
#include <airdcpp/TimerManager.h>

namespace dcpp {

class RecentEntry {
public:
	enum Type {
		TYPE_HUB,
		TYPE_PRIVATE_CHAT,
		TYPE_FILELIST,
		TYPE_LAST
	};

	RecentEntry(const string& aName, const string& aDescription, const string& aUrl, const UserPtr& aUser = nullptr, time_t aLastOpened = GET_TIME()) :
		name(aName), description(aDescription), url(aUrl), user(aUser), lastOpened(aLastOpened) {

	}

	typedef std::shared_ptr<RecentEntry> Ptr;

	GETSET(string, url, Url);
	GETSET(string, name, Name);
	GETSET(string, description, Description);

	time_t getLastOpened() const noexcept { return lastOpened; }
	void updateLastOpened() noexcept { lastOpened = GET_TIME(); }

	const UserPtr& getUser() const noexcept {
		return user;
	}


	// HELPERS
	class UrlCompare {
	public:
		UrlCompare(const string& compareTo) : a(compareTo) { }
		bool operator()(const Ptr& p) const noexcept {
			return p->getUrl() == a;
		}
	private:
		UrlCompare& operator=(const UrlCompare&) = delete;
		const string& a;
	};

	class CidCompare {
	public:
		CidCompare(const CID& compareTo) : a(compareTo) { }
		bool operator()(const Ptr& p) const noexcept {
			return p->getUser()->getCID() == a;
		}
	private:
		CidCompare& operator=(const CidCompare&) = delete;
		const CID& a;
	};

	struct Sort {
		bool operator()(const typename RecentEntry::Ptr& a, const typename RecentEntry::Ptr& b) const noexcept { return a->getLastOpened() > b->getLastOpened(); }
	};
private:
	const UserPtr user;

	time_t lastOpened;
};

typedef RecentEntry::Ptr RecentEntryPtr;
typedef std::vector<RecentEntryPtr> RecentEntryList;

}
#endif