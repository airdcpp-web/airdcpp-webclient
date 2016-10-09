
#include "stdinc.h"
#include "DCPlusPlus.h"

#include "HttpConnection.h"
#include "RSSManager.h"
#include "LogManager.h"
#include "SearchManager.h"
#include "ScopedFunctor.h"
#include "AirUtil.h"
#include "SimpleXMLReader.h"
#include <boost/algorithm/string/trim.hpp>

namespace dcpp {

#define CONFIG_NAME "RSS.xml"
#define CONFIG_DIR Util::PATH_USER_CONFIG
#define DATABASE_DIR Util::getPath(CONFIG_DIR) + "RSS" PATH_SEPARATOR_STR
#define DATABASE_VERSION "1"

RSSManager::RSSManager() : tasks(true) {
	File::ensureDirectory(DATABASE_DIR);
}

RSSManager::~RSSManager()
{
	TimerManager::getInstance()->removeListener(this);
}

void RSSManager::clearRSSData(const RSSPtr& aFeed) {
	
	{
		Lock l(cs);
		aFeed->getFeedData().clear(); 
		aFeed->setDirty(true);
	}
	fire(RSSManagerListener::RSSDataCleared(), aFeed);

}

RSSPtr RSSManager::getFeedByName(const string& aName) const {
	Lock l(cs);
	auto r = find_if(rssList.begin(), rssList.end(), [aName](const RSSPtr& a) { return aName == a->getFeedName(); });
	if (r != rssList.end())
		return *r;

	return nullptr;
}

RSSPtr RSSManager::getFeedByUrl(const string& aUrl) const {
	Lock l(cs);
	auto r = find_if(rssList.begin(), rssList.end(), [aUrl](const RSSPtr& a) { return aUrl == a->getUrl(); });
	if (r != rssList.end())
		return *r;

	return nullptr;
}

RSSPtr RSSManager::getFeedByToken(int aToken) const { 
	Lock l(cs);
	auto r = find_if(rssList.begin(), rssList.end(), [aToken](const RSSPtr& a) { return aToken == a->getToken(); });
	if (r != rssList.end())
		return *r;

	return nullptr;
}


void RSSManager::parseAtomFeed(SimpleXML& xml, RSSPtr& aFeed) {
	xml.stepIn();
		while (xml.findChild("entry")) {
			xml.stepIn();
			bool newdata = false;
			string titletmp;
			string link;
			string date;

			if (xml.findChild("link")) {
				link = xml.getChildAttrib("href");
			}
			if (xml.findChild("title")) {
				titletmp = xml.getChildData();
				newdata = checkTitle(aFeed, titletmp);
			}
			if (xml.findChild("updated"))
				date = xml.getChildData();

			if (newdata) 
				addData(titletmp, link, date, aFeed);

			xml.stepOut();
		}
	xml.stepOut();
}

void RSSManager::parseRSSFeed(SimpleXML& xml, RSSPtr& aFeed) {
	xml.stepIn();
	if (xml.findChild("channel")) {
		xml.stepIn();
		while (xml.findChild("item")) {
			xml.stepIn();
			bool newdata = false;
			string titletmp;
			string link;
			string date;
			if (xml.findChild("title")) {
				titletmp = xml.getChildData();
				newdata = checkTitle(aFeed, titletmp);
			}

			if (xml.findChild("link")) {
				link = xml.getChildData();
				//temp fix for some urls
				if (strncmp(link.c_str(), "//", 2) == 0)
					link = "https:" + link;
			}
			if (xml.findChild("pubDate"))
				date = xml.getChildData();


			if (newdata)
				addData(titletmp, link, date, aFeed);

			xml.stepOut();
		}
		xml.stepOut();
	}
	xml.stepOut();
}

void RSSManager::downloadComplete(const string& aUrl) {
	auto feed = getFeedByUrl(aUrl);
	if (!feed)
		return;

	auto& conn = feed->rssDownload;
	ScopedFunctor([&conn] { conn.reset(); });

	if (conn->buf.empty()) {
		LogManager::getInstance()->message(conn->status, LogMessage::SEV_ERROR);
		return;
	}

	string tmpdata(conn->buf);
	string erh;
	string type;
	unsigned long i = 1;
	while (i) {
		unsigned int res = 0;
		sscanf(tmpdata.substr(i-1,4).c_str(), "%x", &res);
		if (res == 0){
			i=0;
		}else{
			if (tmpdata.substr(i-1,3).find("\x0d") != string::npos)
				erh += tmpdata.substr(i+3,res);
			if (tmpdata.substr(i-1,4).find("\x0d") != string::npos)
				erh += tmpdata.substr(i+4,res);
			else
				erh += tmpdata.substr(i+5,res);
			i += res+8;
		}
	}
	try {
		SimpleXML xml;
		xml.fromXML(tmpdata.c_str());
		if(xml.findChild("rss")) {
			parseRSSFeed(xml, feed);
		}
		xml.resetCurrentChild();
		if (xml.findChild("feed")) {
			parseAtomFeed(xml, feed);
		}
	} catch(const Exception& e) {
		LogManager::getInstance()->message("Error updating the " + aUrl + " : " + e.getError().c_str(), LogMessage::SEV_ERROR);
	}
}

bool RSSManager::checkTitle(const RSSPtr& aFeed, string& aTitle) {
	if (aTitle.empty())
		return false;
	boost::algorithm::trim_if(aTitle, boost::is_space() || boost::is_any_of("\r\n"));
	Lock l(cs);
	return aFeed->getFeedData().find(aTitle) == aFeed->getFeedData().end();
}

void RSSManager::addData(const string& aTitle, const string& aLink, const string& aDate, RSSPtr& aFeed) {
	RSSDataPtr data = new RSSData(aTitle, aLink, aDate, aFeed);
	matchFilters(data);
	{
		Lock l(cs);
		aFeed->getFeedData().emplace(aTitle, data);
	}
	aFeed->setDirty(true);
	fire(RSSManagerListener::RSSDataAdded(), data);
}

void RSSManager::matchFilters(const RSSPtr& aFeed) const {
	if (aFeed) {
		Lock l(cs);
		for (auto data : aFeed->getFeedData() | map_values) {
				matchFilters(data);
		}
	}
}

void RSSManager::matchFilters(const RSSDataPtr& aData) const {
	
	for (auto& aF : rssFilterList) {
		if (aF.match(aData->getTitle())) {

			auto targetType = TargetUtil::TargetType::TARGET_PATH;
			AutoSearchManager::getInstance()->addAutoSearch(aData->getTitle(),
				aF.getDownloadTarget(), targetType, true, AutoSearch::RSS_DOWNLOAD, true);

			break; //One match is enough
		}
	}
}

void RSSManager::updateFeedItem(RSSPtr& aFeed, const string& aUrl, const string& aName, int aUpdateInterval, bool aEnable) {
	auto r = rssList.find(aFeed);
	if (r != rssList.end())
	{
		{
			Lock l(cs);
			aFeed->setUrl(aUrl);
			aFeed->setFeedName(aName);
			aFeed->setUpdateInterval(aUpdateInterval);
			aFeed->setEnable(aEnable);
		}
		fire(RSSManagerListener::RSSFeedChanged(), aFeed);
	} else {
		{
			Lock l(cs);
			rssList.emplace(aFeed);
		}
		fire(RSSManagerListener::RSSFeedAdded(), aFeed);
	}
}

void RSSManager::updateFilterList(vector<RSSFilter>& aNewList) {
	Lock l(cs);
	rssFilterList = aNewList;
	for_each(rssFilterList.begin(), rssFilterList.end(), [&](RSSFilter& i) { i.prepare(); });
}

void RSSManager::enableFeedUpdate(const RSSPtr& aFeed, bool enable) {
	Lock l(cs);
	aFeed->setEnable(enable);
	fire(RSSManagerListener::RSSFeedChanged(), aFeed);
}

void RSSManager::removeFeedItem(const RSSPtr& aFeed) {
	Lock l(cs);
	//Delete database file?
	rssList.erase(aFeed);
	fire(RSSManagerListener::RSSFeedRemoved(), aFeed);
}

void RSSManager::downloadFeed(const RSSPtr& aFeed, bool verbose/*false*/) {
	if (!aFeed)
		return;

	string url = aFeed->getUrl();
	aFeed->setLastUpdate(GET_TIME());
	tasks.addTask([=] {
		aFeed->rssDownload.reset(new HttpDownload(aFeed->getUrl(),
			[this, url] { downloadComplete(url); }, false));

		fire(RSSManagerListener::RSSFeedUpdated(), aFeed);
		if(verbose)
			LogManager::getInstance()->message("updating the " + aFeed->getUrl(), LogMessage::SEV_INFO);
	});
}

RSSPtr RSSManager::getUpdateItem() const {
	for (auto i : rssList) {
		if (i->allowUpdate())
			return i;
	}
	return nullptr;
}


void RSSManager::on(TimerManagerListener::Second, uint64_t aTick) noexcept {
	if (rssList.empty())
		return;

	if (nextUpdate < aTick) {
		Lock l(cs);
		downloadFeed(getUpdateItem());
		nextUpdate = GET_TICK() + 1 * 60 * 1000; //Minute between item updates for now, TODO: handle intervals smartly :)
	} else if ((lastXmlSave + 30000) < aTick) {
		tasks.addTask([=] {
			Lock l(cs);
			for (auto r : rssList) {
				if (r->getDirty())
					savedatabase(r);
			}
		});
		lastXmlSave = aTick;
	}
}

class RSSLoader : public SimpleXMLReader::CallBack {
public:
	RSSLoader(){ }
	~RSSLoader() { }
	void startTag(const string& aName, StringPairList& attribs, bool) {
		if (aName == "Data") {
			int version = Util::toInt(getAttrib(attribs, "Version", 0));
			if (version == 0 || version > Util::toInt(DATABASE_VERSION))
				throw Exception("Non-supported RSS database version");
			
			const string& token = getAttrib(attribs, "Token", 1);
			aFeed = RSSManager::getInstance()->getFeedByToken(Util::toInt(token));
			if (!aFeed)
				throw(Exception("No Feed associated with data"));
			
		} else if (aName == "item") {
			const string& title = getAttrib(attribs, "title", 0);
			const string& link = getAttrib(attribs, "link", 1);
			const string& pubdate = getAttrib(attribs, "pubdate", 2);
			const string& dateadded = getAttrib(attribs, "dateadded", 3);
			auto rd = new RSSData(title, link, pubdate, aFeed, Util::toInt64(dateadded));
			aFeed->getFeedData().emplace(rd->getTitle(), rd);
		}
	}

private:
	RSSPtr aFeed;
};

void RSSManager::load() {

	SimpleXML xml;
	SettingsManager::loadSettingFile(xml, CONFIG_DIR, CONFIG_NAME);
	if (xml.findChild("RSS")) {
		xml.stepIn();

		while (xml.findChild("Settings")) {
			auto feed = std::make_shared<RSS>(xml.getChildAttrib("Url"),
				xml.getChildAttrib("Name"),
				xml.getBoolChildAttrib("Enable"),
				Util::toInt64(xml.getChildAttrib("LastUpdate")),
				xml.getIntChildAttrib("UpdateInterval"),
				xml.getIntChildAttrib("Token"));
			rssList.emplace(feed);
		}
		xml.resetCurrentChild();
		while (xml.findChild("Filter")) {
			rssFilterList.emplace_back(
				xml.getChildAttrib("FilterPattern"),
				xml.getChildAttrib("DownloadTarget"),
				Util::toInt(xml.getChildAttrib("Method", "1")));
		}

		for_each(rssFilterList.begin(), rssFilterList.end(), [&](RSSFilter& i) { i.prepare(); });
		xml.stepOut();
	}

	StringList fileList = File::findFiles(DATABASE_DIR, "RSSDataBase*", File::TYPE_FILE);
	try {
		parallel_for_each(fileList.begin(), fileList.end(), [&](const string& path) {
			if (Util::getFileExt(path) == ".xml") {

				try {
					RSSLoader loader;

					File f(path, File::READ, File::OPEN, File::BUFFER_SEQUENTIAL, true);
					SimpleXMLReader(&loader).parse(f);
				}
				catch (const Exception& e) {
					LogManager::getInstance()->message(e.getError(), LogMessage::SEV_INFO);
					File::deleteFile(path);
				}
			}
		});
	}
	catch (std::exception& e) {
		LogManager::getInstance()->message("Loading the RSS failed: " + string(e.what()), LogMessage::SEV_INFO);
	}

	TimerManager::getInstance()->addListener(this);
	nextUpdate = GET_TICK() + 10 * 1000; //start after 10 seconds
}

void RSSManager::saveConfig(bool saveDatabase) {
	SimpleXML xml;
	xml.addTag("RSS");
	xml.stepIn();
	Lock l(cs);
	for (auto r : rssList) {
		xml.addTag("Settings");
		xml.addChildAttrib("Url", r->getUrl());
		xml.addChildAttrib("Name", r->getFeedName());
		xml.addChildAttrib("Enable", r->getEnable());
		xml.addChildAttrib("LastUpdate", Util::toString(r->getLastUpdate()));
		xml.addChildAttrib("UpdateInterval", Util::toString(r->getUpdateInterval()));
		xml.addChildAttrib("Token", Util::toString(r->getToken()));

		if (saveDatabase && r->getDirty())
			savedatabase(r);
	}

	for (auto f : rssFilterList) {
		xml.addTag("Filter");
		xml.addChildAttrib("FilterPattern", f.getFilterPattern());
		xml.addChildAttrib("DownloadTarget", f.getDownloadTarget());
		xml.addChildAttrib("Method", f.getMethod());
	}

	xml.stepOut();
	SettingsManager::saveSettingFile(xml, CONFIG_DIR, CONFIG_NAME);

}

#define LITERAL(n) n, sizeof(n)-1

void RSSManager::savedatabase(const RSSPtr& aFeed) {
	ScopedFunctor([&] { aFeed->setDirty(false); });

	string path = DATABASE_DIR + "RSSDataBase" + Util::toString(aFeed->getToken()) + ".xml";
	try {
		{
			string indent, tmp;

			File ff(path + ".tmp", File::WRITE, File::TRUNCATE | File::CREATE);
			BufferedOutputStream<false> xmlFile(&ff);

			xmlFile.write(SimpleXML::utf8Header);
			xmlFile.write(LITERAL("<Data Version=\"" DATABASE_VERSION));
			xmlFile.write(LITERAL("\" Token=\""));
			xmlFile.write(SimpleXML::escape(Util::toString(aFeed->getToken()), tmp, true));
			xmlFile.write(LITERAL("\">\r\n"));
			indent += '\t';

			for (auto r : aFeed->getFeedData() | map_values) {
				//Don't save more than 3 days old entries... Todo: setting?
				if ((r->getDateAdded() + 3 * 24 * 60 * 60) > GET_TIME()) {
					xmlFile.write(indent);
					xmlFile.write(LITERAL("<item title=\""));
					xmlFile.write(SimpleXML::escape(r->getTitle(), tmp, true));

					xmlFile.write(LITERAL("\" link=\""));
					xmlFile.write(SimpleXML::escape(r->getLink(), tmp, true));

					xmlFile.write(LITERAL("\" pubdate=\""));
					xmlFile.write(SimpleXML::escape(r->getPubDate(), tmp, true));

					xmlFile.write(LITERAL("\" dateadded=\""));
					xmlFile.write(SimpleXML::escape(Util::toString(r->getDateAdded()), tmp, true));

					xmlFile.write(LITERAL("\"/>\r\n"));

				}
			}
			xmlFile.write(LITERAL("</Data>"));
		}

		File::deleteFile(path);
		File::renameFile(path + ".tmp", path);
	}
	catch (Exception& e) {
		LogManager::getInstance()->message("Saving RSSDatabase failed: " + e.getError(), LogMessage::SEV_WARNING);
	}
	
}

}
