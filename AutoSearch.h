/*
* Copyright (C) 2011-2013 AirDC++ Project
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

#ifndef AUTO_SEARCH_H
#define AUTO_SEARCH_H

#include <bitset>

#include "typedefs.h"

#include "GetSet.h"
#include "Pointer.h"
#include "StringMatch.h"
#include "TargetUtil.h"
#include "Util.h"

namespace dcpp {

struct SearchTime {
	uint16_t hour;
	uint16_t minute;

	explicit SearchTime(bool end = false) {
		minute = end ? 59 : 0;
		hour = end ? 23 : 0;
	}
	explicit SearchTime(uint16_t aHours, uint16_t aMinutes) : hour(aHours), minute(aMinutes) {}
	explicit SearchTime(const string& aTime) {
		/*auto s = aTime.find(",");
		if (s != aTime.end()) {
		hours =
		}*/
		if (aTime.length() != 4) {
			hour = 0;
			minute = 0;
			return;
		}

		hour = Util::toUInt(aTime.substr(0, 2));
		minute = Util::toUInt(aTime.substr(2, 2));
	}

	string toString() {
		string hourStr = Util::toString(hour);
		if (hourStr.length() == 1)
			hourStr = "0" + hourStr;
		string minuteStr = Util::toString(minute);
		if (minuteStr.length() == 1)
			minuteStr = "0" + minuteStr;
		return hourStr + minuteStr;
	}
};

class AutoSearch : public intrusive_ptr_base<AutoSearch>, public StringMatch {

public:
	enum ActionType {
		ACTION_DOWNLOAD,
		ACTION_QUEUE,
		ACTION_REPORT
	};

	enum Status {
		STATUS_DISABLED,
		STATUS_EXPIRED,
		STATUS_MANUAL,
		STATUS_SEARCHING,
		STATUS_COLLECTING,
		STATUS_WAITING,
		STATUS_POSTSEARCH,
		STATUS_QUEUED_OK,
		STATUS_FAILED_MISSING,
		STATUS_FAILED_EXTRAS
	};

	AutoSearch(bool aEnabled, const string& aSearchString, const string& aFileType, ActionType aAction, bool aRemove, const string& aTarget, TargetUtil::TargetType aTargetType,
		StringMatch::Method aMatcherType, const string& aMatcherString, const string& aUserMatch, time_t aExpireTime, bool aCheckAlreadyQueued,
		bool aCheckAlreadyShared, bool matchFullPath, const string& aExcluded, ProfileToken aToken = 0) noexcept;

	AutoSearch() noexcept;
	~AutoSearch();
	typedef map<string, time_t> FinishedPathMap;

	GETSET(string, searchString, SearchString);
	GETSET(string, excludedString, ExcludedString);
	GETSET(string, matcherString, MatcherString);
	GETSET(ActionType, action, Action);
	GETSET(string, fileType, FileType);
	GETSET(TargetUtil::TargetType, tType, TargetType);
	GETSET(time_t, expireTime, ExpireTime);

	GETSET(ProfileToken, token, Token);
	GETSET(BundleList, bundles, Bundles);
	GETSET(FinishedPathMap, finishedPaths, FinishedPaths);

	IGETSET(bool, matchFullPath, MatchFullPath, true);
	IGETSET(bool, enabled, Enabled, true);
	IGETSET(bool, remove, Remove, false); //remove after 1 hit
	IGETSET(time_t, lastSearch, LastSearch, 0);
	IGETSET(bool, checkAlreadyQueued, CheckAlreadyQueued, true);
	IGETSET(bool, checkAlreadyShared, CheckAlreadyShared, true);
	IGETSET(bool, manualSearch, ManualSearch, false);
	IGETSET(Status, status, Status, STATUS_SEARCHING);

	IGETSET(int, curNumber, CurNumber, 1);
	IGETSET(int, maxNumber, MaxNumber, 0);
	IGETSET(int, numberLen, NumberLen, 2);
	IGETSET(bool, useParams, UseParams, false);
	IGETSET(time_t, lastIncFinish, LastIncFinish, 0);
	GETSET(string, lastError, LastError);

	SearchTime startTime = SearchTime(false);
	SearchTime endTime = SearchTime(true);
	bitset<7> searchDays = bitset<7>("1111111");

	bool matchNick(const string& aStr) { return userMatcher.match(aStr); }
	const string& getNickPattern() const { return userMatcher.pattern; }
	string getDisplayName();

	string getDisplayType() const;
	string getSearchingStatus() const;
	string getExpiration() const;

	time_t nextAllowedSearch();
	bool allowNewItems() const;
	void updatePattern();
	void changeNumber(bool increase);
	bool updateSearchTime();
	void updateStatus();

	void removeBundle(const BundlePtr& aBundle);
	void addBundle(const BundlePtr& aBundle);
	bool hasBundle(const BundlePtr& aBundle);

	void addPath(const string& aPath, time_t aFinishTime);
	void clearPaths() { finishedPaths.clear(); }
	bool usingIncrementation() const;
	string formatParams(bool formatMatcher) const;
	void setUserMatcher(const string& aPattern) { userMatcher.pattern = aPattern; }
	void prepareUserMatcher() { userMatcher.prepare(); }
	string getTarget() { return target; }
	void setTarget(const string& aTarget);
	bool removePostSearch();
	bool isExcluded(const string& aString);
	void updateExcluded();
	string getFormatedSearchString() const;
private:
	StringMatch userMatcher;
	time_t nextSearchChange = 0;
	bool nextIsDisable = false;
	string target;
	StringSearch::List excluded;
};

}

#endif