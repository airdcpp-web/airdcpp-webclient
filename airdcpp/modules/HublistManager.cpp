/*
* Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#include "HublistManager.h"
#include "HublistEntry.h"

#include <airdcpp/AppUtil.h>
#include <airdcpp/BZUtils.h>
#include <airdcpp/FilteredFile.h>
#include <airdcpp/PathUtil.h>
#include <airdcpp/Streams.h>
#include <airdcpp/StringTokenizer.h>
#include <airdcpp/SimpleXML.h>


namespace dcpp {

HublistManager::HublistManager() {
	File::ensureDirectory(getHublistPath());
}

HublistManager::~HublistManager() {
	if (c) {
		c->removeListener(this);
		delete c;
		c = nullptr;
	}
}

string HublistManager::getHublistPath() noexcept {
	return AppUtil::getPath(AppUtil::PATH_USER_LOCAL) + "HubLists" + PATH_SEPARATOR_STR;
}

HublistEntry::List HublistManager::getPublicHubs() const noexcept {
	RLock l(cs);
	auto i = publicListMatrix.find(publicListServer);
	return i != publicListMatrix.end() ? i->second : HublistEntry::List();
}

class XmlListLoader : public SimpleXMLReader::CallBack {
public:
	explicit XmlListLoader(HublistEntry::List& lst) : publicHubs(lst) { }
	~XmlListLoader() = default;
	void startTag(const string& aName, StringPairList& attribs, bool) override final {
		if (aName == "Hub") {
			const string& name = getAttrib(attribs, "Name", 0);
			const string& server = getAttrib(attribs, "Address", 1);
			const string& description = getAttrib(attribs, "Description", 2);
			const string& users = getAttrib(attribs, "Users", 3);
			const string& country = getAttrib(attribs, "Country", 4);
			const string& shared = getAttrib(attribs, "Shared", 5);
			const string& minShare = getAttrib(attribs, "Minshare", 5);
			const string& minSlots = getAttrib(attribs, "Minslots", 5);
			const string& maxHubs = getAttrib(attribs, "Maxhubs", 5);
			const string& maxUsers = getAttrib(attribs, "Maxusers", 5);
			const string& reliability = getAttrib(attribs, "Reliability", 5);
			const string& rating = getAttrib(attribs, "Rating", 5);
			publicHubs.push_back(HublistEntry(name, server, description, users, country, shared, minShare, minSlots, maxHubs, maxUsers, reliability, rating));
		}
	}
private:
	HublistEntry::List& publicHubs;
};

bool HublistManager::onHttpFinished(bool fromHttp) noexcept {
	MemoryInputStream mis(downloadBuf);
	bool success = true;

	WLock l(cs);
	auto& list = publicListMatrix[publicListServer];
	list.clear();

	try {
		XmlListLoader loader(list);

		if ((listType == TYPE_BZIP2) && (!downloadBuf.empty())) {
			FilteredInputStream<UnBZFilter, false> f(&mis);
			SimpleXMLReader(&loader).parse(f);
		}
		else {
			SimpleXMLReader(&loader).parse(mis);
		}
	}
	catch (const Exception& e) {
		dcdebug("HublistManager::onHttpFinished: %s\n", e.getError().c_str());

		success = false;
		fire(HublistManagerListener::Corrupted(), fromHttp ? publicListServer + ", " + e.getError() : Util::emptyString);
	}

	if (fromHttp) {
		try {
			File f(getHublistPath() + PathUtil::validateFileName(publicListServer), File::WRITE, File::CREATE | File::TRUNCATE);
			f.write(downloadBuf);
		}
		catch (const FileException&) {}
	}

	downloadBuf = Util::emptyString;

	return success;
}

StringList HublistManager::getHubLists() noexcept {
	StringTokenizer<string> lists(SETTING(HUBLIST_SERVERS), ';');
	return lists.getTokens();
}

void HublistManager::setHubList(int aHubList) noexcept {
	lastServer = aHubList;
	refresh();
}

void HublistManager::refresh(bool forceDownload /* = false */) noexcept {
	StringList sl = getHubLists();
	if (sl.empty())
		return;
	publicListServer = sl[(lastServer) % sl.size()];
	if (Util::findSubString(publicListServer, "http://") != 0 && Util::findSubString(publicListServer, "https://") != 0) {
		lastServer++;
		return;
	}

	if (!forceDownload) {
		string path = getHublistPath() + PathUtil::validateFileName(publicListServer);
		if (File::getSize(path) > 0) {
			useHttp = false;
			string fileDate;
			{
				WLock l(cs);
				publicListMatrix[publicListServer].clear();
			}
			listType = (Util::stricmp(path.substr(path.size() - 4), ".bz2") == 0) ? TYPE_BZIP2 : TYPE_NORMAL;
			try {
				File cached(path, File::READ, File::OPEN);
				downloadBuf = cached.read();
				char buf[20];
				time_t fd = cached.getLastModified();
				if (strftime(buf, 20, "%x", localtime(&fd))) {
					fileDate = string(buf);
				}
			}
			catch (const FileException&) {
				downloadBuf = Util::emptyString;
			}
			if (!downloadBuf.empty()) {
				if (onHttpFinished(false)) {
					fire(HublistManagerListener::LoadedFromCache(), publicListServer, fileDate);
				}
				return;
			}
		}
	}

	if (!running) {
		useHttp = true;
		{
			WLock l(cs);
			publicListMatrix[publicListServer].clear();
		}
		fire(HublistManagerListener::DownloadStarting(), publicListServer);
		if (!c)
			c = new HttpConnection();
		c->addListener(this);
		c->downloadFile(publicListServer);
		running = true;
	}
}

// HttpConnectionListener
void HublistManager::on(Data, HttpConnection*, const uint8_t* buf, size_t len) noexcept {
	if (useHttp)
		downloadBuf.append((const char*)buf, len);
}

void HublistManager::on(Failed, HttpConnection*, const string& aLine) noexcept {
	c->removeListener(this);
	lastServer++;
	running = false;
	if (useHttp) {
		downloadBuf = Util::emptyString;
		fire(HublistManagerListener::DownloadFailed(), aLine);
	}
}

void HublistManager::on(Complete, HttpConnection*, const string& aLine) noexcept {
	bool parseSuccess = false;
	c->removeListener(this);
	if (useHttp) {
		if (c->getMimeType() == "application/x-bzip2")
			listType = TYPE_BZIP2;
		parseSuccess = onHttpFinished(true);
	}
	running = false;
	if (parseSuccess) {
		fire(HublistManagerListener::DownloadFinished(), aLine);
	}
}

void HublistManager::on(Redirected, HttpConnection*, const string& aLine) noexcept {
	if (useHttp)
		fire(HublistManagerListener::DownloadStarting(), aLine);
}

} // namespace dcpp
