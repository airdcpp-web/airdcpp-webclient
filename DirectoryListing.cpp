
/* 
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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
#include "Bundle.h"
#include "DirectoryListing.h"

#include "QueueManager.h"
#include "ShareManager.h"

#include "StringTokenizer.h"
#include "SimpleXML.h"
#include "FilteredFile.h"
#include "BZUtils.h"
#include "CryptoManager.h"
#include "ResourceManager.h"
#include "SimpleXMLReader.h"
#include "User.h"
#include "ADLSearch.h"
#include "DirectoryListingManager.h"
#include "ScopedFunctor.h"

#include <boost/range/algorithm/for_each.hpp>
#include <boost/range/algorithm/find_if.hpp>


namespace dcpp {

using boost::range::for_each;
using boost::range::find_if;

//atomic_flag DirectoryListing::running = ATOMIC_FLAG_INIT;

DirectoryListing::DirectoryListing(const HintedUser& aUser, bool aPartial, const string& aFileName, bool aIsClientView, bool aIsOwnList) : 
	hintedUser(aUser), abort(false), root(new Directory(nullptr, Util::emptyString, false, false)), partialList(aPartial), isOwnList(aIsOwnList), fileName(aFileName),
	isClientView(aIsClientView), curSearch(nullptr), secondsEllapsed(0), matchADL(BOOLSETTING(USE_ADLS) && !aPartial), typingFilter(false), reloading(false)
{
	running.clear();
}

DirectoryListing::~DirectoryListing() {
	if (curSearch)
		delete curSearch;
	delete root;
}

void DirectoryListing::sortDirs() {
	root->sortDirs();
}

void DirectoryListing::Directory::sortDirs() {
	for(auto i = directories.begin(); i != directories.end(); ++i) {
		(*i)->sortDirs();
	}
	sort(directories.begin(), directories.end(), Directory::DefaultSort());
}

bool DirectoryListing::Directory::Sort::operator()(const Ptr& a, const Ptr& b) const {
	return compare(a->getName(), b->getName()) < 0;
}

bool DirectoryListing::Directory::DefaultSort::operator()(const Ptr& a, const Ptr& b) const {
	if (a->getAdls() && !b->getAdls())
		return true;
	return Util::DefaultSort(Text::toT(a->getName()).c_str(), Text::toT(b->getName()).c_str()) < 0;
}

bool DirectoryListing::File::Sort::operator()(const Ptr& a, const Ptr& b) const {
	return compare(a->getName(), b->getName()) < 0;
}

UserPtr DirectoryListing::getUserFromFilename(const string& fileName) {
	// General file list name format: [username].[CID].[xml|xml.bz2]

	string name = Util::getFileName(fileName);

	// Strip off any extensions
	if(stricmp(name.c_str() + name.length() - 4, ".bz2") == 0) {
		name.erase(name.length() - 4);
	}

	if(stricmp(name.c_str() + name.length() - 4, ".xml") == 0) {
		name.erase(name.length() - 4);
	}

	// Find CID
	string::size_type i = name.rfind('.');
	if(i == string::npos) {
		return UserPtr();
	}

	size_t n = name.length() - (i + 1);
	// CID's always 39 chars long...
	if(n != 39)
		return UserPtr();

	CID cid(name.substr(i + 1));
	if(cid.isZero())
		return UserPtr();

	return ClientManager::getInstance()->getUser(cid);
}

void DirectoryListing::loadFile(const string& name) {
	// For now, we detect type by ending...
	string ext = Util::getFileExt(name);

	dcpp::File ff(name, dcpp::File::READ, dcpp::File::OPEN);
	if(stricmp(ext, ".bz2") == 0) {
		FilteredInputStream<UnBZFilter, false> f(&ff);
		loadXML(f, false);
	} else if(stricmp(ext, ".xml") == 0) {
		loadXML(ff, false);
	}
}

class ListLoader : public SimpleXMLReader::CallBack {
public:
	ListLoader(DirectoryListing* aList, DirectoryListing::Directory* root, bool aUpdating, const UserPtr& aUser, bool aCheckDupe, bool aPartialList) : 
	  list(aList), cur(root), base("/"), inListing(false), updating(aUpdating), user(aUser), checkDupe(aCheckDupe), partialList(aPartialList) { 
	}

	virtual ~ListLoader() { }

	void startTag(const string& name, StringPairList& attribs, bool simple);
	void endTag(const string& name);

	const string& getBase() const { return base; }
private:
	DirectoryListing* list;
	DirectoryListing::Directory* cur;
	UserPtr user;

	string baseLower;
	string base;
	bool inListing;
	bool updating;
	bool checkDupe;
	bool partialList;
};

string DirectoryListing::updateXML(const string& xml) {
	MemoryInputStream mis(xml);
	return loadXML(mis, true);
}

string DirectoryListing::loadXML(InputStream& is, bool updating) {
	ListLoader ll(this, getRoot(), updating, getUser(), !isOwnList && isClientView && BOOLSETTING(DUPES_IN_FILELIST), partialList);
	try {
		dcpp::SimpleXMLReader(&ll).parse(is);
	} catch(SimpleXMLException& e) {
		//Better to abort and show the error, than just leave it hanging.
		LogManager::getInstance()->message("Error in Filelist loading: "  + e.getError() + ". User: [ " +  
			Util::toString(ClientManager::getInstance()->getNicks(HintedUser(getUser(), Util::emptyString))) + " ]", LogManager::LOG_ERROR);
		//dcdebug("DirectoryListing loadxml error: %s", e.getError());
	}
	return ll.getBase();
}

static const string sFileListing = "FileListing";
static const string sBase = "Base";
static const string sBaseDate = "BaseDate";
static const string sGenerator = "Generator";
static const string sDirectory = "Directory";
static const string sIncomplete = "Incomplete";
static const string sFile = "File";
static const string sName = "Name";
static const string sSize = "Size";
static const string sTTH = "TTH";
static const string sDate = "Date";
void ListLoader::startTag(const string& name, StringPairList& attribs, bool simple) {
	if(list->getAbort()) {
		throw AbortException();
	}

	if(inListing) {
		if(name == sFile) {
			const string& n = getAttrib(attribs, sName, 0);
			if(n.empty())
				return;
			const string& s = getAttrib(attribs, sSize, 1);
			if(s.empty())
				return;
			auto size = Util::toInt64(s);

			const string& h = getAttrib(attribs, sTTH, 2);
			if(h.empty() && !SettingsManager::lanMode)
				return;		
			TTHValue tth(h); /// @todo verify validity?

			DirectoryListing::File* f = new DirectoryListing::File(cur, n, size, tth, checkDupe);
			cur->files.push_back(f);
		} else if(name == sDirectory) {
			const string& n = getAttrib(attribs, sName, 0);
			if(n.empty()) {
				throw SimpleXMLException("Directory missing name attribute");
			}
			bool incomp = getAttrib(attribs, sIncomplete, 1) == "1";
			const string& size = getAttrib(attribs, sSize, 2);
			const string& date = getAttrib(attribs, sDate, 3);

			DirectoryListing::Directory* d = nullptr;
			if(updating) {
				auto s =  list->baseDirs.find(baseLower + Text::toLower(n) + '/');
				if (s != list->baseDirs.end()) {
					d = s->second.first;
				}
			}

			if(!d) {
				d = new DirectoryListing::Directory(cur, n, false, !incomp, (partialList && checkDupe), size, date);
				cur->directories.push_back(d);
			} else {
				if(!d->getComplete()) {
					d->setComplete(!incomp);
				}
				d->setDate(date);
			}
			cur = d;

			if(simple) {
				// To handle <Directory Name="..." />
				endTag(name);
			}
		}
	} else if(name == sFileListing) {
		if (updating) {
			const string& b = getAttrib(attribs, sBase, 2);
			if(b.size() >= 1 && b[0] == '/' && b[b.size()-1] == '/') {
				base = b;
			}
			const string& date = getAttrib(attribs, sBaseDate, 3);

			StringList sl = StringTokenizer<string>(base.substr(1), '/').getTokens();
			for(auto i = sl.begin(); i != sl.end(); ++i) {
				auto s = find_if(cur->directories, [&](DirectoryListing::Directory* dir) { return dir->getName() == *i; });
				if (s == cur->directories.end()) {
					auto d = new DirectoryListing::Directory(cur, *i, false, false, true);
					cur->directories.push_back(d);
					list->baseDirs[Text::toLower(Util::toAdcFile(d->getPath()))] = make_pair(d, false);
					cur = d;
				} else {
					cur = *s;
				}
			}

			baseLower = Text::toLower(base);
			auto& p = list->baseDirs[baseLower];

			if ((!cur->directories.empty() || !cur->files.empty() || base.empty()) && p.second) {
				//we have been here already, just reload all items
				cur->clearAll();

				//also clean the visited dirs
				for(auto i = list->baseDirs.begin(); i != list->baseDirs.end(); ) {
					auto tmp = i->first;
					if (AirUtil::isSub(i->first, base)) {
						list->baseDirs.erase(i++);
					} else {
						i++;
					}
				}
			}

			//set the dir as visited
			p.second = true;

			cur->setDate(date);
		}

		cur->setComplete(true);
		inListing = true;

		if(simple) {
			// To handle <Directory Name="..." />
			endTag(name);
		}
	}
}

void ListLoader::endTag(const string& name) {
	if(inListing) {
		if(name == sDirectory) {
			cur = cur->getParent();
		} else if(name == sFileListing) {
			// cur should be root now...
			inListing = false;
		}
	}
}

DirectoryListing::File::File(Directory* aDir, const string& aName, int64_t aSize, const TTHValue& aTTH, bool checkDupe) noexcept : 
	name(aName), size(aSize), parent(aDir), tthRoot(aTTH), adls(false), dupe(DUPE_NONE) {
	if (checkDupe && size > 0) {
		dupe = SettingsManager::lanMode ? AirUtil::checkFileDupe(name, size) : AirUtil::checkFileDupe(tthRoot, name);
	}
}

DirectoryListing::Directory::Directory(Directory* aParent, const string& aName, bool _adls, bool aComplete, bool checkDupe /*false*/, const string& aSize /*empty*/, const string& aDate /*empty*/) 
		: name(aName), parent(aParent), adls(_adls), complete(aComplete), dupe(DUPE_NONE), partialSize(0) {

	if (!aSize.empty()) {
		partialSize = Util::toInt64(aSize);
	}

	if (checkDupe) {
		dupe = AirUtil::checkDirDupe(getPath(), partialSize);
	}

	setDate(aDate);
}

void DirectoryListing::Directory::setDate(const string& aDate) {
	time_t dateRaw = 0;
	if (!aDate.empty()) {
		dateRaw = static_cast<time_t>(Util::toUInt32(aDate));
		//workaround for versions 2.2x, remove later
		if (dateRaw < 10000 && aDate.length() == 10) {
			int yy=0, mm=0, dd=0;
			struct tm when;
			sscanf_s(aDate.c_str(), "%d-%d-%d", &yy, &mm, &dd );
			when.tm_year = yy - 1900;
			when.tm_mon = mm - 1;
			when.tm_mday = dd;
			when.tm_isdst = 0;
			when.tm_hour=16;
			when.tm_min=0;
			when.tm_sec=0;
			dateRaw = mktime(&when);
		}
	}
	date = dateRaw;
}

void DirectoryListing::Directory::search(DirectSearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults) {
	if (adls)
		return;

	if (aStrings.hasRoot) {
		auto pos = find_if(files, [aStrings](File* aFile) { return aFile->getTTH() == aStrings.root; });
		if (pos != files.end()) {
			DirectSearchResultPtr sr(new DirectSearchResult(Util::toAdcFile(getPath())));
			aResults.push_back(sr);
		}
	} else {
		if(aStrings.matchesDirectDirectoryName(name)) {
			auto path = parent ? Util::toAdcFile(parent->getPath()) : "/";
			auto res = find_if(aResults, [path](DirectSearchResultPtr sr) { return sr->getPath() == path; });
			if (res == aResults.end() && aStrings.matchesSize(getTotalSize(false))) {
				DirectSearchResultPtr sr(new DirectSearchResult(path));
				aResults.push_back(sr);
			}
		}

		if(!aStrings.isDirectory) {
			for(auto i = files.begin(); i != files.end(); ++i) {
				if(aStrings.matchesDirectFile((*i)->getName(), (*i)->getSize())) {
					DirectSearchResultPtr sr(new DirectSearchResult(Util::toAdcFile(getPath())));
					aResults.push_back(sr);
					break;
				}
			}
		}
	}

	for(auto l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
		(*l)->search(aResults, aStrings, maxResults);
	}
}

string DirectoryListing::getPath(const Directory* d) const {
	if(d == root)
		return Util::emptyString;

	string dir;
	dir.reserve(128);
	dir.append(d->getName());
	dir.append(1, '\\');

	Directory* cur = d->getParent();
	while(cur!=root) {
		dir.insert(0, cur->getName() + '\\');
		cur = cur->getParent();
	}
	return dir;
}

bool DirectoryListing::Directory::findIncomplete() {
	/* Recursive check for incomplete dirs */
	if(!complete) {
		return true;
	}
	return find_if(directories, [](Directory* dir) { return dir->findIncomplete(); }) != directories.end();
}

void DirectoryListing::downloadDir(Directory* aDir, const string& aTarget, TargetUtil::TargetType aTargetType, bool isSizeUnknown, QueueItem::Priority prio, bool first, BundlePtr aBundle) {
	string target;
	if (first) {
		//check if there are incomplete dirs in a partial list
		if (partialList && aDir->findIncomplete()) {
			if (isClientView) {
				DirectoryListingManager::getInstance()->addDirectoryDownload(aDir->getPath(), hintedUser, aTarget, aTargetType, isSizeUnknown ? ASK_USER : NO_CHECK, prio);
			} else {
				//there shoudn't be incomplete dirs in recursive partial lists, most likely the other client doesn't support the RE flag
				DirectoryListingManager::getInstance()->addDirectoryDownload(aDir->getPath(), hintedUser, aTarget, aTargetType, isSizeUnknown ? ASK_USER : NO_CHECK, prio, true);
			}
			return;
		}

		//validate the target
		target = Util::validateFileName(Util::formatTime(aTarget + (aDir == getRoot() ? Util::emptyString : aDir->getName() + PATH_SEPARATOR), 
			(BOOLSETTING(FORMAT_DIR_REMOTE_TIME) && aDir->getDate() > 0) ? aDir->getDate() : GET_TIME()));

		/* Check if this is a root dir containing release dirs */
		boost::regex reg;
		reg.assign(AirUtil::getReleaseRegBasic());

		if (!boost::regex_match(aDir->getName(), reg) && aDir->files.empty() && count_if(aDir->directories.begin(), aDir->directories.end(), 
			[&reg](Directory* d) { return boost::regex_match(d->getName(), reg); }) == (int)aDir->directories.size()) {
			
			/* Create bundles from each subfolder */
			for_each(aDir->directories, [&](Directory* dir) { downloadDir(dir, target, aTargetType, isSizeUnknown, prio, false, nullptr); });
			return;
		}
	} else {
		target = aTarget + aDir->getName() + PATH_SEPARATOR;
	}

	auto& dirList = aDir->directories;
	auto& fileList = aDir->files;

	if (!aBundle) {
		aBundle = BundlePtr(new Bundle(target, GET_TIME(), (Bundle::Priority)prio, aDir->getDate()));
		first = true;
	}

	// First, recurse over the directories
	sort(dirList.begin(), dirList.end(), Directory::Sort());
	for_each(dirList, [&](Directory* dir) { downloadDir(dir, target, aTargetType, isSizeUnknown, prio, false, aBundle); });

	// Then add the files
	sort(fileList.begin(), fileList.end(), File::Sort());
	for(auto i = fileList.begin(); i != fileList.end(); ++i) {
		try {
			download(*i, target + (*i)->getName(), false, QueueItem::DEFAULT, aBundle);
		} catch(const QueueException&) {
			// Catch it here to allow parts of directories to be added...
		} catch(const FileException&) {
			//..
		}
	}

	if (first) {
		QueueManager::getInstance()->addBundle(aBundle);
	}
}

void DirectoryListing::downloadDir(const string& aDir, const string& aTarget, TargetUtil::TargetType aTargetType, bool highPrio, QueueItem::Priority prio) {
	dcassert(aDir.size() > 2);
	dcassert(aDir[aDir.size() - 1] == '\\'); // This should not be PATH_SEPARATOR
	Directory* d = findDirectory(aDir, getRoot());
	if(d)
		downloadDir(d, aTarget, aTargetType, highPrio, prio);
}

int64_t DirectoryListing::getDirSize(const string& aDir) {
	dcassert(aDir.size() > 2);
	dcassert(aDir[aDir.size() - 1] == '\\'); // This should not be PATH_SEPARATOR
	Directory* d = findDirectory(aDir, getRoot());
	if(d)
		return d->getTotalSize(false);
	return 0;
}

void DirectoryListing::download(File* aFile, const string& aTarget, bool view, QueueItem::Priority prio, BundlePtr aBundle) {
	Flags::MaskType flags = (Flags::MaskType)(view ? (QueueItem::FLAG_TEXT | QueueItem::FLAG_CLIENT_VIEW) : 0);

	QueueManager::getInstance()->add(aTarget, aFile->getSize(), aFile->getTTH(), getHintedUser(), aFile->getPath() + aFile->getName(), flags, true, prio, aBundle);
}

DirectoryListing::Directory* DirectoryListing::findDirectory(const string& aName, Directory* current) {
	string::size_type end = aName.find('\\');
	dcassert(end != string::npos);
	string name = aName.substr(0, end);

	auto i = find(current->directories.begin(), current->directories.end(), name);
	if(i != current->directories.end()) {
		if(end == (aName.size() - 1))
			return *i;
		else
			return findDirectory(aName.substr(end + 1), *i);
	}
	return nullptr;
}

void DirectoryListing::Directory::findFiles(const boost::regex& aReg, File::List& aResults) const {
	for(auto i = files.begin(); i != files.end(); ++i) {
		auto df = *i;
		if (boost::regex_match(df->getName(), aReg)) {
			aResults.push_back(df);
		}
	}

	for_each(directories, [&aReg, &aResults](Directory* dir) { dir->findFiles(aReg, aResults); });
}

bool DirectoryListing::findNfo(const string& aPath) {
	auto dir = findDirectory(aPath, root);
	if (dir) {
		boost::regex reg;
		reg.assign("(.+\\.nfo)", boost::regex_constants::icase);
		File::List results;
		dir->findFiles(reg, results);

		if (!results.empty()) {
			try {
				QueueManager::getInstance()->add(Util::getOpenPath(results.front()->getName()), results.front()->getSize(), results.front()->getTTH(), 
					hintedUser, results.front()->getPath() + results.front()->getName(), QueueItem::FLAG_CLIENT_VIEW | QueueItem::FLAG_TEXT);
			} catch(const Exception&) { }
			return true;
		}
	}

	if (isClientView)
		fire(DirectoryListingListener::UpdateStatusMessage(), CSTRING(NO_NFO_FOUND));
	else
		LogManager::getInstance()->message(Util::toString(ClientManager::getInstance()->getNicks(hintedUser)) + ": " + STRING(NO_NFO_FOUND), LogManager::LOG_INFO);
	return false;
}

struct HashContained {
	HashContained(const DirectoryListing::Directory::TTHSet& l) : tl(l) { }
	const DirectoryListing::Directory::TTHSet& tl;
	bool operator()(const DirectoryListing::File::Ptr i) const {
		return tl.count((i->getTTH())) && (DeleteFunction()(i), true);
	}
private:
	HashContained& operator=(HashContained&);
};

struct DirectoryEmpty {
	bool operator()(const DirectoryListing::Directory::Ptr i) const {
		bool r = i->getFileCount() + i->directories.size() == 0;
		if (r) DeleteFunction()(i);
		return r;
	}
};

DirectoryListing::Directory::~Directory() {
	for_each(directories, DeleteFunction());
	for_each(files, DeleteFunction());
}

void DirectoryListing::Directory::clearAll() {
	for_each(directories, DeleteFunction());
	for_each(files, DeleteFunction());
	directories.clear();
	files.clear();
}

void DirectoryListing::Directory::filterList(DirectoryListing& dirList) {
	DirectoryListing::Directory* d = dirList.getRoot();

	TTHSet l;
	d->getHashList(l);
	filterList(l);
}

void DirectoryListing::Directory::filterList(DirectoryListing::Directory::TTHSet& l) {
	for(auto i = directories.begin(); i != directories.end(); ++i) (*i)->filterList(l);

	directories.erase(std::remove_if(directories.begin(),directories.end(),DirectoryEmpty()),directories.end());

	files.erase(std::remove_if(files.begin(),files.end(),HashContained(l)),files.end());
	if((SETTING(SKIP_SUBTRACT) > 0) && (files.size() < 2)) {   //setting for only skip if folder filecount under x ?
		for(auto f = files.begin(); f != files.end(); ) {
			if((*f)->getSize() < (SETTING(SKIP_SUBTRACT) *1024) ) {
				files.erase(f);
			} else ++f;
		}
	}
}

void DirectoryListing::Directory::getHashList(DirectoryListing::Directory::TTHSet& l) {
	for(auto i = directories.begin(); i != directories.end(); ++i) (*i)->getHashList(l);
		for(DirectoryListing::File::Iter i = files.begin(); i != files.end(); ++i) l.insert((*i)->getTTH());
}
	
void DirectoryListing::getLocalPaths(const File* f, StringList& ret) {
	if(f->getParent()->getAdls() && (f->getParent()->getParent() == root || !isOwnList))
		return;

	string path;
	if (f->getParent()->getAdls())
		path = ((AdlDirectory*)f->getParent())->getFullPath();
	else
		path = getPath(f->getParent());

	ShareManager::getInstance()->getRealPaths(Util::toAdcFile(path + f->getName()), ret, Util::toInt(fileName));
}

void DirectoryListing::getLocalPaths(const Directory* d, StringList& ret) {
	if(d->getAdls() && (d->getParent() == root || !isOwnList))
		return;

	string path;
	if (d->getAdls())
		path = ((AdlDirectory*)d)->getFullPath();
	else
		path = getPath(d);
	ShareManager::getInstance()->getRealPaths(Util::toAdcFile(path), ret, Util::toInt(fileName));
}

int64_t DirectoryListing::Directory::getTotalSize(bool countAdls) {
	if(!complete)
		return partialSize;
	if(!countAdls && adls)
		return 0;
	
	int64_t x = getFilesSize();
	for(auto i = directories.begin(); i != directories.end(); ++i) {
		if(!countAdls && (*i)->getAdls())
			continue;
		x += (*i)->getTotalSize(adls);
	}
	return x;
}

size_t DirectoryListing::Directory::getTotalFileCount(bool countAdls) {
	if(!countAdls && adls)
		return 0;

	size_t x = getFileCount();
	for(auto i = directories.begin(); i != directories.end(); ++i) {
		if(!countAdls && (*i)->getAdls())
			continue;
		x += (*i)->getTotalFileCount(adls);
	}
	return x;
}

void DirectoryListing::Directory::clearAdls() {
	for(auto i = directories.begin(); i != directories.end(); ++i) {
		if((*i)->getAdls()) {
			delete *i;
			directories.erase(i);
			--i;
		}
	}
}

string DirectoryListing::Directory::getPath() const {
	string tmp;
	//make sure to not try and get the name of the root dir
	if(getParent() && getParent()->getParent()){
		return getParent()->getPath() +  getName() + '\\';
	}
	return getName() + '\\';
}

int64_t DirectoryListing::Directory::getFilesSize() const {
	int64_t x = 0;
	for(auto i = files.begin(); i != files.end(); ++i) {
		x+=(*i)->getSize();
	}
	return x;
}

uint8_t DirectoryListing::Directory::checkShareDupes() {
	uint8_t result = DUPE_NONE;
	bool first = true;
	for(auto i = directories.begin(); i != directories.end(); ++i) {
		result = (*i)->checkShareDupes();
		if(getDupe() == DUPE_NONE && first)
			setDupe((DupeType)result);

		//full dupe with same type for non-dupe dir, change to partial (or pass partial dupes to upper level folder)
		else if((result == SHARE_DUPE || result == PARTIAL_SHARE_DUPE) && getDupe() == DUPE_NONE && !first)
			setDupe(PARTIAL_SHARE_DUPE);
		else if((result == QUEUE_DUPE || result == PARTIAL_QUEUE_DUPE) && getDupe() == DUPE_NONE && !first)
			setDupe(PARTIAL_QUEUE_DUPE);

		//change to mixed dupe type
		else if((getDupe() == SHARE_DUPE || getDupe() == PARTIAL_SHARE_DUPE) && (result == QUEUE_DUPE || result == PARTIAL_QUEUE_DUPE))
			setDupe(SHARE_QUEUE_DUPE);
		else if((getDupe() == QUEUE_DUPE || getDupe() == PARTIAL_QUEUE_DUPE) && (result == SHARE_DUPE || result == PARTIAL_SHARE_DUPE))
			setDupe(SHARE_QUEUE_DUPE);

		else if (result == SHARE_QUEUE_DUPE)
			setDupe(SHARE_QUEUE_DUPE);

		first = false;
	}

	first = true;
	for(auto i = files.begin(); i != files.end(); ++i) {
		//don't count 0 byte files since it'll give lots of partial dupes
		//of no interest
		if((*i)->getSize() > 0) {			
			//if it's the first file in the dir and no sub-folders exist mark it as a dupe.
			if(getDupe() == DUPE_NONE && (*i)->getDupe() == SHARE_DUPE && directories.empty() && first)
				setDupe(SHARE_DUPE);
			else if(getDupe() == DUPE_NONE && (*i)->isQueued() && directories.empty() && first)
				setDupe(QUEUE_DUPE);

			//if it's the first file in the dir and we do have sub-folders but no dupes, mark as partial.
			else if(getDupe() == DUPE_NONE && (*i)->getDupe() == SHARE_DUPE && !directories.empty() && first)
				setDupe(PARTIAL_SHARE_DUPE);
			else if(getDupe() == DUPE_NONE && (*i)->isQueued() && !directories.empty() && first)
				setDupe(PARTIAL_QUEUE_DUPE);
			
			//if it's not the first file in the dir and we still don't have a dupe, mark it as partial.
			else if(getDupe() == DUPE_NONE && (*i)->getDupe() == SHARE_DUPE && !first)
				setDupe(PARTIAL_SHARE_DUPE);
			else if(getDupe() == DUPE_NONE && (*i)->isQueued() && !first)
				setDupe(PARTIAL_QUEUE_DUPE);
			
			//if it's a dupe and we find a non-dupe, mark as partial.
			else if(getDupe() == SHARE_DUPE && (*i)->getDupe() != SHARE_DUPE)
				setDupe(PARTIAL_SHARE_DUPE);
			else if(getDupe() == QUEUE_DUPE && !(*i)->isQueued())
				setDupe(PARTIAL_QUEUE_DUPE);

			//if we find different type of dupe, change to mixed
			else if((getDupe() == SHARE_DUPE || getDupe() == PARTIAL_SHARE_DUPE) && (*i)->isQueued())
				setDupe(SHARE_QUEUE_DUPE);
			else if((getDupe() == QUEUE_DUPE || getDupe() == PARTIAL_QUEUE_DUPE) && (*i)->getDupe() == SHARE_DUPE)
				setDupe(SHARE_QUEUE_DUPE);

			first = false;
		}
	}
	return getDupe();
}

void DirectoryListing::checkShareDupes() {
	root->checkShareDupes();
	root->setDupe(DUPE_NONE); //never show the root as a dupe or partial dupe.
}

void DirectoryListing::addMatchADLTask() {
	tasks.add(MATCH_ADL, nullptr);
	runTasks();
}

struct ListDiffTask : public Task {
	ListDiffTask(const string& aName, bool aOwnList) : name(aName), 
		ownList(aOwnList) { }

	string name;
	bool ownList;
};

void DirectoryListing::addListDiffTask(const string& aFile, bool aOwnList) {
	tasks.add(LISTDIFF, unique_ptr<Task>(new ListDiffTask(aFile, aOwnList)));
	runTasks();
}

struct PartialLoadingTask : public Task {
	PartialLoadingTask(const string& aXml, std::function<void ()> aF) : f(aF), xml(aXml) { }

	string xml;
	std::function<void ()> f;
};

void DirectoryListing::addPartialListTask(const string& aXmlDir, std::function<void ()> f) {
	tasks.add(REFRESH_DIR, unique_ptr<Task>(new PartialLoadingTask(aXmlDir, f)));
	runTasks();
}

void DirectoryListing::addFullListTask(const string& aDir) {
	tasks.add(LOAD_FILE, unique_ptr<Task>(new StringTask(aDir)));
	runTasks();
}

void DirectoryListing::addQueueMatchTask() {
	tasks.add(MATCH_QUEUE, nullptr);
	runTasks();
}

void DirectoryListing::close() {
	tasks.add(CLOSE, nullptr);
	runTasks();
}

struct SearchTask : public Task {
	SearchTask(const string& aSearchString, int64_t aSize, int aTypeMode, int aSizeMode, const StringList& aExtList, const string& aDir) : searchString(aSearchString), 
		size(aSize), typeMode(aTypeMode), sizeMode(aSizeMode), extList(aExtList), directory(aDir) { }

	string searchString;
	int64_t size;
	int typeMode;
	int sizeMode;
	StringList extList;
	string directory;
};

void DirectoryListing::addSearchTask(const string& aSearchString, int64_t aSize, int aTypeMode, int aSizeMode, const StringList& aExtList, const string& aDir) {
	tasks.add(SEARCH, unique_ptr<Task>(new SearchTask(aSearchString, aSize, aTypeMode, aSizeMode, aExtList, aDir)));
	runTasks();
}

struct DirDownloadTask : public Task {
	DirDownloadTask(DirectoryListing::Directory* aDir, const string& aTarget, TargetUtil::TargetType aTargetType, bool aIsSizeUnknown, QueueItem::Priority aPrio) : dir(aDir), 
		target(aTarget), targetType(aTargetType), isSizeUnknown(aIsSizeUnknown), prio(aPrio) { }

	DirectoryListing::Directory* dir;
	QueueItem::Priority prio;
	bool isSizeUnknown;
	TargetUtil::TargetType targetType;
	string target;
};

void DirectoryListing::addDirDownloadTask(Directory* aDir, const string& aTarget, TargetUtil::TargetType aTargetType, bool isSizeUnknown, QueueItem::Priority prio) {
	tasks.add(DIR_DOWNLOAD, unique_ptr<Task>(new DirDownloadTask(aDir, aTarget, aTargetType, isSizeUnknown, prio)));
	runTasks();
}

void DirectoryListing::addFilterTask() {
	if (tasks.addUnique(FILTER, nullptr))
		runTasks();
	else
		typingFilter = true;
}

void DirectoryListing::runTasks() {
	if (!running.test_and_set()) {
		join();
		try {
			start();
			setThreadPriority(Thread::NORMAL);
		} catch(const ThreadException& /*e*/) {
			LogManager::getInstance()->message("DirListThread error", LogManager::LOG_WARNING);
			running.clear();
		}
	}
}

int DirectoryListing::run() {
	for (;;) {
		TaskQueue::TaskPair t;
		if (!tasks.getFront(t))
			break;

		ScopedFunctor([this] { tasks.pop_front(); });

		try {
			int64_t start = GET_TICK();
			
			if (t.first == LISTDIFF) {
				auto ldt = static_cast<ListDiffTask*>(t.second);
				DirectoryListing dirList(hintedUser, partialList, ldt->name, false, ldt->ownList);
				if (ldt->ownList) {
					auto mis = ShareManager::getInstance()->generatePartialList("/", true, Util::toInt(fileName));
					if (mis) {
						dirList.loadXML(*mis, true);
					} else {
						throw CSTRING(FILE_NOT_AVAILABLE);
					}
				} else {
					dirList.loadFile(ldt->name);
				}

				root->filterList(dirList);
				fire(DirectoryListingListener::LoadingFinished(), start, Util::emptyString, false, true);
			} else if(t.first == MATCH_ADL) {
				root->clearAdls(); //not much to check even if its the first time loaded without adls...
				ADLSearchManager::getInstance()->matchListing(*this);
				fire(DirectoryListingListener::LoadingFinished(), start, Util::emptyString, false, true);
			} else if(t.first == FILTER) {
				for(;;) {
					typingFilter = false;
					sleep(500);
					if (!typingFilter)
						break;
				}

				fire(DirectoryListingListener::Filter());
			}else if(t.first == LOAD_FILE) {
				partialList = false;

				fire(DirectoryListingListener::LoadingStarted());
				if (reloading) {
					root->clearAll();
					baseDirs.clear();
				}

				if (isOwnList) {
					auto mis = ShareManager::getInstance()->generatePartialList("/", true, Util::toInt(fileName));
					if (mis)
						loadXML(*mis, true);
					else
						throw CSTRING(FILE_NOT_AVAILABLE);
				} else {
					loadFile(fileName);
				}
				
				if(matchADL) {
					fire(DirectoryListingListener::UpdateStatusMessage(), CSTRING(MATCHING_ADL));
					ADLSearchManager::getInstance()->matchListing(*this);
				}

				fire(DirectoryListingListener::LoadingFinished(), start, static_cast<StringTask*>(t.second)->str, reloading, true);
				reloading = false;
			} else if (t.first == REFRESH_DIR) {
				if (!partialList)
					return 0;

				if (reloading) {
					baseDirs.clear();
					root->clearAll();
					root->setComplete(false);
				}

				auto lt = static_cast<PartialLoadingTask*>(t.second);
				
				string path;
				if (isOwnList) {
					auto mis = ShareManager::getInstance()->generatePartialList(Util::toAdcFile(lt->xml), false, Util::toInt(fileName));
					if (mis) {
						path = loadXML(*mis, true);
					} else {
						throw CSTRING(FILE_NOT_AVAILABLE);
					}
				} else {
					path = updateXML(lt->xml);
				}

				fire(DirectoryListingListener::LoadingFinished(), start, Util::toNmdcFile(path), reloading, lt->f == nullptr);
				reloading = false;
				if (lt->f) {
					lt->f();
				}
			} else if (t.first == CLOSE) {
				//delete this;
				fire(DirectoryListingListener::Close());
				return 0;
			} else if (t.first == MATCH_QUEUE) {
				int matches=0, newFiles=0;
				BundleList bundles;
				QueueManager::getInstance()->matchListing(*this, matches, newFiles, bundles);
				fire(DirectoryListingListener::QueueMatched(), AirUtil::formatMatchResults(matches, newFiles, bundles, false));
			} else if (t.first == DIR_DOWNLOAD) {
				auto dli = static_cast<DirDownloadTask*>(t.second);
				downloadDir(dli->dir, dli->target, dli->targetType, dli->isSizeUnknown , dli->prio);
			} else if (t.first == SEARCH) {
				secondsEllapsed = 0;
				searchResults.clear();
				if (curSearch)
					delete curSearch;

				auto s = static_cast<SearchTask*>(t.second);
				fire(DirectoryListingListener::SearchStarted());

				if(s->typeMode == SearchManager::TYPE_TTH) {
					curSearch = new AdcSearch(TTHValue(s->searchString));
				} else {
					curSearch = new AdcSearch(s->searchString, s->extList);
					if(s->sizeMode == SearchManager::SIZE_ATLEAST) {
						curSearch->gt = s->size;
					} else if(s->sizeMode == SearchManager::SIZE_ATMOST) {
						curSearch->lt = s->size;
					}

					curSearch->isDirectory = (s->typeMode == SearchManager::TYPE_DIRECTORY);
				}

				if (isOwnList && partialList) {
					try {
						ShareManager::getInstance()->directSearch(searchResults, *curSearch, 50, Util::toInt(fileName), s->directory);
					} catch (...) { }
					endSearch(false);
				} else if (partialList) {
					SearchManager::getInstance()->addListener(this);
					TimerManager::getInstance()->addListener(this);

					searchToken = Util::toString(Util::rand());
					ClientManager::getInstance()->directSearch(hintedUser, s->sizeMode, s->size, s->typeMode, s->searchString, searchToken, s->extList, s->directory);
					//...
				} else {
					const auto dir = (s->directory.empty()) ? root : findDirectory(s->directory, root);
					if (dir)
						root->search(searchResults, *curSearch, 100);
					endSearch(false);
				}
			}
		} catch(const AbortException) {
			fire(DirectoryListingListener::LoadingFailed(), Util::emptyString);
			break;
		} catch(const ShareException& e) {
			fire(DirectoryListingListener::LoadingFailed(), e.getError());
		} catch(const Exception& e) {
			fire(DirectoryListingListener::LoadingFailed(), ClientManager::getInstance()->getNicks(getUser()->getCID(), hintedUser.hint)[0] + ": " + e.getError());
		}
	}

	running.clear();
	return 0;
}

void DirectoryListing::on(SearchManagerListener::DSR, const DirectSearchResultPtr& aDSR) noexcept {
	if (compare(aDSR->getToken(), searchToken) == 0)
		searchResults.push_back(aDSR);
}

void DirectoryListing::on(TimerManagerListener::Second, uint64_t /*aTick*/) noexcept {
	secondsEllapsed++;
	if (secondsEllapsed == 5) {
		endSearch(true);
	}
}

void DirectoryListing::on(SearchManagerListener::DirectSearchEnd, const string& aToken) noexcept {
	if (compare(aToken, searchToken) == 0)
		endSearch(false);
}

void DirectoryListing::endSearch(bool timedOut /*false*/) {
	SearchManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);

	if (searchResults.empty()) {
		curSearch = nullptr;
		fire(DirectoryListingListener::SearchFailed(), timedOut);
	} else {
		curResult = searchResults.begin();
		changeDir();
	}
}

void DirectoryListing::changeDir(bool reload) {
	auto path = Util::toNmdcFile((*curResult)->getPath());
	if (!partialList) {
		fire(DirectoryListingListener::ChangeDirectory(), path, true);
	} else {
		const auto dir = (path == Util::emptyString) ? root : findDirectory(path, root);
		if (dir && dir->getComplete() && !reload) {
			fire(DirectoryListingListener::ChangeDirectory(), path, true);
		} else if (isOwnList) {
			auto mis = ShareManager::getInstance()->generatePartialList(Util::toAdcFile(path), false, Util::toInt(fileName));
			if (mis) {
				loadXML(*mis, true);
				fire(DirectoryListingListener::LoadingFinished(), 0, path, false, true);
			} else {
				//might happen if have refreshed the share meanwhile
				fire(DirectoryListingListener::LoadingFailed(), CSTRING(FILE_NOT_AVAILABLE));
			}
		} else {
			try {
				QueueManager::getInstance()->addList(hintedUser, QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_CLIENT_VIEW, path);
			} catch (...) { }
		}
	}
}

bool DirectoryListing::nextResult(bool prev) {
	if (prev) {
		if (curResult == searchResults.begin()) {
			return false;
		}
		advance(curResult, -1);
	} else {
		if (curResult == searchResults.end()-1) {
			return false;
		}
		advance(curResult, 1);
	}

	changeDir();
	return true;
}

bool DirectoryListing::isCurrentSearchPath(const string& path) {
	if (searchResults.empty())
		return false;

	return (*curResult)->getPath() == Util::toAdcFile(path);
}

} // namespace dcpp
