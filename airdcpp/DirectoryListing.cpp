/* 
 * Copyright (C) 2001-2016 Jacek Sieka, arnetheduck on gmail point com
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

#include "DirectoryListing.h"

#include "ADLSearch.h"
#include "AirUtil.h"
#include "AutoSearchManager.h"
#include "Bundle.h"
#include "BZUtils.h"
#include "ClientManager.h"
#include "Download.h"
#include "DownloadManager.h"
#include "FilteredFile.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "ShareManager.h"
#include "SimpleXML.h"
#include "SimpleXMLReader.h"
#include "StringTokenizer.h"
#include "User.h"
#include "ViewFileManager.h"


namespace dcpp {

using boost::range::for_each;
using boost::range::find_if;

DirectoryListing::DirectoryListing(const HintedUser& aUser, bool aPartial, const string& aFileName, bool aIsClientView, bool aIsOwnList) : TrackableDownloadItem(aIsOwnList || (!aPartial && !aFileName.empty())),
	hintedUser(aUser), root(new Directory(nullptr, Util::emptyString, Directory::TYPE_INCOMPLETE_NOCHILD, 0)), partialList(aPartial), isOwnList(aIsOwnList), fileName(aFileName),
	isClientView(aIsClientView), matchADL(SETTING(USE_ADLS) && !aPartial), 
	tasks(isClientView, Thread::NORMAL, std::bind(&DirectoryListing::dispatch, this, std::placeholders::_1))
{
	running.clear();

	ClientManager::getInstance()->addListener(this);
	if (isOwnList) {
		ShareManager::getInstance()->addListener(this);
	}
}

DirectoryListing::~DirectoryListing() {
	dcdebug("Filelist deleted\n");
	ClientManager::getInstance()->removeListener(this);
	ShareManager::getInstance()->removeListener(this);

	TimerManager::getInstance()->removeListener(this);
}

bool DirectoryListing::isMyCID() const noexcept {
	return hintedUser.user == ClientManager::getInstance()->getMe();
}


bool DirectoryListing::Directory::Sort::operator()(const Ptr& a, const Ptr& b) const {
	return compare(a->getName(), b->getName()) < 0;
}

bool DirectoryListing::File::Sort::operator()(const Ptr& a, const Ptr& b) const {
	return compare(a->getName(), b->getName()) < 0;
}

string DirectoryListing::getNick(bool aFirstOnly) const noexcept {
	string ret;
	if (!hintedUser.user->isOnline()) {
		if (isOwnList) {
			ret = SETTING(NICK);
		} else if (!partialList) {
			ret = DirectoryListing::getNickFromFilename(fileName);
		}
	}

	if (ret.empty()) {
		if (aFirstOnly) {
			ret = ClientManager::getInstance()->getNick(hintedUser.user, hintedUser.hint, true);
		} else {
			ret = ClientManager::getInstance()->getFormatedNicks(hintedUser);
		}
	}

	return ret;
}

void stripExtensions(string& aName) noexcept {
	if(Util::stricmp(aName.c_str() + aName.length() - 4, ".bz2") == 0) {
		aName.erase(aName.length() - 4);
	}

	if(Util::stricmp(aName.c_str() + aName.length() - 4, ".xml") == 0) {
		aName.erase(aName.length() - 4);
	}
}

ProfileToken DirectoryListing::getShareProfile() const noexcept {
	return Util::toInt(fileName);
}

void DirectoryListing::addHubUrlChangeTask(const string& aHubUrl) noexcept {
	addAsyncTask([=] {
		setHubUrl(aHubUrl);
	});
}

void DirectoryListing::addShareProfileChangeTask(ProfileToken aProfile) noexcept {
	addAsyncTask([=] {
		setShareProfile(aProfile);
	});
}

void DirectoryListing::setHubUrl(const string& aHubUrl) noexcept {
	if (aHubUrl == hintedUser.hint) {
		return;
	}

	hintedUser.hint = aHubUrl;
	fire(DirectoryListingListener::UserUpdated());
}

void DirectoryListing::setShareProfile(ProfileToken aProfile) noexcept {
	if (getShareProfile() == aProfile) {
		return;
	}

	setFileName(Util::toString(aProfile));
	if (partialList) {
		addDirectoryChangeTask(Util::emptyString, RELOAD_ALL);
	} else {
		addFullListTask(Util::emptyString);
	}

	SettingsManager::getInstance()->set(SettingsManager::LAST_LIST_PROFILE, aProfile);
	fire(DirectoryListingListener::ShareProfileChanged());
}

void DirectoryListing::getPartialListInfo(int64_t& totalSize_, size_t& totalFiles_) const noexcept {
	if (isOwnList) {
		ShareManager::getInstance()->getProfileInfo(getShareProfile(), totalSize_, totalFiles_);
	}

	auto si = ClientManager::getInstance()->getShareInfo(hintedUser);
	totalSize_ = si.first;
	totalFiles_ = si.second;
}

string DirectoryListing::getNickFromFilename(const string& fileName) noexcept {
	// General file list name format: [username].[CID].[xml|xml.bz2]

	string name = Util::getFileName(fileName);

	// Strip off any extensions
	stripExtensions(name);

	// Find CID
	string::size_type i = name.rfind('.');
	if(i == string::npos) {
		return STRING(UNKNOWN);
	}

	return name.substr(0, i);
}

UserPtr DirectoryListing::getUserFromFilename(const string& fileName) noexcept {
	// General file list name format: [username].[CID].[xml|xml.bz2]

	string name = Util::getFileName(fileName);

	// Strip off any extensions
	stripExtensions(name);

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
	if(!cid)
		return UserPtr();

	return ClientManager::getInstance()->getUser(cid);
}

bool DirectoryListing::supportsASCH() const noexcept {
	return !partialList || isOwnList || hintedUser.user->isSet(User::ASCH);
}

void DirectoryListing::onStateChanged() noexcept {
	fire(DirectoryListingListener::StateChanged());
}

DirectoryListing::Directory::Ptr DirectoryListing::createBaseDirectory(const string& aBasePath, time_t aDownloadDate) noexcept {
	dcassert(Util::isAdcPath(aBasePath));
	auto cur = root;

	StringList sl = StringTokenizer<string>(aBasePath, '/').getTokens();
	for (const auto& curDirName : sl) {
		auto s = find(cur->directories, curDirName);
		if (s == cur->directories.end()) {
			auto d = make_shared<DirectoryListing::Directory>(cur.get(), curDirName, DirectoryListing::Directory::TYPE_INCOMPLETE_CHILD, aDownloadDate, true);
			cur->directories.push_back(d);
			baseDirs[Text::toLower(Util::toAdcFile(d->getPath()))] = { d, false };
			cur = d;
		} else {
			cur = *s;
		}
	}

	// Mark the directory as visited
	auto& p = baseDirs[Text::toLower(aBasePath)];
	p.second = true;

	return cur;
}

void DirectoryListing::loadFile() throw(Exception, AbortException) {
	if (isOwnList) {
		loadShareDirectory(Util::emptyString, true);
	} else {

		// For now, we detect type by ending...
		string ext = Util::getFileExt(fileName);

		dcpp::File ff(fileName, dcpp::File::READ, dcpp::File::OPEN, dcpp::File::BUFFER_AUTO);
		root->setLastUpdateDate(ff.getLastModified());
		if(Util::stricmp(ext, ".bz2") == 0) {
			FilteredInputStream<UnBZFilter, false> f(&ff);
			loadXML(f, false, "/", ff.getLastModified());
		} else if(Util::stricmp(ext, ".xml") == 0) {
			loadXML(ff, false, "/", ff.getLastModified());
		}
	}
}

class ListLoader : public SimpleXMLReader::CallBack {
public:
	ListLoader(DirectoryListing* aList, DirectoryListing::Directory* root, const string& aBase, bool aUpdating, const UserPtr& aUser, bool aCheckDupe, bool aPartialList, time_t aListDownloadDate) : 
	  list(aList), cur(root), base(aBase), inListing(false), updating(aUpdating), user(aUser), checkDupe(aCheckDupe), partialList(aPartialList), dirsLoaded(0), listDownloadDate(aListDownloadDate) {
	}

	virtual ~ListLoader() { }

	void startTag(const string& name, StringPairList& attribs, bool simple);
	void endTag(const string& name);

	//const string& getBase() const { return base; }
	int getLoadedDirs() { return dirsLoaded; }
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
	int dirsLoaded;
	time_t listDownloadDate;
};

int DirectoryListing::updateXML(const string& xml, const string& aBase) throw(AbortException) {
	MemoryInputStream mis(xml);
	return loadXML(mis, true, aBase);
}

int DirectoryListing::loadXML(InputStream& is, bool updating, const string& aBase, time_t aListDate) throw(AbortException) {
	ListLoader ll(this, root.get(), aBase, updating, getUser(), !isOwnList && isClientView && SETTING(DUPES_IN_FILELIST), partialList, aListDate);
	try {
		dcpp::SimpleXMLReader(&ll).parse(is);
	} catch(SimpleXMLException& e) {
		//Better to abort and show the error, than just leave it hanging.
		LogManager::getInstance()->message("Error in Filelist loading: "  + e.getError() + ". User: [ " +  
			getNick(false) + " ]", LogMessage::SEV_ERROR);
		//dcdebug("DirectoryListing loadxml error: %s", e.getError());
	}
	return ll.getLoadedDirs();
}

static const string sFileListing = "FileListing";
static const string sBase = "Base";
static const string sBaseDate = "BaseDate";
static const string sGenerator = "Generator";
static const string sDirectory = "Directory";
static const string sIncomplete = "Incomplete";
static const string sChildren = "Children";
static const string sFile = "File";
static const string sName = "Name";
static const string sSize = "Size";
static const string sTTH = "TTH";
static const string sDate = "Date";
void ListLoader::startTag(const string& name, StringPairList& attribs, bool simple) {
	if(list->getClosing()) {
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

			auto f = make_shared<DirectoryListing::File>(cur, n, size, tth, checkDupe, Util::toUInt32(getAttrib(attribs, sDate, 3)));
			cur->files.push_back(f);
		} else if(name == sDirectory) {
			const string& n = getAttrib(attribs, sName, 0);
			if(n.empty()) {
				throw SimpleXMLException("Directory missing name attribute");
			}

			bool incomp = getAttrib(attribs, sIncomplete, 1) == "1";
			bool children = getAttrib(attribs, sChildren, 2) == "1";

			const string& size = getAttrib(attribs, sSize, 2);
			const string& date = getAttrib(attribs, sDate, 3);

			DirectoryListing::Directory::Ptr d = nullptr;
			if(updating) {
				dirsLoaded++;
				auto s =  list->baseDirs.find(baseLower + Text::toLower(n) + '/');
				if (s != list->baseDirs.end()) {
					d = s->second.first;
				}
			}

			if(!d) {
				d = make_shared<DirectoryListing::Directory>(cur, n, incomp ? (children ? DirectoryListing::Directory::TYPE_INCOMPLETE_CHILD : DirectoryListing::Directory::TYPE_INCOMPLETE_NOCHILD) : 
					DirectoryListing::Directory::TYPE_NORMAL, listDownloadDate, (partialList && checkDupe), size, Util::toUInt32(date));
				cur->directories.push_back(d);
				if (updating && !incomp) {
					list->baseDirs[baseLower + Text::toLower(n) + '/'] = { d, true }; //recursive partial lists
				}
			} else {
				if(!incomp) {
					d->setComplete();
				}
				d->setRemoteDate(Util::toUInt32(date));
			}
			cur = d.get();
			if (updating && cur->isComplete())
				baseLower += Text::toLower(n) + '/';

			if(simple) {
				// To handle <Directory Name="..." />
				endTag(name);
			}
		}
	} else if(name == sFileListing) {
		if (updating) {
			const string& b = getAttrib(attribs, sBase, 2);

			// Validate the parsed base path
			{
				if (Util::stricmp(b, base) != 0) {
					throw AbortException("The base directory specified in the file list (" + b + ") doesn't match with the expected base (" + base + ")");
				}

				base = b;
			}

			cur = list->createBaseDirectory(base, listDownloadDate).get();

			dcassert(list->findDirectory(Util::toNmdcFile(base)));

			const string& baseDate = getAttrib(attribs, sBaseDate, 3);
			cur->setRemoteDate(Util::toUInt32(baseDate));

			baseLower = Text::toLower(base);
		}

		// Set the root complete only after we have finished loading 
		// This will prevent possible problems, such as GUI counting the size of this folder

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
			if (updating && cur->isComplete())
				baseLower = baseLower.substr(0, baseLower.length()-cur->getName().length()-1);
			cur = cur->getParent();
		} else if(name == sFileListing) {
			// cur should be root now, set it complete
			cur->setComplete();
			inListing = false;
		}
	}
}

DirectoryListing::File::File(Directory* aDir, const string& aName, int64_t aSize, const TTHValue& aTTH, bool checkDupe, time_t aRemoteDate) noexcept : 
	name(aName), size(aSize), parent(aDir), tthRoot(aTTH), remoteDate(aRemoteDate) {

	if (checkDupe && size > 0) {
		dupe = AirUtil::checkFileDupe(tthRoot);
	}

	//dcdebug("DirectoryListing::File (copy) %s was created\n", aName.c_str());
}

DirectoryListing::File::File(const File& rhs, bool _adls) noexcept : name(rhs.name), size(rhs.size), parent(rhs.parent), tthRoot(rhs.tthRoot), adls(_adls), dupe(rhs.dupe), remoteDate(rhs.remoteDate)
{
	//dcdebug("DirectoryListing::File (copy) %s was created\n", rhs.getName().c_str());
}

DirectoryListing::Directory::Directory(Directory* aParent, const string& aName, Directory::DirType aType, time_t aUpdateDate, bool checkDupe, const string& aSize, time_t aRemoteDate /*0*/)
	: name(aName), parent(aParent), type(aType), remoteDate(aRemoteDate), lastUpdateDate(aUpdateDate) {

	if (!aSize.empty()) {
		partialSize = Util::toInt64(aSize);
	}

	if (checkDupe) {
		dupe = AirUtil::checkDirDupe(getPath(), partialSize);
	}

	//dcdebug("DirectoryListing::Directory %s was created\n", aName.c_str());
}

void DirectoryListing::Directory::search(OrderedStringSet& aResults, SearchQuery& aStrings) const noexcept {
	if (getAdls())
		return;

	if(aStrings.matchesDirectory(name)) {
		auto path = parent ? parent->getPath() : Util::emptyString;
		auto res = find(aResults, path);
		if (res == aResults.end() && aStrings.matchesSize(getTotalSize(false))) {
			aResults.insert(path);
		}
	}

	for(auto& f: files) {
		if(aStrings.matchesFile(f->getName(), f->getSize(), f->getRemoteDate(), f->getTTH())) {
			aResults.insert(getPath());
			break;
		}
	}

	for(const auto& d: directories) {
		d->search(aResults, aStrings);
		if (aResults.size() >= aStrings.maxResults) return;
	}
}

bool DirectoryListing::Directory::findIncomplete() const noexcept {
	/* Recursive check for incomplete dirs */
	if(!isComplete()) {
		return true;
	}
	return find_if(directories, [](const Directory::Ptr& dir) { return dir->findIncomplete(); }) != directories.end();
}

void DirectoryListing::Directory::download(const string& aTarget, BundleFileInfo::List& aFiles) noexcept{
	// First, recurse over the directories
	sort(directories.begin(), directories.end(), Directory::Sort());
	for(auto& d: directories) {
		d->download(aTarget + d->getName() + PATH_SEPARATOR, aFiles);
	}

	// Then add the files
	sort(files.begin(), files.end(), File::Sort());
	for(const auto& f: files) {
		aFiles.emplace_back(aTarget + f->getName(), f->getTTH(), f->getSize());
	}
}

bool DirectoryListing::createBundle(Directory::Ptr& aDir, const string& aTarget, QueueItemBase::Priority prio, ProfileToken aAutoSearch) noexcept {
	BundleFileInfo::List aFiles;
	aDir->download(Util::emptyString, aFiles);

	if (aFiles.empty() || (SETTING(SKIP_ZERO_BYTE) && none_of(aFiles.begin(), aFiles.end(), [](const BundleFileInfo& aFile) { return aFile.size > 0; }))) {
		fire(DirectoryListingListener::UpdateStatusMessage(), STRING(DIR_EMPTY) + " " + aDir->getName());
		return false;
	}

	string errorMsg;
	BundlePtr b = nullptr;
	try {
		b = QueueManager::getInstance()->createDirectoryBundle(aTarget, hintedUser.user == ClientManager::getInstance()->getMe() && !isOwnList ? HintedUser() : hintedUser,
			aFiles, prio, aDir->getRemoteDate(), errorMsg);
	} catch (const std::bad_alloc&) {
		LogManager::getInstance()->message(STRING_F(BUNDLE_CREATION_FAILED, aTarget % STRING(OUT_OF_MEMORY)), LogMessage::SEV_ERROR);
		return false;
	} catch (const Exception& e) {
		LogManager::getInstance()->message(STRING_F(BUNDLE_CREATION_FAILED, aTarget % e.getError()), LogMessage::SEV_ERROR);
		return false;
	}

	if (!errorMsg.empty()) {
		if (aAutoSearch == 0) {
			LogManager::getInstance()->message(STRING_F(ADD_BUNDLE_ERRORS_OCC, aTarget % getNick(false) % errorMsg), LogMessage::SEV_WARNING);
		} else {
			AutoSearchManager::getInstance()->onBundleError(aAutoSearch, errorMsg, aTarget, hintedUser);
		}
	}

	if (b) {
		if (aAutoSearch > 0) {
			AutoSearchManager::getInstance()->onBundleCreated(b, aAutoSearch);
		}
		return true;
	}

	return false;
}

bool DirectoryListing::downloadDirImpl(Directory::Ptr& aDir, const string& aTarget, QueueItemBase::Priority prio, ProfileToken aAutoSearch) noexcept {
	dcassert(!aDir->findIncomplete());

	/* Check if this is a root dir containing release dirs */
	boost::regex reg;
	reg.assign(AirUtil::getReleaseRegBasic());
	if (!boost::regex_match(aDir->getName(), reg) && aDir->files.empty() && !aDir->directories.empty() &&
		all_of(aDir->directories.begin(), aDir->directories.end(), [&reg](const Directory::Ptr& d) { return boost::regex_match(d->getName(), reg); })) {
			
		/* Create bundles from each subfolder */
		bool queued = false;
		for(auto& d: aDir->directories) {
			if (createBundle(d, aTarget + d->getName() + PATH_SEPARATOR, prio, aAutoSearch))
				queued = true;
		}
		return queued;
	}

	return createBundle(aDir, aTarget, prio, aAutoSearch);
}

bool DirectoryListing::downloadDir(const string& aDir, const string& aTarget, QueueItemBase::Priority prio, ProfileToken aAutoSearch) noexcept {
	dcassert(aDir.size() > 2);
	dcassert(aDir[aDir.size() - 1] == '\\'); // This should not be PATH_SEPARATOR
	auto d = findDirectory(aDir, root);
	if(d)
		return downloadDirImpl(d, aTarget, prio, aAutoSearch);
	return false;
}

int64_t DirectoryListing::getDirSize(const string& aDir) const noexcept {
	dcassert(aDir.size() > 2);
	dcassert(aDir.empty() || aDir[aDir.size() - 1] == '\\'); // This should not be PATH_SEPARATOR
	auto d = findDirectory(aDir, root);
	if(d)
		return d->getTotalSize(false);
	return 0;
}

bool DirectoryListing::viewAsText(const File::Ptr& aFile) const noexcept {
	if (isOwnList) {
		StringList paths;
		getLocalPaths(aFile, paths);
		if (!paths.empty()) {
			return ViewFileManager::getInstance()->addLocalFile(paths.front(), aFile->getTTH(), true);
		}

		return false;
	}

	return ViewFileManager::getInstance()->addUserFileNotify(aFile->getName(), aFile->getSize(), aFile->getTTH(), hintedUser, true);
}

DirectoryListing::Directory::Ptr DirectoryListing::findDirectory(const string& aName, const Directory::Ptr& current) const noexcept {
	if (aName.empty())
		return root;

	string::size_type end = aName.find('\\');
	dcassert(end != string::npos);
	string name = aName.substr(0, end);

	auto i = find(current->directories, name);
	if(i != current->directories.end()) {
		if(end == (aName.size() - 1))
			return *i;
		else
			return findDirectory(aName.substr(end + 1), *i);
	}

	return nullptr;
}

void DirectoryListing::Directory::findFiles(const boost::regex& aReg, File::List& aResults) const noexcept {
	copy_if(files.begin(), files.end(), back_inserter(aResults), [&aReg](const File::Ptr& df) { return boost::regex_match(df->getName(), aReg); });

	for(const auto& d: directories)
		d->findFiles(aReg, aResults); 
}

void DirectoryListing::findNfoImpl(const string& aPath, bool aAllowQueueList, DupeOpenF aDupeF) noexcept {
	auto dir = findDirectory(aPath, root);
	if (getIsOwnList()) {
		if (!aDupeF) {
			return;
		}

		try {
			SearchResultList results;
			auto query = SearchQuery(Util::emptyString, StringList(), { ".nfo" }, Search::MATCH_NAME_PARTIAL);
			query.maxResults = 1;

			ShareManager::getInstance()->adcSearch(results, query, getShareProfile(), ClientManager::getInstance()->getMyCID(), Util::toAdcFile(aPath));

			if (!results.empty()) {
				auto paths = AirUtil::getFileDupePaths(DUPE_SHARE_FULL, results.front()->getTTH());
				if (!paths.empty()) {
					aDupeF(paths.front());
				}

				return;
			}
		} catch (...) {
		
		}
	} else if ((!dir || !dir->isComplete() || dir->findIncomplete())) {
		if (!aAllowQueueList) {
			// Don't try to queue the same list over and over again if it's malformed
			return;
		}

		try {
			QueueManager::getInstance()->addList(hintedUser, QueueItem::FLAG_VIEW_NFO | QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_RECURSIVE_LIST, dir->getPath());
		}
		catch (const Exception&) {

		}

		return;
	} else {
		boost::regex reg;
		reg.assign("(.+\\.nfo)", boost::regex_constants::icase);
		File::List results;
		dir->findFiles(reg, results);

		if (!results.empty()) {
			try {
				viewAsText(results.front());
			} catch (const Exception&) {
			
			}
			return;
		}
	}

	LogManager::getInstance()->message(dir->getName() + ": " + STRING(NO_NFO_FOUND), LogMessage::SEV_NOTIFY);
}

struct HashContained {
	HashContained(const DirectoryListing::Directory::TTHSet& l) : tl(l) { }
	const DirectoryListing::Directory::TTHSet& tl;
	bool operator()(const DirectoryListing::File::Ptr& i) const {
		return tl.count(i->getTTH()) > 0;
	}
};

struct DirectoryEmpty {
	bool operator()(const DirectoryListing::Directory::Ptr& aDir) const {
		bool r = aDir->getFileCount() + aDir->directories.size() == 0;
		return r;
	}
};

struct SizeLess {
	bool operator()(const DirectoryListing::File::Ptr& f) const {
		return f->getSize() < Util::convertSize(SETTING(SKIP_SUBTRACT), Util::KB);
	}
};

DirectoryListing::Directory::~Directory() {
	//dcdebug("DirectoryListing::Directory %s deleted\n", name.c_str());
}

void DirectoryListing::Directory::clearAll() noexcept {
	//for_each(files, DeleteFunction());
	directories.clear();
	files.clear();
}

void DirectoryListing::Directory::filterList(DirectoryListing& dirList) noexcept {
	auto d = dirList.getRoot();

	TTHSet l;
	d->getHashList(l);
	filterList(l);
}

void DirectoryListing::Directory::filterList(DirectoryListing::Directory::TTHSet& l) noexcept {
	for(auto& d: directories) 
		d->filterList(l);

	directories.erase(remove_if(directories.begin(), directories.end(), DirectoryEmpty()), directories.end());
	files.erase(remove_if(files.begin(), files.end(), HashContained(l)), files.end());

	if((SETTING(SKIP_SUBTRACT) > 0) && (files.size() < 2)) {   //setting for only skip if folder filecount under x ?
		files.erase(remove_if(files.begin(), files.end(), SizeLess()), files.end());
	}
}

void DirectoryListing::Directory::getHashList(DirectoryListing::Directory::TTHSet& l) const noexcept {
	for(const auto& d: directories)  
		d->getHashList(l);

	for(const auto& f: files) 
		l.insert(f->getTTH());
}
	
void DirectoryListing::getLocalPaths(const File::Ptr& f, StringList& ret) const throw(ShareException) {
	if(f->getParent()->getAdls() && (f->getParent()->getParent() == root.get() || !isOwnList))
		return;

	if (isOwnList) {
		string path;
		if (f->getParent()->getAdls())
			path = ((AdlDirectory*) f->getParent())->getFullPath();
		else
			path = f->getParent()->getPath();

		ShareManager::getInstance()->getRealPaths(Util::toAdcFile(path + f->getName()), ret, getShareProfile());
	} else {
		ret = AirUtil::getFileDupePaths(f->getDupe(), f->getTTH());
	}
}

void DirectoryListing::getLocalPaths(const Directory::Ptr& d, StringList& ret) const throw(ShareException) {
	if(d->getAdls() && (d->getParent() == root.get() || !isOwnList))
		return;

	string path;
	if (d->getAdls())
		path = ((AdlDirectory*) d.get())->getFullPath();
	else
		path = d->getPath();

	if (isOwnList) {
		ShareManager::getInstance()->getRealPaths(Util::toAdcFile(path), ret, getShareProfile());
	} else {
		ret = ShareManager::getInstance()->getNmdcDirPaths(path);
	}
}

int64_t DirectoryListing::Directory::getTotalSize(bool countAdls) const noexcept {
	if(!isComplete())
		return partialSize;
	if(!countAdls && getAdls())
		return 0;
	
	auto x = getFilesSize();
	for(const auto& d: directories) {
		if(!countAdls && d->getAdls())
			continue;
		x += d->getTotalSize(getAdls());
	}
	return x;
}

size_t DirectoryListing::Directory::getTotalFileCount(bool countAdls) const noexcept {
	if(!countAdls && getAdls())
		return 0;

	auto x = getFileCount();
	for(const auto& d: directories) {
		if(!countAdls && d->getAdls())
			continue;
		x += d->getTotalFileCount(getAdls());
	}
	return x;
}

void DirectoryListing::Directory::clearAdls() noexcept {
	for(auto i = directories.begin(); i != directories.end();) {
		if((*i)->getAdls()) {
			i = directories.erase(i);
		} else {
			++i;
		}
	}
}

string DirectoryListing::Directory::getPath() const noexcept {
	//make sure to not try and get the name of the root dir
	if(parent) {
		return parent->getPath() + name + '\\';
	}

	// root
	return Util::emptyString;
}

void DirectoryListing::setActive() noexcept {
	fire(DirectoryListingListener::SetActive());
}

int64_t DirectoryListing::Directory::getFilesSize() const noexcept {
	int64_t x = 0;
	for(const auto& f: files) {
		x += f->getSize();
	}
	return x;
}

bool DirectoryListing::File::isInQueue() const noexcept {
	return AirUtil::isQueueDupe(dupe) || AirUtil::isFinishedDupe(dupe);
}

uint8_t DirectoryListing::Directory::checkShareDupes() noexcept {
	uint8_t result = DUPE_NONE;
	bool first = true;
	for(auto& d: directories) {
		result = d->checkShareDupes();
		if(dupe == DUPE_NONE && first)
			setDupe((DupeType)result);

		//full dupe with same type for non-dupe dir, change to partial (or pass partial dupes to upper level folder)
		else if (result == DUPE_SHARE_FULL && dupe == DUPE_NONE && !first)
			setDupe(DUPE_SHARE_PARTIAL);
		else if(result == DUPE_SHARE_PARTIAL && (dupe == DUPE_NONE || dupe == DUPE_SHARE_FULL) && !first)
			setDupe(DUPE_SHARE_PARTIAL);
		else if (result == DUPE_QUEUE_FULL && dupe == DUPE_NONE && !first)
			setDupe(DUPE_QUEUE_PARTIAL);
		else if( result == DUPE_QUEUE_PARTIAL && (dupe == DUPE_NONE || dupe == DUPE_QUEUE_FULL) && !first)
			setDupe(DUPE_QUEUE_PARTIAL);

		//change to mixed dupe type
		else if((dupe == DUPE_SHARE_FULL || dupe == DUPE_SHARE_PARTIAL) && (result == DUPE_QUEUE_FULL || result == DUPE_QUEUE_PARTIAL))
			setDupe(DUPE_SHARE_QUEUE);
		else if ((dupe == DUPE_QUEUE_FULL || dupe == DUPE_QUEUE_PARTIAL) && (result == DUPE_SHARE_FULL || result == DUPE_SHARE_PARTIAL))
			setDupe(DUPE_SHARE_QUEUE);

		else if (result == DUPE_SHARE_QUEUE)
			setDupe(DUPE_SHARE_QUEUE);

		first = false;
	}

	first = true;
	for(auto& f: files) {
		//don't count 0 byte files since it'll give lots of partial dupes
		//of no interest
		if(f->getSize() > 0) {			
			//if it's the first file in the dir and no sub-folders exist mark it as a dupe.
			if (dupe == DUPE_NONE && f->getDupe() == DUPE_SHARE_FULL && directories.empty() && first)
				setDupe(DUPE_SHARE_FULL);
			else if (dupe == DUPE_NONE && f->isInQueue() && directories.empty() && first)
				setDupe(DUPE_QUEUE_FULL);

			//if it's the first file in the dir and we do have sub-folders but no dupes, mark as partial.
			else if (dupe == DUPE_NONE && f->getDupe() == DUPE_SHARE_FULL && !directories.empty() && first)
				setDupe(DUPE_SHARE_PARTIAL);
			else if (dupe == DUPE_NONE && f->isInQueue() && !directories.empty() && first)
				setDupe(DUPE_QUEUE_PARTIAL);
			
			//if it's not the first file in the dir and we still don't have a dupe, mark it as partial.
			else if (dupe == DUPE_NONE && f->getDupe() == DUPE_SHARE_FULL && !first)
				setDupe(DUPE_SHARE_PARTIAL);
			else if (dupe == DUPE_NONE && f->isInQueue() && !first)
				setDupe(DUPE_QUEUE_PARTIAL);
			
			//if it's a dupe and we find a non-dupe, mark as partial.
			else if (dupe == DUPE_SHARE_FULL && f->getDupe() != DUPE_SHARE_FULL)
				setDupe(DUPE_SHARE_PARTIAL);
			else if (dupe == DUPE_QUEUE_FULL && !f->isInQueue())
				setDupe(DUPE_QUEUE_PARTIAL);

			//if we find different type of dupe, change to mixed
			else if (AirUtil::isShareDupe(dupe) && f->isInQueue())
				setDupe(DUPE_SHARE_QUEUE);
			else if (AirUtil::isQueueDupe(dupe) && f->getDupe() == DUPE_SHARE_FULL)
				setDupe(DUPE_SHARE_QUEUE);

			first = false;
		}
	}
	return dupe;
}

void DirectoryListing::checkShareDupes() noexcept {
	root->checkShareDupes();
	root->setDupe(DUPE_NONE); //never show the root as a dupe or partial dupe.
}

void DirectoryListing::addViewNfoTask(const string& aPath, bool aAllowQueueList, DupeOpenF aDupeF) noexcept {
	addAsyncTask([=] { findNfoImpl(aPath, aAllowQueueList, aDupeF); });
}

void DirectoryListing::addMatchADLTask() noexcept {
	addAsyncTask([=] { matchAdlImpl(); });
}

void DirectoryListing::addListDiffTask(const string& aFile, bool aOwnList) noexcept {
	addAsyncTask([=] { listDiffImpl(aFile, aOwnList); });
}

void DirectoryListing::addPartialListTask(const string& aXml, const string& aBase, bool reloadAll /*false*/, bool changeDir /*true*/, std::function<void()> f) noexcept {
	//onStateChanged();
	addAsyncTask([=] { loadPartialImpl(aXml, aBase, reloadAll, changeDir, f); });
}

void DirectoryListing::addFullListTask(const string& aDir) noexcept {
	//onStateChanged();
	addAsyncTask([=] { loadFileImpl(aDir); });
}

void DirectoryListing::addQueueMatchTask() noexcept {
	addAsyncTask([=] { matchQueueImpl(); });
}

void DirectoryListing::close() noexcept {
	closing = true;
	tasks.stop([=] {
		fire(DirectoryListingListener::Close());
	});
}

void DirectoryListing::addSearchTask(const SearchPtr& aSearch) noexcept {
	addAsyncTask([=] { searchImpl(aSearch); });
}

void DirectoryListing::addAsyncTask(DispatcherQueue::Callback&& f) noexcept {
	if (isClientView) {
		tasks.addTask(move(f));
	} else {
		dispatch(f);
	}
}

void DirectoryListing::dispatch(DispatcherQueue::Callback& aCallback) noexcept {
	try {
		aCallback();
	} catch (const std::bad_alloc&) {
		LogManager::getInstance()->message(STRING_F(LIST_LOAD_FAILED, ClientManager::getInstance()->getNick(hintedUser.user, hintedUser.hint) % STRING(OUT_OF_MEMORY)), LogMessage::SEV_ERROR);
		fire(DirectoryListingListener::LoadingFailed(), "Out of memory");
	} catch (const AbortException&) {
		fire(DirectoryListingListener::LoadingFailed(), Util::emptyString);
	} catch(const ShareException& e) {
		fire(DirectoryListingListener::LoadingFailed(), e.getError());
	} catch (const QueueException& e) {
		fire(DirectoryListingListener::UpdateStatusMessage(), "Queueing failed:" + e.getError());
	} catch (const Exception& e) {
		LogManager::getInstance()->message(STRING_F(LIST_LOAD_FAILED, ClientManager::getInstance()->getNick(hintedUser.user, hintedUser.hint) % e.getError()), LogMessage::SEV_ERROR);
		fire(DirectoryListingListener::LoadingFailed(), ClientManager::getInstance()->getNick(hintedUser.user, hintedUser.hint) + ": " + e.getError());
	}
}

void DirectoryListing::listDiffImpl(const string& aFile, bool aOwnList) throw(Exception, AbortException) {
	int64_t start = GET_TICK();
	if (isOwnList && partialList) {
		// we need the recursive list for this
		loadShareDirectory(Util::emptyString, true);
		partialList = false;
	}

	DirectoryListing dirList(hintedUser, false, aFile, false, aOwnList);
	dirList.loadFile();

	root->filterList(dirList);
	fire(DirectoryListingListener::LoadingFinished(), start, Util::emptyString, false, true);
}

void DirectoryListing::matchAdlImpl() throw(AbortException) {
	fire(DirectoryListingListener::LoadingStarted(), false);

	int64_t start = GET_TICK();
	root->clearAdls();

	if (isOwnList) {
		// No point in matching own partial list
		setMatchADL(true);
		loadFileImpl(Util::emptyString);
	} else {
		fire(DirectoryListingListener::UpdateStatusMessage(), CSTRING(MATCHING_ADL));
		ADLSearchManager::getInstance()->matchListing(*this);
		fire(DirectoryListingListener::LoadingFinished(), start, Util::emptyString, false, true);
	}
}

void DirectoryListing::loadFileImpl(const string& aInitialDir) throw(Exception, AbortException) {
	int64_t start = GET_TICK();
	partialList = false;

	fire(DirectoryListingListener::LoadingStarted(), false);
	bool reloading = !root->directories.empty();

	if (reloading) {
		root->clearAll();
		baseDirs.clear();
	}

	loadFile();

	if (matchADL) {
		fire(DirectoryListingListener::UpdateStatusMessage(), CSTRING(MATCHING_ADL));
		ADLSearchManager::getInstance()->matchListing(*this);
	}

	onLoadingFinished(start, aInitialDir, reloading, true);
}

void DirectoryListing::onLoadingFinished(int64_t aStartTime, const string& aDir, bool aReloadList, bool aChangeDir) noexcept {
	if (!getIsOwnList() && SETTING(DUPES_IN_FILELIST) && isClientView)
		checkShareDupes();

	auto dir = findDirectory(aDir);
	dcassert(dir);
	if (dir) {
		dir->setLoading(false);
		if (aChangeDir) {
			updateCurrentLocation(dir);
		}
		onStateChanged();
	}
	
	fire(DirectoryListingListener::LoadingFinished(), aStartTime, aDir, aReloadList, aChangeDir);
}

void DirectoryListing::updateCurrentLocation(const Directory::Ptr& aCurrentDirectory) noexcept {
	currentLocation.directories = aCurrentDirectory->directories.size();
	currentLocation.files = aCurrentDirectory->files.size();
	currentLocation.totalSize = aCurrentDirectory->getTotalSize(false);
	currentLocation.directory = aCurrentDirectory;
}

void DirectoryListing::searchImpl(const SearchPtr& aSearch) noexcept {
	searchResults.clear();

	fire(DirectoryListingListener::SearchStarted());

	curSearch.reset(SearchQuery::getSearch(aSearch));
	if (isOwnList && partialList) {
		SearchResultList results;
		try {
			ShareManager::getInstance()->adcSearch(results, *curSearch, getShareProfile(), CID(), aSearch->path);
		} catch (...) {}

		for (const auto& sr : results)
			searchResults.insert(sr->getPath());

		endSearch(false);
	} else if (partialList && !hintedUser.user->isNMDC()) {
		TimerManager::getInstance()->addListener(this);

		directSearch.reset(new DirectSearch(hintedUser, aSearch));
	} else {
		const auto dir = findDirectory(Util::toNmdcFile(aSearch->path), root);
		if (dir)
			dir->search(searchResults, *curSearch);

		endSearch(false);
	}
}

void DirectoryListing::loadPartialImpl(const string& aXml, const string& aBaseDir, bool aReloadAll, bool aChangeDir, std::function<void()> aCompletionF) throw(Exception, AbortException) {
	if (!partialList)
		return;

	auto baseDir = isOwnList && aReloadAll ? "/" : Util::toAdcFile(aBaseDir);

	bool reloading = aReloadAll;
	if (!reloading) {
		auto bd = baseDirs.find(Text::toLower(baseDir));
		if (bd != baseDirs.end()) {
			reloading = bd->second.second;
		}
	}

	if (reloading) {
		fire(DirectoryListingListener::LoadingStarted(), false);

		if (baseDir.empty() || aReloadAll) {
			baseDirs.clear();
			root->clearAll();
			if (baseDir.empty())
				root->setComplete();
			else
				root->setType(Directory::TYPE_INCOMPLETE_CHILD);
		} else {
			auto cur = findDirectory(Util::toNmdcFile(baseDir));
			if (cur && (!cur->directories.empty() || !cur->files.empty())) {
				//we have been here already, just reload all items
				cur->clearAll();

				//also clean the visited dirs
				for (auto i = baseDirs.begin(); i != baseDirs.end();) {
					if (AirUtil::isSubAdc(i->first, baseDir)) {
						i = baseDirs.erase(i);
					}
					else {
						i++;
					}
				}
			}
		}
	}

	if (!reloading) {
		fire(DirectoryListingListener::LoadingStarted(), true);
	}

	int dirsLoaded = 0;
	if (isOwnList) {
		dirsLoaded = loadShareDirectory(Util::toNmdcFile(baseDir));
	} else {
		dirsLoaded = updateXML(aXml, baseDir);
	}

	onLoadingFinished(0, Util::toNmdcFile(baseDir), aReloadAll || (reloading && baseDir == "/"), aChangeDir);

	if (aCompletionF) {
		aCompletionF();
	}
}

bool DirectoryListing::isLoaded() const noexcept {
	return currentLocation.directory && !currentLocation.directory->getLoading();
}

void DirectoryListing::matchQueueImpl() noexcept {
	int matches = 0, newFiles = 0;
	BundleList bundles;
	QueueManager::getInstance()->matchListing(*this, matches, newFiles, bundles);
	fire(DirectoryListingListener::QueueMatched(), AirUtil::formatMatchResults(matches, newFiles, bundles));
}

void DirectoryListing::on(ClientManagerListener::UserDisconnected, const UserPtr& aUser, bool /*wentOffline*/) noexcept {
	onUserUpdated(aUser);
}
void DirectoryListing::on(ClientManagerListener::UserUpdated, const OnlineUser& aUser) noexcept {
	onUserUpdated(aUser);
}

void DirectoryListing::on(ClientManagerListener::UserConnected, const OnlineUser& aUser, bool /*wasOffline*/) noexcept {
	onUserUpdated(aUser);
}

void DirectoryListing::onUserUpdated(const UserPtr& aUser) noexcept {
	if (aUser != hintedUser.user) {
		return;
	}

	fire(DirectoryListingListener::UserUpdated());
}

void DirectoryListing::on(TimerManagerListener::Second, uint64_t /*aTick*/) noexcept {
	if (directSearch && directSearch->finished()) {
		endSearch(directSearch->hasTimedOut());
	}
}

void DirectoryListing::endSearch(bool timedOut /*false*/) noexcept {
	if (directSearch) {
		directSearch->getPaths(searchResults, true);
		directSearch.reset(nullptr);
	}

	if (searchResults.size() == 0) {
		curSearch = nullptr;
		fire(DirectoryListingListener::SearchFailed(), timedOut);
	} else {
		curResult = searchResults.begin();
		addDirectoryChangeTask(*curResult, RELOAD_NONE, true);
	}
}

int DirectoryListing::loadShareDirectory(const string& aPath, bool aRecurse) throw(Exception, AbortException) {
	auto mis = ShareManager::getInstance()->generatePartialList(Util::toAdcFile(aPath), aRecurse, getShareProfile());
	if (mis) {
		return loadXML(*mis, true, Util::toAdcFile(aPath));
	}

	//might happen if have refreshed the share meanwhile
	throw Exception(CSTRING(FILE_NOT_AVAILABLE));
}

bool DirectoryListing::changeDirectory(const string& aPath, ReloadMode aReloadMode, bool aIsSearchChange) noexcept {
	Directory::Ptr dir;
	if (partialList) {
		// Directory may not exist when searching in partial lists 
		// or when opening directories from search (or via the API) for existing filelists
		dir = createBaseDirectory(Util::toAdcFile(aPath));
	} else {
		dir = findDirectory(aPath, root);
		if (!dir) {
			dcassert(0);
			return false;
		}
	}

	dcassert(findDirectory(aPath, root) != nullptr);

	clearLastError();
	updateCurrentLocation(dir);
	fire(DirectoryListingListener::ChangeDirectory(), aPath, aIsSearchChange);

	if (!partialList || dir->getLoading() || (dir->isComplete() && aReloadMode == RELOAD_NONE)) {
		// No need to load anything
	} else if (partialList) {
		if (isOwnList || getUser()->isOnline()) {
			dir->setLoading(true);

			try {
				if (isOwnList) {
					addPartialListTask(aPath, aPath, aReloadMode == RELOAD_ALL);
				} else {
					QueueManager::getInstance()->addList(hintedUser, QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_CLIENT_VIEW, aPath);
				}
			} catch (const Exception& e) {
				fire(DirectoryListingListener::LoadingFailed(), e.getError());
			}
		} else {
			fire(DirectoryListingListener::UpdateStatusMessage(), STRING(USER_OFFLINE));
		}
	} else {
		return false;
	}

	return true;
}

bool DirectoryListing::nextResult(bool prev) noexcept {
	if (prev) {
		if (curResult == searchResults.begin()) {
			return false;
		}
		advance(curResult, -1);
	} else {
		if (static_cast<size_t>(distance(searchResults.begin(), curResult)) == searchResults.size()-1) {
			return false;
		}
		advance(curResult, 1);
	}

	addDirectoryChangeTask(*curResult, RELOAD_NONE, true);
	return true;
}

void DirectoryListing::addDirectoryChangeTask(const string& aPath, ReloadMode aReloadMode, bool aIsSearchChange) noexcept {
	addAsyncTask([=] {
		changeDirectory(aPath, aReloadMode, aIsSearchChange);
	});
}

bool DirectoryListing::isCurrentSearchPath(const string& path) const noexcept {
	if (searchResults.empty())
		return false;

	return *curResult == path;
}

void DirectoryListing::setRead() noexcept {
	if (read) {
		return;
	}

	read = true;
	fire(DirectoryListingListener::Read());
}

void DirectoryListing::onListRemovedQueue(const string& aTarget, const string& aDir, bool aFinished) noexcept {
	if (!aFinished) {
		addAsyncTask([=] {
			auto dir = findDirectory(aDir);
			if (dir) {
				dir->setLoading(false);
				fire(DirectoryListingListener::RemovedQueue(), aDir);

				onStateChanged();
			}
		});
	}

	TrackableDownloadItem::onRemovedQueue(aTarget, aFinished);
}

void DirectoryListing::on(ShareManagerListener::DirectoriesRefreshed, uint8_t, const RefreshPathList& aPaths) noexcept{
	if (!partialList)
		return;

	// Reload all locations by virtual path
	string lastVirtual;
	for (const auto& p : aPaths) {
		auto vPath = ShareManager::getInstance()->realToVirtual(p, getShareProfile());
		if (!vPath.empty() && lastVirtual != vPath && findDirectory(vPath)) {
			addPartialListTask(Util::emptyString, vPath, false, false, nullptr);
			lastVirtual = vPath;
		}
	}
}

} // namespace dcpp
