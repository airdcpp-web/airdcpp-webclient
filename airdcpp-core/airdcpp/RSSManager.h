
#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Util.h"
#include "Singleton.h"
#include "Speaker.h"
#include "SimpleXML.h"
#include "HttpDownload.h"
#include "TimerManager.h"
#include "ShareManager.h"
#include "AutoSearchManager.h"
#include "TargetUtil.h"

namespace dcpp {

class RSSFilter : public StringMatch {
public:

	RSSFilter(const string& aFilterPattern, const string& aDownloadTarget, int aMethod) noexcept :
		filterPattern(aFilterPattern), downloadTarget(aDownloadTarget)
	{
		pattern = aFilterPattern;
		setMethod((StringMatch::Method)aMethod);
	}

	~RSSFilter() noexcept {};

	GETSET(string, filterPattern, FilterPattern);
	GETSET(string, downloadTarget, DownloadTarget);

};

class RSS : private boost::noncopyable {
public:

	RSS(const string& aUrl, const string& aName, bool aEnable, time_t aLastUpdate, int aUpdateInterval = 60, int aToken = 0) noexcept :
		url(aUrl), feedName(aName), lastUpdate(aLastUpdate), updateInterval(aUpdateInterval), token(aToken), enable(aEnable)
	{
		if (aUpdateInterval < 10)
			updateInterval = 10;

		if (token == 0)
			token = Util::randInt(10);

		rssDownload.reset();
	}

	RSS() noexcept
	{
		updateInterval = 60;

		if (token == 0)
			token = Util::randInt(10);

		rssDownload.reset();
	}

	~RSS() noexcept {};

	GETSET(string, url, Url);
	GETSET(string, feedName, FeedName);
	GETSET(time_t, lastUpdate, LastUpdate);
	IGETSET(int, updateInterval, UpdateInterval, 60);
	GETSET(int, token, Token);
	IGETSET(bool, dirty, Dirty, false);
	IGETSET(bool, enable, Enable, true);

	//bool operator==(const RSSPtr& rhs) const { return url == rhs->getUrl(); }

	unordered_map<string, RSSDataPtr>& getFeedData() { return rssData; }
	vector<RSSFilter>& getRssFilterList() { return rssFilterList; }

	unique_ptr<HttpDownload> rssDownload;
	vector<RSSFilter> rssFilterList;

	bool allowUpdate() {
		return enable && (getLastUpdate() + getUpdateInterval() * 60) < GET_TIME();
	}

private:

	unordered_map<string, RSSDataPtr> rssData;

};

class RSSData: public intrusive_ptr_base<RSSData>, private boost::noncopyable {
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
	typedef X<1> RSSDataCleared;
	typedef X<2> RSSFeedUpdated;
	typedef X<3> RSSFeedChanged;
	typedef X<4> RSSFeedRemoved;
	typedef X<5> RSSFeedAdded;

	virtual void on(RSSDataAdded, const RSSDataPtr&) noexcept { }
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
	void saveConfig(bool saveDatabase = true);

	void clearRSSData(const RSSPtr& aFeed);
	void matchFilters(const RSSPtr& aFeed) const;
	
	RSSPtr getFeedByName(const string& aName) const;
	RSSPtr getFeedByUrl(const string& aUrl) const;
	RSSPtr getFeedByToken(int aToken) const;

	CriticalSection& getCS() { return cs; }

	unordered_set<RSSPtr>& getRss(){
		return rssList;
	}

	void downloadFeed(const RSSPtr& aFeed, bool verbose = false);

	void updateFeedItem(RSSPtr& aFeed, const string& aUrl, const string& aName, int aUpdateInterval, bool aEnable);
	
	void updateFilterList(const RSSPtr& aFeed, vector<RSSFilter>& aNewList);

	void removeFeedItem(const RSSPtr& aFeed);

	void enableFeedUpdate(const RSSPtr& aFeed, bool enable);

private:

	void savedatabase(const RSSPtr& aFeed);

	uint64_t nextUpdate;
	uint64_t lastXmlSave = GET_TICK();

	RSSPtr getUpdateItem() const;
	
	void matchFilters(const RSSPtr& aFeed, const RSSDataPtr& aData) const;

	unordered_set<RSSPtr> rssList;

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