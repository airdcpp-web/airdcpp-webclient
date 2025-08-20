/*
* Copyright (C) 2012-2024 AirDC++ Project
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

#ifndef RSS_MANAGER_H
#define RSS_MANAGER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <airdcpp/forward.h>

#include <airdcpp/core/types/GetSet.h>
#include <airdcpp/util/Util.h>
#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/core/Singleton.h>
#include <airdcpp/core/Speaker.h>

#include <airdcpp/core/queue/DispatcherQueue.h>
#include <airdcpp/connection/http/HttpDownload.h>
#include <airdcpp/message/Message.h>
#include <airdcpp/util/text/StringMatch.h>
#include <airdcpp/util/ValueGenerator.h>

#include <airdcpp/core/timer/TimerManager.h>

namespace dcpp {

class RSS;
typedef std::shared_ptr<RSS> RSSPtr;
class RSSData;
typedef std::shared_ptr<RSSData> RSSDataPtr;


class RSSFilter : public StringMatch {
public:

	RSSFilter(const string& aFilterPattern, const string& aDownloadTarget, int aMethod, const string& aGroup, bool aSkipDupes, int aAction, int aExpireDays, bool aFormatTime) noexcept :
		filterPattern(aFilterPattern), downloadTarget(aDownloadTarget), autosearchGroup(aGroup), skipDupes(aSkipDupes), filterAction(aAction), expireDays(aExpireDays),
		formatTimeParams(aFormatTime)
	{
		pattern = aFilterPattern;
		setMethod((StringMatch::Method)aMethod);
	}

	~RSSFilter() noexcept {};

	GETSET(string, filterPattern, FilterPattern);
	GETSET(string, downloadTarget, DownloadTarget);
	IGETSET(string, autosearchGroup, AutosearchGroup, Util::emptyString);
	IGETSET(int, filterAction, FilterAction, DOWNLOAD);
	IGETSET(int, expireDays, ExpireDays, 3);
	IGETSET(bool, formatTimeParams, FormatTimeParams, false);

	bool skipDupes = true;

	enum filterActions {
		DOWNLOAD = 0,
		REMOVE = 1,
		ADD_AUTOSEARCH = 2,
	};

};

class RSS : private boost::noncopyable {
public:

	RSS(const string& aUrl, const string& aName, bool aEnable, time_t aLastUpdate, int aUpdateInterval = 60, int aToken = 0) noexcept :
		url(aUrl), feedName(aName), lastUpdate(aLastUpdate), updateInterval(aUpdateInterval), token(aToken), enable(aEnable)
	{
		if (aUpdateInterval < 10)
			updateInterval = 10;

		if (token == 0)
			token = ValueGenerator::randInt(10);

		rssDownload.reset();
	}

	RSS() noexcept
	{
		updateInterval = 60;

		if (token == 0)
			token = ValueGenerator::randInt(10);

		rssDownload.reset();
	}

	~RSS() noexcept {};

	GETSET(string, url, Url);
	GETSET(string, feedName, FeedName);
	IGETSET(time_t, lastUpdate, LastUpdate, 0);
	IGETSET(int, updateInterval, UpdateInterval, 60);
	IGETSET(int, token, Token, 0);
	IGETSET(bool, dirty, Dirty, false);
	IGETSET(bool, enable, Enable, true);

	//bool operator==(const RSSPtr& rhs) const { return url == rhs->getUrl(); }

	unordered_map<string, RSSDataPtr>& getFeedData() { return rssData; }
	vector<RSSFilter>& getRssFilterList() { return rssFilterList; }

	unique_ptr<HttpDownload> rssDownload;
	vector<RSSFilter> rssFilterList;

	bool allowUpdate() {
		return getEnable() && (getLastUpdate() + getUpdateInterval() * 60) < GET_TIME();
	}

private:

	unordered_map<string, RSSDataPtr> rssData;

};

class RSSData: private boost::noncopyable {
public:
	RSSData(const string& aTitle, const string& aLink, const string& aPubDate, const RSSPtr& aFeed, time_t aDateAdded = GET_TIME()) noexcept :
		title(aTitle), link(aLink), pubDate(aPubDate), feed(aFeed), dateAdded(aDateAdded)  {
	}
	~RSSData() noexcept { };
	
	GETSET(string, title, Title);
	GETSET(string, link, Link);
	GETSET(string, pubDate, PubDate);
	GETSET(RSSPtr, feed, Feed);
	GETSET(time_t, dateAdded, DateAdded); //For prune old entries in database...

};

class RSSManagerListener {
public:
	virtual ~RSSManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> RSSDataAdded;
	typedef X<1> RSSDataRemoved;
	typedef X<2> RSSDataCleared;
	typedef X<3> RSSFeedUpdated;
	typedef X<4> RSSFeedChanged;
	typedef X<5> RSSFeedRemoved;
	typedef X<6> RSSFeedAdded;

	virtual void on(RSSDataAdded, const RSSDataPtr&) noexcept { }
	virtual void on(RSSDataRemoved, const RSSDataPtr&) noexcept { }
	virtual void on(RSSDataCleared, const RSSPtr&) noexcept { }
	virtual void on(RSSFeedUpdated, const RSSPtr&) noexcept { }
	virtual void on(RSSFeedChanged, const RSSPtr&) noexcept { }
	virtual void on(RSSFeedRemoved, const RSSPtr&) noexcept { }
	virtual void on(RSSFeedAdded, const RSSPtr&) noexcept { }

};


class RSSManager : public Speaker<RSSManagerListener>, public Singleton<RSSManager>, private TimerManagerListener
{
public:
	friend class Singleton<RSSManager>;	
	RSSManager();
	~RSSManager();

	void load();
	void save(bool aSaveDatabase = false);

	void clearRSSData(const RSSPtr& aFeed) noexcept;
	void matchFilters(const RSSPtr& aFeed);
	
	RSSPtr getFeedByName(const string& aName) const noexcept;
	RSSPtr getFeedByUrl(const string& aUrl) const noexcept;
	RSSPtr getFeedByToken(int aToken) const noexcept;

	CriticalSection& getCS() { return cs; }

	vector<RSSPtr>& getRss(){
		return rssList;
	}

	void downloadFeed(const RSSPtr& aFeed, bool verbose = false) noexcept;

	void updateFeedItem(RSSPtr& aFeed, const string& aUrl, const string& aName, int aUpdateInterval, bool aEnable) noexcept;
	
	void updateFilterList(const RSSPtr& aFeed, vector<RSSFilter>& aNewList);

	void removeFeedItem(const RSSPtr& aFeed) noexcept;

	void enableFeedUpdate(const RSSPtr& aFeed, bool enable) noexcept;

	void removeFeedData(const RSSPtr& aFeed, const RSSDataPtr& aData);

	void loadFilters(SimpleXML& aXml, vector<RSSFilter>& aList);
	void saveFilters(SimpleXML& aXml, const vector<RSSFilter>& aList);

private:
	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;

	void savedatabase(const RSSPtr& aFeed);

	uint64_t nextUpdate = 0;
	uint64_t lastXmlSave = GET_TICK();

	RSSPtr getUpdateItem() const noexcept;
	
	void matchFilters(const RSSPtr& aFeed, const RSSDataPtr& aData);
	bool addAutoSearchItem(const RSSFilter& aFilter, const RSSDataPtr& aData) noexcept;

	vector<RSSPtr> rssList;

	void parseRSSFeed(SimpleXML& xml, RSSPtr& aFeed);
	void parseAtomFeed(SimpleXML& xml, RSSPtr& aFeed);
	void addData(const string& aTitle, const string& aLink, const string& aDate, RSSPtr& aFeed);

	//trim title, return true if new data.
	bool checkTitle(const RSSPtr& aFeed, string& aTitle);

	mutable CriticalSection cs;

	DispatcherQueue tasks;

	void downloadComplete(const string& aUrl);
	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t tick) noexcept;

};

}
#endif