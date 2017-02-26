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

#include "stdinc.h"

#include "AutoSearch.h"
#include "ShareScannerManager.h"

#include <airdcpp/ActionHook.h>
#include <airdcpp/Bundle.h>
#include <airdcpp/ResourceManager.h>
#include <airdcpp/SearchQuery.h>
#include <airdcpp/SearchManager.h>
#include <airdcpp/SimpleXML.h>
#include <airdcpp/TimerManager.h>

#include <boost/range/algorithm/max_element.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>


namespace dcpp {

using boost::max_element;
using namespace boost::posix_time;
using namespace boost::gregorian;

AutoSearch::AutoSearch() noexcept : token(Util::randInt(10)) {

}

AutoSearch::AutoSearch(bool aEnabled, const string& aSearchString, const string& aFileType, ActionType aAction, bool aRemove, const string& aTarget,
	StringMatch::Method aMethod, const string& aMatcherString, const string& aUserMatch, time_t aExpireTime,
	bool aCheckAlreadyQueued, bool aCheckAlreadyShared, bool aMatchFullPath, const string& aExcluded, int aSearchInterval, ItemType aType, bool aUserMetcherExclude, ProfileToken aToken /*rand*/) noexcept :
	enabled(aEnabled), searchString(aSearchString), fileType(aFileType), action(aAction), remove(aRemove),
	expireTime(aExpireTime), checkAlreadyQueued(aCheckAlreadyQueued), checkAlreadyShared(aCheckAlreadyShared), searchInterval(aSearchInterval),
	token(aToken), matchFullPath(aMatchFullPath), matcherString(aMatcherString), excludedString(aExcluded), asType(aType), userMatcherExclude(aUserMetcherExclude) {

	if (timeAdded == 0)
		timeAdded = GET_TIME();

	if (token == 0)
		token = Util::randInt(10);

	if (searchInterval == 0)
		searchInterval = AS_DEFAULT_SEARCH_INTERVAL;

	setPriority(calculatePriority());

	checkRecent();
	setTarget(aTarget);
	setMethod(aMethod);
	userMatcher.setMethod(StringMatch::WILDCARD);
	userMatcher.pattern = aUserMatch;
	userMatcher.prepare();
};

AutoSearch::~AutoSearch() noexcept {};

bool AutoSearch::allowNewItems() const noexcept {
	if (!enabled)
		return false;

	if (status < STATUS_QUEUED_OK)
		return true;

	if (status == STATUS_FAILED_MISSING)
		return true;

	return !remove && !usingIncrementation();
}

bool AutoSearch::allowAutoSearch() const noexcept{
	return allowNewItems() && (nextAllowedSearch() <= GET_TIME()) && (isRecent() || (lastSearch + searchInterval * 60 <= GET_TIME()));
}

time_t AutoSearch::getNextSearchTime() const noexcept {
	auto next_s = isRecent() ? lastSearch + 1 * 60 : lastSearch + searchInterval * 60;
	return max(next_s, nextAllowedSearch());
}

bool AutoSearch::onBundleRemoved(const BundlePtr& aBundle, bool finished) noexcept {
	removeBundle(aBundle);

	auto usingInc = usingIncrementation();
	auto expired = usingInc && maxNumberReached() && finished && SETTING(AS_DELAY_HOURS) == 0 && bundles.empty();
	if (finished) {
		auto time = GET_TIME();
		addPath(aBundle->getTarget(), time);
		if (usingInc) {
			if (SETTING(AS_DELAY_HOURS) > 0) {
				lastIncFinish = time;
				setStatus(AutoSearch::STATUS_POSTSEARCH);
				expired = false;
			} else {
				changeNumber(true);
			}
		}
	}
	updateStatus();

	return expired;
}

bool AutoSearch::checkRecent() { 
	if (getTimeAdded() == 0 || getAsType() == NORMAL)
		recent = false;
	else {
		recent = GET_TIME() < (getTimeAdded() + 3 * 60 * 60);
	}
	return recent;
}

bool AutoSearch::removeOnCompleted() const noexcept{
	return remove && !usingIncrementation();
}

bool AutoSearch::maxNumberReached() const noexcept{
	return useParams && curNumber >= maxNumber && maxNumber > 0 && lastIncFinish == 0;
}

bool AutoSearch::expirationTimeReached() const noexcept{
	return expireTime > 0 && expireTime <= GET_TIME();
}

void AutoSearch::changeNumber(bool increase) noexcept {
	if (usingIncrementation()) {
		lastIncFinish = 0;
		increase ? curNumber++ : curNumber--;
		updatePattern();
	}
}

bool AutoSearch::isExcluded(const string& aString) noexcept {
	return excluded.match_any(aString);
}

void AutoSearch::updateExcluded() noexcept {
	excluded.clear();
	if (!excludedString.empty()) {
		auto ex = move(SearchQuery::parseSearchString(excludedString));
		for (const auto& i : ex)
			excluded.addString(i);
	}
}

string AutoSearch::formatParams(bool formatMatcher) const noexcept {
	ParamMap params;
	if (usingIncrementation()) {
		params["inc"] = [&] {
			auto num = Util::toString(getCurNumber());
			if (static_cast<int>(num.length()) < getNumberLen()) {
				//prepend the zeroes
				num.insert(num.begin(), getNumberLen() - num.length(), '0');
			}
			return num;
		};
	}

	return Util::formatParams(formatMatcher ? matcherString : searchString, params);
}

string AutoSearch::getDisplayName() noexcept {
	if (!useParams)
		return searchString;

	return formatParams(false) + " (" + searchString + ")";
}

void AutoSearch::setTarget(const string& aTarget) noexcept {
	target = Util::validatePath(aTarget, true);
}

void AutoSearch::updatePattern() noexcept {
	if (matcherString.empty())
		matcherString = searchString;

	if (useParams) {
		pattern = formatParams(true);
	} else {
		pattern = matcherString;
	}
	prepare();
}

string AutoSearch::getDisplayType() const noexcept {
	return SearchManager::isDefaultTypeStr(fileType) ? SearchManager::getTypeStr(fileType[0] - '0') : fileType;
}

void AutoSearch::addBundle(const BundlePtr& aBundle) noexcept {
	if (find(bundles, aBundle) == bundles.end())
		bundles.push_back(aBundle);

	updateStatus();
}

void AutoSearch::removeBundle(const BundlePtr& aBundle) noexcept {
	auto p = find(bundles, aBundle);
	if (p != bundles.end())
		bundles.erase(p);
}

bool AutoSearch::hasBundle(const BundlePtr& aBundle) noexcept {
	return find(bundles, aBundle) != bundles.end();
}

void AutoSearch::addPath(const string& aPath, time_t aFinishTime) noexcept {
	finishedPaths[aPath] = aFinishTime;
}

bool AutoSearch::usingIncrementation() const noexcept {
	return useParams && searchString.find("%[inc]") != string::npos;
}

string AutoSearch::getSearchingStatus() const noexcept {
	if (status == STATUS_DISABLED) {
		return STRING(DISABLED);
	} else if (status == STATUS_EXPIRED) {
		return STRING(EXPIRED);
	} else if (status == STATUS_MANUAL) {
		return STRING(MATCHING_MANUAL);
	} else if (status == STATUS_COLLECTING) {
		return STRING(COLLECTING_RESULTS);
	} else if (status == STATUS_POSTSEARCH) {
		return STRING(POST_SEARCHING);
	} else if (status == STATUS_WAITING) {
		auto time = GET_TIME();
		if (nextSearchChange > time) {
			auto timeStr = Util::formatTime(nextSearchChange - time, true, true);
			return nextIsDisable ? STRING_F(ACTIVE_FOR, timeStr) : STRING_F(WAITING_LEFT, timeStr);
		}
	} else if (remove || usingIncrementation()) {
		if (status == STATUS_QUEUED_OK) {
			return STRING(INACTIVE_QUEUED);
		} else if (status == STATUS_FAILED_MISSING) {
			return STRING_F(X_MISSING_FILES, STRING(ACTIVE));
		} else if (status == STATUS_FAILED_EXTRAS) {
			return STRING_F(X_FAILED_SHARING, STRING(INACTIVE));
		}
	}

	return STRING(ACTIVE);
}

string AutoSearch::getExpiration() const noexcept {
	if (expireTime == 0)
		return STRING(NEVER);

	auto curTime = GET_TIME();
	if (expireTime <= curTime) {
		return STRING(EXPIRED);
	} else {
		return Util::formatTime(expireTime - curTime, true, true);
	}
}

void AutoSearch::updateStatus() noexcept {
	if (!enabled) {
		if (manualSearch) {
			status = AutoSearch::STATUS_MANUAL;
		} else if (expirationTimeReached() || maxNumberReached()) {
			status = AutoSearch::STATUS_EXPIRED;
		} else {
			status = AutoSearch::STATUS_DISABLED;
		}
		return;
	}

	if (bundles.empty()) {
		if (lastIncFinish > 0) {
			status = AutoSearch::STATUS_POSTSEARCH;
		} else {
			status = AutoSearch::STATUS_SEARCHING;
		}
	} else {
		auto maxBundle = *boost::max_element(bundles, Bundle::StatusOrder());
		if(maxBundle->getStatus() == Bundle::STATUS_VALIDATION_ERROR) {
			if (ActionHookRejection::matches(maxBundle->getHookError(), SHARE_SCANNER_HOOK_ID, SHARE_SCANNER_ERROR_MISSING)) {
				status = AutoSearch::STATUS_FAILED_MISSING;
			}
			else if (ActionHookRejection::matches(maxBundle->getHookError(), SHARE_SCANNER_HOOK_ID, SHARE_SCANNER_ERROR_INVALID_CONTENT)) {
				status = AutoSearch::STATUS_FAILED_EXTRAS;
			}
		} else {
			status = AutoSearch::STATUS_QUEUED_OK;
		}
	}

	if (status != AutoSearch::STATUS_FAILED_MISSING && nextAllowedSearch() > GET_TIME()) {
		status = AutoSearch::STATUS_WAITING;
		return;
	}
}

bool AutoSearch::removePostSearch() noexcept {
	if (lastIncFinish > 0 && lastIncFinish + SETTING(AS_DELAY_HOURS) + 60 * 60 <= GET_TIME()) {
		lastIncFinish = 0;
		return true;
	}

	return false;
}


time_t AutoSearch::nextAllowedSearch() const noexcept {
	if (nextSearchChange == 0 || nextIsDisable || status == STATUS_FAILED_MISSING)
		return 0;

	return nextSearchChange;
}

bool AutoSearch::updateSearchTime() noexcept {
	if (searchDays.all() && startTime.hour == 0 && endTime.minute == 59 && endTime.hour == 23 && startTime.minute == 0) {
		// always searching
		nextSearchChange = 0;
		return false;
	}


	//get the current time from the clock -- one second resolution
	ptime now = second_clock::local_time();
	ptime nextSearch(now);

	//have we passed the end time from this day already?
	if (endTime.hour < nextSearch.time_of_day().hours() ||
		(endTime.hour == nextSearch.time_of_day().hours() && endTime.minute < nextSearch.time_of_day().minutes())) {

			nextSearch = ptime(nextSearch.date() + days(1));
	}

	auto addTime = [this, &nextSearch](bool toEnabled) -> void {
		//check the next weekday when the searching can be done (or when it's being disabled)
		if (searchDays[nextSearch.date().day_of_week()] != toEnabled) {
			//get the next day when we can search for it
			int p = nextSearch.date().day_of_week();
			if (!toEnabled)
				p++; //start from the next day as we know already that we are able to search today

			int d = 0;

			for (d = 0; d < 6; d++) {
				if (p == 7)
					p = 0;

				if (searchDays[p++] == toEnabled)
					break;
			}

			nextSearch = ptime(nextSearch.date() + days(d)); // start from the midnight
		}

		//add the start (or end) hours and minutes (if needed)
		auto& timeStruct = toEnabled ? startTime : endTime;
		if (timeStruct.hour > nextSearch.time_of_day().hours()) {
			nextSearch += (hours(timeStruct.hour) + minutes(timeStruct.minute)) - (hours(nextSearch.time_of_day().hours()) + minutes(nextSearch.time_of_day().minutes()));
		} else if ((timeStruct.hour == nextSearch.time_of_day().hours() && timeStruct.minute > nextSearch.time_of_day().minutes())) {
			nextSearch += minutes(timeStruct.minute - nextSearch.time_of_day().minutes());
		}
	};

	addTime(true);

	if (nextSearch == now) {
		//we are allowed to search already, check when it's going to be disabled
		addTime(false);
		nextIsDisable = true;
	} else {
		nextIsDisable = false;
	}

	tm td_tm = to_tm(nextSearch);
	time_t next = mktime(&td_tm);
	if (next != nextSearchChange) {
		nextSearchChange = next;
		updateStatus();
	}

	return true;
}

void AutoSearch::saveToXml(SimpleXML& xml) {
	xml.addTag("Autosearch");
	xml.addChildAttrib("Enabled", getEnabled());
	xml.addChildAttrib("SearchString", getSearchString());
	xml.addChildAttrib("FileType", getFileType());
	xml.addChildAttrib("Action", getAction());
	xml.addChildAttrib("Remove", getRemove());
	xml.addChildAttrib("Target", getTarget());
	xml.addChildAttrib("MatcherType", getMethod()),
	xml.addChildAttrib("MatcherString", getMatcherString()),
	xml.addChildAttrib("UserMatch", getNickPattern());
	xml.addChildAttrib("ExpireTime", getExpireTime());
	xml.addChildAttrib("CheckAlreadyQueued", getCheckAlreadyQueued());
	xml.addChildAttrib("CheckAlreadyShared", getCheckAlreadyShared());
	xml.addChildAttrib("SearchDays", searchDays.to_string());
	xml.addChildAttrib("StartTime", startTime.toString());
	xml.addChildAttrib("EndTime", endTime.toString());
	xml.addChildAttrib("LastSearchTime", Util::toString(getLastSearch()));
	xml.addChildAttrib("MatchFullPath", getMatchFullPath());
	xml.addChildAttrib("ExcludedWords", getExcludedString());
	xml.addChildAttrib("SearchInterval", Util::toString(getSearchInterval()));
	xml.addChildAttrib("ItemType", Util::toString(getAsType()));
	xml.addChildAttrib("Token", Util::toString(getToken()));
	xml.addChildAttrib("TimeAdded", Util::toString(getTimeAdded()));
	xml.addChildAttrib("Group", getGroup());
	xml.addChildAttrib("UserMatcherExclude", getUserMatcherExclude());

	xml.stepIn();

	xml.addTag("Params");
	xml.addChildAttrib("Enabled", getUseParams());
	xml.addChildAttrib("CurNumber", getCurNumber());
	xml.addChildAttrib("MaxNumber", getMaxNumber());
	xml.addChildAttrib("MinNumberLen", getNumberLen());
	xml.addChildAttrib("LastIncFinish", getLastIncFinish());

	if (!getFinishedPaths().empty()) {
		xml.addTag("FinishedPaths");
		xml.stepIn();
		for (auto& p : getFinishedPaths()) {
			xml.addTag("Path", p.first);
			xml.addChildAttrib("FinishTime", p.second);
		}
		xml.stepOut();
	}

	if (!getBundles().empty()) {
		xml.addTag("Bundles");
		xml.stepIn();
		for (const auto& b : getBundles()) {
			xml.addTag("Bundle", Util::toString(b->getToken()));
		}
		xml.stepOut();
	}
	xml.stepOut();
}


}