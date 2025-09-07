
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

#include "stdinc.h"

#include "AutoSearchManager.h"
#include "RSSManager.h"

#include <airdcpp/connection/http/HttpConnection.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/queue/QueueManager.h>
#include <airdcpp/search/SearchTypes.h>
#include <airdcpp/hub/ClientManager.h>

#include <airdcpp/core/classes/ScopedFunctor.h>
#include <airdcpp/core/io/xml/SimpleXML.h>
#include <airdcpp/core/io/xml/SimpleXMLReader.h>
#include <airdcpp/core/io/stream/Streams.h>

#include <boost/algorithm/string/trim.hpp>

namespace dcpp {

#define CONFIG_NAME "RSS.xml"
#define CONFIG_DIR AppUtil::PATH_USER_CONFIG
#define DATABASE_DIR AppUtil::getPath(CONFIG_DIR) + "RSS" PATH_SEPARATOR_STR
#define DATABASE_VERSION "1"

RSSManager::RSSManager() : tasks(true) {
	File::ensureDirectory(DATABASE_DIR);
}

RSSManager::~RSSManager()
{
	TimerManager::getInstance()->removeListener(this);
}

void RSSManager::clearRSSData(const RSSPtr& aFeed) noexcept {
	
	{
		Lock l(cs);
		aFeed->getFeedData().clear(); 
		aFeed->setDirty(true);
	}
	fire(RSSManagerListener::RSSDataCleared(), aFeed);

}

RSSPtr RSSManager::getFeedByName(const string& aName) const noexcept {
	Lock l(cs);
	auto r = find_if(rssList.begin(), rssList.end(), [aName](const RSSPtr& a) { return aName == a->getFeedName(); });
	if (r != rssList.end())
		return *r;

	return nullptr;
}

RSSPtr RSSManager::getFeedByUrl(const string& aUrl) const noexcept {
	Lock l(cs);
	auto r = find_if(rssList.begin(), rssList.end(), [aUrl](const RSSPtr& a) { return aUrl == a->getUrl(); });
	if (r != rssList.end())
		return *r;

	return nullptr;
}

RSSPtr RSSManager::getFeedByToken(int aToken) const noexcept {
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
			string title;
			string link;
			string date;

			if (xml.findChild("link")) {
				link = xml.getChildAttrib("href");
			}
			xml.resetCurrentChild();
			if (xml.findChild("title")) {
				title = xml.getChildData();
				newdata = checkTitle(aFeed, title);
			}
			xml.resetCurrentChild();
			if (xml.findChild("updated"))
				date = xml.getChildData();

			if (newdata) {
				addData(title, link, date, aFeed);
			}

			xml.resetCurrentChild();
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
			string title;
			string link;
			string date;
			if (xml.findChild("title")) {
				title = xml.getChildData();
				newdata = checkTitle(aFeed, title);
			}
			xml.resetCurrentChild();

			if (newdata) {
				if (xml.findChild("link")) {
					link = xml.getChildData();
					//temp fix for some urls
					if (strncmp(link.c_str(), "//", 2) == 0)
						link = "https:" + link;
				}

				xml.resetCurrentChild();
				if (xml.findChild("pubDate"))
					date = xml.getChildData();

				addData(title, link, date, aFeed);
				xml.resetCurrentChild();
			}

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

	ScopedFunctor([&] { feed->rssDownload.reset(); });

	if (feed->rssDownload->buf.empty()) {
		log(feed->rssDownload->status, LogMessage::SEV_ERROR);
		return;
	}

	try {
		SimpleXML xml;
		xml.fromXML(feed->rssDownload->buf);
		if(xml.findChild("rss")) {
			parseRSSFeed(xml, feed);
		}
		xml.resetCurrentChild();
		if (xml.findChild("feed")) {
			parseAtomFeed(xml, feed);
		}
	} catch(const Exception& e) {
		log(STRING_F(ERROR_UPDATING_FEED, aUrl) + " : " + e.getError().c_str(), LogMessage::SEV_ERROR);
	}

	fire(RSSManagerListener::RSSFeedUpdated(), feed);
}


void RSSManager::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(RSS_FEEDS));
}

bool RSSManager::checkTitle(const RSSPtr& aFeed, string& aTitle) {
	if (aTitle.empty())
		return false;
	boost::algorithm::trim_if(aTitle, boost::is_space() || boost::is_any_of("\r\n"));
	Lock l(cs);
	return aFeed->getFeedData().find(aTitle) == aFeed->getFeedData().end();
}

void RSSManager::addData(const string& aTitle, const string& aLink, const string& aDate, RSSPtr& aFeed) {
	auto data = make_shared<RSSData>(aTitle, aLink, aDate, aFeed);
	{
		Lock l(cs);
		aFeed->getFeedData().emplace(data->getTitle(), data);
	}
	aFeed->setDirty(true);
	fire(RSSManagerListener::RSSDataAdded(), data);
	
	Lock l(cs);
	matchFilters(aFeed, data);
}

void RSSManager::matchFilters(const RSSPtr& aFeed) {
	if (aFeed) {
		Lock l(cs);
		ranges::for_each(aFeed->getFeedData() | views::values, [&](const RSSDataPtr& data) { matchFilters(aFeed, data); });
	}
}

void RSSManager::matchFilters(const RSSPtr& aFeed, const RSSDataPtr& aData) {
	
	//Match remove filters first, so it works kind of as a skiplist also
	bool remove = find_if(aFeed->getRssFilterList().begin(), aFeed->getRssFilterList().end(), [&](const RSSFilter& a)
		{ return a.getFilterAction() == RSSFilter::REMOVE && a.match(aData->getTitle()); }) != aFeed->getRssFilterList().end();

	if (remove) {
		tasks.addTask([=] { removeFeedData(aFeed, aData); });
		return;
	}

	for (auto& aF : aFeed->getRssFilterList()) {
		if (aF.match(aData->getTitle())) {
			if (aF.skipDupes) {
				if (ShareManager::getInstance()->getAdcDirectoryDupe(aData->getTitle(), 0) != DUPE_NONE)
					break; //Need to match other filters?
				if (QueueManager::getInstance()->getAdcDirectoryDupe(aData->getTitle(), 0) != DUPE_NONE)
					break; //Need to match other filters?
			}
			if (aF.getFilterAction() == RSSFilter::DOWNLOAD || aF.getFilterAction() == RSSFilter::ADD_AUTOSEARCH) {
				addAutoSearchItem(aF, aData);
			} 
			break; //One match is enough
		}
	}
}

bool RSSManager::addAutoSearchItem(const RSSFilter& aFilter, const RSSDataPtr& aData) noexcept {
	if (!AutoSearchManager::getInstance()->validateAutoSearchStr(aData->getTitle())) {
		return false;
	}

	time_t expireTime = aFilter.getExpireDays() > 0 ? GET_TIME() + aFilter.getExpireDays() * 24 * 60 * 60 : 0;

	AutoSearchPtr as = std::make_shared<AutoSearch>(aFilter.getFilterAction() == RSSFilter::DOWNLOAD, aData->getTitle(), SEARCH_TYPE_DIRECTORY, AutoSearch::ACTION_DOWNLOAD, true, aFilter.getDownloadTarget(),
		StringMatch::EXACT, Util::emptyString, Util::emptyString, expireTime, true, true, false, Util::emptyString, AutoSearch::RSS_DOWNLOAD, false);

	//format time params, befora adding to autosearch, so we can use RSS date for folder
	if(aFilter.getFormatTimeParams())
		as->setTarget(Util::formatTime(aFilter.getDownloadTarget(), GET_TIME()));

	as->setGroup(aFilter.getAutosearchGroup());

	/*
	a hack, try to avoid growing autosearch list, allow adding max 5 items to internal searchqueue directly... will result in ~2 minute searchqueue time.
	Hopefylly most of these will get hits so we dont need to search them again.
	*/
	bool search = (aFilter.getFilterAction() == RSSFilter::DOWNLOAD) && ClientManager::getInstance()->getMaxSearchQueueSize() < 5;
	AutoSearchManager::getInstance()->addAutoSearch(as, search, false);
	return true;
}

void RSSManager::updateFeedItem(RSSPtr& aFeed, const string& aUrl, const string& aName, int aUpdateInterval, bool aEnable) noexcept {
	bool added = false;
	{
		Lock l(cs);
		aFeed->setUrl(aUrl);
		aFeed->setFeedName(aName);
		aFeed->setUpdateInterval(aUpdateInterval);
		aFeed->setEnable(aEnable);

		auto r = find_if(rssList.begin(), rssList.end(), [aFeed](const RSSPtr& a) { return aFeed->getToken() == a->getToken(); });
		if (r == rssList.end()) {
			added = true;
			rssList.emplace_back(aFeed);
		}
	}

	if(added)
		fire(RSSManagerListener::RSSFeedAdded(), aFeed);
	else
		fire(RSSManagerListener::RSSFeedChanged(), aFeed);

}

void RSSManager::updateFilterList(const RSSPtr& aFeed, vector<RSSFilter>& aNewList) {
	Lock l(cs);
	aFeed->rssFilterList = aNewList;
	for_each(aFeed->rssFilterList.begin(), aFeed->rssFilterList.end(), [&](RSSFilter& i) { i.prepare(); });
}

void RSSManager::enableFeedUpdate(const RSSPtr& aFeed, bool enable) noexcept {
	Lock l(cs);
	aFeed->setEnable(enable);
	fire(RSSManagerListener::RSSFeedChanged(), aFeed);
}

void RSSManager::removeFeedItem(const RSSPtr& aFeed) noexcept {
	Lock l(cs);
	//Delete database file?
	auto [first, last] = ranges::remove_if(rssList, [aFeed](const RSSPtr& a) { return aFeed->getToken() == a->getToken(); });
	rssList.erase(first, last);
	fire(RSSManagerListener::RSSFeedRemoved(), aFeed);
}

void RSSManager::removeFeedData(const RSSPtr& aFeed, const RSSDataPtr& aData) {
	fire(RSSManagerListener::RSSDataRemoved(), aData);
	Lock l(cs);
	aFeed->getFeedData().erase(aData->getTitle());
	aFeed->setDirty(true);
}

void RSSManager::downloadFeed(const RSSPtr& aFeed, bool verbose/*false*/) noexcept {
	if (!aFeed)
		return;

	aFeed->setLastUpdate(GET_TIME());

	tasks.addTask([=] {
		aFeed->rssDownload.reset(new HttpDownload(aFeed->getUrl(),
			[this, aFeed] { downloadComplete(aFeed->getUrl()); }));

		if(verbose)
			log(STRING(UPDATING) + " " + aFeed->getUrl(), LogMessage::SEV_INFO);
	});

	//Lets resort the list to get a better chance for all other items to update and not end up updating the same one.
	Lock l(cs);
	sort(rssList.begin(), rssList.end(), [](const RSSPtr& a, const RSSPtr& b) { return a->getLastUpdate() < b->getLastUpdate(); });
}

RSSPtr RSSManager::getUpdateItem() const noexcept {
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
	} else if ((lastXmlSave + 15000) < aTick) {
		Lock l(cs);
		for_each(rssList.begin(), rssList.end(), [&](RSSPtr r) { if (r->getDirty()) tasks.addTask([=] { savedatabase(r); }); });
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
				throw Exception("No Feed associated with data");
			
		} else if (aName == "item") {
			const string& title = getAttrib(attribs, "title", 0);
			const string& link = getAttrib(attribs, "link", 1);
			const string& pubdate = getAttrib(attribs, "pubdate", 2);
			const string& dateadded = getAttrib(attribs, "dateadded", 3);

			Lock l(RSSManager::getInstance()->getCS());
			auto rd = make_shared<RSSData>(title, link, pubdate, aFeed, Util::toInt64(dateadded));
			aFeed->getFeedData().emplace(rd->getTitle(), rd);
		}
	}

private:
	RSSPtr aFeed;
};

void RSSManager::load() {
	SettingsManager::loadSettingFile(CONFIG_DIR, CONFIG_NAME, [this](SimpleXML& xml) {
		if (xml.findChild("RSS")) {
			xml.stepIn();

			while (xml.findChild("Settings"))
			{
				auto feed = std::make_shared<RSS>(xml.getChildAttrib("Url"),
					xml.getChildAttrib("Name"),
					xml.getBoolChildAttrib("Enable"),
					Util::toInt64(xml.getChildAttrib("LastUpdate")),
					xml.getIntChildAttrib("UpdateInterval"),
					xml.getIntChildAttrib("Token"));
				xml.stepIn();

				loadFilters(xml, feed->rssFilterList);

				xml.resetCurrentChild();
				xml.stepOut();

				Lock l(cs);
				for_each(feed->rssFilterList.begin(), feed->rssFilterList.end(), [&](RSSFilter& i) { i.prepare(); });
				rssList.emplace_back(feed);
			}
			xml.resetCurrentChild();
			xml.stepOut();
		}
	});

	try {
		StringList fileList = File::findFiles(DATABASE_DIR, "RSSDataBase*", File::TYPE_FILE);
		parallel_for_each(fileList.begin(), fileList.end(), [&](const string& path) {
			if (PathUtil::getFileExt(path) == ".xml") {
				try {
					RSSLoader loader;

					File f(path, File::READ, File::OPEN, File::BUFFER_SEQUENTIAL, true);
					SimpleXMLReader(&loader).parse(f);
				}
				catch (const Exception& e) {
					log(e.getError(), LogMessage::SEV_INFO);
					File::deleteFile(path);
				}
			}
		});
	} catch (...) { }

	nextUpdate = GET_TICK() + 120 * 1000; //start after 120 seconds
	TimerManager::getInstance()->addListener(this);

}

void RSSManager::loadFilters(SimpleXML& xml, vector<RSSFilter>& aList) {
	if (xml.findChild("Filters")) {
		xml.stepIn();
		while (xml.findChild("Filter")) {
			aList.emplace_back(
				xml.getChildAttrib("FilterPattern"),
				xml.getChildAttrib("DownloadTarget"),
				Util::toInt(xml.getChildAttrib("Method", "1")),
				xml.getChildAttrib("AutoSearchGroup"),
				xml.getBoolChildAttrib("SkipDupes"),
				Util::toInt(xml.getChildAttrib("FilterAction", "0")),
				Util::toInt(xml.getChildAttrib("ExpireDays", "3")),
				xml.getBoolChildAttrib("FormatTimeParams"));
		}
		xml.stepOut();
	}
}

void RSSManager::save(bool aSaveDatabase/*false*/) {
	SimpleXML xml;
	xml.addTag("RSS");
	xml.stepIn();
	Lock l(cs);
	vector<RSSPtr> saveList;
	for (auto r : rssList) {
		xml.addTag("Settings");
		xml.addChildAttrib("Url", r->getUrl());
		xml.addChildAttrib("Name", r->getFeedName());
		xml.addChildAttrib("Enable", r->getEnable());
		xml.addChildAttrib("LastUpdate", Util::toString(r->getLastUpdate()));
		xml.addChildAttrib("UpdateInterval", Util::toString(r->getUpdateInterval()));
		xml.addChildAttrib("Token", Util::toString(r->getToken()));
		
		xml.stepIn();
		saveFilters(xml, r->getRssFilterList());
		xml.stepOut();
		if (aSaveDatabase && r->getDirty())
			saveList.push_back(r);
	}
	xml.stepOut();
	SettingsManager::saveSettingFile(xml, CONFIG_DIR, CONFIG_NAME);
	for_each(saveList.begin(), saveList.end(), [&](RSSPtr r) { savedatabase(r); });

}

void RSSManager::saveFilters(SimpleXML& aXml, const vector<RSSFilter>& aList) {
	if (!aList.empty()) {
		aXml.addTag("Filters");
		aXml.stepIn();
		for (auto f : aList) {
			aXml.addTag("Filter");
			aXml.addChildAttrib("FilterPattern", f.getFilterPattern());
			aXml.addChildAttrib("DownloadTarget", f.getDownloadTarget());
			aXml.addChildAttrib("Method", f.getMethod());
			aXml.addChildAttrib("AutoSearchGroup", f.getAutosearchGroup());
			aXml.addChildAttrib("SkipDupes", f.skipDupes);
			aXml.addChildAttrib("FilterAction", f.getFilterAction());
			aXml.addChildAttrib("ExpireDays", f.getExpireDays());
			aXml.addChildAttrib("FormatTimeParams", f.getFormatTimeParams());
		}
		aXml.stepOut();
	}
}

#define LITERAL(n) n, sizeof(n)-1

void RSSManager::savedatabase(const RSSPtr& aFeed) {
	
	aFeed->setDirty(false);

	string path = DATABASE_DIR + "RSSDataBase" + Util::toString(aFeed->getToken()) + ".xml";
	try {
		{
			string indent, tmp;

			File ff(path + ".tmp", File::WRITE, File::TRUNCATE | File::CREATE, File::BUFFER_WRITE_THROUGH);
			BufferedOutputStream<false> xmlFile(&ff);

			xmlFile.write(SimpleXML::utf8Header);
			xmlFile.write(LITERAL("<Data Version=\"" DATABASE_VERSION));
			xmlFile.write(LITERAL("\" Token=\""));
			xmlFile.write(SimpleXML::escape(Util::toString(aFeed->getToken()), tmp, true));
			xmlFile.write(LITERAL("\">\r\n"));
			indent += '\t';

			Lock l(cs);
			for (const auto& r : aFeed->getFeedData() | views::values) {
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
		log("Saving RSSDatabase failed: " + e.getError(), LogMessage::SEV_WARNING);
	}
	
}

}
