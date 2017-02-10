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

#ifndef DCPLUSPLUS_DCPP_RECENT_ENTRY_H_
#define DCPLUSPLUS_DCPP_RECENT_ENTRY_H_

#include "GetSet.h"
#include "typedefs.h"

#include "HintedUser.h"
#include "TimerManager.h"

namespace dcpp {

class RecentBase {
public:
	RecentBase(time_t aLastOpened) : lastOpened(aLastOpened) { }

	time_t getLastOpened() const noexcept { return lastOpened; }
	void updateLastOpened() noexcept { lastOpened = GET_TIME(); }

	template<typename T> struct Sort {
		bool operator()(typename T::Ptr& a, typename T::Ptr& b) const noexcept { return a->getLastOpened() > b->getLastOpened(); }
	};
private:
	time_t lastOpened;
};

class RecentHubEntry : public RecentBase {
public:
	RecentHubEntry(const string& aUrl, const string& aName = "*", const string& aDescription = "*", time_t aLastOpened = GET_TIME()) :
		url(aUrl), name(aName), description(aDescription), RecentBase(aLastOpened) {

	}

	typedef std::shared_ptr<RecentHubEntry> Ptr;

	GETSET(string, url, Url);
	GETSET(string, name, Name);
	GETSET(string, description, Description);

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
};

class RecentUserEntry : public RecentBase {
public:
	RecentUserEntry(const HintedUser& aHintedUser, time_t aLastOpened = GET_TIME()) :
		user(aHintedUser), RecentBase(aLastOpened) {

	}

	typedef std::shared_ptr<RecentUserEntry> Ptr;

	const HintedUser& getUser() const noexcept { return user; }

	class CidCompare {
	public:
		CidCompare(const CID& compareTo) : a(compareTo) { }
		bool operator()(const Ptr& p) const noexcept {
			return p->getUser().user->getCID() == a;
		}
	private:
		CidCompare& operator=(const CidCompare&) = delete;
		const CID& a;
	};
private:
	const HintedUser user;
};

typedef RecentHubEntry::Ptr RecentHubEntryPtr;
typedef std::vector<RecentHubEntryPtr> RecentHubEntryList;
//typedef std::set<RecentHubEntryPtr, RecentBase::Sort<RecentHubEntrySet>> RecentHubEntrySet;

typedef RecentUserEntry::Ptr RecentUserEntryPtr;
typedef std::vector<RecentUserEntryPtr> RecentUserEntryList;
//typedef std::set<RecentHubEntryPtr, RecentBase::Sort<RecentUserEntry>> RecentUserEntrySet;

}
#endif