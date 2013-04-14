/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#include <string>
#include "stdinc.h"
#include "ShareManager.h"
#include "ScopedFunctor.h"

#include "ResourceManager.h"

#include "CryptoManager.h"
#include "ClientManager.h"
#include "LogManager.h"
#include "HashManager.h"
#include "QueueManager.h"

#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "File.h"
#include "FilteredFile.h"
#include "BZUtils.h"
#include "Transfer.h"
#include "UserConnection.h"
#include "Download.h"
#include "SearchResult.h"
#include "Wildcards.h"
#include "AirUtil.h"

#include "version.h"

#include <boost/range/algorithm/remove_if.hpp>
#include <boost/algorithm/cxx11/copy_if.hpp>
#include <boost/range/algorithm/search.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/algorithm/count_if.hpp>
#include <boost/range/algorithm/copy.hpp>

#include <ppl.h>

#ifdef _WIN32
# include <ShlObj.h>
#else
# include <dirent.h>
# include <sys/stat.h>
# include <unistd.h>
# include <fnmatch.h>
#endif

#include <limits>

namespace dcpp {

using std::string;
using boost::adaptors::filtered;
using boost::range::find_if;
using boost::range::for_each;
using boost::range::copy;

#define SHARE_CACHE_VERSION "2"

#ifdef ATOMIC_FLAG_INIT
atomic_flag ShareManager::refreshing = ATOMIC_FLAG_INIT;
#else
atomic_flag ShareManager::refreshing;
#endif

boost::regex ShareManager::rxxReg;

ShareManager::ShareManager() : lastFullUpdate(GET_TICK()), lastIncomingUpdate(GET_TICK()), sharedSize(0),
	xml_saving(false), lastSave(0), aShutdown(false), refreshRunning(false)
{ 
	SettingsManager::getInstance()->addListener(this);
	QueueManager::getInstance()->addListener(this);

	rxxReg.assign("[Rr0-9][Aa0-9][Rr0-9]");
#ifdef _WIN32
	// don't share Windows directory
	TCHAR path[MAX_PATH];
	::SHGetFolderPath(NULL, CSIDL_WINDOWS, NULL, SHGFP_TYPE_CURRENT, path);
	winDir = Text::toLower(Text::fromT(path)) + PATH_SEPARATOR;
#endif

	File::ensureDirectory(Util::getPath(Util::PATH_SHARECACHE));
}

ShareManager::~ShareManager() {
	SettingsManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);

	join();
}

void ShareManager::startup(function<void (const string&)> splashF, function<void (float)> progressF) {
	AirUtil::updateCachedSettings();
	if (!getShareProfile(SP_DEFAULT)) {
		ShareProfilePtr sp = ShareProfilePtr(new ShareProfile(STRING(DEFAULT), SP_DEFAULT));
		shareProfiles.push_back(sp);
	}

	ShareProfilePtr hidden = ShareProfilePtr(new ShareProfile("Hidden", SP_HIDDEN));
	shareProfiles.push_back(hidden);

	setSkipList();

	if(!loadCache(progressF)) {
		if (splashF)
			splashF(STRING(REFRESHING_SHARE));
		refresh(false, TYPE_STARTUP, progressF);
	}

	rebuildTotalExcludes();

	TimerManager::getInstance()->addListener(this);
}

void ShareManager::abortRefresh() {
	//abort buildtree and refresh, we are shutting down.
	aShutdown = true;
}

void ShareManager::shutdown(function<void (float)> progressF) {
	saveXmlList(false, progressF);

	try {
		RLock l (cs);
		StringList lists = File::findFiles(Util::getPath(Util::PATH_USER_CONFIG), "files?*.xml.bz2");

		//clear refs so we can delete filelists.
		for(auto f: shareProfiles) {
			if(f->getProfileList() && f->getProfileList()->bzXmlRef.get()) 
				f->getProfileList()->bzXmlRef.reset(); 
		}

		for_each(lists, File::deleteFile);
	} catch(...) { }
}

void ShareManager::setProfilesDirty(ProfileTokenSet aProfiles, bool forceXmlRefresh /*false*/) {
	RLock l(cs);
	for(const auto aProfile: aProfiles) {
		auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
		if(i != shareProfiles.end()) {
			if (forceXmlRefresh)
				(*i)->getProfileList()->setForceXmlRefresh(true);
			(*i)->getProfileList()->setXmlDirty(true);
			(*i)->setProfileInfoDirty(true);
		}
	}
}

ShareManager::Directory::Directory(const string& aRealName, const ShareManager::Directory::Ptr& aParent, uint32_t aLastWrite, ProfileDirectory::Ptr aProfileDir) :
	size(0),
	realName(aRealName),
	parent(aParent.get()),
	fileTypes(1 << SearchManager::TYPE_DIRECTORY),
	profileDir(aProfileDir),
	lastWrite(aLastWrite), 
	realNameLower(Text::toLower(aRealName))
{
}

ShareManager::Directory::File::Set::const_iterator ShareManager::Directory::findFile(const string& aName) const {
	//TODO: use binary search
	dcassert(Text::isLower(aName));
	return find_if(files, [&aName](const Directory::File& f) { return compare(aName, f.getNameLower()) == 0; });
}

void ShareManager::Directory::getResultInfo(ProfileToken aProfile, int64_t& size_, size_t& files_, size_t& folders_) const noexcept {
	for(const auto& d: directories) {
		if (d->isLevelExcluded(aProfile))
			continue;
		
		d->getResultInfo(aProfile, size_, files_, folders_);
	}

	folders_ += directories.size();
	size_ += size;
	files_ += files.size();
}

int64_t ShareManager::Directory::getSize(ProfileToken aProfile) const noexcept {
	int64_t tmp = size;
	for(const auto& d: directories) {
		if (d->isLevelExcluded(aProfile))
			continue;
		tmp += d->getSize(aProfile);
	}
	return tmp;
}

int64_t ShareManager::Directory::getTotalSize() const noexcept {
	int64_t tmp = size;
	for(const auto& d: directories)
		tmp += d->getTotalSize();

	return tmp;
}

string ShareManager::Directory::getADCPath(ProfileToken aProfile) const noexcept {
	if(profileDir && profileDir->hasRootProfile(aProfile))
		return '/' + profileDir->getName(aProfile) + '/';
	return parent->getADCPath(aProfile) + realName + '/';
}

string ShareManager::Directory::getVirtualName(ProfileToken aProfile) const noexcept {
	if(profileDir && profileDir->hasRootProfile(aProfile))
		return profileDir->getName(aProfile);
	return realName;
}

string ShareManager::Directory::getFullName(ProfileToken aProfile) const noexcept {
	if(profileDir && profileDir->hasRootProfile(aProfile))
		return profileDir->getName(aProfile) + '\\';
	dcassert(parent);
	return parent->getFullName(aProfile) + realName + '\\';
}

void ShareManager::Directory::addType(uint32_t type) noexcept {
	if(!hasType(type)) {
		fileTypes |= (1 << type);
		if(parent)
			parent->addType(type);
	}
}

string ShareManager::getRealPath(const string& aFileName, int64_t aSize) const {
	RLock l(cs);
	for(const auto f: tthIndex | map_values) {
		if(stricmp(aFileName.c_str(), f->getName().c_str()) == 0 && f->getSize() == aSize) {
			return f->getRealPath();
		}
	}
	return Util::emptyString;
}

string ShareManager::getRealPath(const TTHValue& root) const {
	RLock l(cs);
	const auto i = tthIndex.find(const_cast<TTHValue*>(&root)); 
	if(i != tthIndex.end()) {
		return i->second->getRealPath();
	}

	const auto k = tempShares.find(root);
	if (k != tempShares.end()) {
		return k->second.path;
	}

	return Util::emptyString;
}


bool ShareManager::isTTHShared(const TTHValue& tth) const {
	RLock l(cs);
	return tthIndex.find(const_cast<TTHValue*>(&tth)) != tthIndex.end();
}

string ShareManager::Directory::getRealPath(const string& path, bool checkExistance /*true*/) const {
	if(getParent()) {
		return getParent()->getRealPath(realName + PATH_SEPARATOR_STR + path, checkExistance);
	}

	string rootDir = getProfileDir()->getPath() + path;

	if(!checkExistance) //no extra checks for finding the file while loading share cache.
		return rootDir;

	/*check for the existance here if we have moved the file/folder and only refreshed the new location.
	should we even look, what's moved is moved, user should refresh both locations.*/
	if(Util::fileExists(rootDir))
		return rootDir;
	else
		return ShareManager::getInstance()->findRealRoot(realName, path); // all display names should be compared really..
}

string ShareManager::findRealRoot(const string& virtualRoot, const string& virtualPath) const {
	for(const auto& s: rootPaths | map_values) {
		for(const auto& vName: s->getProfileDir()->getRootProfiles() | map_values) {
			if(stricmp(vName, virtualRoot) == 0) {
				string path = vName + virtualPath;
				dcdebug("Matching %s\n", path.c_str());
				if(FileFindIter(path) != FileFindIter()) {
					return path;
				}
			}
		}
	}
	
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

bool ShareManager::Directory::isRootLevel(ProfileToken aProfile) const {
	return profileDir && profileDir->hasRootProfile(aProfile) ? true : false;
}

bool ShareManager::Directory::hasProfile(const ProfileTokenSet& aProfiles) const {
	if (profileDir && profileDir->hasRootProfile(aProfiles))
		return true;
	if (parent)
		return parent->hasProfile(aProfiles);
	return false;
}


void ShareManager::Directory::copyRootProfiles(ProfileTokenSet& aProfiles, bool setCacheDirty) const {
	if (profileDir) {
		copy(profileDir->getRootProfiles() | map_keys, inserter(aProfiles, aProfiles.begin()));
		if (setCacheDirty)
			profileDir->setCacheDirty(true);
	}

	if (parent)
		parent->copyRootProfiles(aProfiles, setCacheDirty);
}

bool ShareManager::ProfileDirectory::hasRootProfile(const ProfileTokenSet& aProfiles) const {
	for(const auto ap: aProfiles) {
		if (rootProfiles.find(ap) != rootProfiles.end())
			return true;
	}
	return false;
}

bool ShareManager::Directory::hasProfile(ProfileToken aProfile) const {
	if(profileDir) {
		if (isLevelExcluded(aProfile))
			return false;
		if (profileDir->hasRootProfile(aProfile))
			return true;
	} 
	
	if (parent) {
		return parent->hasProfile(aProfile);
	}
	return false;
}

bool ShareManager::Directory::matchBloom(const StringSearch::List& aSearches) const {
	const auto& bloom = getBloom();
	for(const auto& i: aSearches) {
		if(!bloom.match(i.getPattern()))
			return false;
	}

	return true;
}

ShareManager::ShareBloom& ShareManager::Directory::getBloom() const {
	if (parent) {
		return parent->getBloom();
	}

	return *profileDir->bloom.get();
}

bool ShareManager::ProfileDirectory::hasRootProfile(ProfileToken aProfile) const {
	return rootProfiles.find(aProfile) != rootProfiles.end();
}

bool ShareManager::ProfileDirectory::isExcluded(const ProfileTokenSet& aProfiles) const {
	//return all_of(excludedProfiles.begin(), excludedProfiles.end(), [](const ProfileToken t) { return aProfiles.find(t) != aProfiles.end() });

	//TODO: FIX THIS (doesn't detect the excludes correctly but not really used now)
	return std::search(excludedProfiles.begin(), excludedProfiles.end(), aProfiles.begin(), aProfiles.end()) != excludedProfiles.end();
}

bool ShareManager::Directory::isLevelExcluded(ProfileToken aProfile) const {
	if (profileDir && profileDir->isExcluded(aProfile))
		return true;
	return false;
}

bool ShareManager::Directory::isLevelExcluded(const ProfileTokenSet& aProfiles) const {
	if (profileDir && profileDir->isExcluded(aProfiles))
		return true;
	return false;
}

bool ShareManager::ProfileDirectory::isExcluded(ProfileToken aProfile) const {
	return !excludedProfiles.empty() && excludedProfiles.find(aProfile) != excludedProfiles.end();
}

ShareManager::ProfileDirectory::ProfileDirectory(const string& aRootPath, const string& aVname, ProfileToken aProfile, bool incoming /*false*/) : path(aRootPath), bloom(new ShareBloom(5)), cacheDirty(false) { 
	rootProfiles[aProfile] = aVname;
	setFlag(FLAG_ROOT);
	if (incoming)
		setFlag(FLAG_INCOMING);
}

ShareManager::ProfileDirectory::ProfileDirectory(const string& aRootPath, ProfileToken aProfile) : path(aRootPath), bloom(new ShareBloom(5)), cacheDirty(false) {
	excludedProfiles.insert(aProfile);
	setFlag(FLAG_EXCLUDE_PROFILE);
}

void ShareManager::ProfileDirectory::addRootProfile(const string& aName, ProfileToken aProfile) { 
	rootProfiles[aProfile] = aName;
	setFlag(FLAG_ROOT);
}

void ShareManager::ProfileDirectory::addExclude(ProfileToken aProfile) {
	setFlag(FLAG_EXCLUDE_PROFILE);
	excludedProfiles.insert(aProfile);
}

bool ShareManager::ProfileDirectory::removeRootProfile(ProfileToken aProfile) {
	rootProfiles.erase(aProfile);
	return rootProfiles.empty();
}

bool ShareManager::ProfileDirectory::removeExcludedProfile(ProfileToken aProfile) {
	excludedProfiles.erase(aProfile);
	return excludedProfiles.empty();
}

string ShareManager::ProfileDirectory::getName(ProfileToken aProfile) const {
	auto p = rootProfiles.find(aProfile);
	return p == rootProfiles.end() ? Util::emptyString : p->second; 
}

string ShareManager::toVirtual(const TTHValue& tth, ProfileToken aProfile) const  {
	
	RLock l(cs);

	FileList* fl = getFileList(aProfile);
	if(tth == fl->getBzXmlRoot()) {
		return Transfer::USER_LIST_NAME_BZ;
	} else if(tth == fl->getXmlRoot()) {
		return Transfer::USER_LIST_NAME;
	}

	auto i = tthIndex.find(const_cast<TTHValue*>(&tth)); 
	if(i != tthIndex.end()) 
		return i->second->getADCPath(aProfile);

	//nothing found throw;
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

FileList* ShareManager::getFileList(ProfileToken aProfile) const{
	const auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
	if(i != shareProfiles.end()) {
		dcassert((*i)->getProfileList());
		return (*i)->getProfileList();
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	
	//return shareProfiles[SP_DEFAULT]->second->getProfileList();
}

pair<int64_t, string> ShareManager::getFileListInfo(const string& virtualFile, ProfileToken aProfile) {
	if(virtualFile == "MyList.DcLst") 
		throw ShareException("NMDC-style lists no longer supported, please upgrade your client");

	if(virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
		FileList* fl = generateXmlList(aProfile);
		return make_pair(fl->getBzXmlListLen(), fl->getFileName());
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

void ShareManager::toRealWithSize(const string& virtualFile, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_) {
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		TTHValue tth(virtualFile.substr(4));

		RLock l(cs);
		if(any_of(aProfiles.begin(), aProfiles.end(), [](ProfileToken s) { return s != SP_HIDDEN; })) {
			const auto flst = tthIndex.equal_range(const_cast<TTHValue*>(&tth));
			for(auto f = flst.first; f != flst.second; ++f) {
				noAccess_ = false; //we may throw if the file doesn't exist on the disk so always reset this to prevent invalid access denied messages
				if(f->second->getParent()->hasProfile(aProfiles)) {
					path_ = f->second->getRealPath();
					size_ = f->second->getSize();
					return;
				} else {
					noAccess_ = true;
				}
			}
		}

		const auto files = tempShares.equal_range(tth);
		for(auto i = files.first; i != files.second; ++i) {
			noAccess_ = false;
			if(i->second.key.empty() || (i->second.key == aUser.user->getCID().toBase32())) { // if no key is set, it means its a hub share.
				path_ = i->second.path;
				size_ = i->second.size;
				return;
			} else {
				noAccess_ = true;
			}
		}
	} else {
		DirectoryList dirs;

		RLock l (cs);
		findVirtuals<ProfileTokenSet>(virtualFile, aProfiles, dirs);

		auto fileName = Text::toLower(Util::getFileName(Util::toNmdcFile(virtualFile)));
		for(const auto& d: dirs) {
			auto it = d->findFile(fileName);
			if(it != d->files.end()) {
				path_ = it->getRealPath();
				size_ = it->getSize();
				return;
			}
		}
	}

	throw ShareException(noAccess_ ? "You don't have access to this file" : UserConnection::FILE_NOT_AVAILABLE);
}

TTHValue ShareManager::getListTTH(const string& virtualFile, ProfileToken aProfile) const {
	RLock l(cs);
	if(virtualFile == Transfer::USER_LIST_NAME_BZ) {
		return getFileList(aProfile)->getBzXmlRoot();
	} else if(virtualFile == Transfer::USER_LIST_NAME) {
		return getFileList(aProfile)->getXmlRoot();
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

MemoryInputStream* ShareManager::getTree(const string& virtualFile, ProfileToken aProfile) const {
	TigerTree tree;
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		if(!HashManager::getInstance()->getTree(TTHValue(virtualFile.substr(4)), tree))
			return 0;
	} else {
		try {
			TTHValue tth = getListTTH(virtualFile, aProfile);
			HashManager::getInstance()->getTree(tth, tree);
		} catch(const Exception&) {
			return 0;
		}
	}

	ByteVector buf = tree.getLeafData();
	return new MemoryInputStream(&buf[0], buf.size());
}

AdcCommand ShareManager::getFileInfo(const string& aFile, ProfileToken aProfile) {
	if(aFile == Transfer::USER_LIST_NAME) {
		FileList* fl = generateXmlList(aProfile);
		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(fl->getXmlListLen()));
		cmd.addParam("TR", fl->getXmlRoot().toBase32());
		return cmd;
	} else if(aFile == Transfer::USER_LIST_NAME_BZ) {
		FileList* fl = generateXmlList(aProfile);
		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(fl->getBzXmlListLen()));
		cmd.addParam("TR", fl->getBzXmlRoot().toBase32());
		return cmd;
	}

	if(aFile.compare(0, 4, "TTH/") != 0)
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);

	TTHValue val(aFile.substr(4));
	
	RLock l(cs);
	auto i = tthIndex.find(const_cast<TTHValue*>(&val)); 
	if(i != tthIndex.end()) {
		const Directory::File& f = *i->second;
		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", f.getADCPath(aProfile));
		cmd.addParam("SI", Util::toString(f.getSize()));
		cmd.addParam("TR", f.getTTH().toBase32());
		return cmd;
	}

	//not found throw
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

bool ShareManager::isTempShared(const string& aKey, const TTHValue& tth) {
	RLock l(cs);
	const auto fp = tempShares.equal_range(tth);
	for(auto i = fp.first; i != fp.second; ++i) {
		if(i->second.key.empty() || (i->second.key == aKey)) // if no key is set, it means its a hub share.
			return true;
	}
	return false;
}

void ShareManager::addTempShare(const string& aKey, const TTHValue& tth, const string& filePath, int64_t aSize, ProfileToken aProfile) {
	
	//first check if already exists in Share.
	if(isFileShared(tth, Util::getFileName(filePath), aProfile)) {
		return;
	} else {
		WLock l(cs);
		const auto files = tempShares.equal_range(tth);
		for(auto i = files.first; i != files.second; ++i) {
			if(i->second.key == aKey)
				return;
		}
		//didnt exist.. fine, add it.
		tempShares.emplace(tth, TempShareInfo(aKey, filePath, aSize));
	}
}
void ShareManager::removeTempShare(const string& aKey, const TTHValue& tth) {
	WLock l(cs);
	const auto files = tempShares.equal_range(tth);
	for(auto i = files.first; i != files.second; ++i) {
		if(i->second.key == aKey) {
			tempShares.erase(i);
			break;
		}
	}
}

void ShareManager::getRealPaths(const string& path, StringList& ret, ProfileToken aProfile) const {
	if(path.empty())
		throw ShareException("empty virtual path");

	DirectoryList dirs;

	RLock l (cs);
	findVirtuals<ProfileToken>(path, aProfile, dirs);

	if(*(path.end() - 1) == '/') {
		for(const auto& d: dirs)
			ret.push_back(d->getRealPath(true));
	} else { //its a file
		auto fileName = Text::toLower(Util::getFileName(Util::toNmdcFile(path)));
		for(const auto& d: dirs) {
			auto it = d->findFile(fileName);
			if(it != d->files.end()) {
				ret.push_back(it->getRealPath());
				return;
			}
		}
	}
}

bool ShareManager::isRealPathShared(const string& aPath) {
	RLock l (cs);
	auto d = findDirectory(Util::getFilePath(aPath), false, false, true);
	if (d) {
		auto it = d->findFile(Text::toLower(Util::getFileName(aPath)));
		if(it != d->files.end()) {
			return true;
		}
	}

	return false;
}

string ShareManager::validateVirtual(const string& aVirt) const noexcept {
	string tmp = aVirt;
	string::size_type idx = 0;

	while( (idx = tmp.find_first_of("\\/"), idx) != string::npos) {
		tmp[idx] = '_';
	}
	return tmp;
}

void ShareManager::addRoot(const string& aPath, Directory::Ptr& aDir) {
	rootPaths[Text::toLower(aPath)] = aDir;
}

ShareManager::DirMap::const_iterator ShareManager::findRoot(const string& aPath) const {
	return rootPaths.find(Text::toLower(aPath));
}

void ShareManager::loadProfile(SimpleXML& aXml, const string& aName, ProfileToken aToken) {
	ShareProfilePtr sp = ShareProfilePtr(new ShareProfile(aName, aToken));
	shareProfiles.push_back(sp);

	aXml.stepIn();
	while(aXml.findChild("Directory")) {
		string realPath = aXml.getChildData();
		if(realPath.empty()) {
			continue;
		}
		// make sure realPath ends with PATH_SEPARATOR
		if(realPath[realPath.size() - 1] != PATH_SEPARATOR) {
			realPath += PATH_SEPARATOR;
		}

		const string& virtualName = aXml.getChildAttrib("Virtual");
		string vName = validateVirtual(virtualName.empty() ? Util::getLastDir(realPath) : virtualName);

		ProfileDirectory::Ptr pd = nullptr;
		auto p = profileDirs.find(realPath);
		if (p != profileDirs.end()) {
			pd = p->second;
			pd->addRootProfile(virtualName, aToken);
		} else {
			pd = ProfileDirectory::Ptr(new ProfileDirectory(realPath, virtualName, aToken, aXml.getBoolChildAttrib("Incoming")));
			profileDirs[realPath] = pd;
		}

		auto j = findRoot(realPath);
		if (j == rootPaths.end()) {
			auto dir = Directory::create(virtualName, nullptr, 0, pd);
			addRoot(realPath, dir);
		}
	}

	aXml.resetCurrentChild();
	if(aXml.findChild("NoShare")) {
		aXml.stepIn();
		while(aXml.findChild("Directory")) {
			auto path = aXml.getChildData();

			const auto p = profileDirs.find(path);
			if (p != profileDirs.end()) {
				auto pd = p->second;
				pd->addExclude(aToken);
			} else {
				auto pd = ProfileDirectory::Ptr(new ProfileDirectory(path, aToken));
				profileDirs[path] = pd;
			}
		}
		aXml.stepOut();
	}
	aXml.stepOut();
}

void ShareManager::load(SimpleXML& aXml) {
	//WLock l(cs);
	aXml.resetCurrentChild();

	if(aXml.findChild("Share")) {
		string name = aXml.getChildAttrib("Name");
		loadProfile(aXml, !name.empty() ? name : STRING(DEFAULT), SP_DEFAULT);
	}

	aXml.resetCurrentChild();
	while(aXml.findChild("ShareProfile")) {
		auto token = aXml.getIntChildAttrib("Token");
		string name = aXml.getChildAttrib("Name");
		if (token > 10 && !name.empty()) //reserve a few numbers for predefined profiles
			loadProfile(aXml, name, token);
	}
}

ShareProfilePtr ShareManager::getShareProfile(ProfileToken aProfile, bool allowFallback /*false*/) const {
	RLock l (cs);
	const auto p = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
	if (p != shareProfiles.end()) {
		return *p;
	} else if (allowFallback) {
		dcassert(aProfile != SP_DEFAULT);
		return *shareProfiles.begin();
	}
	return nullptr;
}

ShareManager::Directory::Ptr ShareManager::Directory::create(const string& aName, const Ptr& aParent, uint32_t&& aLastWrite, ProfileDirectory::Ptr aRoot /*nullptr*/) {
	auto dir = Ptr(new Directory(aName, aParent, aLastWrite, aRoot));
	if (aParent)
		aParent->directories.insert_sorted(dir);
	return dir;
}

static const string SDIRECTORY = "Directory";
static const string SFILE = "File";
static const string SNAME = "Name";
static const string SSIZE = "Size";
static const string DATE = "Date";
static const string SHARE = "Share";
static const string SVERSION = "Version";

struct ShareLoader : public SimpleXMLReader::CallBack {
	ShareLoader(ShareManager::RefreshInfo& aRefreshInfo) : 
		ri(aRefreshInfo),
		curDirPath(aRefreshInfo.root->getProfileDir()->getPath()),
		cur(aRefreshInfo.root)
	{ }


	void startTag(const string& name, StringPairList& attribs, bool simple) {
		if(name == SDIRECTORY) {
			const string& name = getAttrib(attribs, SNAME, 0);
			const string& date = getAttrib(attribs, DATE, 1);

			if(!name.empty()) {
				curDirPath += name + PATH_SEPARATOR;

				ShareManager::ProfileDirectory::Ptr pd = nullptr;
				if (!ri.subProfiles.empty()) {
					auto i = ri.subProfiles.find(curDirPath);
					if(i != ri.subProfiles.end()) {
						pd = i->second;
					}
				}

				cur = ShareManager::Directory::create(name, cur, Util::toUInt32(date), pd);
				if (pd && pd->isSet(ShareManager::ProfileDirectory::FLAG_ROOT)) {
					ri.rootPathsNew[Text::toLower(curDirPath)] = cur;
				}

				cur->addBloom(*ri.newBloom.get());
				ri.dirNameMapNew.emplace(name, cur);
				lastFileIter = cur->files.begin();
			}

			if(simple) {
				if(cur) {
					cur = cur->getParent();
					if(cur)
						lastFileIter = cur->files.begin();
				}
			}
		} else if(cur && name == SFILE) {
			const string& fname = getAttrib(attribs, SNAME, 0);
			if(fname.empty()) {
				dcdebug("Invalid file found: %s\n", fname.c_str());
				return;
			}

			try {
				HashedFile fi;
				HashManager::getInstance()->getFileInfo(curDirPath + fname, fi);
				lastFileIter = cur->files.emplace_hint(lastFileIter, fname, cur, fi);
				ShareManager::updateIndices(*cur, lastFileIter++, *ri.newBloom.get(), ri.addedSize, ri.tthIndexNew);
			}catch(Exception& e) {
				ri.hashSize += File::getSize(curDirPath + fname);
				dcdebug("Error loading file list %s \n", e.getError().c_str());
			}
		} else if (name == SHARE) {
			int version = Util::toInt(getAttrib(attribs, SVERSION, 0));
			if (version > Util::toInt(SHARE_CACHE_VERSION))
				throw("Newer cache version"); //don't load those...

			cur->addBloom(*ri.newBloom.get());
			cur->setLastWrite(Util::toUInt32(getAttrib(attribs, DATE, 2)));
			lastFileIter = cur->files.begin();
		}
	}
	void endTag(const string& name) {
		if(name == SDIRECTORY) {
			if(cur) {
				curDirPath = Util::getParentDir(curDirPath);
				cur = cur->getParent();
				if(cur)
					lastFileIter = cur->files.begin();
			}
		}
	}

private:
	ShareManager::Directory::File::Set::iterator lastFileIter;
	ShareManager::Directory::Ptr cur;
	ShareManager::RefreshInfo& ri;

	string curDirPath;
};

bool ShareManager::loadCache(function<void (float)> progressF) {
	HashManager::HashPauser pauser;

	Util::migrate(Util::getPath(Util::PATH_SHARECACHE), "ShareCache_*");

	StringList fileList = File::findFiles(Util::getPath(Util::PATH_SHARECACHE), "ShareCache_*");

	if (fileList.empty()) {
		if (Util::fileExists(Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml"))	{
			//delete the old cache
			File::deleteFile(Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml");
		}
		return rootPaths.empty();
	}

	RefreshInfoList ll;
	DirMap parents;

	//get the parent dirs
	for(const auto& p: rootPaths) {
		if (find_if(rootPaths | map_keys, [&p](const string& path) { return AirUtil::isSub(p.second->getProfileDir()->getPath(), path); } ).base() == rootPaths.end())
			parents.emplace(p);
	}

	//create the info dirs
	for(const auto& p: fileList) {
		if (Util::getFileExt(p) == ".xml") {
			auto rp = find_if(parents | map_values, [&p](const Directory::Ptr& aDir) { return stricmp(aDir->getProfileDir()->getCachePath(), p) == 0; });
			if (rp.base() != parents.end()) { //make sure that subdirs are never listed here...
				ll.emplace_back(rp.base()->first, *rp, p);
				continue;
			}
		}

		//no use for extra files
		File::deleteFile(p);
	}


	//load the XML files
	atomic<long> loaded = 0;
	const auto dirCount = ll.size();
	bool hasFailed = false;

	concurrency::parallel_for_each(ll.begin(), ll.end(), [&](RefreshInfo& ri) {
		try {
			ShareLoader loader(ri);

			dcpp::File f(ri.loaderPath, dcpp::File::READ, dcpp::File::OPEN, false);

			SimpleXMLReader(&loader).parse(f);
		} catch(SimpleXMLException& e) {
			LogManager::getInstance()->message("Error loading " + ri.loaderPath + ": "+  e.getError(), LogManager::LOG_ERROR);
			hasFailed = true;
			File::deleteFile(ri.loaderPath);
		} catch(...) {
			hasFailed = true;
			File::deleteFile(ri.loaderPath);
		}

		if(progressF) {
			progressF(static_cast<float>(loaded++) / static_cast<float>(dirCount));
		}
	});

	if (hasFailed)
		return false;

	//apply the changes
	int64_t hashSize = 0;

	DirMap newRoots;
	mergeRefreshChanges(ll, dirNameMap, newRoots, tthIndex, hashSize, sharedSize);

	StringList refreshPaths;
	for (auto& i: parents) {
		auto p = newRoots.find(i.first);
		if (p != newRoots.end()) {
			rootPaths[i.first] = p->second;
		} else {
			//add for refresh
			refreshPaths.push_back(i.second->getProfileDir()->getPath());
		}
	}

	addTask(REFRESH_DIR, refreshPaths, TYPE_MANUAL, Util::emptyString);

	if (hashSize > 0) {
		LogManager::getInstance()->message(STRING_F(FILES_ADDED_FOR_HASH_STARTUP, Util::formatBytes(hashSize)), LogManager::LOG_INFO);
	}

	return true;
}

void ShareManager::save(SimpleXML& aXml) {
	RLock l(cs);
	for(const auto& sp: shareProfiles) {
		if (sp->getToken() == SP_HIDDEN) {
			continue;
		}

		aXml.addTag(sp->getToken() == SP_DEFAULT ? "Share" : "ShareProfile");
		aXml.addChildAttrib("Token", sp->getToken());
		aXml.addChildAttrib("Name", sp->getPlainName());
		aXml.stepIn();

		for(const auto& d: rootPaths | map_values) {
			if (!d->getProfileDir()->hasRootProfile(sp->getToken()))
				continue;
			aXml.addTag("Directory", d->getRealPath(false));
			aXml.addChildAttrib("Virtual", d->getProfileDir()->getName(sp->getToken()));
			//if (p->second->getRoot()->hasFlag(ProfileDirectory::FLAG_INCOMING))
			aXml.addChildAttrib("Incoming", d->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING));
		}

		aXml.addTag("NoShare");
		aXml.stepIn();
		for(const auto& pd: profileDirs | map_values) {
			if (pd->isExcluded(sp->getToken()))
				aXml.addTag("Directory", pd->getPath());
		}
		aXml.stepOut();
		aXml.stepOut();
	}
}

void ShareManager::validatePath(const string& realPath, const string& virtualName) {
	if(realPath.empty() || virtualName.empty()) {
		throw ShareException(STRING(NO_DIRECTORY_SPECIFIED));
	}

	if (!checkHidden(realPath)) {
		throw ShareException(STRING(DIRECTORY_IS_HIDDEN));
	}

	if(stricmp(SETTING(TEMP_DOWNLOAD_DIRECTORY), realPath) == 0) {
		throw ShareException(STRING(DONT_SHARE_TEMP_DIRECTORY));
	}

#ifdef _WIN32
	//need to throw here, so throw the error and dont use airutil
	TCHAR path[MAX_PATH];
	::SHGetFolderPath(NULL, CSIDL_WINDOWS, NULL, SHGFP_TYPE_CURRENT, path);
	string windows = Text::fromT((tstring)path) + PATH_SEPARATOR;
	// don't share Windows directory
	if(strnicmp(realPath, windows, windows.length()) == 0) {
		throw ShareException(STRING_F(CHECK_FORBIDDEN, realPath));
	}
#endif
}

void ShareManager::getByVirtual(const string& virtualName, ProfileToken aProfile, DirectoryList& dirs) const noexcept {
	for(const auto& d: rootPaths | map_values) {
		if(d->getProfileDir()->hasRootProfile(aProfile) && stricmp(d->getProfileDir()->getName(aProfile), virtualName) == 0) {
			dirs.push_back(d);
		}
	}
}

void ShareManager::getByVirtual(const string& virtualName, const ProfileTokenSet& aProfiles, DirectoryList& dirs) const noexcept {
	for(const auto& d: rootPaths | map_values) {
		for(auto& k: d->getProfileDir()->getRootProfiles()) {
			if(aProfiles.find(k.first) != aProfiles.end() && stricmp(k.second, virtualName) == 0) {
				dirs.push_back(d);
			}
		}
	}
}

int64_t ShareManager::getShareSize(const string& realPath, ProfileToken aProfile) const noexcept {
	RLock l(cs);
	const auto j = findRoot(realPath);
	if(j != rootPaths.end()) {
		return j->second->getSize(aProfile);
	}
	return -1;

}

void ShareManager::Directory::getProfileInfo(ProfileToken aProfile, int64_t& totalSize, size_t& filesCount) const {
	totalSize += size;
	filesCount += files.size();

	for(const auto& d: directories) {
		if (d->isLevelExcluded(aProfile))
			continue;
		d->getProfileInfo(aProfile, totalSize, filesCount);
	}
}

void ShareManager::getProfileInfo(ProfileToken aProfile, int64_t& size, size_t& files) const {
	RLock l(cs);
	//auto p = find_if(shareProfiles, [](const ShareProfilePtr& sp) { return sp->getToken() == aProfile; });
	auto sp = getShareProfile(aProfile);
	if (sp) {
		if (sp->getProfileInfoDirty()) {
			for(const auto& d: rootPaths | map_values) {
				if(d->getProfileDir()->hasRootProfile(aProfile)) {
					d->getProfileInfo(aProfile, size, files);
				}
			}
			sp->setSharedFiles(files);
			sp->setShareSize(size);
			sp->setProfileInfoDirty(false);
		}

		size = sp->getShareSize();
		files = sp->getSharedFiles();
	}
}

int64_t ShareManager::getTotalShareSize(ProfileToken aProfile) const noexcept {
	int64_t ret = 0;

	RLock l(cs);
	for(const auto& d: rootPaths | map_values) {
		if(d->getProfileDir()->hasRootProfile(aProfile)) {
			ret += d->getSize(aProfile);
		}
	}
	return ret;
}

bool ShareManager::isDirShared(const string& aDir) const {
	RLock l (cs);
	return getDirByName(aDir) ? true : false;
}

uint8_t ShareManager::isDirShared(const string& aDir, int64_t aSize) const {
	RLock l (cs);
	auto dir = getDirByName(aDir);
	if (!dir)
		return 0;
	return dir->getTotalSize() == aSize ? 2 : 1;
}

tstring ShareManager::getDirPath(const string& aDir) {
	RLock l (cs);
	auto dir = getDirByName(aDir);
	if (!dir)
		return Util::emptyStringT;

	return Text::toT(dir->getRealPath(true));
}

/* This isn't optimized for matching subdirs but there shouldn't be need to match many of those 
   at once (especially not in filelists, but there might be some when searching though) */
ShareManager::Directory::Ptr ShareManager::getDirByName(const string& aDir) const {
	if (aDir.size() < 3)
		return nullptr;

	//get the last directory, we might need the position later with subdirs
	string dir = aDir;
	if (dir[dir.length()-1] == PATH_SEPARATOR)
		dir.erase(aDir.size()-1, aDir.size());
	auto pos = dir.rfind(PATH_SEPARATOR);
	if (pos != string::npos)
		dir = dir.substr(pos+1);

	const auto directories = dirNameMap.equal_range(dir);
	if (directories.first == directories.second)
		return nullptr;

	//check the parents for dirs like CD1 to prevent false matches
	if (boost::regex_match(dir, AirUtil::subDirRegPlain) && pos != string::npos) {
		string::size_type i, j;
		dir = PATH_SEPARATOR + aDir;

		for(auto s = directories.first; s != directories.second; ++s) {
			//start matching from the parent dir, as we know the last one already
			i = pos;
			Directory::Ptr cur = s->second->getParent();

			for(;;) {
				if (!cur)
					break;

				j = dir.find_last_of(PATH_SEPARATOR, i);
				if(j == string::npos)
					break;

				auto remoteDir = dir.substr(j+1, i-j);
				if(stricmp(cur->getRealName(), remoteDir) == 0) {
					if (!boost::regex_match(remoteDir, AirUtil::subDirRegPlain)) { //another subdir? don't break in that case
						return s->second;
					}
				} else {
					//this is something different... continue to next match
					break;
				}
				cur = cur->getParent();
				i = j - 1;
			}
		}
	} else {
		return directories.first->second;
	}

	return nullptr;
}

bool ShareManager::isFileShared(const TTHValue& aTTH, const string& fileName) const {
	RLock l (cs);
	/*const auto files = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
	for(auto i = files.first; i != files.second; ++i) {
		if(stricmp(fileName.c_str(), i->second->getName().c_str()) == 0) {
			return true;
		}
	}
	return false;*/
	return tthIndex.find(const_cast<TTHValue*>(&aTTH)) != tthIndex.end();
}

bool ShareManager::isFileShared(const TTHValue& aTTH, const string& fileName, ProfileToken aProfile) const {
	RLock l (cs);
	const auto files = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
	for(auto i = files.first; i != files.second; ++i) {
		if((stricmp(fileName.c_str(), i->second->getName().c_str()) == 0) && i->second->getParent()->hasProfile(aProfile)) {
			return true;
		}
	}
	return false;
}

bool ShareManager::isFileShared(const string& aFileName, int64_t aSize) const {
	RLock l (cs);
	for(const auto f: tthIndex | map_values) {
		if(stricmp(aFileName.c_str(), f->getName().c_str()) == 0 && f->getSize() == aSize) {
			return true;
		}
	}

	return false;
}

void ShareManager::buildTree(const string& aPath, const Directory::Ptr& aDir, bool checkQueued, const ProfileDirMap& aSubRoots, DirMultiMap& aDirs, DirMap& newShares, int64_t& hashSize, int64_t& addedSize, HashFileMap& tthIndexNew, ShareBloom& aBloom) {
	auto lastFileIter = aDir->files.begin();
	FileFindIter end;

#ifdef _WIN32
	for(FileFindIter i(aPath + "*"); i != end && !aShutdown; ++i) {
#else
	//the fileiter just searches directorys for now, not sure if more 
	//will be needed later
	//for(FileFindIter i(aName + "*"); i != end; ++i) {
	for(FileFindIter i(aName); i != end; ++i) {
#endif
		string name = i->getFileName();
		if(name.empty()) {
			LogManager::getInstance()->message("Invalid file name found while hashing folder "+ aPath + ".", LogManager::LOG_WARNING);
			return;
		}

		if(!SETTING(SHARE_HIDDEN) && i->isHidden())
			continue;

		if(i->isDirectory()) {
			string curPath = aPath + name + PATH_SEPARATOR;

			{
				RLock l (dirNames);
				if (!checkSharedName(curPath, true)) {
					continue;
				}

				//check queue so we dont add incomplete stuff to share.
				if(checkQueued && std::binary_search(bundleDirs.begin(), bundleDirs.end(), Text::toLower(curPath))) {
					continue;
				}
			}

			ProfileDirectory::Ptr profileDir = nullptr;
			if (!aSubRoots.empty()) {
				//add excluded dirs and sub roots in our new maps
				auto p = aSubRoots.find(curPath);
				if (p != aSubRoots.end()) {
					if (p->second->isSet(ProfileDirectory::FLAG_ROOT) || p->second->isSet(ProfileDirectory::FLAG_EXCLUDE_PROFILE))
						profileDir = p->second;
					if (p->second->isSet(ProfileDirectory::FLAG_EXCLUDE_TOTAL))
						continue;
				}
			}

			auto dir = Directory::create(name, aDir, i->getLastWriteTime(), profileDir);

			buildTree(curPath, dir, checkQueued, aSubRoots, aDirs, newShares, hashSize, addedSize, tthIndexNew, aBloom);

			//roots will always be added
			if (profileDir && profileDir->isSet(ProfileDirectory::FLAG_ROOT)) {
				newShares[Text::toLower(curPath)] = dir;
			} else if (SETTING(SKIP_EMPTY_DIRS_SHARE) && dir->directories.empty() && dir->files.empty()) {
				//remove it
				auto p = aDir->directories.find(dir->getRealNameLower());
				aDir->directories.erase(p);
				continue;
			}

			aDirs.emplace(name, dir);
			dir->addBloom(aBloom);
		} else {
			// Not a directory, assume it's a file...
			string path = aPath + name;
			int64_t size = i->getSize();

			if (!checkSharedName(path, false, true, size)) {
				continue;
			}

			try {
				auto lastWrite = i->getLastWriteTime();
				TTHValue tth;
				if(HashManager::getInstance()->checkTTH(path, size, lastWrite, tth)) {
					HashedFile fi(tth, lastWrite, size);
					lastFileIter = aDir->files.emplace_hint(lastFileIter, name, aDir, fi);
					updateIndices(*aDir, lastFileIter++, aBloom, addedSize, tthIndexNew);
				} else {
					hashSize += size;
				}
			} catch(const HashException&) {
			}
		}
	}
}

bool ShareManager::checkHidden(const string& aName) const {
	if (SETTING(SHARE_HIDDEN))
		return true;

	auto ff = FileFindIter(aName.substr(0, aName.size() - 1));
	if (ff != FileFindIter()) {
		return !ff->isHidden();
	}

	return true;
}

uint32_t ShareManager::findLastWrite(const string& aName) const {
	auto ff = FileFindIter(aName.substr(0, aName.size() - 1));

	if (ff != FileFindIter()) {
		return ff->getLastWriteTime();
	}

	return 0;
}

void ShareManager::Directory::addBloom(ShareBloom& aBloom) const {
	if (profileDir && profileDir->hasRoots()) {
		for(const auto& vName: profileDir->getRootProfiles() | map_values) {
			aBloom.add(Text::toLower(vName));
		}
	} else {
		aBloom.add(realNameLower);
	}
}

void ShareManager::updateIndices(Directory::Ptr& dir, ShareBloom& aBloom, int64_t& sharedSize, HashFileMap& tthIndex, DirMultiMap& aDirNames) {
	// add to bloom
	dir->addBloom(aBloom);
	aDirNames.emplace(dir->getRealName(), dir);

	// update all sub items
	for(auto d: dir->directories) {
		updateIndices(d, aBloom, sharedSize, tthIndex, aDirNames);
	}

	for(auto i = dir->files.begin(); i != dir->files.end(); ) {
		updateIndices(*dir, i++, aBloom, sharedSize, tthIndex);
	}
}

void ShareManager::updateIndices(Directory& dir, const Directory::File::Set::iterator& i, ShareBloom& aBloom, int64_t& sharedSize, HashFileMap& tthIndex) {
	const Directory::File& f = *i;
	dir.size += f.getSize();
	sharedSize += f.getSize();

	dir.addType(getType(f.getName()));

	tthIndex.emplace(const_cast<TTHValue*>(&f.getTTH()), i);
	aBloom.add(f.getNameLower());
}

int ShareManager::refresh(const string& aDir){
	string path = aDir;

	if(path[ path.length() -1 ] != PATH_SEPARATOR)
		path += PATH_SEPARATOR;

	StringList refreshPaths;
	string displayName;

	{
		RLock l(cs);
		const auto i = findRoot(path);
		if(i == rootPaths.end()) {
			//check if it's a virtual path

			OrderedStringSet vNames;
			for(const auto& d: rootPaths | map_values) {
				// compare all virtual names for real paths
				for(const auto& vName: d->getProfileDir()->getRootProfiles() | map_values) {
					if(stricmp(vName, aDir ) == 0) {
						refreshPaths.push_back(d->getRealPath(false));
						vNames.insert(vName);
					}
				}
			}

			sort(refreshPaths.begin(), refreshPaths.end());
			refreshPaths.erase(unique(refreshPaths.begin(), refreshPaths.end()), refreshPaths.end());

			if (!vNames.empty())
				displayName = Util::toString(vNames);
		} else {
			refreshPaths.push_back(path);
		}
	}

	return addTask(REFRESH_DIR, refreshPaths, TYPE_MANUAL, displayName);
}


int ShareManager::refresh(bool incoming, RefreshType aType, function<void (float)> progressF /*nullptr*/) {
	StringList dirs;

	{
		RLock l (cs);
		for(const auto& d: rootPaths | map_values | filtered(Directory::IsParent())) {
			if (incoming && !d->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING))
				continue;
			dirs.push_back(d->getProfileDir()->getPath());
		}
	}

	return addTask(incoming ? REFRESH_INCOMING : REFRESH_ALL, dirs, aType, Util::emptyString, progressF);
}

struct ShareTask : public Task {
	ShareTask(const StringList& aDirs, const string& aDisplayName, ShareManager::RefreshType aRefreshType) : dirs(aDirs), displayName(aDisplayName), type(aRefreshType) { }
	StringList dirs;
	string displayName;
	ShareManager::RefreshType type;
};

int ShareManager::addTask(uint8_t aTask, StringList& dirs, RefreshType aRefreshType, const string& displayName /*Util::emptyString*/, function<void (float)> progressF /*nullptr*/) noexcept {
	if (dirs.empty()) {
		return REFRESH_PATH_NOT_FOUND;
	}

	{
		Lock l(tasks.cs);
		auto& tq = tasks.getTasks();
		if (aTask == REFRESH_ALL) {
			//don't queue multiple full refreshes
			const auto p = find_if(tq, [](const TaskQueue::UniqueTaskPair& tp) { return tp.first == REFRESH_ALL; });
			if (p != tq.end())
				return REFRESH_ALREADY_QUEUED;
		} else {
			//remove directories that have already been queued for refreshing
			for(const auto& i: tq) {
				auto t = static_cast<ShareTask*>(i.second.get());
				dirs.erase(boost::remove_if(dirs, [t](const string& s) { return boost::find(t->dirs, s) != t->dirs.end(); }), dirs.end());
			}
		}
	}

	if (dirs.empty()) {
		return REFRESH_ALREADY_QUEUED;
	}

	tasks.add(aTask, unique_ptr<Task>(new ShareTask(dirs, displayName, aRefreshType)));

	if(refreshing.test_and_set()) {
		string msg;
		switch (aTask) {
			case(REFRESH_ALL):
				msg = STRING(REFRESH_QUEUED);
				break;
			case(REFRESH_DIR):
				if (!displayName.empty()) {
					msg = STRING_F(VIRTUAL_REFRESH_QUEUED, displayName);
				} else if (dirs.size() == 1) {
					msg = STRING_F(DIRECTORY_REFRESH_QUEUED, *dirs.begin());
				}
				break;
			case(ADD_DIR):
				if (dirs.size() == 1) {
					msg = STRING_F(ADD_DIRECTORY_QUEUED, *dirs.begin());
				} else {
					msg = STRING_F(ADD_DIRECTORIES_QUEUED, dirs.size());
				}
				break;
			case(REFRESH_INCOMING):
				msg = STRING(INCOMING_REFRESH_QUEUED);
				break;
		};

		if (!msg.empty()) {
			LogManager::getInstance()->message(msg, LogManager::LOG_INFO);
		}
		return REFRESH_IN_PROGRESS;
	}

	if(aRefreshType == TYPE_STARTUP) { 
		runTasks(progressF);
	} else {
		join();
		try {
			start();
			setThreadPriority(Thread::NORMAL);
		} catch(const ThreadException& e) {
			LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_FAILED) + " " + e.getError(), LogManager::LOG_WARNING);
			refreshing.clear();
		}
	}

	return REFRESH_STARTED;
}

void ShareManager::getParentPaths(StringList& aDirs) const {
	//removes subroots from shared dirs
	RLock l (cs);
	for(const auto& dp: rootPaths) {
		if (find_if(rootPaths | map_keys, [&dp](const string& aPath) { return AirUtil::isSub(dp.first, aPath); }).base() == rootPaths.end())
			aDirs.push_back(dp.second->getProfileDir()->getPath());
	}
}

ShareManager::ProfileDirMap ShareManager::getSubProfileDirs(const string& aPath) {
	ProfileDirMap aRoots;
	for(const auto& i: profileDirs) {
		if (AirUtil::isSub(i.first, aPath)) {
			aRoots[i.second->getPath()] = i.second;
		}
	}

	return aRoots;
}

void ShareManager::addProfiles(const ShareProfile::set& aProfiles) {
	WLock l (cs);
	for(auto& i: aProfiles) {
		shareProfiles.insert(shareProfiles.end()-1, i);
	}
}

void ShareManager::removeProfiles(ProfileTokenList aProfiles) {
	WLock l (cs);
	for(auto& aProfile: aProfiles)
		shareProfiles.erase(remove(shareProfiles.begin(), shareProfiles.end(), aProfile), shareProfiles.end()); 
}

void ShareManager::addDirectories(const ShareDirInfo::List& aNewDirs) {
	StringList add, refresh;
	ProfileTokenSet dirtyProfiles;

	{
		WLock l (cs);
		for(const auto& d: aNewDirs) {
			const auto& sdiPath = d->path;
			auto i = findRoot(sdiPath);
			if (i != rootPaths.end()) {
				// Trying to share an already shared root
				i->second->getProfileDir()->addRootProfile(d->vname, d->profile);
				dirtyProfiles.insert(d->profile);
			} else {
				auto p = find_if(rootPaths | map_keys, [&sdiPath](const string& path) { return AirUtil::isSub(sdiPath, path); });
				if (p.base() != rootPaths.end()) {
					// It's a subdir
					auto dir = findDirectory(sdiPath, false, false);
					if (dir) {
						if (dir->getProfileDir()) {
							//an existing subroot exists
							dcassert(dir->getProfileDir()->hasExcludes());
							dir->getProfileDir()->addRootProfile(d->vname, d->profile);
						} else {
							auto root = ProfileDirectory::Ptr(new ProfileDirectory(sdiPath, d->vname, d->profile, d->incoming));
							dir->setProfileDir(root);
							profileDirs[sdiPath] = root;
						}
						addRoot(sdiPath, dir);
						dirtyProfiles.insert(d->profile);
					} else {
						//this is probably in an excluded dirs of an existing root, add it
						dir = findDirectory(sdiPath, true, true, false);
						if (dir) {
							auto root = ProfileDirectory::Ptr(new ProfileDirectory(sdiPath, d->vname, d->profile, d->incoming));
							dir->setProfileDir(root);
							profileDirs[sdiPath] = root;
							addRoot(sdiPath, dir);
							if (find_if(aNewDirs, [p](const ShareDirInfoPtr& aSDI) { return stricmp(aSDI->path, *p) == 0; }) == aNewDirs.end())
								refresh.push_back(*p); //refresh the top directory unless it's also added now.....
						}
					}
				} else {
					// It's a new parent, will be handled in the task thread
					auto root = ProfileDirectory::Ptr(new ProfileDirectory(sdiPath, d->vname, d->profile, d->incoming));
					//root->setFlag(ProfileDirectory::FLAG_ADD);
					Directory::Ptr dp = Directory::create(Util::getLastDir(sdiPath), nullptr, findLastWrite(sdiPath), root);
					addRoot(sdiPath, dp);
					profileDirs[sdiPath] = root;
					dirNameMap.emplace(dp->getRealName(), dp);
					add.push_back(sdiPath);
				}
			}
		}
	}

	rebuildTotalExcludes();
	if (!refresh.empty())
		addTask(REFRESH_DIR, refresh, TYPE_MANUAL);

	if (add.empty()) {
		//we are only modifying existing trees
		setProfilesDirty(dirtyProfiles);
		return;
	}

	addTask(ADD_DIR, add, TYPE_MANUAL);
}

void ShareManager::removeDirectories(const ShareDirInfo::List& aRemoveDirs) {
	ProfileTokenSet dirtyProfiles;
	StringList stopHashing;

	{
		WLock l (cs);
		for(const auto& rd: aRemoveDirs) {
			auto k = findRoot(rd->path);
			if (k != rootPaths.end()) {
				dirtyProfiles.insert(rd->profile);

				auto sd = k->second;
				if (sd->getProfileDir()->removeRootProfile(rd->profile)) {
					//can we remove the profile dir?
					if (!sd->getProfileDir()->hasExcludes()) {
						profileDirs.erase(rd->path);
					}

					stopHashing.push_back(rd->path);
					rootPaths.erase(k);
					if (sd->getParent()) {
						//the content still stays shared.. just null the profile
						sd->setProfileDir(nullptr);
						continue;
					}

					cleanIndices(*sd);
					File::deleteFile(sd->getProfileDir()->getCachePath());

					//no parent directories, get all child roots for this
					DirectoryList subDirs;
					for(auto& sdp: rootPaths) {
						if(AirUtil::isSub(sdp.first, rd->path)) {
							subDirs.push_back(sdp.second);
						}
					}

					//check the folder levels
					size_t minLen = UNC_MAX_PATH;
					for (auto& d: subDirs) {
						minLen = min(Util::getParentDir(d->getProfileDir()->getPath()).length(), minLen);
					}

					//update our new parents
					for (auto& d: subDirs) {
						if (Util::getParentDir(d->getProfileDir()->getPath()).length() == minLen) {
							d->setParent(nullptr);
							d->getProfileDir()->bloom.reset(new ShareBloom(1<<20));
							d->getProfileDir()->setCacheDirty(true);
							updateIndices(d, *d->getProfileDir()->bloom.get(), sharedSize, tthIndex, dirNameMap);
						}
					}
				}
			}
		}
	}

	if (stopHashing.size() == 1)
		LogManager::getInstance()->message(STRING_F(SHARED_DIR_REMOVED, stopHashing.front()), LogManager::LOG_INFO);
	else if (!stopHashing.empty())
		LogManager::getInstance()->message(STRING_F(X_SHARED_DIRS_REMOVED, stopHashing.size()), LogManager::LOG_INFO);

	for(const auto& p: stopHashing) {
		HashManager::getInstance()->stopHashing(p);
	}

	rebuildTotalExcludes();
	setProfilesDirty(dirtyProfiles);
}

void ShareManager::changeDirectories(const ShareDirInfo::List& changedDirs)  {
	//updates the incoming status and the virtual name (called from GUI)

	ProfileTokenSet dirtyProfiles;

	{
		WLock l(cs);
		for(const auto& cd: changedDirs) {
			string vName = validateVirtual(cd->vname);
			dirtyProfiles.insert(cd->profile);

			auto p = findRoot(cd->path);
			if (p != rootPaths.end()) {
				p->second->getProfileDir()->addRootProfile(vName, cd->profile); //renames it really
				auto pd = p->second->getProfileDir();
				cd->incoming ? p->second->getProfileDir()->setFlag(ProfileDirectory::FLAG_INCOMING) : p->second->getProfileDir()->unsetFlag(ProfileDirectory::FLAG_INCOMING);
			}
		}
	}

	setProfilesDirty(dirtyProfiles);
}

void ShareManager::reportTaskStatus(uint8_t aTask, const StringList& directories, bool finished, int64_t aHashSize, const string& displayName, RefreshType aRefreshType) {
	string msg;
	switch (aTask) {
		case(REFRESH_ALL):
			msg = finished ? STRING(FILE_LIST_REFRESH_FINISHED) : STRING(FILE_LIST_REFRESH_INITIATED);
			break;
		case(REFRESH_DIR):
			if (!displayName.empty()) {
				msg = finished ? STRING_F(VIRTUAL_DIRECTORY_REFRESHED, displayName) : STRING_F(FILE_LIST_REFRESH_INITIATED_VPATH, displayName);
			} else if (directories.size() == 1) {
				msg = finished ? STRING_F(DIRECTORY_REFRESHED, *directories.begin()) : STRING_F(FILE_LIST_REFRESH_INITIATED_RPATH, *directories.begin());
			} else {
				msg = finished ? STRING_F(X_DIRECTORIES_REFRESHED, directories.size()) : STRING_F(FILE_LIST_REFRESH_INITIATED_X_PATHS, directories.size());
			}
			break;
		case(ADD_DIR):
			if (directories.size() == 1) {
				msg = finished ? STRING_F(DIRECTORY_ADDED, *directories.begin()) : STRING_F(ADDING_SHARED_DIR, *directories.begin());
			} else {
				msg = finished ? STRING_F(ADDING_X_SHARED_DIRS, directories.size()) : STRING_F(DIRECTORIES_ADDED, directories.size());
			}
			break;
		case(REFRESH_INCOMING):
			msg = finished ? STRING(INCOMING_REFRESHED) : STRING(FILE_LIST_REFRESH_INITIATED_INCOMING);
			break;
	};

	if (!msg.empty()) {
		if (aHashSize > 0) {
			msg += " " + STRING_F(FILES_ADDED_FOR_HASH, Util::formatBytes(aHashSize));
		} else if (aRefreshType == TYPE_SCHEDULED && !SETTING(LOG_SCHEDULED_REFRESHES)) {
			return;
		}
		LogManager::getInstance()->message(msg, LogManager::LOG_INFO);
	}
}

int ShareManager::run() {
	runTasks();
	return 0;
}

ShareManager::RefreshInfo::RefreshInfo(RefreshInfo&& rhs) {
	newBloom.swap(rhs.newBloom);

	oldRoot.swap(rhs.oldRoot);
	root.swap(rhs.root);

	hashSize = rhs.hashSize;
	addedSize = rhs.addedSize;

	subProfiles.swap(rhs.subProfiles);

	dirNameMapNew.swap(rhs.dirNameMapNew);
	tthIndexNew.swap(rhs.tthIndexNew);
	rootPathsNew.swap(rhs.rootPathsNew);

	loaderPath.swap(rhs.loaderPath);
}

ShareManager::RefreshInfo::~RefreshInfo() {

}

ShareManager::RefreshInfo::RefreshInfo(const string& aPath, Directory::Ptr aOldRoot, const string& aLoaderPath /*Util::emptyString*/) : loaderPath(aLoaderPath), newBloom(new ShareBloom(1<<20)), oldRoot(aOldRoot), addedSize(0), hashSize(0) {
	subProfiles = std::move(getInstance()->getSubProfileDirs(aPath));

	//create the new root
	root = Directory::create(Util::getLastDir(aPath), nullptr, getInstance()->findLastWrite(aPath), aOldRoot->getProfileDir());
	dirNameMapNew.emplace(Util::getLastDir(aPath), root);
	rootPathsNew[Text::toLower(aPath)] = root;
}

void ShareManager::runTasks(function<void (float)> progressF /*nullptr*/) {
	HashManager::HashPauser pauser;
	refreshRunning = true;

	for (;;) {
		TaskQueue::TaskPair t;
		if (!tasks.getFront(t))
			break;
		ScopedFunctor([this] { tasks.pop_front(); });


		auto task = static_cast<ShareTask*>(t.second);

		RefreshInfoList refreshDirs;
		ProfileTokenSet dirtyProfiles;

		//find excluded dirs and sub-roots for each directory being refreshed (they will be passed on to buildTree for matching)
		{
			RLock l (cs);
			for(auto& i: task->dirs) {
				auto d = findRoot(i);
				if (d != rootPaths.end()) {
					refreshDirs.emplace_back(i, d->second);
					d->second->copyRootProfiles(dirtyProfiles, false);
				}
			}
		}

		reportTaskStatus(t.first, task->dirs, false, 0, task->displayName, task->type);
		if (t.first == REFRESH_INCOMING) {
			lastIncomingUpdate = GET_TICK();
		} else if (t.first == REFRESH_ALL) {
			lastFullUpdate = GET_TICK();
			lastIncomingUpdate = GET_TICK();
		}

		//get unfinished directories
		bundleDirs.clear();
		QueueManager::getInstance()->getForbiddenPaths(bundleDirs, task->dirs);


		//build the new tree
		atomic<long> progressCounter = 0;
		const size_t dirCount = refreshDirs.size();

		concurrency::parallel_for_each(refreshDirs.begin(), refreshDirs.end(), [&](RefreshInfo& ri) {
			if (checkHidden(ri.root->getProfileDir()->getPath())) {
				buildTree(ri.root->getProfileDir()->getPath(), ri.root, true, ri.subProfiles, ri.dirNameMapNew, ri.rootPathsNew, ri.hashSize, ri.addedSize, ri.tthIndexNew, *ri.newBloom.get());
			}

			if(progressF) {
				progressF(static_cast<float>(progressCounter++) / static_cast<float>(dirCount));
			}

			ri.root->getProfileDir()->setCacheDirty(true);
		});

		if (aShutdown)
			goto end;

		int64_t totalHash=0;

		//append the changes
		{		
			WLock l(cs);
			if(t.first != REFRESH_ALL) {
				for(auto& ri: refreshDirs) {
					//recursively remove the content of this dir from TTHIndex and dir name list
					cleanIndices(*ri.oldRoot);

					//clear this path and its children from root paths
					for(auto i = rootPaths.begin(); i != rootPaths.end(); ) {
						if (AirUtil::isParentOrExact(ri.root->getProfileDir()->getPath(), i->first)) {
							if (t.first == ADD_DIR && AirUtil::isSub(i->first, ri.root->getProfileDir()->getPath()) && !i->second->getParent()) {
								//in case we are adding a new parent
								File::deleteFile(i->second->getProfileDir()->getCachePath());
								cleanIndices(*i->second);
								i->second->getProfileDir()->bloom.reset(nullptr);
							}

							i = rootPaths.erase(i);
						} else {
							i++;
						}
					}
				}

				mergeRefreshChanges(refreshDirs, dirNameMap, rootPaths, tthIndex, totalHash, sharedSize);
			} else {
				int64_t totalAdded=0;
				DirMultiMap newDirNames;
				DirMap newRoots;
				HashFileMap newTTHs;

				mergeRefreshChanges(refreshDirs, newDirNames, newRoots, newTTHs, totalHash, totalAdded);

				rootPaths.swap(newRoots);
				dirNameMap.swap(newDirNames);
				tthIndex.swap(newTTHs);

				sharedSize = totalAdded;
			}
		}

		setProfilesDirty(dirtyProfiles);
			
		ClientManager::getInstance()->infoUpdated();

		reportTaskStatus(t.first, task->dirs, true, totalHash, task->displayName, task->type);
	}

end:
	{
		WLock l (dirNames);
		bundleDirs.clear();
	}
	refreshRunning = false;
	refreshing.clear();
}

void ShareManager::mergeRefreshChanges(RefreshInfoList& aList, DirMultiMap& aDirNameMap, DirMap& aRootPaths, HashFileMap& aTTHIndex, int64_t& totalHash, int64_t& totalAdded) {
	for (auto& ri: aList) {
		aDirNameMap.insert(ri.dirNameMapNew.begin(), ri.dirNameMapNew.end());
		aRootPaths.insert(ri.rootPathsNew.begin(), ri.rootPathsNew.end());
		aTTHIndex.insert(ri.tthIndexNew.begin(), ri.tthIndexNew.end());

		ri.root->getProfileDir()->bloom.reset(ri.newBloom.release());

		totalHash += ri.hashSize;
		totalAdded += ri.addedSize;
	}
}

void ShareManager::on(TimerManagerListener::Minute, uint64_t tick) noexcept {

	if(lastSave == 0 || lastSave + 15*60*1000 <= tick) {
		saveXmlList();
	}

	if(SETTING(AUTO_REFRESH_TIME) > 0 && lastFullUpdate + SETTING(AUTO_REFRESH_TIME) * 60 * 1000 <= tick) {
		lastIncomingUpdate = tick;
		lastFullUpdate = tick;
		refresh(false, TYPE_SCHEDULED);
	} else if(SETTING(INCOMING_REFRESH_TIME) > 0 && lastIncomingUpdate + SETTING(INCOMING_REFRESH_TIME) * 60 * 1000 <= tick) {
		lastIncomingUpdate = tick;
		refresh(true, TYPE_SCHEDULED);
	}
}

void ShareManager::getShares(ShareDirInfo::Map& aDirs) {
	RLock l (cs);
	for(const auto& d: rootPaths | map_values) {
		const auto& profiles = d->getProfileDir()->getRootProfiles();
		for(const auto& pd: profiles) {
			auto sdi = new ShareDirInfo(pd.second, pd.first, d->getRealPath(false), d->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING));
			sdi->size = d->getSize(pd.first);
			aDirs[pd.first].push_back(sdi);
		}
	}

}

/*size_t ShareManager::getSharedFiles(ProfileToken aProfile) const noexcept {
	return boost::count_if(tthIndex | map_values, [aProfile](Directory::File::Set::const_iterator f) { return f->getParent()->hasProfile(aProfile); });
}*/
		
void ShareManager::getBloom(HashBloom& bloom) const {
	RLock l(cs);
	for(const auto tth: tthIndex | map_keys)
		bloom.add(*tth);

	for(const auto& tth: tempShares | map_keys)
		bloom.add(tth);
}

string ShareManager::generateOwnList(ProfileToken aProfile) {
	FileList* fl = generateXmlList(aProfile, true);
	return fl->getFileName();
}


//forwards the calls to createFileList for creating the filelist that was reguested.
FileList* ShareManager::generateXmlList(ProfileToken aProfile, bool forced /*false*/) {
	FileList* fl = nullptr;

	{
		WLock l(cs);
		const auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
		if(i == shareProfiles.end()) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		fl = (*i)->getProfileList() ? (*i)->getProfileList() : (*i)->generateProfileList();
	}


	if(fl->generateNew(forced)) {
		auto tmpName = fl->getFileName().substr(0, fl->getFileName().length()-4);
		try {
			//auto start = GET_TICK();
			{
				File f(tmpName, File::RW, File::TRUNCATE | File::CREATE, false);

				f.write(SimpleXML::utf8Header);
				f.write("<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"/\" Generator=\"DC++ " DCVERSIONSTRING "\">\r\n");

				string tmp;
				string indent = "\t";

				auto root = new FileListDir(Util::emptyString, 0, 0);

				{
					RLock l(cs);
					for(const auto& d: rootPaths | map_values | filtered(Directory::HasRootProfile(aProfile))) { 
						d->toFileList(root, aProfile, true);
					}

					//auto end2 = GET_TICK();
					//LogManager::getInstance()->message("Full list directories combined in " + Util::toString(end2-start2) + " ms (" + Util::toString((end2-start2)/1000) + " seconds)", LogManager::LOG_INFO);

					for(const auto it2: root->listDirs | map_values) {
						it2->toXml(f, indent, tmp, true);
					}
				}

				delete root;

				f.write("</FileListing>");
				f.flush();

				fl->setXmlListLen(f.getSize());

				File bz(fl->getFileName(), File::WRITE, File::TRUNCATE | File::CREATE, false);
				// We don't care about the leaves...
				CalcOutputStream<TTFilter<1024*1024*1024>, false> bzTree(&bz);
				FilteredOutputStream<BZFilter, false> bzipper(&bzTree);
				CalcOutputStream<TTFilter<1024*1024*1024>, false> newXmlFile(&bzipper);

				newXmlFile.write(f.read());
				newXmlFile.flush();

				newXmlFile.getFilter().getTree().finalize();
				bzTree.getFilter().getTree().finalize();

				fl->setXmlRoot(newXmlFile.getFilter().getTree().getRoot());
				fl->setBzXmlRoot(bzTree.getFilter().getTree().getRoot());
			}

			//auto end = GET_TICK();
			//LogManager::getInstance()->message("Full list generated in " + Util::toString(end-start) + " ms (" + Util::toString((end-start)/1000) + " seconds)", LogManager::LOG_INFO);

			fl->saveList();
			fl->unsetDirty(false);
		} catch(const Exception&) {
			// No new file lists...
			fl->unsetDirty(true);
		}

		File::deleteFile(tmpName);
	}
	return fl;
}

MemoryInputStream* ShareManager::generatePartialList(const string& dir, bool recurse, ProfileToken aProfile) const {
	if(dir[0] != '/' || dir[dir.size()-1] != '/')
		return 0;

	string xml = SimpleXML::utf8Header;

	{
		unique_ptr<FileListDir> root(new FileListDir(Util::emptyString, 0, 0));
		string tmp;

		RLock l(cs);

		//auto start = GET_TICK();
		if(dir == "/") {
			dcdebug("Generating partial from root folders");
			for(const auto& sd: rootPaths | map_values) {
				if(sd->getProfileDir()->hasRootProfile(aProfile)) {
					sd->toFileList(root.get(), aProfile, recurse);
				}
			}
		} else {
			dcdebug("wanted %s \n", dir);
			try {
				DirectoryList result;
				findVirtuals<ProfileToken>(dir, aProfile, result); 
				if (!result.empty()) {
					root->shareDirs = result;

					//add the subdirs
					for(const auto& it: result) {
						for(const auto& d: it->directories) { 
							if (!d->isLevelExcluded(aProfile))
								d->toFileList(root.get(), aProfile, recurse); 
						}
						root->date = max(root->date, it->getLastWrite());
					}
				}
			} catch(...) {
				return nullptr;
			}
		}

		xml += "<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + 
			"\" Base=\"" + SimpleXML::escape(dir, tmp, false) + 
			"\" BaseDate=\"" + Util::toString(root->date) +
			"\" Generator=\"DC++ " DCVERSIONSTRING "\">\r\n";

		StringOutputStream sos(xml);
		string indent = "\t";

		for(const auto ld: root->listDirs | map_values)
			ld->toXml(sos, indent, tmp, recurse);

		root->filesToXml(sos, indent, tmp, !recurse);

		//auto end = GET_TICK();
		//LogManager::getInstance()->message("Partial list from path " + dir +  " generated in " + Util::toString(end-start) + " ms (" + Util::toString((end-start)/1000) + " seconds)", LogManager::LOG_INFO);

		sos.write("</FileListing>");
		//LogManager::getInstance()->message(xml, LogManager::LOG_WARNING);
	}

	if (xml.empty()) {
		dcdebug("Partial NULL");
		return NULL;
	} else {
		dcdebug("Partial list Generated.");
		return new MemoryInputStream(xml);
	}
}

void ShareManager::Directory::toFileList(FileListDir* aListDir, ProfileToken aProfile, bool isFullList) {
	auto n = getVirtualName(aProfile);

	FileListDir* newListDir = nullptr;
	auto pos = aListDir->listDirs.find(n);
	if (pos != aListDir->listDirs.end()) {
		newListDir = pos->second;
		if (!isFullList)
			newListDir->size += getSize(aProfile);
		newListDir->date = max(newListDir->date, lastWrite);
	} else {
		newListDir = new FileListDir(n, isFullList ? 0 : getSize(aProfile), lastWrite);
		aListDir->listDirs.emplace(n, newListDir);
	}

	newListDir->shareDirs.push_back(this);

	if (isFullList) {
		for(auto& d: directories) {
			if (!d->isLevelExcluded(aProfile)) 
				d->toFileList(newListDir, aProfile, isFullList);
		}
	}
}

ShareManager::FileListDir::FileListDir(const string& aName, int64_t aSize, int aDate) : name(aName), size(aSize), date(aDate) { }

#define LITERAL(n) n, sizeof(n)-1
void ShareManager::FileListDir::toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool fullList) const {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(name, tmp2, true));
	if (!fullList) {
		xmlFile.write(LITERAL("\" Size=\""));
		xmlFile.write(Util::toString(size));
	}
	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(Util::toString(date));

	if(fullList) {
		xmlFile.write(LITERAL("\">\r\n"));

		indent += '\t';
		for(const auto& d: listDirs | map_values) {
			d->toXml(xmlFile, indent, tmp2, fullList);
		}

		filesToXml(xmlFile, indent, tmp2, !fullList);

		indent.erase(indent.length()-1);
		xmlFile.write(indent);
		xmlFile.write(LITERAL("</Directory>\r\n"));
	} else {
		bool hasDirs = any_of(shareDirs.begin(), shareDirs.end(), [](const Directory::Ptr& d) { return !d->directories.empty(); });
		if(!hasDirs && all_of(shareDirs.begin(), shareDirs.end(), [](const Directory::Ptr& d) { return d->files.empty(); })) {
			xmlFile.write(LITERAL("\" />\r\n"));
		} else {
			xmlFile.write(LITERAL("\" Incomplete=\"1\" Children=\""));
			xmlFile.write(Util::toString(hasDirs));
			xmlFile.write(LITERAL("\" />\r\n"));
		}
	}
}

void ShareManager::FileListDir::filesToXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const {
	bool filesAdded = false;
	for(auto di = shareDirs.begin(); di != shareDirs.end(); ++di) {
		if (filesAdded) {
			for(const auto& fi: (*di)->files) {
				//go through the dirs that we have added already
				if (none_of(shareDirs.begin(), di, [&fi](const Directory::Ptr& d) { return d->files.find(fi) != d->files.end(); })) {
					fi.toXml(xmlFile, indent, tmp2, addDate);
				}
			}
		} else if (!(*di)->files.empty()) {
			filesAdded = true;
			for(const auto& f: (*di)->files)
				f.toXml(xmlFile, indent, tmp2, addDate);
		}
	}
}

void ShareManager::Directory::filesToXmlList(OutputStream& xmlFile, string& indent, string& tmp2) const {
	for(const auto& f: files) {
		xmlFile.write(indent);
		xmlFile.write(LITERAL("<File Name=\""));
		xmlFile.write(SimpleXML::escape(f.getName(), tmp2, true));
		/*xmlFile.write(LITERAL("\" Size=\""));
		xmlFile.write(Util::toString(f.getSize()));*/
		xmlFile.write(LITERAL("\"/>\r\n"));
	}
}

ShareManager::Directory::File::File(const string& aName, Directory::Ptr aParent, HashedFile& aFileInfo) : 
	size(aFileInfo.getSize()), parent(aParent.get()), tth(aFileInfo.getRoot()), lastWrite(aFileInfo.getTimeStamp()) {
	
	nameLower = Text::toLower(aName);
	name = (compare(nameLower, aName) == 0 ? nullptr : new string(aName)); 
}

ShareManager::Directory::File::~File() {
	if (name)
		delete name;
}

void ShareManager::Directory::File::toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<File Name=\""));
	xmlFile.write(SimpleXML::escape(getName(), tmp2, true));
	xmlFile.write(LITERAL("\" Size=\""));
	xmlFile.write(Util::toString(size));
	xmlFile.write(LITERAL("\" TTH=\""));
	tmp2.clear();
	xmlFile.write(getTTH().toBase32(tmp2));

	if (addDate) {
		xmlFile.write(LITERAL("\" Date=\""));
		xmlFile.write(SimpleXML::escape(Util::toString(lastWrite), tmp2, true));
	}
	xmlFile.write(LITERAL("\"/>\r\n"));
}

ShareManager::FileListDir::~FileListDir() {
	for_each(listDirs | map_values, DeleteFunction());
}

string ShareManager::ProfileDirectory::getCachePath() const {
	return Util::validateFileName(Util::getPath(Util::PATH_SHARECACHE) + "ShareCache_" + Util::cleanPathChars(path) + ".xml");
}

#define LITERAL(n) n, sizeof(n)-1

void ShareManager::saveXmlList(bool verbose /*false*/, function<void (float)> progressF /*nullptr*/) {

	if(xml_saving)
		return;

	xml_saving = true;

	if (progressF)
		progressF(0);

	int cur = 0;

	{
		RLock l(cs);

		DirectoryList dirtyDirs;
		//boost::algorithm::copy_if(
		boost::algorithm::copy_if(rootPaths | map_values, back_inserter(dirtyDirs), [](const Directory::Ptr& aDir) { return aDir->getProfileDir()->getCacheDirty() && !aDir->getParent(); });

		concurrency::parallel_for_each(dirtyDirs.begin(), dirtyDirs.end(), [&](const Directory::Ptr& d) {
			string path = d->getProfileDir()->getCachePath();
			try {
				string indent, tmp;

				//create a backup first in case we get interrupted on creation.
				File ff(path + ".tmp", File::WRITE, File::TRUNCATE | File::CREATE);
				BufferedOutputStream<false> xmlFile(&ff);

				xmlFile.write(SimpleXML::utf8Header);
				xmlFile.write(LITERAL("<Share Version=\"" SHARE_CACHE_VERSION));
				xmlFile.write(LITERAL("\" Path=\""));
				xmlFile.write(SimpleXML::escape(d->getProfileDir()->getPath(), tmp, true));

				xmlFile.write(LITERAL("\" Date=\""));
				xmlFile.write(SimpleXML::escape(Util::toString(d->getLastWrite()), tmp, true));
				xmlFile.write(LITERAL("\">\r\n"));
				indent +='\t';

				for(const auto& child: d->directories) {
					child->toXmlList(xmlFile, d->getProfileDir()->getPath() + child->getRealName() + PATH_SEPARATOR, indent, tmp);
				}
				d->filesToXmlList(xmlFile, indent, tmp);

				xmlFile.write(LITERAL("</Share>"));
				xmlFile.flush();
				ff.close();

				File::deleteFile(path);
				File::renameFile(path + ".tmp", path);
			} catch(Exception& e){
				LogManager::getInstance()->message("Error saving " + path + ": " + e.getError(), LogManager::LOG_WARNING);
			}

			d->getProfileDir()->setCacheDirty(false);
			if (progressF) {
				cur++;
				progressF(static_cast<float>(cur) / static_cast<float>(dirtyDirs.size()));
			}
		});
	}

	xml_saving = false;
	lastSave = GET_TICK();
	if (verbose)
		LogManager::getInstance()->message("Share cache saved.", LogManager::LOG_INFO);
}

void ShareManager::Directory::toXmlList(OutputStream& xmlFile, string&& path, string& indent, string& tmp) {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(realName, tmp, true));

	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(SimpleXML::escape(Util::toString(lastWrite), tmp, true));
	xmlFile.write(LITERAL("\">\r\n"));

	indent += '\t';
	filesToXmlList(xmlFile, indent, tmp);

	for(const auto& d: directories) {
		d->toXmlList(xmlFile, path + d->getRealName() + PATH_SEPARATOR, indent, tmp);
	}

	indent.erase(indent.length()-1);
	xmlFile.write(indent);
	xmlFile.write(LITERAL("</Directory>\r\n"));
}

MemoryInputStream* ShareManager::generateTTHList(const string& dir, bool recurse, ProfileToken aProfile) const {
	
	if(aProfile == SP_HIDDEN)
		return NULL;
	
	string tths;
	string tmp;
	StringOutputStream sos(tths);

	try{
		RLock l(cs);
		DirectoryList result;
		findVirtuals<ProfileToken>(dir, aProfile, result); 
		for(const auto& it: result) {
			//dcdebug("result name %s \n", (*it)->getProfileDir()->getName(aProfile));
			it->toTTHList(sos, tmp, recurse);
		}
	} catch(...) {
		return NULL;
	}

	if (tths.size() == 0) {
		dcdebug("Partial NULL");
		return NULL;
	} else {
		//LogManager::getInstance()->message(tths);
		return new MemoryInputStream(tths);
	}
}

void ShareManager::Directory::toTTHList(OutputStream& tthList, string& tmp2, bool recursive) const {
	if (recursive) {
		for(const auto& d: directories) {
			d->toTTHList(tthList, tmp2, recursive);
		}
	}

	for(const auto& f: files) {
		tmp2.clear();
		tthList.write(f.getTTH().toBase32(tmp2));
		tthList.write(LITERAL(" "));
	}
}

// These ones we can look up as ints (4 bytes...)...

static const char* typeAudio[] = { ".mp3", ".mp2", ".mid", ".wav", ".ogg", ".wma", ".669", ".aac", ".aif", ".amf", ".ams", ".ape", ".dbm", ".dmf", ".dsm", ".far", ".mdl", ".med", ".mod", ".mol", ".mp1", ".mp4", ".mpa", ".mpc", ".mpp", ".mtm", ".nst", ".okt", ".psm", ".ptm", ".rmi", ".s3m", ".stm", ".ult", ".umx", ".wow" };
static const char* typeCompressed[] = { ".rar", ".zip", ".ace", ".arj", ".hqx", ".lha", ".sea", ".tar", ".tgz", ".uc2" };
static const char* typeDocument[] = { ".nfo", ".htm", ".doc", ".txt", ".pdf", ".chm" };
static const char* typeExecutable[] = { ".exe", ".com" };
static const char* typePicture[] = { ".jpg", ".gif", ".png", ".eps", ".img", ".pct", ".psp", ".pic", ".tif", ".rle", ".bmp", ".pcx", ".jpe", ".dcx", ".emf", ".ico", ".psd", ".tga", ".wmf", ".xif" };
static const char* typeVideo[] = { ".vob", ".mpg", ".mov", ".asf", ".avi", ".wmv", ".ogm", ".mkv", ".pxp", ".m1v", ".m2v", ".mpe", ".mps", ".mpv", ".ram" };

static const string type2Audio[] = { ".au", ".it", ".ra", ".xm", ".aiff", ".flac", ".midi", };
static const string type2Compressed[] = { ".gz" };
static const string type2Picture[] = { ".jpeg", ".ai", ".ps", ".pict", ".tiff" };
static const string type2Video[] = { ".mpeg", ".rm", ".divx", ".mp1v", ".mp2v", ".mpv1", ".mpv2", ".qt", ".rv", ".vivo" };

#define IS_TYPE(x) ( type == (*((uint32_t*)x)) )
#define IS_TYPE2(x) (stricmp(aString.c_str() + aString.length() - x.length(), x.c_str()) == 0) //hmm lower conversion...

bool ShareManager::checkType(const string& aString, int aType) {

	if(aType == SearchManager::TYPE_ANY)
		return true;

	if(aString.length() < 5)
		return false;
	
	const char* c = aString.c_str() + aString.length() - 3;
	if(!Text::isAscii(c))
		return false;

	uint32_t type = '.' | (Text::asciiToLower(c[0]) << 8) | (Text::asciiToLower(c[1]) << 16) | (((uint32_t)Text::asciiToLower(c[2])) << 24);

	switch(aType) {
	case SearchManager::TYPE_AUDIO:
		{
			for(size_t i = 0; i < (sizeof(typeAudio) / sizeof(typeAudio[0])); i++) {
				if(IS_TYPE(typeAudio[i])) {
					return true;
				}
			}
			for(size_t i = 0; i < (sizeof(type2Audio) / sizeof(type2Audio[0])); i++) {
				if(IS_TYPE2(type2Audio[i])) {
					return true;
				}
			}
		}
		break;
	case SearchManager::TYPE_COMPRESSED:
		{
			for(size_t i = 0; i < (sizeof(typeCompressed) / sizeof(typeCompressed[0])); i++) {
				if(IS_TYPE(typeCompressed[i])) {
					return true;
				}
			}
			if( IS_TYPE2(type2Compressed[0]) ) {
				return true;
			}
		}
		break;
	case SearchManager::TYPE_DOCUMENT:
		for(size_t i = 0; i < (sizeof(typeDocument) / sizeof(typeDocument[0])); i++) {
			if(IS_TYPE(typeDocument[i])) {
				return true;
			}
		}
		break;
	case SearchManager::TYPE_EXECUTABLE:
		if(IS_TYPE(typeExecutable[0]) || IS_TYPE(typeExecutable[1])) {
			return true;
		}
		break;
	case SearchManager::TYPE_PICTURE:
		{
			for(size_t i = 0; i < (sizeof(typePicture) / sizeof(typePicture[0])); i++) {
				if(IS_TYPE(typePicture[i])) {
					return true;
				}
			}
			for(size_t i = 0; i < (sizeof(type2Picture) / sizeof(type2Picture[0])); i++) {
				if(IS_TYPE2(type2Picture[i])) {
					return true;
				}
			}
		}
		break;
	case SearchManager::TYPE_VIDEO:
		{
			for(size_t i = 0; i < (sizeof(typeVideo) / sizeof(typeVideo[0])); i++) {
				if(IS_TYPE(typeVideo[i])) {
					return true;
				}
			}
			for(size_t i = 0; i < (sizeof(type2Video) / sizeof(type2Video[0])); i++) {
				if(IS_TYPE2(type2Video[i])) {
					return true;
				}
			}
		}
		break;
	default:
		dcassert(0);
		break;
	}
	return false;
}

SearchManager::TypeModes ShareManager::getType(const string& aFileName) noexcept {
	if(aFileName[aFileName.length() - 1] == PATH_SEPARATOR) {
		return SearchManager::TYPE_DIRECTORY;
	}
	 /*
	 optimize, check for compressed(rar) and audio first, the ones sharing the most are probobly sharing rars or mp3.
	 a test to match with regexp for rars first, otherwise it will match everything and end up setting type any for  .r01 ->
	 */
	try{ 
		if(boost::regex_search(aFileName.substr(aFileName.length()-4), rxxReg))
			return SearchManager::TYPE_COMPRESSED;
	} catch(...) { } //not vital if it fails, just continue the type check.
	
	if(checkType(aFileName, SearchManager::TYPE_AUDIO))
		return SearchManager::TYPE_AUDIO;
	else if(checkType(aFileName, SearchManager::TYPE_VIDEO))
		return SearchManager::TYPE_VIDEO;
	else if(checkType(aFileName, SearchManager::TYPE_DOCUMENT))
		return SearchManager::TYPE_DOCUMENT;
	else if(checkType(aFileName, SearchManager::TYPE_COMPRESSED))
		return SearchManager::TYPE_COMPRESSED;
	else if(checkType(aFileName, SearchManager::TYPE_PICTURE))
		return SearchManager::TYPE_PICTURE;
	else if(checkType(aFileName, SearchManager::TYPE_EXECUTABLE))
		return SearchManager::TYPE_EXECUTABLE;

	return SearchManager::TYPE_ANY;
}

/**
 * Alright, the main point here is that when searching, a search string is most often found in 
 * the filename, not directory name, so we want to make that case faster. Also, we want to
 * avoid changing StringLists unless we absolutely have to --> this should only be done if a string
 * has been matched in the directory name. This new stringlist should also be used in all descendants,
 * but not the parents...
 */
void ShareManager::Directory::search(SearchResultList& aResults, StringSearch::List& aStrings, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults) const noexcept {
	// Skip everything if there's nothing to find here (doh! =)
	if(!hasType(aFileType))
		return;

	StringSearch::List* cur = &aStrings;
	unique_ptr<StringSearch::List> newStr;

	// Find any matches in the directory name
	for(const auto& k: aStrings) {
		if(k.matchLower(profileDir ? Text::toLower(profileDir->getName(SP_DEFAULT)) : realNameLower)) {
			if(!newStr.get()) {
				newStr = unique_ptr<StringSearch::List>(new StringSearch::List(aStrings));
			}
			newStr->erase(remove(newStr->begin(), newStr->end(), k), newStr->end());
		}
	}

	if(newStr.get() != 0) {
		cur = newStr.get();
	}

	bool sizeOk = (aSearchType != SearchManager::SIZE_ATLEAST) || (aSize == 0);
	if( (cur->empty()) && 
		(((aFileType == SearchManager::TYPE_ANY) && sizeOk) || (aFileType == SearchManager::TYPE_DIRECTORY)) ) {
		// We satisfied all the search words! Add the directory...(NMDC searches don't support directory size)
		//getInstance()->addDirResult(getFullName(SP_DEFAULT), aResults, SP_DEFAULT, false);

		SearchResultPtr sr(new SearchResult(SearchResult::TYPE_DIRECTORY, 0, getFullName(SP_DEFAULT), TTHValue(), 0, 0));
		aResults.push_back(sr);
	}

	if(aFileType != SearchManager::TYPE_DIRECTORY) {
		for(const auto& f: files) {
			
			if(aSearchType == SearchManager::SIZE_ATLEAST && aSize > f.getSize()) {
				continue;
			} else if(aSearchType == SearchManager::SIZE_ATMOST && aSize < f.getSize()) {
				continue;
			}

			auto j = cur->begin();
			for(; j != cur->end() && j->matchLower(f.getNameLower()); ++j) 
				;	// Empty
			
			if(j != cur->end())
				continue;
			
			// Check file type...
			if(checkType(f.getName(), aFileType)) {
				SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, f.getSize(), getFullName(SP_DEFAULT) + f.getName(), f.getTTH(), 0, 1));
				aResults.push_back(sr);
				if(aResults.size() >= maxResults) {
					break;
				}
			}
		}
	}

	for(auto l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
		if ((*l)->isLevelExcluded(SP_DEFAULT))
			continue;
		(*l)->search(aResults, *cur, aSearchType, aSize, aFileType, maxResults);
	}
}
//NMDC Search
void ShareManager::search(SearchResultList& results, const string& aString, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults, bool aHideShare) noexcept {
	if(aFileType == SearchManager::TYPE_TTH) {
		if(aString.compare(0, 4, "TTH:") == 0) {
			TTHValue tth(aString.substr(4));
			
			RLock l (cs);
			if(!aHideShare) {
				const auto i = tthIndex.find(const_cast<TTHValue*>(&tth));
				if(i != tthIndex.end() && i->second->getParent()->hasProfile(SP_DEFAULT)) {
					i->second->addSR(results, SP_DEFAULT, false);
				} 
			}

			auto files = tempShares.equal_range(tth);
			for(auto& i = files.first; i != files.second; ++i) {
				if(i->second.key.empty()) { // nmdc shares are shared to everyone.
					SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, i->second.size, "tmp\\" + Util::getFileName(i->second.path), i->first, 0, 1));
					results.push_back(sr);
				}
			}
		}
		return;
	} 
	
	if(aHideShare)
		return;

	StringTokenizer<string> t(aString, '$');
	const StringList& sl = t.getTokens();

	StringSearch::List ssl;
	for(const auto& i: sl) {
		if(!i.empty()) {
			ssl.emplace_back(i);
		}
	}

	if(ssl.empty())
		return;


	RLock l (cs);
	for(auto j = rootPaths.begin(); (j != rootPaths.end()) && (results.size() < maxResults); ++j) {
		if(j->second->getProfileDir()->hasRootProfile(SP_DEFAULT) && j->second->matchBloom(ssl))
			j->second->search(results, ssl, aSearchType, aSize, aFileType, maxResults);
	}
}

bool ShareManager::addDirResult(const string& aPath, SearchResultList& aResults, ProfileToken aProfile, AdcSearch& srch) const {
	const string path = srch.addParents ? (Util::getParentDir(aPath, true)) : aPath;

	//have we added it already?
	auto p = find_if(aResults, [&path](const SearchResultPtr& sr) { return sr->getFile() == path; });
	if (p != aResults.end())
		return false;

	//get all dirs with this path
	DirectoryList result;

	try {
		findVirtuals<ProfileToken>(Util::toAdcFile(path), aProfile, result);
	} catch(...) {
		dcassert(0);
	}

	uint32_t date = 0;
	int64_t size = 0;
	size_t files = 0, folders = 0;
	for(const auto& d: result) {
		if (!d->isLevelExcluded(aProfile)) {
			d->getResultInfo(aProfile, size, files, folders);
			date = max(date, d->getLastWrite());
		}
	}

	if (srch.matchesDate(date)) {
		SearchResultPtr sr(new SearchResult(SearchResult::TYPE_DIRECTORY, size, path, TTHValue(), date, files, folders));
		aResults.push_back(sr);
		return true;
	}

	return false;
}

void ShareManager::Directory::File::addSR(SearchResultList& aResults, ProfileToken aProfile, bool addParent) const {
	if (addParent) {
		//getInstance()->addDirResult(getFullName(aProfile), aResults, aProfile, true);
		SearchResultPtr sr(new SearchResult(parent->getFullName(aProfile)));
		aResults.push_back(sr);
	} else {
		SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, 
			size, getFullName(aProfile), getTTH(), getLastWrite(), 1));
		aResults.push_back(sr);
	}
}

void ShareManager::Directory::search(SearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults, ProfileToken aProfile) const noexcept {
	for(const auto& i: aStrings.exclude) {
		if(i.matchLower(profileDir ? Text::toLower(profileDir->getName(aProfile)) : realNameLower))
			return;
	}

	StringSearch::List* old = aStrings.include;

	unique_ptr<StringSearch::List> newStr;

	// Find any matches in the directory name
	for(const auto& k: *aStrings.include) {
		if(k.matchLower(profileDir ? Text::toLower(profileDir->getName(aProfile)) : realNameLower)) {
			if(!newStr.get()) {
				newStr = unique_ptr<StringSearch::List>(new StringSearch::List(*aStrings.include));
			}
			newStr->erase(remove(newStr->begin(), newStr->end(), k), newStr->end());
		}
	}

	if(newStr.get() != 0 && aStrings.matchType == AdcSearch::MATCH_FULL_PATH) {
		aStrings.include = newStr.get();
	}

	bool sizeOk = (aStrings.gt == 0) && aStrings.matchesDate(lastWrite);
	if((aStrings.include->empty() || (newStr.get() && newStr.get()->empty())) && aStrings.ext.empty() && sizeOk && aStrings.itemType != AdcSearch::TYPE_FILE) {
		// We satisfied all the search words! Add the directory...
		getInstance()->addDirResult(getFullName(aProfile), aResults, aProfile, aStrings);
	}

	if(aStrings.itemType != AdcSearch::TYPE_DIRECTORY) {
		for(const auto& f: files) {

			if(!(f.getSize() >= aStrings.gt)) {
				continue;
			} else if(!(f.getSize() <= aStrings.lt)) {
				continue;
			} else if (!aStrings.matchesDate(f.getLastWrite())) {
				continue;
			}

			auto j = aStrings.include->begin();
			for(; j != aStrings.include->end() && j->matchLower(f.getNameLower()); ++j) 
				;	// Empty

			if(j != aStrings.include->end())
				continue;

			// Check file type...
			if(aStrings.hasExt(f.getName())) {
				if(aStrings.isExcluded(f.getName()))
					continue;

				f.addSR(aResults, aProfile, aStrings.addParents);

				if(aResults.size() >= maxResults) {
					return;
				}

				if (aStrings.addParents)
					break;
			}
		}
	}

	for(auto l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
		if ((*l)->isLevelExcluded(aProfile))
			continue;
		(*l)->search(aResults, aStrings, maxResults, aProfile);
	}

	aStrings.include = old;
}


void ShareManager::search(SearchResultList& results, AdcSearch& srch, StringList::size_type maxResults, ProfileToken aProfile, const CID& cid, const string& aDir) {

	RLock l(cs);
	if(srch.hasRoot) {
		const auto i = tthIndex.equal_range(const_cast<TTHValue*>(&srch.root));
		for(auto& f: i | map_values) {
			if (f->hasProfile(aProfile) && AirUtil::isParentOrExact(aDir, f->getADCPath(aProfile))) {
				f->addSR(results, aProfile, srch.addParents);
				return;
			}
		}

		const auto files = tempShares.equal_range(srch.root);
		for(const auto& f: files | map_values) {
			if(f.key.empty() || (f.key == cid.toBase32())) { // if no key is set, it means its a hub share.
				//TODO: fix the date?
				SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, f.size, "tmp\\" + Util::getFileName(f.path), srch.root, 0, 1));
				results.push_back(sr);
			}
		}
		return;
	}

	if (srch.itemType == AdcSearch::TYPE_DIRECTORY && srch.matchType == AdcSearch::MATCH_EXACT) {
		const auto i = dirNameMap.equal_range(srch.includeX.front().getPattern());
		for(const auto& d: i | map_values) {
			string path = d->getADCPath(aProfile);
			if (d->hasProfile(aProfile) && AirUtil::isParentOrExact(aDir, path) && srch.matchesDate(d->getLastWrite()) && addDirResult(path, results, aProfile, srch)) {
				return;
			}
		}

		return;
	}

	if (aDir.empty() || aDir == "/") {
		for(auto j = rootPaths.begin(); (j != rootPaths.end()) && (results.size() < maxResults); ++j) {
			if(j->second->getProfileDir()->hasRootProfile(aProfile) && j->second->matchBloom(srch.includeX))
				j->second->search(results, srch, maxResults, aProfile);
		}
	} else {
		DirectoryList result;
		findVirtuals<ProfileToken>(aDir, aProfile, result);
		for(auto j = result.begin(); (j != result.end()) && (results.size() < maxResults); ++j) {
			if ((*j)->hasProfile(aProfile) && (*j)->matchBloom(srch.includeX))
				(*j)->search(results, srch, maxResults, aProfile);
		}
	}
}

void ShareManager::cleanIndices(Directory& dir, const Directory::File::Set::iterator& i) {
	dir.size -= (*i).getSize();
	sharedSize -= (*i).getSize();

	auto flst = tthIndex.equal_range(const_cast<TTHValue*>(&i->getTTH()));
	if (distance(flst.first, flst.second) == 1) {
		tthIndex.erase(flst.first);
	} else {
		auto p = find_if(flst | map_values, [i](decltype(tthIndex.begin()->second)& aF) { return *aF == *i; });
		if (p.base() != flst.second)
			tthIndex.erase(p.base());
		else
			dcassert(0);
	}
}

void ShareManager::cleanIndices(Directory& dir) {
	for(auto& d: dir.directories) {
		cleanIndices(*d);
	}

	//remove from the name map
	auto directories = dirNameMap.equal_range(dir.getRealName());
	auto p = find_if(directories | map_values, [&dir](const Directory::Ptr& d) { return d.get() == &dir; });
	if (p.base() != dirNameMap.end())
		dirNameMap.erase(p.base());
	else
		dcassert(0);

	//remove all files
	for(auto i = dir.files.begin(); i != dir.files.end(); ++i) {
		cleanIndices(dir, i);
	}
}

void ShareManager::on(QueueManagerListener::BundleAdded, const BundlePtr& aBundle) noexcept {
	WLock l (dirNames);
	bundleDirs.insert(upper_bound(bundleDirs.begin(), bundleDirs.end(), aBundle->getTarget()), aBundle->getTarget());
}

void ShareManager::on(QueueManagerListener::BundleHashed, const string& path) noexcept {
	ProfileTokenSet dirtyProfiles;
	{
		WLock l(cs);
		Directory::Ptr dir = findDirectory(path, true, true);
		if (!dir) {
			LogManager::getInstance()->message(STRING_F(BUNDLE_SHARING_FAILED, Util::getLastDir(path)), LogManager::LOG_WARNING);
			return;
		}

		if (!dir->files.empty() || !dir->files.empty()) {
			/* get rid of any existing crap we might have in the bundle directory and refresh it.
			done at this point as the file and directory pointers should still be valid, if there are any */

			cleanIndices(*dir);

			//there went our dir..
			dirNameMap.emplace(dir->getRealName(), dir);

			dir->files.clear();
			dir->directories.clear();
		}

		ProfileDirMap profileDirs;
		DirMap newShares;
		int64_t hashSize = 0;

		buildTree(path, dir, false, profileDirs, dirNameMap, newShares, hashSize, sharedSize, tthIndex, dir->getBloom());

		dir->copyRootProfiles(dirtyProfiles, true);
	}

	setProfilesDirty(dirtyProfiles);
	LogManager::getInstance()->message(STRING_F(BUNDLE_X_SHARED, Util::getLastDir(path)), LogManager::LOG_INFO);
}

bool ShareManager::allowAddDir(const string& aPath) noexcept {
	//LogManager::getInstance()->message("QueueManagerListener::BundleFilesMoved");
	{
		RLock l(cs);
		const auto mi = find_if(rootPaths | map_keys, [&aPath](const string& p) { return AirUtil::isParentOrExact(p, aPath); });
		if (mi.base() != rootPaths.end()) {
			//check the skiplist
			string fullPath = *mi;
			StringList sl = StringTokenizer<string>(aPath.substr(fullPath.length()), PATH_SEPARATOR).getTokens();
			for(const auto& name: sl) {
				fullPath += Text::toLower(name) + PATH_SEPARATOR;
				if (!checkSharedName(fullPath, true, true)) {
					return false;
				}

				auto m = profileDirs.find(fullPath);
				if (m != profileDirs.end() && m->second->isSet(ProfileDirectory::FLAG_EXCLUDE_TOTAL)) {
					return false;
				}
			}
			return true;
		}
	}
	return false;
}

ShareManager::Directory::Ptr ShareManager::findDirectory(const string& fname, bool allowAdd, bool report, bool checkExcludes /*true*/) {
	auto mi = find_if(rootPaths | map_keys, [&fname](const string& n) { return AirUtil::isParentOrExact(n, fname); }).base();
	if (mi != rootPaths.end()) {
		auto curDir = mi->second;
		StringList sl = StringTokenizer<string>(fname.substr(mi->first.length()), PATH_SEPARATOR).getTokens();
		string fullPath = mi->first;
		for(const auto& name: sl) {
			fullPath += name + PATH_SEPARATOR;
			auto j = curDir->directories.find(Text::toLower(name));
			if (j != curDir->directories.end()) {
				curDir = *j;
			} else if (!allowAdd || !checkSharedName(fullPath, true, report)) {
				return nullptr;
			} else {
				auto m = profileDirs.find(fullPath);
				if (checkExcludes && m != profileDirs.end() && m->second->isSet(ProfileDirectory::FLAG_EXCLUDE_TOTAL)) {
					return nullptr;
				}

				curDir = Directory::create(name, curDir, GET_TIME(), m != profileDirs.end() ? m->second : nullptr);
				dirNameMap.emplace(name, curDir);
				curDir->addBloom(curDir->getBloom());
			}
		}
		return curDir;
	}
	return nullptr;
}

void ShareManager::onFileHashed(const string& fname, HashedFile& fileInfo) noexcept {
	ProfileTokenSet dirtyProfiles;
	{
		WLock l(cs);
		Directory::Ptr d = findDirectory(Util::getFilePath(fname), true, false);
		if (!d) {
			return;
		}

		string name = Util::getFileName(fname);
		auto i = d->findFile(Text::toLower(name));
		if(i != d->files.end()) {
			// Get rid of false constness...
			cleanIndices(*d, i);
			d->files.erase(i);
		}

		auto it = d->files.emplace(name, d, fileInfo).first;
		updateIndices(*d, it, d->getBloom(), sharedSize, tthIndex);

		d->copyRootProfiles(dirtyProfiles, true);
	}

	setProfilesDirty(dirtyProfiles);
}

void ShareManager::getExcludes(ProfileToken aProfile, StringList& excludes) {
	for(const auto& i: profileDirs) {
		if (i.second->isExcluded(aProfile))
			excludes.push_back(i.first);
	}
}

void ShareManager::changeExcludedDirs(const ProfileTokenStringList& aAdd, const ProfileTokenStringList& aRemove) {
	ProfileTokenSet dirtyProfiles;

	{
		WLock l (cs);

		//add new exludes
		for(const auto i: aAdd) {
			auto dir = findDirectory(i.second, false, false);
			if (dir) {
				dirtyProfiles.insert(i.first);
				if (dir->getProfileDir()) {
					dir->getProfileDir()->addExclude(i.first);
					continue;
				}
			}

			auto pd = ProfileDirectory::Ptr(new ProfileDirectory(i.second, i.first));
			if (dir)
				dir->setProfileDir(pd);
			profileDirs[i.second] = pd;
		}

		//remove existing excludes
		for(const auto i: aRemove) {
			auto pdPos = profileDirs.find(i.second);
			if (pdPos != profileDirs.end() && pdPos->second->removeExcludedProfile(i.first) && !pdPos->second->hasRoots()) {
				profileDirs.erase(pdPos);
			}
		}
	}

	setProfilesDirty(dirtyProfiles);
	rebuildTotalExcludes();
}

void ShareManager::rebuildTotalExcludes() {
	RLock l (cs);
	for(auto& pdPos: profileDirs) {
		auto pd = pdPos.second;

		//profileDirs also include all shared roots...
		if (!pd->hasExcludes() || pd->hasRoots())
			continue;

		pd->unsetFlag(ProfileDirectory::FLAG_EXCLUDE_TOTAL);

		ProfileTokenSet sharedProfiles;

		//List all profiles where this dir is shared in
		for(const auto& s: rootPaths) {
			if (AirUtil::isParentOrExact(s.first, pdPos.first)) {
				s.second->copyRootProfiles(sharedProfiles, false);
			}
		}


		//Is the directory excluded in all profiles?
		if (any_of(sharedProfiles.begin(), sharedProfiles.end(), [&pd](const ProfileToken aToken) { return pd->getExcludedProfiles().find(aToken) == pd->getExcludedProfiles().end(); }))
			continue;


		//Are there shared roots in subdirs?
		auto subDirs = find_if(profileDirs | map_values, [pdPos](const ProfileDirectory::Ptr& spd) { 
			return spd->hasRoots() && AirUtil::isSub(spd->getPath(), pdPos.first); 
		});

		if (subDirs.base() == profileDirs.end()) {
			//LogManager::getInstance()->message(pdPos->first + " is a total exclude", LogManager::LOG_INFO);
			pdPos.second->setFlag(ProfileDirectory::FLAG_EXCLUDE_TOTAL);
		}
	}
}

vector<pair<string, StringList>> ShareManager::getGroupedDirectories() const noexcept {
	vector<pair<string, StringList>> ret;
	
	{
		RLock l (cs);
		for(const auto& d: rootPaths | map_values | filtered(Directory::IsParent())) {
			for(const auto& vName: d->getProfileDir()->getRootProfiles() | map_values) {
				auto retVirtual = find_if(ret, CompareFirst<string, StringList>(vName));

				const auto& rp = d->getRealPath(false);
				if (retVirtual != ret.end()) {
					//insert under an old virtual node if the real path doesn't exist there already
					if (find(retVirtual->second, rp) == retVirtual->second.end()) {
						retVirtual->second.push_back(rp); //sorted
					}
				} else {
					StringList tmp;
					tmp.push_back(rp);
					ret.emplace_back(vName, tmp);
				}
			}
		}
	}

	sort(ret.begin(), ret.end());
	return ret;
}

bool ShareManager::checkSharedName(const string& aPath, bool isDir, bool report /*true*/, int64_t size /*0*/) {
	string aName;
	aName = isDir ? Util::getLastDir(aPath) : Util::getFileName(aPath);

	if(aName == "." || aName == "..")
		return false;

	if (skipList.match(aName)) {
		if(SETTING(REPORT_SKIPLIST) && report)
			LogManager::getInstance()->message(STRING(SKIPLIST_HIT) + aPath, LogManager::LOG_INFO);
		return false;
	}

	aName = Text::toLower(aName); //we only need this now
	if (!isDir) {
		string fileExt = Util::getFileExt(aName);
		if( (strcmp(aName.c_str(), "dcplusplus.xml") == 0) || 
			(strcmp(aName.c_str(), "favorites.xml") == 0) ||
			(strcmp(fileExt.c_str(), ".dctmp") == 0) ||
			(strcmp(fileExt.c_str(), ".antifrag") == 0) ) 
		{
			return false;
		}

		//check for forbidden file patterns
		if(SETTING(REMOVE_FORBIDDEN)) {
			string::size_type nameLen = aName.size();
			if ((strcmp(fileExt.c_str(), ".tdc") == 0) ||
				(strcmp(fileExt.c_str(), ".getright") == 0) ||
				(strcmp(fileExt.c_str(), ".temp") == 0) ||
				(strcmp(fileExt.c_str(), ".tmp") == 0) ||
				(strcmp(fileExt.c_str(), ".jc!") == 0) ||	//FlashGet
				(strcmp(fileExt.c_str(), ".dmf") == 0) ||	//Download Master
				(strcmp(fileExt.c_str(), ".!ut") == 0) ||	//uTorrent
				(strcmp(fileExt.c_str(), ".bc!") == 0) ||	//BitComet
				(strcmp(fileExt.c_str(), ".missing") == 0) ||
				(strcmp(fileExt.c_str(), ".bak") == 0) ||
				(strcmp(fileExt.c_str(), ".bad") == 0) ||
				(nameLen > 9 && aName.rfind("part.met") == nameLen - 8) ||				
				(aName.find("__padding_") == 0) ||			//BitComet padding
				(aName.find("__incomplete__") == 0)		//winmx
				) {
					if (report) {
						LogManager::getInstance()->message(STRING(FORBIDDEN_FILE) + aPath, LogManager::LOG_INFO);
					}
					return false;
			}
		}

		if(Util::stricmp(aPath, AirUtil::privKeyFile) == 0) {
			return false;
		}

		if(SETTING(NO_ZERO_BYTE) && !(size > 0))
			return false;

		if ((SETTING(MAX_FILE_SIZE_SHARED) != 0) && (size > static_cast<int64_t>(SETTING(MAX_FILE_SIZE_SHARED))*1024*1024)) {
			if (report) {
				LogManager::getInstance()->message(STRING(BIG_FILE_NOT_SHARED) + " " + aPath, LogManager::LOG_INFO);
			}
			return false;
		}
	} else {
#ifdef _WIN32
		// don't share Windows directory
		if(aPath.length() >= winDir.length() && stricmp(aPath.substr(0, winDir.length()), winDir) == 0)
			return false;
#endif
		if((stricmp(aPath, AirUtil::tempDLDir) == 0)) {
			return false;
		}
	}
	return true;
}

void ShareManager::setSkipList() {
	WLock l (dirNames);
	skipList.pattern = SETTING(SKIPLIST_SHARE);
	skipList.setMethod(SETTING(SHARE_SKIPLIST_USE_REGEXP) ? StringMatch::REGEX : StringMatch::WILDCARD);
	skipList.prepare();
}

} // namespace dcpp