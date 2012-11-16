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

#include <boost/range/algorithm/remove_if.hpp>
#include <boost/range/algorithm/for_each.hpp>
#include <boost/algorithm/cxx11/copy_if.hpp>
#include <boost/range/algorithm/search.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/count_if.hpp>
#include <boost/range/algorithm/copy.hpp>

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
using boost::adaptors::map_values;
using boost::adaptors::map_keys;
using boost::adaptors::filtered;
using boost::range::find_if;
using boost::range::for_each;
using boost::range::copy;

#define SHARE_CACHE_VERSION "1"

#ifdef ATOMIC_FLAG_INIT
atomic_flag ShareManager::refreshing = ATOMIC_FLAG_INIT;
#else
atomic_flag ShareManager::refreshing;
#endif

ShareManager::ShareManager() : lastFullUpdate(GET_TICK()), lastIncomingUpdate(GET_TICK()), bloom(1<<20), sharedSize(0), ShareCacheDirty(false),
	xml_saving(false), lastSave(GET_TICK()), aShutdown(false), refreshRunning(false)
{ 
	SettingsManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);
	QueueManager::getInstance()->addListener(this);

	rxxReg.assign("[Rr0-9][Aa0-9][Rr0-9]");
#ifdef _WIN32
	// don't share Windows directory
	TCHAR path[MAX_PATH];
	::SHGetFolderPath(NULL, CSIDL_WINDOWS, NULL, SHGFP_TYPE_CURRENT, path);
	winDir = Text::toLower(Text::fromT(path)) + PATH_SEPARATOR;
#endif
}

ShareManager::~ShareManager() {

	SettingsManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);

	join();
}

void ShareManager::startup() {
	AirUtil::updateCachedSettings();
	if (!getShareProfile(SP_DEFAULT)) {
		ShareProfilePtr sp = ShareProfilePtr(new ShareProfile(STRING(DEFAULT), SP_DEFAULT));
		shareProfiles.push_back(sp);
	}

	ShareProfilePtr hidden = ShareProfilePtr(new ShareProfile("Hidden", SP_HIDDEN));
	shareProfiles.push_back(hidden);

	if(!loadCache()) {
		refresh(false, true);
	}

	rebuildTotalExcludes();
	setSkipList();
}

void ShareManager::shutdown() {
	//abort buildtree and refresh, we are shutting down.
	aShutdown = true;

	if(ShareCacheDirty || !Util::fileExists(Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml"))
		saveXmlList();

	try {
		RLock l (cs);
		StringList lists = File::findFiles(Util::getPath(Util::PATH_USER_CONFIG), "files?*.xml.bz2");

		//clear refs so we can delete filelists.
		for(auto f = shareProfiles.begin(); f != shareProfiles.end(); ++f) {
			if((*f)->getProfileList() && (*f)->getProfileList()->bzXmlRef.get()) 
				(*f)->getProfileList()->bzXmlRef.reset(); 
		}

		for_each(lists, File::deleteFile);
	} catch(...) { }
}

void ShareManager::setDirty(ProfileTokenSet aProfiles, bool setCacheDirty, bool forceXmlRefresh /*false*/) {
	RLock l(cs);
	for_each(aProfiles, [this, setCacheDirty, forceXmlRefresh](ProfileToken aProfile) {
		auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
		if(i != shareProfiles.end()) {
			if (forceXmlRefresh)
				(*i)->getProfileList()->setForceXmlRefresh(true);
			(*i)->getProfileList()->setXmlDirty(true);
		}
	});

	if (setCacheDirty)
		ShareCacheDirty = true;
}

ShareManager::Directory::Directory(const string& aRealName, const ShareManager::Directory::Ptr& aParent, uint32_t aLastWrite, ProfileDirectory::Ptr aProfileDir) :
	size(0),
	realName(aRealName),
	parent(aParent.get()),
	fileTypes(1 << SearchManager::TYPE_DIRECTORY),
	profileDir(aProfileDir),
	lastWrite(aLastWrite)
{
}
int64_t ShareManager::Directory::getSize(ProfileToken aProfile) const noexcept {
	int64_t tmp = size;
	for(auto i = directories.begin(); i != directories.end(); ++i) {
		if (i->second->isLevelExcluded(aProfile))
			continue;
		tmp += i->second->getSize(aProfile);
	}
	return tmp;
}

int64_t ShareManager::Directory::getTotalSize() const noexcept {
	int64_t tmp = size;
	for(auto i = directories.begin(); i != directories.end(); ++i) {
		tmp += i->second->getTotalSize();
	}
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

string ShareManager::getRealPath(const string& aFileName, int64_t aSize) {
	RLock l(cs);
	for(auto i = tthIndex.begin(); i != tthIndex.end(); ++i) {
		if(stricmp(aFileName.c_str(), i->second->getName().c_str()) == 0 && i->second->getSize() == aSize) {
			return i->second->getRealPath();
		}
	}
	return Util::emptyString;
}

string ShareManager::getRealPath(const TTHValue& root) {
	RLock l(cs);
	auto i = tthIndex.find(const_cast<TTHValue*>(&root)); 
	if(i != tthIndex.end()) {
		return i->second->getRealPath();
	}
	return Util::emptyString;
}


bool ShareManager::isTTHShared(const TTHValue& tth) {
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
	for(auto i = shares.begin(); i != shares.end(); ++i) {
		auto& profiles = i->second->getProfileDir()->getRootProfiles();
		for(auto k = profiles.begin(); k != profiles.end(); ++k) {
			if(stricmp(k->second, virtualRoot) == 0) {
				string name = k->second + virtualPath;
				dcdebug("Matching %s\n", name.c_str());
				if(FileFindIter(name) != FileFindIter()) {
					return name;
				}
			}
		}
	}
	
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

bool ShareManager::Directory::isRootLevel(ProfileToken aProfile) {
	return profileDir && profileDir->hasRootProfile(aProfile) ? true : false;
}

bool ShareManager::Directory::hasProfile(const ProfileTokenSet& aProfiles) {
	if (profileDir && profileDir->hasRootProfile(aProfiles))
		return true;
	if (parent)
		return parent->hasProfile(aProfiles);
	return false;
}


void ShareManager::Directory::copyRootProfiles(ProfileTokenSet& aProfiles) {
	if (profileDir) {
		copy(profileDir->getRootProfiles() | map_keys, inserter(aProfiles, aProfiles.begin()));
	}

	if (parent)
		parent->copyRootProfiles(aProfiles);
}

bool ShareManager::ProfileDirectory::hasRootProfile(const ProfileTokenSet& aProfiles) {
	for(auto i = aProfiles.begin(); i != aProfiles.end(); ++i) {
		if (rootProfiles.find(*i) != rootProfiles.end())
			return true;
	}
	return false;
}

bool ShareManager::Directory::hasProfile(ProfileToken aProfile) {
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

bool ShareManager::ProfileDirectory::hasRootProfile(ProfileToken aProfile) {
	return rootProfiles.find(aProfile) != rootProfiles.end();
}

bool ShareManager::ProfileDirectory::isExcluded(const ProfileTokenSet& aProfiles) {
	return std::search(excludedProfiles.begin(), excludedProfiles.end(), aProfiles.begin(), aProfiles.end()) != aProfiles.end();
}

bool ShareManager::Directory::isLevelExcluded(ProfileToken aProfile) {
	if (profileDir && profileDir->isExcluded(aProfile))
		return true;
	return false;
}

bool ShareManager::Directory::isLevelExcluded(const ProfileTokenSet& aProfiles) {
	if (profileDir && profileDir->isExcluded(aProfiles))
		return true;
	return false;
}

bool ShareManager::ProfileDirectory::isExcluded(ProfileToken aProfile) {
	return !excludedProfiles.empty() && excludedProfiles.find(aProfile) != excludedProfiles.end();
}

ShareManager::ProfileDirectory::ProfileDirectory(const string& aRootPath, const string& aVname, ProfileToken aProfile) : path(aRootPath) { 
	rootProfiles[aProfile] = aVname;
	setFlag(FLAG_ROOT);
}

ShareManager::ProfileDirectory::ProfileDirectory(const string& aRootPath, ProfileToken aProfile) : path(aRootPath) { 
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

string ShareManager::ProfileDirectory::getName(ProfileToken aProfile) {
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
	auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
	if(i != shareProfiles.end()) {
		dcassert((*i)->getProfileList());
		return (*i)->getProfileList();
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	
	//return shareProfiles[SP_DEFAULT]->second->getProfileList();
}

string ShareManager::getFileListName(const string& virtualFile, ProfileToken aProfile) {
	if(virtualFile == "MyList.DcLst") 
		throw ShareException("NMDC-style lists no longer supported, please upgrade your client");

	if(virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
		FileList* fl = generateXmlList(aProfile);
		return fl->getFileName();
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

void ShareManager::toRealWithSize(const string& virtualFile, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_) {
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		TTHValue tth(virtualFile.substr(4));

		if(any_of(aProfiles.begin(), aProfiles.end(), [](ProfileToken s) { return s != SP_HIDDEN; })) {
			RLock l(cs);
			auto flst = tthIndex.equal_range(const_cast<TTHValue*>(&tth));
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

		Lock l(tScs);
		auto files = tempShares.equal_range(tth);
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

		auto fileName = Util::getFileName(Util::toNmdcFile(virtualFile));
		for(auto v = dirs.begin(); v != dirs.end(); ++v) {
			auto it = find_if((*v)->files, Directory::File::StringComp(fileName));
			if(it != (*v)->files.end()) {
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
	Lock l(tScs);
	auto fp = tempShares.equal_range(tth);
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
		Lock l(tScs);
		auto Files = tempShares.equal_range(tth);
		for(auto i = Files.first; i != Files.second; ++i) {
			if(i->second.key == aKey)
				return;
		}
		//didnt exist.. fine, add it.
		tempShares.insert(make_pair(tth, TempShareInfo(aKey, filePath, aSize)));
	}
}
void ShareManager::removeTempShare(const string& aKey, const TTHValue& tth) {
	Lock l(tScs);
	auto Files = tempShares.equal_range(tth);
	for(auto i = Files.first; i != Files.second; ++i) {
		if(i->second.key == aKey) {
			tempShares.erase(i);
			break;
		}
	}
}

void ShareManager::getRealPaths(const string& path, StringList& ret, ProfileToken aProfile) {
	if(path.empty())
		throw ShareException("empty virtual path");

	DirectoryList dirs;

	RLock l (cs);
	findVirtuals<ProfileToken>(path, aProfile, dirs);

	if(*(path.end() - 1) == '/') {
		for(auto i = dirs.begin(); i != dirs.end(); ++i) {
			ret.push_back((*i)->getRealPath(true));
		}
	} else { //its a file
		auto fileName = Util::getFileName(Util::toNmdcFile(path));
		for(auto v = dirs.begin(); v != dirs.end(); ++v) {
			auto it = find_if((*v)->files, Directory::File::StringComp(fileName));
			if(it != (*v)->files.end()) {
				ret.push_back(it->getRealPath());
				return;
			}
		}
	}
}
string ShareManager::validateVirtual(const string& aVirt) const noexcept {
	string tmp = aVirt;
	string::size_type idx = 0;

	while( (idx = tmp.find_first_of("\\/"), idx) != string::npos) {
		tmp[idx] = '_';
	}
	return tmp;
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
			pd = ProfileDirectory::Ptr(new ProfileDirectory(realPath, virtualName, aToken));
			profileDirs[realPath] = pd;
		}

		auto j = shares.find(realPath);
		if (j == shares.end()) {
			auto dir = Directory::create(virtualName, nullptr, 0, pd);
			shares[realPath] = dir;
		}

		if (aXml.getBoolChildAttrib("Incoming"))
			pd->setFlag(ProfileDirectory::FLAG_INCOMING);
	}

	aXml.resetCurrentChild();
	if(aXml.findChild("NoShare")) {
		aXml.stepIn();
		while(aXml.findChild("Directory")) {
			auto path = aXml.getChildData();

			auto p = profileDirs.find(path);
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

ShareProfilePtr ShareManager::getShareProfile(ProfileToken aProfile, bool allowFallback /*false*/) {
	RLock l (cs);
	auto p = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
	if (p != shareProfiles.end()) {
		return *p;
	} else if (allowFallback) {
		dcassert(aProfile != SP_DEFAULT);
		return *shareProfiles.begin();
	}
	return nullptr;
}

static const string SDIRECTORY = "Directory";
static const string SFILE = "File";
static const string SNAME = "Name";
static const string SSIZE = "Size";
static const string STTH = "TTH";
static const string PATH = "Path";
static const string DATE = "Date";

struct ShareLoader : public SimpleXMLReader::CallBack {
	ShareLoader(ShareManager::ProfileDirMap& aDirs) : profileDirs(aDirs), cur(nullptr), depth(0), blockNode(false), hashSize(0) { }
	void startTag(const string& name, StringPairList& attribs, bool simple) {

		if(name == SDIRECTORY) {
			if (!blockNode || depth == 0) {
				blockNode = false;
				const string& name = getAttrib(attribs, SNAME, 0);
				curDirPath = getAttrib(attribs, PATH, 1);
				const string& date = getAttrib(attribs, DATE, 2);

				if(curDirPath[curDirPath.length() - 1] != PATH_SEPARATOR)
					curDirPath += PATH_SEPARATOR;

				if(!name.empty()) {
					cur = ShareManager::Directory::create(name, cur, Util::toUInt32(date));
					auto i = profileDirs.find(curDirPath);
					if(i != profileDirs.end()) {
						cur->setProfileDir(i->second);
						if (i->second->hasRoots())
							ShareManager::getInstance()->addShares(curDirPath, cur);
					} else if (depth == 0) {
						// Something wrong... all roots need to have profile directory
						// Block the whole node so the directories won't be added in the dupe list
						cur = nullptr;
						blockNode = true;
						depth++;
						return;
					}

					dirs.insert(make_pair(name, cur));
					lastFileIter = cur->files.begin();
				}
			}

			if(simple) {
				if(cur) {
					cur = cur->getParent();
					if(cur)
						lastFileIter = cur->files.begin();
				}
			} else {
				depth++;
			}
		} else if(cur && name == SFILE) {
			const string& fname = getAttrib(attribs, SNAME, 0);
			const string& size = getAttrib(attribs, SSIZE, 1);   
			if(fname.empty() || size.empty() ) {
				dcdebug("Invalid file found: %s\n", fname.c_str());
				return;
			}
			/*dont save TTHs, check them from hashmanager, just need path and size.
			this will keep us sync to hashindex */
			try {
				lastFileIter = cur->files.insert(lastFileIter, ShareManager::Directory::File(fname, Util::toInt64(size), cur, SettingsManager::lanMode ? TTHValue() : HashManager::getInstance()->getTTH(curDirPath + fname, Util::toInt64(size))));
			}catch(Exception& e) {
				hashSize += Util::toInt64(size);
				dcdebug("Error loading filelist %s \n", e.getError().c_str());
			}
		}
	}
	void endTag(const string& name) {
		if(name == SDIRECTORY) {
			depth--;
			if(cur) {
				curDirPath = Util::getParentDir(curDirPath);
				cur = cur->getParent();
				if(cur)
					lastFileIter = cur->files.begin();
			}
		}
	}

	int64_t hashSize;
	ShareManager::DirMultiMap dirs;
private:
	ShareManager::ProfileDirMap& profileDirs;

	ShareManager::Directory::File::Set::iterator lastFileIter;
	ShareManager::Directory::Ptr cur;

	bool blockNode;
	size_t depth;
	string curDirPath;
};

bool ShareManager::loadCache() {
	try {
		HashManager::HashPauser pauser;
		ShareLoader loader(profileDirs);
		//look for shares.xml
		dcpp::File ff(Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml", dcpp::File::READ, dcpp::File::OPEN, false);
		SimpleXMLReader(&loader).parse(ff);
		dirNameMap = loader.dirs;
		if (loader.hashSize > 0) {
			LogManager::getInstance()->message(STRING_F(FILES_ADDED_FOR_HASH_STARTUP, Util::formatBytes(loader.hashSize)), LogManager::LOG_INFO);
		}

		rebuildIndices();
	}catch(SimpleXMLException& e) {
		LogManager::getInstance()->message("Error Loading shares.xml: "+ e.getError(), LogManager::LOG_ERROR);
		return false;
	} catch(...) {
		return false;
	}

	return true;
}

void ShareManager::save(SimpleXML& aXml) {
	RLock l(cs);
	for(auto i = shareProfiles.begin(); i != shareProfiles.end(); ++i) {
		if ((*i)->getToken() == SP_HIDDEN) {
			continue;
		}

		aXml.addTag((*i)->getToken() == SP_DEFAULT ? "Share" : "ShareProfile");
		aXml.addChildAttrib("Token", (*i)->getToken());
		aXml.addChildAttrib("Name", (*i)->getPlainName());
		aXml.stepIn();

		for(auto p = shares.begin(); p != shares.end(); ++p) {
			if (!p->second->getProfileDir()->hasRootProfile((*i)->getToken()))
				continue;
			aXml.addTag("Directory", p->first);
			aXml.addChildAttrib("Virtual", p->second->getProfileDir()->getName((*i)->getToken()));
			//if (p->second->getRoot()->hasFlag(ProfileDirectory::FLAG_INCOMING))
			aXml.addChildAttrib("Incoming", p->second->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING));
		}

		aXml.addTag("NoShare");
		aXml.stepIn();
		for(auto j = profileDirs.begin(); j != profileDirs.end(); ++j) {
			if (j->second->isExcluded((*i)->getToken()))
				aXml.addTag("Directory", j->second->getPath());
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
		char buf[MAX_PATH];
		snprintf(buf, sizeof(buf), CSTRING(CHECK_FORBIDDEN), realPath.c_str());
		throw ShareException(buf);
	}
#endif
}

void ShareManager::getByVirtual(const string& virtualName, ProfileToken aProfile, DirectoryList& dirs) const noexcept {
	for(auto i = shares.begin(); i != shares.end(); ++i) {
		if(i->second->getProfileDir()->hasRootProfile(aProfile) && stricmp(i->second->getProfileDir()->getName(aProfile), virtualName) == 0) {
			dirs.push_back(i->second);
		}
	}
}

void ShareManager::getByVirtual(const string& virtualName, const ProfileTokenSet& aProfiles, DirectoryList& dirs) const noexcept {
	for(auto i = shares.begin(); i != shares.end(); ++i) {
		auto& spList = i->second->getProfileDir()->getRootProfiles();
		for(auto k = spList.begin(); k != spList.end(); ++k) {
			if(aProfiles.find(k->first) != aProfiles.end() && stricmp(k->second, virtualName) == 0) {
				dirs.push_back(i->second);
			}
		}
	}
}

int64_t ShareManager::getShareSize(const string& realPath, ProfileToken aProfile) const noexcept {
	RLock l(cs);
	auto j = shares.find(realPath);
	if(j != shares.end()) {
		return j->second->getSize(aProfile);
	}
	return -1;

}

void ShareManager::Directory::getProfileInfo(ProfileToken aProfile, int64_t& totalSize, size_t& filesCount) const {
	totalSize += size;
	filesCount += files.size();

	for(auto i = directories.begin(); i != directories.end(); ++i) {
		if (i->second->isLevelExcluded(aProfile))
			continue;
		i->second->getProfileInfo(aProfile, totalSize, filesCount);
	}
}

void ShareManager::getProfileInfo(ProfileToken aProfile, int64_t& size, size_t& files) const {
	RLock l(cs);
	for(auto i = shares.begin(); i != shares.end(); ++i) {
		if(i->second->getProfileDir()->hasRootProfile(aProfile)) {
			i->second->getProfileInfo(aProfile, size, files);
		}
	}
}

int64_t ShareManager::getTotalShareSize(ProfileToken aProfile) const noexcept {
	int64_t ret = 0;

	RLock l(cs);
	for(auto i = shares.begin(); i != shares.end(); ++i) {
		if(i->second->getProfileDir()->hasRootProfile(aProfile)) {
			ret += i->second->getSize(aProfile);
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

	auto directories = dirNameMap.equal_range(dir);
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
	auto files = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
	for(auto i = files.first; i != files.second; ++i) {
		if(stricmp(fileName.c_str(), i->second->getName().c_str()) == 0) {
			return true;
		}
	}
	return false;
}

bool ShareManager::isFileShared(const TTHValue& aTTH, const string& fileName, ProfileToken aProfile) const {
	RLock l (cs);
	auto files = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
	for(auto i = files.first; i != files.second; ++i) {
		if((stricmp(fileName.c_str(), i->second->getName().c_str()) == 0) && i->second->getParent()->hasProfile(aProfile)) {
			return true;
		}
	}
	return false;
}

bool ShareManager::isFileShared(const string& aFileName, int64_t aSize) const {
	RLock l (cs);
	for(auto i = tthIndex.begin(); i != tthIndex.end(); ++i) {
		if(stricmp(aFileName.c_str(), i->second->getName().c_str()) == 0 && i->second->getSize() == aSize) {
			return true;
		}
	}

	return false;
}

void ShareManager::removeDir(ShareManager::Directory::Ptr aDir) {
	for_each(aDir->directories | map_values, [this](Directory::Ptr d) { removeDir(d); });

	//speed this up a bit
	auto directories = dirNameMap.equal_range(aDir->getRealName());
	string realPath = aDir->getRealPath(false);
	
	auto p = find_if(directories | map_values, [&realPath](const Directory::Ptr d) { return d->getRealPath(false) == realPath; });
	//dcassert(p != directories.second);
	if (p.base() != dirNameMap.end())
		dirNameMap.erase(p.base());
}

void ShareManager::buildTree(const string& aPath, const Directory::Ptr& aDir, bool checkQueued, const ProfileDirMap& aSubRoots, DirMultiMap& aDirs, DirMap& newShares, int64_t& hashSize) {
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

		if(!BOOLSETTING(SHARE_HIDDEN) && i->isHidden())
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
			aDirs.insert(make_pair(name, dir));
			if (profileDir && profileDir->isSet(ProfileDirectory::FLAG_ROOT))
				newShares[curPath] = dir;

			buildTree(curPath, dir, checkQueued, aSubRoots, aDirs, newShares, hashSize);
		} else {
			// Not a directory, assume it's a file...
			string path = aPath + name;
			int64_t size = i->getSize();

			if (!checkSharedName(path, false, true, size)) {
				continue;
			}

			try {
				if (SettingsManager::lanMode) {
					lastFileIter = aDir->files.insert(lastFileIter, Directory::File(name, size, aDir, TTHValue()));
				} else if(HashManager::getInstance()->checkTTH(path, size, i->getLastWriteTime())) {
					lastFileIter = aDir->files.insert(lastFileIter, Directory::File(name, size, aDir, HashManager::getInstance()->getTTH(path, size)));
				} else {
					hashSize += size;
				}
			} catch(const HashException&) {
			}
		}
	}
}

bool ShareManager::checkHidden(const string& aName) const {
	FileFindIter ff = FileFindIter(aName.substr(0, aName.size() - 1));

	if (ff != FileFindIter()) {
		return (BOOLSETTING(SHARE_HIDDEN) || !ff->isHidden());
	}

	return true;
}

uint32_t ShareManager::findLastWrite(const string& aName) const {
	FileFindIter ff = FileFindIter(aName.substr(0, aName.size() - 1));

	if (ff != FileFindIter()) {
		return ff->getLastWriteTime();
	}

	return 0;
}

void ShareManager::updateIndices(Directory& dir) {
	dir.size = 0;
	if (dir.getProfileDir() && dir.getProfileDir()->hasRoots()) {
		auto& profiles = dir.getProfileDir()->getRootProfiles();
		for(auto k = profiles.begin(); k != profiles.end(); ++k) {
			bloom.add(Text::toLower(k->second));
		}
	} else {
		bloom.add(Text::toLower(dir.getRealName()));
	}

	for(auto i = dir.directories.begin(); i != dir.directories.end(); ++i) {
		updateIndices(*i->second);
	}

	for(auto i = dir.files.begin(); i != dir.files.end(); ) {
		updateIndices(dir, i++);
	}
}

void ShareManager::rebuildIndices() {
	sharedSize = 0;
	bloom.clear();
	tthIndex.clear();

	DirMap parents;
	getParents(parents);
	for(auto i = parents.begin(); i != parents.end(); ++i) {
		updateIndices(*i->second);
	}
}

void ShareManager::updateIndices(Directory& dir, const Directory::File::Set::iterator& i) {
	const Directory::File& f = *i;
	dir.size += f.getSize();
	sharedSize += f.getSize();

	dir.addType(getType(f.getName()));

	tthIndex.insert(make_pair(const_cast<TTHValue*>(&f.getTTH()), i));
	bloom.add(Text::toLower(f.getName()));
}

int ShareManager::refresh(const string& aDir){
	string path = aDir;

	if(path[ path.length() -1 ] != PATH_SEPARATOR)
		path += PATH_SEPARATOR;

	StringList refreshPaths;
	string displayName;

	{
		RLock l(cs);
		auto i = shares.find(path); //case insensitive
		if(i == shares.end()) {
			//check if it's a virtual path

			StringList vNames;
			for(auto j = shares.begin(); j != shares.end(); ++j) {
				auto& profiles = j->second->getProfileDir()->getRootProfiles();
				for(auto k = profiles.begin(); k != profiles.end(); ++k) {
					if(stricmp(k->second, aDir ) == 0 ) {
						refreshPaths.push_back(j->first);
						for_each(profiles, [&vNames](pair <ProfileToken, string> tp) { 
							if (find(vNames.begin(), vNames.end(), tp.second) == vNames.end()) 
								vNames.push_back(tp.second); 
						});
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

	return addTask(REFRESH_DIR, refreshPaths, displayName);
}


int ShareManager::refresh(bool incoming /*false*/, bool isStartup /*false*/){
	StringList dirs;

	DirMap parents;
	{
		RLock l (cs);
		getParents(parents);
	}

	for(auto i = parents.begin(); i != parents.end(); ++i) {
		if (incoming && !i->second->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING))
			continue;
		dirs.push_back(i->first);
	}

	return addTask(incoming ? REFRESH_INCOMING : REFRESH_ALL, dirs, Util::emptyString, isStartup);
}

struct ShareTask : public Task {
	ShareTask(const StringList& aDirs, const string& aDisplayName) : dirs(aDirs), displayName(aDisplayName) { }
	StringList dirs;
	string displayName;
};

int ShareManager::addTask(uint8_t aType, StringList& dirs, const string& displayName /*Util::emptyString*/, bool isStartup /*false*/) noexcept {
	if (dirs.empty()) {
		return REFRESH_PATH_NOT_FOUND;
	}

	{
		//remove directories that have already been queued for refreshing
		Lock l(tasks.cs);
		auto& tq = tasks.getTasks();
		for(auto i = tq.begin(); i != tq.end(); ++i) {
			auto t = static_cast<ShareTask*>(i->second.get());
			dirs.erase(boost::remove_if(dirs, [t](const string& s) { return boost::find(t->dirs, s) != t->dirs.end(); }), dirs.end());
		}
	}

	if (dirs.empty()) {
		return REFRESH_ALREADY_QUEUED;
	}

	tasks.add(aType, unique_ptr<Task>(new ShareTask(dirs, displayName)));

	if(refreshing.test_and_set()) {
		string msg;
		switch (aType) {
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

	join();
	try {
		start();
		if(isStartup) { 
			join();
		} else {
			setThreadPriority(Thread::NORMAL);
		}

	} catch(const ThreadException& e) {
		LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_FAILED) + " " + e.getError(), LogManager::LOG_WARNING);
		refreshing.clear();
	}

	return REFRESH_STARTED;
}

void ShareManager::getParents(DirMap& aDirs) const {
	for(auto i = shares.begin(); i != shares.end(); ++i) {
		if (find_if(shares | map_keys, [i](const string& path) { return AirUtil::isSub(i->first, path); } ).base() == shares.end())
			aDirs.insert(*i);
	}
}

void ShareManager::getParentPaths(StringList& aDirs) const {
	//removes subroots from shared dirs
	RLock l (cs);
	for(auto i = shares.begin(); i != shares.end(); ++i) {
		if (find_if(shares | map_keys, [i](const string& path) { return AirUtil::isSub(i->first, path); } ).base() == shares.end())
			aDirs.push_back(i->first);
	}
}

ShareManager::ProfileDirMap ShareManager::getSubProfileDirs(const string& aPath) {
	ProfileDirMap aRoots;
	for(auto i = profileDirs.begin(); i != profileDirs.end(); ++i) {
		if (AirUtil::isSub(i->first, aPath)) {
			aRoots[i->second->getPath()] = i->second;
		}
	}

	return aRoots;
}

void ShareManager::addProfiles(const ShareProfile::set& aProfiles) {
	WLock l (cs);
	for(auto i = aProfiles.begin(); i != aProfiles.end(); ++i) {
		shareProfiles.insert(shareProfiles.end()-1, *i);
	}
}

void ShareManager::removeProfiles(ProfileTokenList aProfiles) {
	WLock l (cs);
	for_each(aProfiles, [this](ProfileToken aProfile) { shareProfiles.erase(remove(shareProfiles.begin(), shareProfiles.end(), aProfile), shareProfiles.end()); });
}

void ShareManager::addDirectories(const ShareDirInfo::list& aNewDirs) {
	StringList add, refresh;
	ProfileTokenSet dirtyProfiles;

	{
		WLock l (cs);
		for(auto p = aNewDirs.begin(); p != aNewDirs.end(); ++p) {
			ShareDirInfo* d = *p;
			auto i = shares.find(d->path);
			if (i != shares.end()) {
				// Trying to share an already shared root
				i->second->getProfileDir()->addRootProfile(d->vname, d->profile);
				dirtyProfiles.insert(d->profile);
			} else {
				auto p = find_if(shares | map_keys, [d](const string& path) { return AirUtil::isSub(d->path, path); });
				if (p.base() != shares.end()) {
					// It's a subdir
					auto dir = findDirectory(d->path, false, false);
					if (dir) {
						if (dir->getProfileDir()) {
							//an existing subroot exists
							dcassert(dir->getProfileDir()->hasExcludes());
							dir->getProfileDir()->addRootProfile(d->vname, d->profile);
						} else {
							auto root = ProfileDirectory::Ptr(new ProfileDirectory(d->path, d->vname, d->profile));
							dir->setProfileDir(root);
							profileDirs[d->path] = root;
						}
						shares[d->path] = dir;
						dirtyProfiles.insert(d->profile);
					} else {
						//this is probably in an excluded dirs of an existing root, add it
						dir = findDirectory(d->path, true, true, false);
						if (dir) {
							auto root = ProfileDirectory::Ptr(new ProfileDirectory(d->path, d->vname, d->profile));
							profileDirs[d->path] = root;
							shares[d->path] = dir;
							refresh.push_back(*p); //refresh the top directory.....
						}
					}
				} else {
					// It's a new parent, will be handled in the task thread
					auto root = ProfileDirectory::Ptr(new ProfileDirectory(d->path, d->vname, d->profile));
					//root->setFlag(ProfileDirectory::FLAG_ADD);
					Directory::Ptr dp = Directory::create(Util::getLastDir(d->path), nullptr, findLastWrite(d->path), root);
					shares[d->path] = dp;
					profileDirs[d->path] = root;
					add.push_back(d->path);
				}
			}
		}
	}

	rebuildTotalExcludes();
	if (!refresh.empty())
		addTask(REFRESH_DIR, refresh);

	if (add.empty()) {
		//we are only modifying existing trees
		setDirty(dirtyProfiles, false);
		return;
	}

	addTask(ADD_DIR, add);
}

void ShareManager::removeDirectories(const ShareDirInfo::list& aRemoveDirs) {
	ProfileTokenSet dirtyProfiles;
	bool rebuildIncides = false;

	{
		WLock l (cs);
		for(auto i = aRemoveDirs.begin(); i != aRemoveDirs.end(); ++i) {
			auto k = shares.find((*i)->path);
			if (k != shares.end()) {
				dirtyProfiles.insert((*i)->profile);

				auto d = k->second;
				if (d->getProfileDir()->removeRootProfile((*i)->profile)) {
					//can we remove the profile dir?
					if (!d->getProfileDir()->hasExcludes()) {
						//dcassert(profileDirs.find((*i)->path) != profileDirs.end());
						profileDirs.erase((*i)->path);
					}

					shares.erase(k);
					if (d->getParent()) {
						//the content still stays shared.. just null the profile
						d->setProfileDir(nullptr);
						continue;
					}

					removeDir(d);

					//no parent directories, check if we have any child roots for other profiles inside this tree and get the most top one
					Directory::Ptr subDir = nullptr;
					for(auto p = shares.begin(); p != shares.end(); ++p) {
						if(strnicmp((*i)->path, p->first, (*i)->path.length()) == 0 && (!subDir || p->first.length() < subDir->getProfileDir()->getPath().length())) {
							subDir = p->second;
						}
					}

					if (subDir) {
						//this dir becomes the new parent
						subDir->setParent(nullptr);
					}

					rebuildIncides = true;
				}
			}
		}

		if (rebuildIncides)
			rebuildIndices();
	}

	rebuildTotalExcludes();
	setDirty(dirtyProfiles, rebuildIncides);
}

void ShareManager::changeDirectories(const ShareDirInfo::list& changedDirs)  {
	//updates the incoming status and the virtual name (called from GUI)

	ProfileTokenSet dirtyProfiles;
	for(auto i = changedDirs.begin(); i != changedDirs.end(); ++i) {
		string vName = validateVirtual((*i)->vname);
		dirtyProfiles.insert((*i)->profile);

		WLock l(cs);
		auto p = shares.find((*i)->path);
		if (p != shares.end()) {
			p->second->getProfileDir()->addRootProfile(vName, (*i)->profile); //renames it really
			auto pd = p->second->getProfileDir();
			(*i)->incoming ? p->second->getProfileDir()->setFlag(ProfileDirectory::FLAG_INCOMING) : p->second->getProfileDir()->unsetFlag(ProfileDirectory::FLAG_INCOMING);
		}
	}

	setDirty(dirtyProfiles, false);
}

void ShareManager::reportTaskStatus(uint8_t aTask, const StringList& directories, bool finished, int64_t aHashSize, const string& displayName) {
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
			msg = finished ? STRING(FILE_LIST_REFRESH_INITIATED_INCOMING) : STRING(INCOMING_REFRESHED);
			break;
	};

	if (!msg.empty()) {
		if (aHashSize > 0) {
			msg += " " + STRING_F(FILES_ADDED_FOR_HASH, Util::formatBytes(aHashSize));
		}
		LogManager::getInstance()->message(msg, LogManager::LOG_INFO);
	}
}

int ShareManager::run() {
	HashManager::HashPauser pauser;
	refreshRunning = true;

	for (;;) {
		TaskQueue::TaskPair t;
		if (!tasks.getFront(t))
			break;
		ScopedFunctor([this] { tasks.pop_front(); });

		int64_t hashSize = 0;
		vector<pair<string, pair<Directory::Ptr, ProfileDirMap>>> dirs;
		auto task = static_cast<ShareTask*>(t.second);

		ProfileTokenSet dirtyProfiles;

		//find excluded dirs and sub-roots for each directory being refreshed (they will be passed on to buildTree for matching)
		for(auto i = task->dirs.begin(); i != task->dirs.end(); ++i) {
			RLock l (cs);
			auto d = shares.find(*i);
			if (d != shares.end()) {
				dirs.push_back(make_pair(*i, make_pair(d->second, std::move(getSubProfileDirs(*i)))));
				d->second->copyRootProfiles(dirtyProfiles);
			}
		}

		reportTaskStatus(t.first, task->dirs, false, hashSize, task->displayName);
		if (t.first == REFRESH_INCOMING) {
			lastIncomingUpdate = GET_TICK();
		} else if (t.first == REFRESH_ALL) {
			lastFullUpdate = GET_TICK();
			lastIncomingUpdate = GET_TICK();
		}

		//get unfinished directories
		bundleDirs.clear();
		QueueManager::getInstance()->getForbiddenPaths(bundleDirs, task->dirs);

		DirMultiMap newShareDirs;
		DirMap newShares;

		if(t.first == REFRESH_DIR || t.first == REFRESH_INCOMING || t.first == ADD_DIR) {
			{
				WLock l (cs);
				newShares = shares;

				//recursively remove the content of this dir from the dupe list
				for(auto i = dirs.begin(); i != dirs.end(); ++i) {
					removeDir(i->second.first);
				}
			}

			//erase all sub roots from the new list (they will be readded in buildTree)
			for(auto i = dirs.begin(); i != dirs.end(); ++i) {
				auto m = find_if(newShares | map_keys, [i](const string& path) { return AirUtil::isSub(path, i->first); });
				if(m.base() != newShares.end()) {
					newShares.erase(m.base());
				}
			}
		}
		
		for(auto i = dirs.begin(); i != dirs.end(); ++i) {
			if (checkHidden(i->first)) {
				Directory::Ptr dp = Directory::create(Util::getLastDir(i->first), nullptr, findLastWrite(i->first), i->second.first->getProfileDir());
				newShareDirs.insert(make_pair(Util::getLastDir(i->first), dp));
				newShares[i->first] = dp;
				buildTree(i->first, dp, true, i->second.second, newShareDirs, newShares, hashSize);
				if(aShutdown) goto end;  //abort refresh
			}
		}

		{		
			WLock l(cs);
			shares = newShares;
			if(t.first == REFRESH_DIR || t.first == REFRESH_INCOMING || t.first == ADD_DIR) {
				dirNameMap.insert(newShareDirs.begin(), newShareDirs.end());
			} else {
				dirNameMap = newShareDirs;
			}
				
			rebuildIndices();
		}

		setDirty(dirtyProfiles, true);
			
		if (t.first == REFRESH_STARTUP) {
			generateXmlList(SP_DEFAULT, true);
			saveXmlList();
		} else {
			ClientManager::getInstance()->infoUpdated();
		}

		reportTaskStatus(t.first, task->dirs, true, hashSize, task->displayName);
	}
end:
	{
		WLock l (dirNames);
		bundleDirs.clear();
	}
	refreshRunning = false;
	refreshing.clear();
	return 0;
}

void ShareManager::on(TimerManagerListener::Minute, uint64_t tick) noexcept {

	if(ShareCacheDirty && lastSave + 15*60*1000 <= tick) {
		saveXmlList();
	}

	if(SETTING(AUTO_REFRESH_TIME) > 0 && lastFullUpdate + SETTING(AUTO_REFRESH_TIME) * 60 * 1000 <= tick) {
		lastIncomingUpdate = tick;
		lastFullUpdate = tick;
		refresh(false);
	} else if(SETTING(INCOMING_REFRESH_TIME) > 0 && lastIncomingUpdate + SETTING(INCOMING_REFRESH_TIME) * 60 * 1000 <= tick) {
		lastIncomingUpdate = tick;
		refresh(true);
	}
}

void ShareManager::getShares(ShareDirInfo::map& aDirs) {
	RLock l (cs);
	for(auto i = shares.begin(); i != shares.end(); ++i) {
		auto& profiles = i->second->getProfileDir()->getRootProfiles();
		for(auto p = profiles.begin(); p != profiles.end(); ++p) {
			auto sdi = new ShareDirInfo(p->second, p->first, i->first, i->second->getProfileDir()->isSet(ProfileDirectory::FLAG_INCOMING));
			sdi->size = i->second->getSize(p->first);
			aDirs[p->first].push_back(sdi);
		}
	}

}

/*size_t ShareManager::getSharedFiles(ProfileToken aProfile) const noexcept {
	return boost::count_if(tthIndex | map_values, [aProfile](Directory::File::Set::const_iterator f) { return f->getParent()->hasProfile(aProfile); });
}*/
		
void ShareManager::getBloom(HashBloom& bloom) const {
	RLock l(cs);
	for(auto i = tthIndex.begin(); i != tthIndex.end(); ++i) {
		bloom.add(*i->first);
	}
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
		auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
		if(i == shareProfiles.end()) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		fl = (*i)->getProfileList() ? (*i)->getProfileList() : (*i)->generateProfileList();
	}


	if(fl->generateNew(forced)) {
		try {
			//auto start = GET_TICK();
			{
				File f(fl->getFileName(), File::WRITE, File::TRUNCATE | File::CREATE, false);
				// We don't care about the leaves...
				CalcOutputStream<TTFilter<1024*1024*1024>, false> bzTree(&f);
				FilteredOutputStream<BZFilter, false> bzipper(&bzTree);
				CountOutputStream<false> count(&bzipper);
				CalcOutputStream<TTFilter<1024*1024*1024>, false> newXmlFile(&count);

				newXmlFile.write(SimpleXML::utf8Header);
				newXmlFile.write("<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"/\" Generator=\"DC++ " DCVERSIONSTRING "\">\r\n");

				string tmp;
				string indent = "\t";

				auto root = new FileListDir(Util::emptyString, 0, 0);

				{
					RLock l(cs);
					for_each(shares | map_values | filtered(Directory::HasRootProfile(aProfile)), [aProfile, root](Directory::Ptr d) { 
						d->toFileList(root, aProfile, true);
					});

					//auto end2 = GET_TICK();
					//LogManager::getInstance()->message("Full list directories combined in " + Util::toString(end2-start2) + " ms (" + Util::toString((end2-start2)/1000) + " seconds)", LogManager::LOG_INFO);

					for(auto it2 = root->listDirs.begin(); it2 != root->listDirs.end(); ++it2) {
						it2->second->toXml(newXmlFile, indent, tmp, true);
					}
				}

				delete root;

				newXmlFile.write("</FileListing>");
				newXmlFile.flush();

				fl->setXmlListLen(count.getCount());

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
	}

	return fl;
}

MemoryInputStream* ShareManager::generatePartialList(const string& dir, bool recurse, ProfileToken aProfile) {
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
			for(auto i = shares.begin(); i != shares.end(); ++i) {
				if(i->second->getProfileDir()->hasRootProfile(aProfile)) {
					i->second->toFileList(root.get(), aProfile, recurse);
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
					for(auto it = result.begin(); it != result.end(); ++it) {
						for_each((*it)->directories | map_values, [&](Directory::Ptr d) { if (!d->isLevelExcluded(aProfile)) d->toFileList(root.get(), aProfile, recurse); });
						root->date = max(root->date, (*it)->getLastWrite());
					}
				}
			} catch(...) {
				return NULL;
			}
		}

		xml += "<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + 
			"\" Base=\"" + SimpleXML::escape(dir, tmp, false) + 
			"\" BaseDate=\"" + Util::toString(root->date) +
			"\" Generator=\"DC++ " DCVERSIONSTRING "\">\r\n";

		StringOutputStream sos(xml);
		string indent = "\t";

		for(auto it2 = root->listDirs.begin(); it2 != root->listDirs.end(); ++it2) {
			it2->second->toXml(sos, indent, tmp, recurse);
		}
		root->filesToXml(sos, indent, tmp);

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
		aListDir->listDirs.insert(make_pair(n, newListDir));
	}

	newListDir->shareDirs.push_back(this);

	if (isFullList)
		for_each(directories | map_values, [&](Ptr d) { if (!d->isLevelExcluded(aProfile)) d->toFileList(newListDir, aProfile, isFullList); });
}

ShareManager::FileListDir::FileListDir(const string& aName, int64_t aSize, int aDate) : name(aName), size(aSize), date(aDate) { }

#define LITERAL(n) n, sizeof(n)-1
void ShareManager::FileListDir::toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool fullList) {
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
		for(auto i = listDirs.begin(); i != listDirs.end(); ++i) {
			i->second->toXml(xmlFile, indent, tmp2, fullList);
		}

		filesToXml(xmlFile, indent, tmp2);

		indent.erase(indent.length()-1);
		xmlFile.write(indent);
		xmlFile.write(LITERAL("</Directory>\r\n"));
	} else {
		if(find_if(shareDirs, [](Directory::Ptr d) { return !d->files.empty() || !d->directories.empty(); } ) == shareDirs.end()) {
			xmlFile.write(LITERAL("\" />\r\n"));
		} else {
			xmlFile.write(LITERAL("\" Incomplete=\"1\" />\r\n"));
		}
	}
}

void ShareManager::FileListDir::filesToXml(OutputStream& xmlFile, string& indent, string& tmp2) {
	bool filesAdded = false;
	for(auto di = shareDirs.begin(); di != shareDirs.end(); ++di) {
		if (filesAdded) {
			for(auto fi = (*di)->files.begin(); fi != (*di)->files.end(); ++fi) {
				//go through the dirs that we have added already
				if (find_if(shareDirs.begin(), di-1, [fi](Directory::Ptr d) { return d->files.find(*fi) != d->files.end(); }) ==  shareDirs.end()) {
					fi->toXml(xmlFile, indent, tmp2);
				}
			}
		} else if (!(*di)->files.empty()) {
			filesAdded = true;
			for_each((*di)->files, [&](const Directory::File& f) { f.toXml(xmlFile, indent, tmp2); });
		}
	}
}

void ShareManager::Directory::File::toXml(OutputStream& xmlFile, string& indent, string& tmp2) const {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<File Name=\""));
	xmlFile.write(SimpleXML::escape(name, tmp2, true));
	xmlFile.write(LITERAL("\" Size=\""));
	xmlFile.write(Util::toString(size));
	xmlFile.write(LITERAL("\" TTH=\""));
	tmp2.clear();
	xmlFile.write(tth.toBase32(tmp2));
	xmlFile.write(LITERAL("\"/>\r\n"));
}

ShareManager::FileListDir::~FileListDir() {
	for_each(listDirs | map_values, DeleteFunction());
}

#define LITERAL(n) n, sizeof(n)-1

void ShareManager::saveXmlList(bool verbose /* false */) {

	if(xml_saving)
		return;

	xml_saving = true;

	string indent;
	try {
		//create a backup first incase we get interrupted on creation.
		string newCache = Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml.tmp";
		File ff(newCache, File::WRITE, File::TRUNCATE | File::CREATE);
		BufferedOutputStream<false> xmlFile(&ff);
	
		xmlFile.write(SimpleXML::utf8Header);
		xmlFile.write(LITERAL("<Share Version=\"" SHARE_CACHE_VERSION "\">\r\n"));
		indent +='\t';

		{
			RLock l(cs);
			for(auto i = shares.begin(); i != shares.end(); ++i) {
				i->second->toXmlList(xmlFile, i->first, indent);
			}
		}

		xmlFile.write(LITERAL("</Share>"));
		xmlFile.flush();
		ff.close();
		File::deleteFile(Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml");
		File::renameFile(newCache,  (Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml"));
	}catch(Exception& e){
		LogManager::getInstance()->message("Error Saving Shares.xml: " + e.getError(), LogManager::LOG_WARNING);
	}

	//delete xmlFile;
	xml_saving = false;
	ShareCacheDirty = false;
	lastSave = GET_TICK();
	if (verbose)
		LogManager::getInstance()->message("shares.xml saved.", LogManager::LOG_INFO);
}

void ShareManager::Directory::toXmlList(OutputStream& xmlFile, const string& path, string& indent){
	string tmp, tmp2;
	
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(realName, tmp, true));
	xmlFile.write(LITERAL("\" Path=\""));
	xmlFile.write(SimpleXML::escape(path, tmp, true));
	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(SimpleXML::escape(Util::toString(lastWrite), tmp, true));
	xmlFile.write(LITERAL("\">\r\n"));

	indent += '\t';
	for(auto i = directories.begin(); i != directories.end(); ++i) {
		i->second->toXmlList(xmlFile, path + i->first + PATH_SEPARATOR, indent);
	}

	for(auto i = files.begin(); i != files.end(); ++i) {
		const Directory::File& f = *i;

		xmlFile.write(indent);
		xmlFile.write(LITERAL("<File Name=\""));
		xmlFile.write(SimpleXML::escape(f.getName(), tmp2, true));
		xmlFile.write(LITERAL("\" Size=\""));
		xmlFile.write(Util::toString(f.getSize()));
		xmlFile.write(LITERAL("\"/>\r\n"));
	}

	indent.erase(indent.length()-1);
	xmlFile.write(indent);
	xmlFile.write(LITERAL("</Directory>\r\n"));
}

MemoryInputStream* ShareManager::generateTTHList(const string& dir, bool recurse, ProfileToken aProfile) {
	
	if(aProfile == SP_HIDDEN)
		return NULL;
	
	string tths;
	string tmp;
	StringOutputStream sos(tths);

	try{
		RLock l(cs);
		DirectoryList result;
		findVirtuals<ProfileToken>(dir, aProfile, result); 
		for(auto it = result.begin(); it != result.end(); ++it) {
			//dcdebug("result name %s \n", (*it)->getProfileDir()->getName(aProfile));
			(*it)->toTTHList(sos, tmp, recurse);
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

void ShareManager::Directory::toTTHList(OutputStream& tthList, string& tmp2, bool recursive) {
	//dcdebug("toTTHList2");
	if (recursive) {
		for(auto i = directories.begin(); i != directories.end(); ++i) {
			i->second->toTTHList(tthList, tmp2, recursive);
		}
	}
	for(auto i = files.begin(); i != files.end(); ++i) {
		const Directory::File& f = *i;
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
	for(auto k = aStrings.begin(); k != aStrings.end(); ++k) {
		if(k->match(profileDir ? profileDir->getName(SP_DEFAULT) : realName)) {
			if(!newStr.get()) {
				newStr = unique_ptr<StringSearch::List>(new StringSearch::List(aStrings));
			}
			newStr->erase(remove(newStr->begin(), newStr->end(), *k), newStr->end());
		}
	}

	if(newStr.get() != 0) {
		cur = newStr.get();
	}

	bool sizeOk = (aSearchType != SearchManager::SIZE_ATLEAST) || (aSize == 0);
	if( (cur->empty()) && 
		(((aFileType == SearchManager::TYPE_ANY) && sizeOk) || (aFileType == SearchManager::TYPE_DIRECTORY)) ) {
		// We satisfied all the search words! Add the directory...(NMDC searches don't support directory size)
		SearchResultPtr sr(new SearchResult(SearchResult::TYPE_DIRECTORY, 0, getFullName(SP_DEFAULT), TTHValue()));
		aResults.push_back(sr);
	}

	if(aFileType != SearchManager::TYPE_DIRECTORY) {
		for(auto i = files.begin(); i != files.end(); ++i) {
			
			if(aSearchType == SearchManager::SIZE_ATLEAST && aSize > i->getSize()) {
				continue;
			} else if(aSearchType == SearchManager::SIZE_ATMOST && aSize < i->getSize()) {
				continue;
			}

			auto j = cur->begin();
			for(; j != cur->end() && j->match(i->getName()); ++j) 
				;	// Empty
			
			if(j != cur->end())
				continue;
			
			// Check file type...
			if(checkType(i->getName(), aFileType)) {
				SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, i->getSize(), getFullName(SP_DEFAULT) + i->getName(), i->getTTH()));
				aResults.push_back(sr);
				if(aResults.size() >= maxResults) {
					break;
				}
			}
		}
	}

	for(auto l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
		if (l->second->isLevelExcluded(SP_DEFAULT))
			continue;
		l->second->search(aResults, *cur, aSearchType, aSize, aFileType, maxResults);
	}
}
//NMDC Search
void ShareManager::search(SearchResultList& results, const string& aString, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults, bool aHideShare) noexcept {
	if(aFileType == SearchManager::TYPE_TTH) {
		if(aString.compare(0, 4, "TTH:") == 0) {
			TTHValue tth(aString.substr(4));
			
			if(!aHideShare) {
				RLock l (cs);
				auto i = tthIndex.find(const_cast<TTHValue*>(&tth));
				if(i != tthIndex.end() && i->second->getParent()->hasProfile(SP_DEFAULT)) {
					SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, i->second->getSize(), 
						i->second->getParent()->getFullName(SP_DEFAULT) + i->second->getName(), i->second->getTTH()));

					results.push_back(sr);
				} 
			}

			Lock l(tScs);
			auto Files = tempShares.equal_range(tth);
			for(auto i = Files.first; i != Files.second; ++i) {
				if(i->second.key.empty()) { // nmdc shares are shared to everyone.
					SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, i->second.size, "tmp\\" + Util::getFileName(i->second.path), i->first));
					results.push_back(sr);
				}
			}
		}
		return;
	} 
	
	if(aHideShare)
		return;

	StringTokenizer<string> t(Text::toLower(aString), '$');
	StringList& sl = t.getTokens();


	RLock l (cs);
	if(!bloom.match(sl)) {
		return;
	}

	StringSearch::List ssl;
	for(auto i = sl.begin(); i != sl.end(); ++i) {
		if(!i->empty()) {
			ssl.push_back(StringSearch(*i));
		}
	}
	if(ssl.empty())
		return;

	for(auto j = shares.begin(); (j != shares.end()) && (results.size() < maxResults); ++j) {
		if(j->second->getProfileDir()->hasRootProfile(SP_DEFAULT))
			j->second->search(results, ssl, aSearchType, aSize, aFileType, maxResults);
	}
}

/* Each matching directory is only being added once in the results. For directory results we return the path of the parent directory and for files the current directory */
void ShareManager::Directory::directSearch(DirectSearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults, ProfileToken aProfile) const noexcept {
	if(aStrings.matchesDirectDirectoryName(profileDir ? profileDir->getName(aProfile) : realName)) {
		auto path = parent ? parent->getADCPath(aProfile) : "/";
		auto res = find_if(aResults, [path](DirectSearchResultPtr sr) { return sr->getPath() == path; });
		if (res == aResults.end() && aStrings.matchesSize(getSize(aProfile))) {
			DirectSearchResultPtr sr(new DirectSearchResult(path));
			aResults.push_back(sr);
		}
	}

	if(!aStrings.isDirectory) {
		for(auto i = files.begin(); i != files.end(); ++i) {
			if(aStrings.matchesDirectFile((*i).getName(), (*i).getSize())) {
				DirectSearchResultPtr sr(new DirectSearchResult(getADCPath(aProfile)));
				aResults.push_back(sr);
				break;
			}
		}
	}


	for(auto l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
		if (l->second->isLevelExcluded(aProfile))
			continue;
		l->second->directSearch(aResults, aStrings, maxResults, aProfile);
	}
}

void ShareManager::directSearch(DirectSearchResultList& results, AdcSearch& srch, StringList::size_type maxResults, ProfileToken aProfile, const string& aDirectory) noexcept {
	RLock l(cs);
	if(srch.hasRoot) {
		auto flst = tthIndex.equal_range(const_cast<TTHValue*>(&srch.root));
		for(auto f = flst.first; f != flst.second; ++f) {
			if (f->second->getParent()->hasProfile(aProfile)) {
				DirectSearchResultPtr sr(new DirectSearchResult(f->second->getParent()->getADCPath(aProfile)));
				results.push_back(sr);
			}
		}
		return;
	}

	for(auto i = srch.includeX.begin(); i != srch.includeX.end(); ++i) {
		if(!bloom.match(i->getPattern())) {
			return;
		}
	}

	/*for(auto j = dirNameMap.begin(); (j != dirNameMap.end()) && (results.size() < maxResults); ++j) {
		if(j->second->getProfileDir()->hasProfile(aProfile))
			j->second->directSearch(results, srch, maxResults, aProfile);
	}*/

	if (aDirectory.empty() || aDirectory == "/") {
		for(auto j = shares.begin(); (j != shares.end()) && (results.size() < maxResults); ++j) {
			if(j->second->getProfileDir()->hasRootProfile(aProfile))
				j->second->directSearch(results, srch, maxResults, aProfile);
		}
	} else {
		DirectoryList result;
		findVirtuals<ProfileToken>(aDirectory, aProfile, result); 
		Directory::Ptr root;
		for(auto it = result.begin(); it != result.end(); ++it) {
			if (!(*it)->isLevelExcluded(aProfile))
				(*it)->directSearch(results, srch, maxResults, aProfile);
		}
	}
}

void ShareManager::Directory::search(SearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults, ProfileToken aProfile) const noexcept {
	StringSearch::List* old = aStrings.include;

	unique_ptr<StringSearch::List> newStr;

	// Find any matches in the directory name
	for(auto k = aStrings.include->begin(); k != aStrings.include->end(); ++k) {
		if(k->match(profileDir ? profileDir->getName(aProfile) : realName) && !aStrings.isExcluded(profileDir ? profileDir->getName(aProfile) : realName)) {
			if(!newStr.get()) {
				newStr = unique_ptr<StringSearch::List>(new StringSearch::List(*aStrings.include));
			}
			newStr->erase(remove(newStr->begin(), newStr->end(), *k), newStr->end());
		}
	}

	if(newStr.get() != 0) {
		aStrings.include = newStr.get();
	}

	bool sizeOk = (aStrings.gt == 0);
	if( aStrings.include->empty() && aStrings.ext.empty() && sizeOk ) {
		// We satisfied all the search words! Add the directory...
		SearchResultPtr sr(new SearchResult(SearchResult::TYPE_DIRECTORY, getSize(aProfile), getFullName(aProfile), TTHValue()));
		aResults.push_back(sr);
	}

	if(!aStrings.isDirectory) {
		for(auto i = files.begin(); i != files.end(); ++i) {

			if(!(i->getSize() >= aStrings.gt)) {
				continue;
			} else if(!(i->getSize() <= aStrings.lt)) {
				continue;
			}	

			if(aStrings.isExcluded(i->getName()))
				continue;

			auto j = aStrings.include->begin();
			for(; j != aStrings.include->end() && j->match(i->getName()); ++j) 
				;	// Empty

			if(j != aStrings.include->end())
				continue;

			// Check file type...
			if(aStrings.hasExt(i->getName())) {

				SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, 
					i->getSize(), getFullName(aProfile) + i->getName(), i->getTTH()));
				aResults.push_back(sr);
				if(aResults.size() >= maxResults) {
					return;
				}
			}
		}
	}

	for(auto l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
		if (l->second->isLevelExcluded(aProfile))
			continue;
		l->second->search(aResults, aStrings, maxResults, aProfile);
	}

	//faster to check this?
	if (aStrings.include->size() != old->size())
		aStrings.include = old;
}


void ShareManager::search(SearchResultList& results, const StringList& params, StringList::size_type maxResults, ProfileToken aProfile, const CID& cid) noexcept {

	AdcSearch srch(params);	

	RLock l(cs);

	if(srch.hasRoot) {
		auto i = tthIndex.find(const_cast<TTHValue*>(&srch.root));
		if(i != tthIndex.end() && i->second->getParent()->hasProfile(aProfile)) {
			SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, 
				i->second->getSize(), i->second->getParent()->getFullName(aProfile) + i->second->getName(), 
				i->second->getTTH()));
			results.push_back(sr);
			return;
		}

		Lock l(tScs);
		auto Files = tempShares.equal_range(srch.root);
		for(auto i = Files.first; i != Files.second; ++i) {
			if(i->second.key.empty() || (i->second.key == cid.toBase32())) { // if no key is set, it means its a hub share.
				SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, i->second.size, "tmp\\" + Util::getFileName(i->second.path), i->first));
				results.push_back(sr);
			}
		}
		return;
	}

	for(auto i = srch.includeX.begin(); i != srch.includeX.end(); ++i) {
		if(!bloom.match(i->getPattern())) {
			return;
		}
	}

	for(auto j = shares.begin(); (j != shares.end()) && (results.size() < maxResults); ++j) {
		if(j->second->getProfileDir()->hasRootProfile(aProfile))
			j->second->search(results, srch, maxResults, aProfile);
	}
}
void ShareManager::cleanIndices(Directory::Ptr& dir) {
	for(auto i = dir->directories.begin(); i != dir->directories.end(); ++i) {
		removeDir(dir);
		cleanIndices(i->second);
	}

	for(auto i = dir->files.begin(); i != dir->files.end(); ++i) {
		auto flst = tthIndex.equal_range(const_cast<TTHValue*>(&i->getTTH()));
		auto p = find(flst | map_values, i);
		if (p.base() != flst.second)
			tthIndex.erase(p.base());
	}

	dir->files.clear();
	dir->directories.clear();
}

void ShareManager::on(QueueManagerListener::BundleAdded, const BundlePtr aBundle) noexcept {
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

		/* get rid of any existing crap we might have in the bundle directory and refresh it.
		done at this point as the file and directory pointers should still be valid, if there are any */
		cleanIndices(dir);

		ProfileDirMap profileDirs;
		DirMap newShares;
		int64_t hashSize = 0;
		buildTree(path, dir, false, profileDirs, dirNameMap, newShares, hashSize);
		updateIndices(*dir);
		dir->copyRootProfiles(dirtyProfiles);
	}

	setDirty(dirtyProfiles, true);
	LogManager::getInstance()->message(STRING_F(BUNDLE_X_SHARED, Util::getLastDir(path)), LogManager::LOG_INFO);
}

bool ShareManager::allowAddDir(const string& aPath) noexcept {
	//LogManager::getInstance()->message("QueueManagerListener::BundleFilesMoved");
	{
		RLock l(cs);
		auto mi = find_if(shares | map_keys, [&aPath](const string& p) { return AirUtil::isParentOrExact(p, aPath); });
		if (mi.base() != shares.end()) {
			//check the skiplist
			string fullPath = *mi;
			StringList sl = StringTokenizer<string>(aPath.substr(fullPath.length()), PATH_SEPARATOR).getTokens();
			for(auto i = sl.begin(); i != sl.end(); ++i) {
				fullPath += Text::toLower(*i) + PATH_SEPARATOR;
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
	auto mi = find_if(shares, [fname](pair<string, Directory::Ptr> dp) { return AirUtil::isParentOrExact(dp.first, fname); });
	if (mi != shares.end()) {
		auto curDir = mi->second;
		StringList sl = StringTokenizer<string>(fname.substr(mi->first.length()), PATH_SEPARATOR).getTokens();
		string fullPath = Text::toLower(mi->first);
		for(auto i = sl.begin(); i != sl.end(); ++i) {
			fullPath += *i + PATH_SEPARATOR;
			auto j = curDir->directories.find(*i);
			if (j != curDir->directories.end()) {
				curDir = j->second;
			} else if (!allowAdd || !checkSharedName(fullPath, true, report)) {
				return nullptr;
			} else {
				auto m = profileDirs.find(fullPath);
				if (checkExcludes && m != profileDirs.end() && m->second->isSet(ProfileDirectory::FLAG_EXCLUDE_TOTAL)) {
					return nullptr;
				}

				curDir = Directory::create(*i, curDir, GET_TIME(), m != profileDirs.end() ? m->second : nullptr);
				dirNameMap.insert(make_pair(*i, curDir));
			}
		}
		return curDir;
	}
	return nullptr;
}

void ShareManager::onFileHashed(const string& fname, const TTHValue& root) noexcept {
	ProfileTokenSet dirtyProfiles;
	{
		WLock l(cs);
		Directory::Ptr d = findDirectory(Util::getFilePath(fname), true, false);
		if (!d) {
			return;
		}

		auto i = d->findFile(Util::getFileName(fname));
		if(i != d->files.end()) {
			// Get rid of false constness...
			auto flst = tthIndex.equal_range(const_cast<TTHValue*>(&i->getTTH()));
			auto p = find(flst | map_values, i);
			if (p.base() != flst.second)
				tthIndex.erase(p.base());

			Directory::File* f = const_cast<Directory::File*>(&(*i));
			f->setTTH(root);
			tthIndex.insert(make_pair(const_cast<TTHValue*>(&f->getTTH()), i));
		} else {
			string name = Util::getFileName(fname);
			int64_t size = File::getSize(fname);
			auto it = d->files.insert(Directory::File(name, size, d, root)).first;
			updateIndices(*d, it);
		}

		d->copyRootProfiles(dirtyProfiles);
	}

	setDirty(dirtyProfiles, true);
}

void ShareManager::getExcludes(ProfileToken aProfile, StringSet& excludes) {
	for(auto i = profileDirs.begin(); i != profileDirs.end(); ++i) {
		if (i->second->isExcluded(aProfile))
			excludes.insert(i->first);
	}
}

void ShareManager::changeExcludedDirs(const ProfileTokenStringList& aAdd, const ProfileTokenStringList& aRemove) {
	ProfileTokenSet dirtyProfiles;

	{
		WLock l (cs);

		//add new exludes
		for(auto i = aAdd.begin(); i != aAdd.end(); ++i) {
			auto dir = findDirectory(i->second, false, false);
			if (dir) {
				dirtyProfiles.insert(i->first);
				if (dir->getProfileDir()) {
					dir->getProfileDir()->addExclude(i->first);
					continue;
				}
			}

			auto pd = ProfileDirectory::Ptr(new ProfileDirectory(i->second, i->first));
			if (dir)
				dir->setProfileDir(pd);
			profileDirs[i->second] = pd;
		}

		//remove existing excludes
		for(auto i = aRemove.begin(); i != aRemove.end(); ++i) {
			auto pdPos = profileDirs.find(i->second);
			if (pdPos != profileDirs.end() && pdPos->second->removeExcludedProfile(i->first) && !pdPos->second->hasRoots()) {
				profileDirs.erase(pdPos);
			}
		}
	}

	setDirty(dirtyProfiles, false);
	rebuildTotalExcludes();
}

void ShareManager::rebuildTotalExcludes() {
	RLock l (cs);
	for(auto pdPos = profileDirs.begin(); pdPos != profileDirs.end(); ++pdPos) {
		auto pd = pdPos->second;

		//profileDirs also include all shared roots...
		if (!pd->hasExcludes() || pd->hasRoots())
			continue;

		pd->unsetFlag(ProfileDirectory::FLAG_EXCLUDE_TOTAL);

		ProfileTokenSet sharedProfiles;

		//List all profiles where this dir is shared in
		for(auto j = shares.begin(); j != shares.end(); ++j) {
			if (AirUtil::isParentOrExact(j->first, pdPos->first)) {
				j->second->copyRootProfiles(sharedProfiles);
			}
		}


		bool stop = false;
		//Is the directory excluded in all profiles?
		for(auto j = sharedProfiles.begin(); j != sharedProfiles.end(); ++j) {
			if (pd->getExcludedProfiles().find(*j) == pd->getExcludedProfiles().end()) {
				stop = true;
				break;
			}
		}

		if (stop)
			continue;

		//Are there shared roots in subdirs?
		auto subDirs = find_if(profileDirs | map_values, [pdPos](const ProfileDirectory::Ptr spd) { 
			return spd->hasRoots() && AirUtil::isSub(spd->getPath(), pdPos->first); 
		});

		if (subDirs.base() == profileDirs.end()) {
			//LogManager::getInstance()->message(pdPos->first + " is a total exclude", LogManager::LOG_INFO);
			pdPos->second->setFlag(ProfileDirectory::FLAG_EXCLUDE_TOTAL);
		}
	}
}

vector<pair<string, StringList>> ShareManager::getGroupedDirectories() const noexcept {
	vector<pair<string, StringList>> ret;
	DirMap parents;
	
	{
		RLock l (cs);
		getParents(parents);
		for(auto i = shares.begin(); i != shares.end(); ++i) {
			auto& spl = i->second->getProfileDir()->getRootProfiles();
			for(auto p = spl.begin(); p != spl.end(); ++p) {
				auto retVirtual = find_if(ret, CompareFirst<string, StringList>(p->second));
				if (retVirtual != ret.end()) {
					//insert under an old virtual node if the real path doesn't exist there already
					if (find(retVirtual->second, i->first) == retVirtual->second.end()) {
						retVirtual->second.insert(upper_bound(retVirtual->second.begin(), retVirtual->second.end(), i->first), i->first); //insert sorted
					}
				} else {
					StringList tmp;
					tmp.push_back(i->first);
					ret.push_back(make_pair(p->second, tmp));
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
		if(BOOLSETTING(REPORT_SKIPLIST) && report)
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
		if(BOOLSETTING(REMOVE_FORBIDDEN)) {
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

		if(BOOLSETTING(NO_ZERO_BYTE) && !(size > 0))
			return false;

		if ((SETTING(MAX_FILE_SIZE_SHARED) != 0) && (size > (SETTING(MAX_FILE_SIZE_SHARED)*1024*1024))) {
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
	skipList.setMethod(BOOLSETTING(SHARE_SKIPLIST_USE_REGEXP) ? StringMatch::REGEX : StringMatch::WILDCARD);
	skipList.prepare();
}

} // namespace dcpp