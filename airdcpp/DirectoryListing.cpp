/* 
 * Copyright (C) 2001-2017 Jacek Sieka, arnetheduck on gmail point com
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
#include "BZUtils.h"
#include "ClientManager.h"
#include "FilteredFile.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "ShareManager.h"
#include "SimpleXML.h"
#include "SimpleXMLReader.h"
#include "StringTokenizer.h"
#include "User.h"


namespace dcpp {

//using boost::algorithm::all_of;
using boost::range::for_each;
using boost::range::find_if;

DirectoryListing::DirectoryListing(const HintedUser& aUser, bool aPartial, const string& aFileName, bool aIsClientView, bool aIsOwnList) : 
	TrackableDownloadItem(aIsOwnList || (!aPartial && Util::fileExists(aFileName))), // API requires the download state to be set correctly
	hintedUser(aUser), root(Directory::create(nullptr, ADC_ROOT_STR, Directory::TYPE_INCOMPLETE_NOCHILD, 0)), partialList(aPartial), isOwnList(aIsOwnList), fileName(aFileName),
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
		addDirectoryChangeTask(ADC_ROOT_STR, true);
	} else {
		addFullListTask(ADC_ROOT_STR);
	}

	SettingsManager::getInstance()->set(SettingsManager::LAST_LIST_PROFILE, aProfile);
	fire(DirectoryListingListener::ShareProfileChanged());
}

void DirectoryListing::getPartialListInfo(int64_t& totalSize_, size_t& totalFiles_) const noexcept {
	if (isOwnList) {
		ShareManager::getInstance()->getProfileInfo(getShareProfile(), totalSize_, totalFiles_);
	}

	auto si = ClientManager::getInstance()->getShareInfo(hintedUser);
	if (si) {
		totalSize_ = (*si).size;
		totalFiles_ = (*si).fileCount;
	}
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

	const auto sl = StringTokenizer<string>(aBasePath, ADC_SEPARATOR).getTokens();
	for (const auto& curDirName: sl) {
		auto s = cur->directories.find(&curDirName);
		if (s == cur->directories.end()) {
			auto d = DirectoryListing::Directory::create(cur.get(), curDirName, DirectoryListing::Directory::TYPE_INCOMPLETE_CHILD, aDownloadDate, true);
			cur = d;
		} else {
			cur = s->second;
		}
	}

	return cur;
}

void DirectoryListing::loadFile() {
	if (isOwnList) {
		loadShareDirectory(ADC_ROOT_STR, true);
	} else {

		// For now, we detect type by ending...
		string ext = Util::getFileExt(fileName);

		dcpp::File ff(fileName, dcpp::File::READ, dcpp::File::OPEN, dcpp::File::BUFFER_AUTO);
		root->setLastUpdateDate(ff.getLastModified());
		if(Util::stricmp(ext, ".bz2") == 0) {
			FilteredInputStream<UnBZFilter, false> f(&ff);
			loadXML(f, false, ADC_ROOT_STR, ff.getLastModified());
		} else if(Util::stricmp(ext, ".xml") == 0) {
			loadXML(ff, false, ADC_ROOT_STR, ff.getLastModified());
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
	void validateName(const string& aName);

	DirectoryListing* list;
	DirectoryListing::Directory* cur;
	UserPtr user;

	string base;
	bool inListing;
	bool updating;
	bool checkDupe;
	bool partialList;
	int dirsLoaded;
	time_t listDownloadDate;
};

int DirectoryListing::loadPartialXml(const string& aXml, const string& aBase) {
	MemoryInputStream mis(aXml);
	return loadXML(mis, true, aBase);
}

int DirectoryListing::loadXML(InputStream& is, bool aUpdating, const string& aBase, time_t aListDate) {
	ListLoader ll(this, root.get(), aBase, aUpdating, getUser(), !isOwnList && isClientView && SETTING(DUPES_IN_FILELIST), partialList, aListDate);
	try {
		dcpp::SimpleXMLReader(&ll).parse(is);
	} catch(SimpleXMLException& e) {
		throw AbortException(e.getError());
	}

	return ll.getLoadedDirs();
}

void ListLoader::validateName(const string& aName) {
	if (aName.empty()) {
		throw SimpleXMLException("Name attribute missing");
	}

	if (aName == "." || aName == "..") {
		throw SimpleXMLException("Forbidden filename");
	}

	if (aName.find(ADC_SEPARATOR) != string::npos) {
		throw SimpleXMLException("Filenames can't contain path separators");
	}
}

static const string sFileListing = "FileListing";
static const string sBase = "Base";
static const string sBaseDate = "BaseDate";
static const string sGenerator = "Generator";
static const string sDirectory = "Directory";
static const string sIncomplete = "Incomplete";
static const string sDirectories = "Directories";
static const string sFiles = "Files";
static const string sChildren = "Children"; // DEPRECATED
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
			validateName(n);

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
			validateName(n);

			bool incomp = getAttrib(attribs, sIncomplete, 1) == "1";
			auto directoriesStr = getAttrib(attribs, sDirectories, 2);
			auto filesStr = getAttrib(attribs, sFiles, 3);

			DirectoryContentInfo contentInfo;
			if (!incomp || !filesStr.empty() || !directoriesStr.empty()) {
				contentInfo = DirectoryContentInfo(Util::toInt(directoriesStr), Util::toInt(filesStr));
			}

			bool children = getAttrib(attribs, sChildren, 2) == "1" || contentInfo.directories > 0; // DEPRECATED

			const string& size = getAttrib(attribs, sSize, 2);
			const string& date = getAttrib(attribs, sDate, 3);

			DirectoryListing::Directory::Ptr d = nullptr;
			if(updating) {
				dirsLoaded++;

				auto i = cur->directories.find(&n);
				if (i != cur->directories.end()) {
					d = i->second;
				}
			}

			if(!d) {
				auto type = incomp ? (children ? DirectoryListing::Directory::TYPE_INCOMPLETE_CHILD : DirectoryListing::Directory::TYPE_INCOMPLETE_NOCHILD) :
					DirectoryListing::Directory::TYPE_NORMAL;

				d = DirectoryListing::Directory::create(cur, n, type, listDownloadDate, (partialList && checkDupe), contentInfo, size, Util::toUInt32(date));
			} else {
				if(!incomp) {
					d->setComplete();
				}
				d->setRemoteDate(Util::toUInt32(date));
			}
			cur = d.get();

			if(simple) {
				// To handle <Directory Name="..." />
				endTag(name);
			}
		}
	} else if(name == sFileListing) {
		if (updating) {
			const string& b = getAttrib(attribs, sBase, 2);
			dcassert(Util::isAdcPath(base));

			// Validate the parsed base path
			{
				if (Util::stricmp(b, base) != 0) {
					throw AbortException("The base directory specified in the file list (" + b + ") doesn't match with the expected base (" + base + ")");
				}
			}

			cur = list->createBaseDirectory(base, listDownloadDate).get();

			dcassert(list->findDirectory(base));

			const string& baseDate = getAttrib(attribs, sBaseDate, 3);
			cur->setRemoteDate(Util::toUInt32(baseDate));
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
			cur = cur->getParent();
		} else if(name == sFileListing) {
			// Cur should be the loaded base path now

			cur->setComplete();

			// Content info is not loaded for the base path
			cur->setContentInfo(cur->getContentInfoRecursive(false));

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

DirectoryListing::Directory::Ptr DirectoryListing::Directory::create(Directory* aParent, const string& aName, DirType aType, time_t aUpdateDate, bool aCheckDupe, const DirectoryContentInfo& aContentInfo, const string& aSize, time_t aRemoteDate) {
	auto dir = Ptr(new Directory(aParent, aName, aType, aUpdateDate, aCheckDupe, aContentInfo, aSize, aRemoteDate));
	if (aParent && aType != TYPE_ADLS) { // This would cause an infinite recursion in ADL search
		dcassert(aParent->directories.find(&dir->getName()) == aParent->directories.end());
		auto res = aParent->directories.emplace(&dir->getName(), dir);
		if (!res.second) {
			throw AbortException("The directory " + dir->getAdcPath() + " contains items with duplicate names (" + dir->getName() + ", " + *(*res.first).first + ")");
		}
	}

	return dir;
}

DirectoryListing::AdlDirectory::Ptr DirectoryListing::AdlDirectory::create(const string& aFullPath, Directory* aParent, const string& aName) {
	dcassert(aParent);

	auto name = aName;
	if (aParent->directories.find(&name) != aParent->directories.end()) {
		// No duplicate file names
		int num = 0;
		for (;;) {
			name = aName + " (" + Util::toString(num++) + ")";
			if (aParent->directories.find(&name) == aParent->directories.end()) {
				break;
			}
		}
	}

	auto dir = Ptr(new AdlDirectory(aFullPath, aParent, name));

	dcassert(aParent->directories.find(&dir->getName()) == aParent->directories.end());
	aParent->directories.emplace(&dir->getName(), dir);

	return dir;
}

DirectoryListing::AdlDirectory::AdlDirectory(const string& aFullAdcPath, DirectoryListing::Directory* aParent, const string& aName) : 
	Directory(aParent, aName, Directory::TYPE_ADLS, GET_TIME(), false, DirectoryContentInfo(), Util::emptyString, 0), fullAdcPath(aFullAdcPath) {

}

DirectoryListing::Directory::Directory(Directory* aParent, const string& aName, Directory::DirType aType, time_t aUpdateDate, bool aCheckDupe, const DirectoryContentInfo& aContentInfo, const string& aSize, time_t aRemoteDate /*0*/)
	: name(aName), parent(aParent), type(aType), remoteDate(aRemoteDate), lastUpdateDate(aUpdateDate), contentInfo(aContentInfo) {

	if (!aSize.empty()) {
		partialSize = Util::toInt64(aSize);
	}

	if (aCheckDupe) {
		dupe = AirUtil::checkAdcDirectoryDupe(getAdcPath(), partialSize);
	}

	//dcdebug("DirectoryListing::Directory %s was created\n", aName.c_str());
}

void DirectoryListing::Directory::search(OrderedStringSet& aResults, SearchQuery& aStrings) const noexcept {
	if (getAdls())
		return;

	if (aStrings.matchesDirectory(name)) {
		auto path = parent ? parent->getAdcPath() : ADC_ROOT_STR;
		auto res = find(aResults, path);
		if (res == aResults.end() && aStrings.matchesSize(getTotalSize(false))) {
			aResults.insert(path);
		}
	}

	for (auto& f: files) {
		if (aStrings.matchesFile(f->getName(), f->getSize(), f->getRemoteDate(), f->getTTH())) {
			aResults.insert(getAdcPath());
			break;
		}
	}

	for (const auto& d: directories | map_values) {
		d->search(aResults, aStrings);
		if (aResults.size() >= aStrings.maxResults) return;
	}
}

bool DirectoryListing::Directory::findIncomplete() const noexcept {
	/* Recursive check for incomplete dirs */
	if(!isComplete()) {
		return true;
	}

	return find_if(directories | map_values, [](const Directory::Ptr& dir) { 
		return dir->findIncomplete(); 
	}).base() != directories.end();
}

DirectoryContentInfo DirectoryListing::Directory::getContentInfoRecursive(bool aCountAdls) const noexcept {
	if (isComplete()) {
		size_t directoryCount = 0, fileCount = 0;
		getContentInfo(directoryCount, fileCount, aCountAdls);
		return DirectoryContentInfo(directoryCount, fileCount);
	}

	return contentInfo;
}

void DirectoryListing::Directory::getContentInfo(size_t& directories_, size_t& files_, bool aCountAdls) const noexcept {
	if (!aCountAdls && getAdls()) {
		return;
	}

	if (isComplete()) {
		directories_ += directories.size();
		files_ += files.size();

		for (const auto& d : directories | map_values) {
			d->getContentInfo(directories_, files_, aCountAdls);
		}
	} else if (Util::hasContentInfo(contentInfo)) {
		directories_ += contentInfo.directories;
		files_ += contentInfo.files;
	}
}

BundleDirectoryItemInfo::List DirectoryListing::Directory::toBundleInfoList() const noexcept {
	BundleDirectoryItemInfo::List bundleFiles;
	toBundleInfoList(Util::emptyString, bundleFiles);
	return bundleFiles;
}

void DirectoryListing::Directory::toBundleInfoList(const string& aTarget, BundleDirectoryItemInfo::List& aFiles) const noexcept {
	// First, recurse over the directories
	for (const auto& d: directories | map_values) {
		d->toBundleInfoList(aTarget + d->getName() + PATH_SEPARATOR, aFiles);
	}

	// Then add the files

	//sort(files.begin(), files.end(), File::Sort());
	for (const auto& f: files) {
		aFiles.emplace_back(aTarget + f->getName(), f->getTTH(), f->getSize());
	}
}

optional<DirectoryBundleAddInfo> DirectoryListing::createBundle(const Directory::Ptr& aDir, const string& aTarget, Priority aPriority, string& errorMsg_) noexcept {
	auto bundleFiles = aDir->toBundleInfoList();

	try {
		auto info = QueueManager::getInstance()->createDirectoryBundle(aTarget, hintedUser.user == ClientManager::getInstance()->getMe() && !isOwnList ? HintedUser() : hintedUser,
			bundleFiles, aPriority, aDir->getRemoteDate(), errorMsg_);

		return info;
	} catch (const std::bad_alloc&) {
		errorMsg_ = STRING(OUT_OF_MEMORY);
		LogManager::getInstance()->message(STRING_F(BUNDLE_CREATION_FAILED, aTarget % STRING(OUT_OF_MEMORY)), LogMessage::SEV_ERROR);
	}

	return boost::none;
}

int64_t DirectoryListing::getDirSize(const string& aDir) const noexcept {
	dcassert(aDir.size() > 2);
	dcassert(aDir == ADC_ROOT_STR || aDir[aDir.size() - 1] == ADC_SEPARATOR);

	auto d = findDirectory(aDir);
	if (d) {
		return d->getTotalSize(false);
	}

	return 0;
}

DirectoryListing::Directory::Ptr DirectoryListing::findDirectory(const string& aName, const Directory* aCurrent) const noexcept {
	if (aName == ADC_ROOT_STR)
		return root;

	auto end = aName.find(ADC_SEPARATOR, 1);
	dcassert(end != string::npos);
	string name = aName.substr(1, end - 1);

	auto i = aCurrent->directories.find(&name);
	if (i != aCurrent->directories.end()) {
		if (end == (aName.size() - 1)) {
			return i->second;
		} else {
			return findDirectory(aName.substr(end), i->second.get());
		}
	}

	return nullptr;
}

void DirectoryListing::Directory::findFiles(const boost::regex& aReg, File::List& aResults) const noexcept {
	copy_if(files.begin(), files.end(), back_inserter(aResults), [&aReg](const File::Ptr& df) { return boost::regex_match(df->getName(), aReg); });

	for (const auto& d : directories | map_values) {
		d->findFiles(aReg, aResults);
	}
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
		return Util::directoryEmpty(aDir->getContentInfo());
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
	for (auto i = directories.begin(); i != directories.end();) {
		auto d = i->second.get();

		d->filterList(l);

		if (d->directories.empty() && d->files.empty()) {
			i = directories.erase(i);
		} else {
			++i;
		}
	}

	files.erase(remove_if(files.begin(), files.end(), HashContained(l)), files.end());

	if((SETTING(SKIP_SUBTRACT) > 0) && (files.size() < 2)) {   //setting for only skip if folder filecount under x ?
		files.erase(remove_if(files.begin(), files.end(), SizeLess()), files.end());
	}
}

void DirectoryListing::Directory::getHashList(DirectoryListing::Directory::TTHSet& l) const noexcept {
	for(const auto& d: directories | map_values)  
		d->getHashList(l);

	for(const auto& f: files) 
		l.insert(f->getTTH());
}
	
void DirectoryListing::getLocalPaths(const File::Ptr& f, StringList& ret) const {
	if(f->getParent()->getAdls() && (f->getParent()->getParent() == root.get() || !isOwnList))
		return;

	if (isOwnList) {
		string path;
		if (f->getParent()->getAdls())
			path = ((AdlDirectory*) f->getParent())->getFullAdcPath();
		else
			path = f->getParent()->getAdcPath();

		ShareManager::getInstance()->getRealPaths(path + f->getName(), ret, getShareProfile());
	} else {
		ret = AirUtil::getFileDupePaths(f->getDupe(), f->getTTH());
	}
}

void DirectoryListing::getLocalPaths(const Directory::Ptr& d, StringList& ret) const {
	if(d->getAdls() && (d->getParent() == root.get() || !isOwnList))
		return;

	string path;
	if (d->getAdls())
		path = ((AdlDirectory*) d.get())->getFullAdcPath();
	else
		path = d->getAdcPath();

	if (isOwnList) {
		ShareManager::getInstance()->getRealPaths(path, ret, getShareProfile());
	} else {
		ret = ShareManager::getInstance()->getAdcDirectoryPaths(path);
	}
}

int64_t DirectoryListing::Directory::getTotalSize(bool countAdls) const noexcept {
	if(!isComplete())
		return partialSize;
	if(!countAdls && getAdls())
		return 0;
	
	auto x = getFilesSize();
	for (const auto& d: directories | map_values) {
		if(!countAdls && d->getAdls())
			continue;
		x += d->getTotalSize(getAdls());
	}
	return x;
}

size_t DirectoryListing::Directory::getTotalFileCount(bool aCountAdls) const noexcept {
	if (!aCountAdls && getAdls())
		return 0;

	return getContentInfoRecursive(aCountAdls).files;
}

void DirectoryListing::Directory::clearAdls() noexcept {
	for (auto i = directories.begin(); i != directories.end();) {
		if (i->second->getAdls()) {
			i = directories.erase(i);
		} else {
			++i;
		}
	}
}

string DirectoryListing::Directory::getAdcPath() const noexcept {
	//make sure to not try and get the name of the root dir
	if (parent) {
		return parent->getAdcPath() + name + ADC_SEPARATOR;
	}

	// root
	return ADC_ROOT_STR;
}

int64_t DirectoryListing::Directory::getFilesSize() const noexcept {
	int64_t x = 0;
	for (const auto& f: files) {
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
	for(auto& d: directories | map_values) {
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

void DirectoryListing::addMatchADLTask() noexcept {
	addAsyncTask([=] { matchAdlImpl(); });
}

void DirectoryListing::addListDiffTask(const string& aFile, bool aOwnList) noexcept {
	addAsyncTask([=] { listDiffImpl(aFile, aOwnList); });
}

void DirectoryListing::addPartialListTask(const string& aXml, const string& aBase, bool aBackgroundTask /*false*/, const AsyncF& aCompletionF) noexcept {
	dcassert(!aBase.empty() && aBase.front() == ADC_SEPARATOR);
	addAsyncTask([=] { loadPartialImpl(aXml, aBase, aBackgroundTask, aCompletionF); });
}

void DirectoryListing::addFullListTask(const string& aDir) noexcept {
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
	dcassert(Util::isAdcPath(aSearch->path));
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
		LogManager::getInstance()->message(STRING_F(LIST_LOAD_FAILED, getNick(false) % STRING(OUT_OF_MEMORY)), LogMessage::SEV_ERROR);
		fire(DirectoryListingListener::LoadingFailed(), "Out of memory");
	} catch (const AbortException& e) {
		// The error is empty on user cancellations
		if (!e.getError().empty()) {
			LogManager::getInstance()->message(STRING_F(LIST_LOAD_FAILED, getNick(false) % e.getError()), LogMessage::SEV_ERROR);
		}

		fire(DirectoryListingListener::LoadingFailed(), e.getError());
	} catch(const ShareException& e) {
		fire(DirectoryListingListener::LoadingFailed(), e.getError());
	} catch (const QueueException& e) {
		fire(DirectoryListingListener::UpdateStatusMessage(), "Queueing failed:" + e.getError());
	} catch (const Exception& e) {
		LogManager::getInstance()->message(STRING_F(LIST_LOAD_FAILED, getNick(false) % e.getError()), LogMessage::SEV_ERROR);
		fire(DirectoryListingListener::LoadingFailed(), getNick(false) + ": " + e.getError());
	}
}

void DirectoryListing::listDiffImpl(const string& aFile, bool aOwnList) {
	int64_t start = GET_TICK();
	if (isOwnList && partialList) {
		// we need the recursive list for this
		loadShareDirectory(ADC_ROOT_STR, true);
		partialList = false;
	}

	DirectoryListing dirList(hintedUser, false, aFile, false, aOwnList);
	dirList.loadFile();

	root->filterList(dirList);
	fire(DirectoryListingListener::LoadingFinished(), start, ADC_ROOT_STR, false);
}

void DirectoryListing::matchAdlImpl() {
	fire(DirectoryListingListener::LoadingStarted(), false);

	int64_t start = GET_TICK();
	root->clearAdls();

	if (isOwnList) {
		// No point in matching own partial list
		setMatchADL(true);
		loadFileImpl(ADC_ROOT_STR);
	} else {
		fire(DirectoryListingListener::UpdateStatusMessage(), CSTRING(MATCHING_ADL));
		ADLSearchManager::getInstance()->matchListing(*this);
		fire(DirectoryListingListener::LoadingFinished(), start, ADC_ROOT_STR, false);
	}
}

void DirectoryListing::loadFileImpl(const string& aInitialDir) {
	int64_t start = GET_TICK();
	partialList = false;

	fire(DirectoryListingListener::LoadingStarted(), false);

	// In case we are reloading...
	root->clearAll();

	loadFile();

	if (matchADL) {
		fire(DirectoryListingListener::UpdateStatusMessage(), CSTRING(MATCHING_ADL));
		ADLSearchManager::getInstance()->matchListing(*this);
	}

	onLoadingFinished(start, aInitialDir, false);
}

void DirectoryListing::onLoadingFinished(int64_t aStartTime, const string& aBasePath, bool aBackgroundTask) noexcept {
	if (!getIsOwnList() && SETTING(DUPES_IN_FILELIST) && isClientView)
		checkShareDupes();

	auto dir = findDirectory(aBasePath);
	dcassert(dir);
	if (dir) {
		dir->setLoading(false);
		if (!aBackgroundTask) {
			updateCurrentLocation(dir);
			read = false;
		}

		onStateChanged();
	}
	
	fire(DirectoryListingListener::LoadingFinished(), aStartTime, aBasePath, aBackgroundTask);
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
			searchResults.insert(sr->getAdcPath());

		endSearch(false);
	} else if (partialList && !hintedUser.user->isNMDC()) {
		TimerManager::getInstance()->addListener(this);

		directSearch.reset(new DirectSearch(hintedUser, aSearch));
	} else {
		const auto dir = findDirectory(aSearch->path);
		if (dir) {
			dir->search(searchResults, *curSearch);
		}

		endSearch(false);
	}
}

void DirectoryListing::loadPartialImpl(const string& aXml, const string& aBasePath, bool aBackgroundTask, const AsyncF& aCompletionF) {
	if (!partialList)
		return;

	// Preparations
	{
		bool reloading = false;

		// Has this directory been loaded before? Existing content must be cleared in that case
		auto d = findDirectory(aBasePath);
		if (d) {
			reloading = d->isComplete();
		}

		// Let the window to be disabled before making any modifications
		fire(DirectoryListingListener::LoadingStarted(), !reloading);

		if (reloading) {
			// Remove all existing directories inside this path
			d->clearAll();
		}
	}

	// Load content
	int dirsLoaded = 0;
	if (isOwnList) {
		dirsLoaded = loadShareDirectory(aBasePath);
	} else {
		dirsLoaded = loadPartialXml(aXml, aBasePath);
	}

	// Done
	onLoadingFinished(0, aBasePath, aBackgroundTask);

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

	addAsyncTask([=] { fire(DirectoryListingListener::UserUpdated()); });
}

void DirectoryListing::on(TimerManagerListener::Second, uint64_t /*aTick*/) noexcept {
	if (directSearch && directSearch->finished()) {
		endSearch(directSearch->hasTimedOut());
	}
}

void DirectoryListing::endSearch(bool timedOut /*false*/) noexcept {
	if (directSearch) {
		directSearch->getAdcPaths(searchResults, true);
		directSearch.reset(nullptr);
	}

	if (searchResults.size() == 0) {
		curSearch = nullptr;
		fire(DirectoryListingListener::SearchFailed(), timedOut);
	} else {
		curResult = searchResults.begin();
		addDirectoryChangeTask(*curResult, false, true);
	}
}

int DirectoryListing::loadShareDirectory(const string& aPath, bool aRecurse) {
	auto mis = ShareManager::getInstance()->generatePartialList(aPath, aRecurse, getShareProfile());
	if (mis) {
		return loadXML(*mis, true, aPath);
	}

	//might happen if have refreshed the share meanwhile
	throw Exception(CSTRING(FILE_NOT_AVAILABLE));
}

bool DirectoryListing::changeDirectory(const string& aAdcPath, bool aReload, bool aIsSearchChange, bool aForceQueue) noexcept {
	Directory::Ptr dir;
	if (partialList) {
		// Directory may not exist when searching in partial lists 
		// or when opening directories from search (or via the API) for existing filelists
		dir = createBaseDirectory(aAdcPath);
	} else {
		dir = findDirectory(aAdcPath);
		if (!dir) {
			dcassert(0);
			return false;
		}
	}

	dcassert(findDirectory(aAdcPath) != nullptr);

	clearLastError();
	updateCurrentLocation(dir);
	fire(DirectoryListingListener::ChangeDirectory(), aAdcPath, aIsSearchChange);

	if (!partialList || dir->getLoading() || (dir->isComplete() && !aReload)) {
		// No need to load anything
	} else if (partialList) {
		if (isOwnList || (getUser()->isOnline() || aForceQueue)) {
			dir->setLoading(true);

			try {
				if (isOwnList) {
					addPartialListTask(Util::emptyString, aAdcPath, false);
				} else {
					QueueManager::getInstance()->addList(hintedUser, QueueItem::FLAG_PARTIAL_LIST | QueueItem::FLAG_CLIENT_VIEW, aAdcPath);
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

	addDirectoryChangeTask(*curResult, false, true);
	return true;
}

void DirectoryListing::addDirectoryChangeTask(const string& aPath, bool aReload, bool aIsSearchChange, bool aForceQueue) noexcept {
	addAsyncTask([=] {
		changeDirectory(aPath, aReload, aIsSearchChange, aForceQueue);
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

	addAsyncTask([=] {
		read = true;
		fire(DirectoryListingListener::Read());
	});
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

void DirectoryListing::on(ShareManagerListener::RefreshCompleted, uint8_t, const RefreshPathList& aPaths) noexcept{
	if (!partialList)
		return;

	// Reload all locations by virtual path
	string lastVirtual;
	for (const auto& p : aPaths) {
		auto vPath = ShareManager::getInstance()->realToVirtualAdc(p, getShareProfile());
		if (!vPath.empty() && lastVirtual != vPath && findDirectory(vPath)) {
			addPartialListTask(Util::emptyString, vPath, true);
			lastVirtual = vPath;
		}
	}
}

} // namespace dcpp
