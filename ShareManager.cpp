/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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
#include "ShareManager.h"

#include "ResourceManager.h"

#include "CryptoManager.h"
#include "ClientManager.h"
#include "LogManager.h"
#include "HashManager.h"
#include "QueueManager.h"

#include "AdcHub.h"
#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "File.h"
#include "FilteredFile.h"
#include "BZUtils.h"
#include "Transfer.h"
#include "UserConnection.h"
#include "Download.h"
#include "HashBloom.h"
#include "SearchResult.h"
#include "Wildcards.h"

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

ShareManager::ShareManager() : hits(0), xmlListLen(0), bzXmlListLen(0),
	xmlDirty(true), forceXmlRefresh(false), listN(0), refreshing(false),
	lastXmlUpdate(0), lastFullUpdate(GET_TICK()), lastIncomingUpdate(GET_TICK()), bloom(1<<20), sharedSize(0), rebuild(false), ShareCacheDirty(false), GeneratingXmlList(false)
{ 
	SettingsManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);
	QueueManager::getInstance()->addListener(this);
	HashManager::getInstance()->addListener(this);
	releaseReg.Init("(((?=\\S*[A-Za-z]\\S*)[A-Z0-9]\\S{3,})-([A-Za-z0-9]{2,}))");
	releaseReg.study();
	subDirReg.Init("(.*\\\\((((DVD)|(CD)|(DIS(K|C))).?([0-9](0-9)?))|(Sample)|(Proof)|(Cover(s)?)|(.{0,5}Sub(s|pack)?)))", PCRE_CASELESS);
	subDirReg.study();
}

ShareManager::~ShareManager() {

	SettingsManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);
	HashManager::getInstance()->removeListener(this);

	join();
	w.join();
}

void ShareManager::shutdown() {
	if(ShareCacheDirty || !Util::fileExists(Util::getPath(Util::PATH_USER_CONFIG) + "Share.xml.bz2"))
		saveXmlList();

	try {
		StringList lists = File::findFiles(Util::getPath(Util::PATH_USER_CONFIG), "files?*.xml.bz2");
			for(StringList::const_iterator i = lists.begin(); i != lists.end(); ++i) {
				File::deleteFile(*i); // cannot delete the current filelist due to the bzxmlref.
			}
			lists.clear();
			
			//leave the latest filelist undeleted, and rename it to files.xml.bz2
			if(bzXmlRef.get()) 
				bzXmlRef.reset(); 

				if(!Util::fileExists(Util::getPath(Util::PATH_USER_CONFIG) + "files.xml.bz2"))				
					File::renameFile(getBZXmlFile(), (Util::getPath(Util::PATH_USER_CONFIG), "files.xml.bz2")); 
				
		} catch(...) {
		//ignore, we just failed to delete
		}
			
	}

ShareManager::Directory::Directory(const string& aName, const ShareManager::Directory::Ptr& aParent) :
	size(0),
	name(aName),
	parent(aParent.get()),
	fileTypes(1 << SearchManager::TYPE_DIRECTORY),
	fullyHashed(true)//ApexDC
{
}

string ShareManager::Directory::getADCPath() const noexcept {
	if(!getParent())
		return '/' + name + '/';
	return getParent()->getADCPath() + name + '/';
}

string ShareManager::Directory::getFullName() const noexcept {
	if(!getParent())
		return getName() + '\\';
	return getParent()->getFullName() + getName() + '\\';
}

void ShareManager::Directory::addType(uint32_t type) noexcept {
	if(!hasType(type)) {
		fileTypes |= (1 << type);
		if(getParent())
			getParent()->addType(type);
	}
}

string ShareManager::Directory::getRealPath(const std::string& path) const {
	if(getParent()) {
		return getParent()->getRealPath(getName() + PATH_SEPARATOR_STR + path);
	} else {
		return ShareManager::getInstance()->findRealRoot(getName(), path);
	}
}
	
string ShareManager::findRealRoot(const string& virtualRoot, const string& virtualPath) const {
	for(StringMap::const_iterator i = shares.begin(); i != shares.end(); ++i) {
		if(stricmp(i->second, virtualRoot) == 0) {
			std::string name = i->first + virtualPath;
			dcdebug("Matching %s\n", name.c_str());
			if(FileFindIter(name) != FileFindIter()) {
				return name;
			}
		}
	}
	
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

int64_t ShareManager::Directory::getSize() const noexcept {
	int64_t tmp = size;
	for(Map::const_iterator i = directories.begin(); i != directories.end(); ++i)
		tmp+=i->second->getSize();
	return tmp;
}

//ApexDC
size_t ShareManager::Directory::countFiles() const noexcept {
	size_t tmp = files.size();
	for(Map::const_iterator i = directories.begin(); i != directories.end(); ++i)
		tmp+=i->second->countFiles();
	return tmp;
}
//End

string ShareManager::toVirtual(const TTHValue& tth) const  {
	if(tth == bzXmlRoot) {
		return Transfer::USER_LIST_NAME_BZ;
	} else if(tth == xmlRoot) {
		return Transfer::USER_LIST_NAME;
	}

	Lock l(cs);
	HashFileMap::const_iterator i = tthIndex.find(tth);
	if(i != tthIndex.end()) {
		return i->second->getADCPath();
	} else {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}
}

string ShareManager::toReal(const string& virtualFile, bool isInSharingHub)  {
	Lock l(cs);
	if(virtualFile == "MyList.DcLst") {
		throw ShareException("NMDC-style lists no longer supported, please upgrade your client");
	} else if(virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
		generateXmlList();
			if (!isInSharingHub) { //Hide Share Mod
				return (Util::getPath(Util::PATH_USER_CONFIG) + "Emptyfiles.xml.bz2");
			}
		return getBZXmlFile();
	}

	return findFile(virtualFile)->getRealPath();
}

TTHValue ShareManager::getTTH(const string& virtualFile) const {
	Lock l(cs);
	if(virtualFile == Transfer::USER_LIST_NAME_BZ) {
		return bzXmlRoot;
	} else if(virtualFile == Transfer::USER_LIST_NAME) {
		return xmlRoot;
	}

	return findFile(virtualFile)->getTTH();
}

MemoryInputStream* ShareManager::getTree(const string& virtualFile) const {
	TigerTree tree;
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		if(!HashManager::getInstance()->getTree(TTHValue(virtualFile.substr(4)), tree))
			return 0;
	} else {
		try {
			TTHValue tth = getTTH(virtualFile);
			HashManager::getInstance()->getTree(tth, tree);
		} catch(const Exception&) {
			return 0;
		}
	}

	ByteVector buf = tree.getLeafData();
	return new MemoryInputStream(&buf[0], buf.size());
}

AdcCommand ShareManager::getFileInfo(const string& aFile) {
	if(aFile == Transfer::USER_LIST_NAME) {
		generateXmlList();
		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(xmlListLen));
		cmd.addParam("TR", xmlRoot.toBase32());
		return cmd;
	} else if(aFile == Transfer::USER_LIST_NAME_BZ) {
		generateXmlList();

		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(bzXmlListLen));
		cmd.addParam("TR", bzXmlRoot.toBase32());
		return cmd;
	}

	if(aFile.compare(0, 4, "TTH/") != 0)
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);

	TTHValue val(aFile.substr(4));
	Lock l(cs);
	HashFileIter i = tthIndex.find(val);
	if(i == tthIndex.end()) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	const Directory::File& f = *i->second;
	AdcCommand cmd(AdcCommand::CMD_RES);
	cmd.addParam("FN", f.getADCPath());
	cmd.addParam("SI", Util::toString(f.getSize()));
	cmd.addParam("TR", f.getTTH().toBase32());
	return cmd;
}

pair<ShareManager::Directory::Ptr, string> ShareManager::splitVirtual(const string& virtualPath) const {
	if(virtualPath.empty() || virtualPath[0] != '/') {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	string::size_type i = virtualPath.find('/', 1);
	if(i == string::npos || i == 1) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	DirList::const_iterator dmi = getByVirtual( virtualPath.substr(1, i - 1));
	if(dmi == directories.end()) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	Directory::Ptr d = *dmi;

	string::size_type j = i + 1;
	while((i = virtualPath.find('/', j)) != string::npos) {
		Directory::MapIter mi = d->directories.find(virtualPath.substr(j, i - j));
		j = i + 1;
		if(mi == d->directories.end())
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		d = mi->second;
	}
	
	return make_pair(d, virtualPath.substr(j));
}

ShareManager::Directory::File::Set::const_iterator ShareManager::findFile(const string& virtualFile) const {
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		HashFileMap::const_iterator i = tthIndex.find(TTHValue(virtualFile.substr(4)));
		if(i == tthIndex.end()) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}
		return i->second;
	}

	pair<Directory::Ptr, string> v = splitVirtual(virtualFile);
	Directory::File::Set::const_iterator it = find_if(v.first->files.begin(), v.first->files.end(),
		Directory::File::StringComp(v.second));
	if(it == v.first->files.end())
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	return it;
}

StringList ShareManager::getRealPaths(const std::string path) {
	if(path.empty())
		throw ShareException("empty virtual path");

		StringList result;	
		string dir;
		Directory::Ptr d = splitVirtual(path).first;

		if(*(path.end() - 1) == '/') {

		if(d->getParent()) {
			dir = d->getParent()->getRealPath(d->getName());
			if(dir[dir.size() -1] != '\\') 
				dir += "\\";
			result.push_back( dir );
		} else {
 			for(StringMap::const_iterator i = shares.begin(); i != shares.end(); ++i) {
				if(stricmp(i->second, d->getName()) == 0) {
					if(FileFindIter(i->first.substr(0, i->first.size() - 1)) != FileFindIter()) {
						dir = i->first;
						if(dir[dir.size() -1] != '\\') 
						dir += "\\";
						result.push_back( dir );

					}
				}
			}
		}
		}else { //its a file
			result.push_back(toReal(path, true));
		}
		return result;
	}
string ShareManager::validateVirtual(const string& aVirt) const noexcept {
	string tmp = aVirt;
	string::size_type idx = 0;

	while( (idx = tmp.find_first_of("\\/"), idx) != string::npos) {
		tmp[idx] = '_';
	}
	return tmp;
}

bool ShareManager::hasVirtual(const string& virtualName) const noexcept {
	Lock l(cs);
	return getByVirtual(virtualName) != directories.end();
}

void ShareManager::load(SimpleXML& aXml) {
	Lock l(cs);

	aXml.resetCurrentChild();
	if(aXml.findChild("Share")) {
		aXml.stepIn();
		while(aXml.findChild("Directory")) {
			string realPath = aXml.getChildData();
			if(realPath.empty()) {
				continue;
			}
			// make sure realPath ends with a PATH_SEPARATOR
			if(realPath[realPath.size() - 1] != PATH_SEPARATOR) {
				realPath += PATH_SEPARATOR;
			}
						
		//	if(!Util::fileExists(realPath))
		//		continue;

			const string& virtualName = aXml.getChildAttrib("Virtual");
			string vName = validateVirtual(virtualName.empty() ? Util::getLastDir(realPath) : virtualName);
			shares.insert(std::make_pair(realPath, vName));
			if(getByVirtual(vName) == directories.end()) {
				directories.push_back(Directory::create(vName));
				addReleaseDir(realPath);
			}
		}
		aXml.stepOut();
	}
	if(aXml.findChild("NoShare")) {
		aXml.stepIn();
		while(aXml.findChild("Directory"))
			notShared.push_back(aXml.getChildData());
	
		aXml.stepOut();
	}
	if(aXml.findChild("incomingDirs")) {
		aXml.stepIn();
		while(aXml.findChild("incoming"))
			incoming.push_back(aXml.getChildData());
	
		aXml.stepOut();
	}
}

static const string SDIRECTORY = "Directory";
static const string SFILE = "File";
static const string SNAME = "Name";
static const string SSIZE = "Size";
static const string STTH = "TTH";
//static const string PATH = "Path";
static const string DATE = "Date";

struct ShareLoader : public SimpleXMLReader::CallBack {
	ShareLoader(ShareManager::DirList& aDirs) : dirs(aDirs), cur(0), depth(0) { }
	void startTag(const string& name, StringPairList& attribs, bool simple) {
		if(name == SDIRECTORY) {
			const string& name = getAttrib(attribs, SNAME, 0);
		//	string path = getAttrib(attribs, PATH, 1);
			string date = getAttrib(attribs, DATE, 2);

		//	if(path[path.length() - 1] != PATH_SEPARATOR)
		//		path += PATH_SEPARATOR;

			if(!name.empty()) {
				if(depth == 0) {
					for(ShareManager::DirList::const_iterator i = dirs.begin(); i != dirs.end(); ++i) {
						if(stricmp((*i)->getName(), name) == 0) {
							cur = *i;
							break;
						}
					}
				} else if(cur) {
					cur = ShareManager::Directory::create(name, cur);
					cur->setLastWrite(date);
					cur->getParent()->directories[cur->getName()] = cur;
					ShareManager::getInstance()->addReleaseDir(cur->getFullName());
				}
			}

			if(simple) {
				if(cur) {
					cur = cur->getParent();
				}
			} else {
				depth++;
			}
		} else if(cur && name == SFILE) {
			const string& fname = getAttrib(attribs, SNAME, 0);
			const string& size = getAttrib(attribs, SSIZE, 1);
			const string& root = getAttrib(attribs, STTH, 2);
			if(fname.empty() || size.empty() || (root.size() != 39)) {
				dcdebug("Invalid file found: %s\n", fname.c_str());
				return;
			}
			cur->files.insert(ShareManager::Directory::File(fname, Util::toInt64(size), cur, TTHValue(root)));
		}
	}
	void endTag(const string& name, const string&) {
		if(name == SDIRECTORY) {
			depth--;
			if(cur) {
				cur = cur->getParent();
			}
		}
	}

private:
	ShareManager::DirList& dirs;

	ShareManager::Directory::Ptr cur;
	size_t depth;
};

bool ShareManager::loadCache() noexcept {
	try {
		ShareLoader loader(directories);
		
		try {
		//look for share.xml
		dcpp::File ff(Util::getPath(Util::PATH_USER_CONFIG) + "Share.xml", dcpp::File::READ, dcpp::File::OPEN);
		SimpleXMLReader(&loader).parse(ff);
		}catch(...) 
			//migrate the old bzipped cache, remove this at some point
		{	
			dcpp::File ff(Util::getPath(Util::PATH_USER_CONFIG) + "Share.xml.bz2", dcpp::File::READ, dcpp::File::OPEN);
			FilteredInputStream<UnBZFilter, false> f(&ff);
			SimpleXMLReader(&loader).parse(f);
		}

		for(DirList::const_iterator i = directories.begin(); i != directories.end(); ++i) {
			const Directory::Ptr& d = *i;
			updateIndices(*d);
		}
		
		setBZXmlFile( Util::getPath(Util::PATH_USER_CONFIG) + "files.xml.bz2");
		if(!Util::fileExists(getBZXmlFile())) //only generate if we dont find old filelist
			generateXmlList(true);
		sortReleaseList();
		return true;
	} catch(const Exception& e) {
		dcdebug("%s\n", e.getError().c_str());
	}
	return false;
}

void ShareManager::save(SimpleXML& aXml) {
	Lock l(cs);

	aXml.addTag("Share");
	aXml.stepIn();
	for(StringMapIter i = shares.begin(); i != shares.end(); ++i) {
		aXml.addTag("Directory", i->first);
		aXml.addChildAttrib("Virtual", i->second);
		
	}
	aXml.stepOut();
	aXml.addTag("NoShare");
	aXml.stepIn();
	for(StringIter j = notShared.begin(); j != notShared.end(); ++j) {
		aXml.addTag("Directory", *j);
	}
	aXml.stepOut();
	
	aXml.addTag("incomingDirs");
	aXml.stepIn();
	for(StringIter k = incoming.begin(); k != incoming.end(); ++k) {
		aXml.addTag("incoming", *k); //List Vname as incoming
	}
	aXml.stepOut();

} 

void ShareManager::addDirectory(const string& realPath, const string& virtualName)  {
//	if(!Util::fileExists(realPath))
//		return;

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
	// don't share Windows directory
	TCHAR path[MAX_PATH];
	::SHGetFolderPath(NULL, CSIDL_WINDOWS, NULL, SHGFP_TYPE_CURRENT, path);
	if(strnicmp(realPath, Text::fromT((tstring)path) + PATH_SEPARATOR, _tcslen(path) + 1) == 0) {
		char buf[MAX_PATH];
		snprintf(buf, sizeof(buf), CSTRING(CHECK_FORBIDDEN), realPath.c_str());
		throw ShareException(buf);
	}
#endif

	list<string> removeMap;
	{
		Lock l(cs);
		
		StringMap a = shares;
		for(StringMapIter i = a.begin(); i != a.end(); ++i) {
			if(strnicmp(realPath, i->first, i->first.length()) == 0) {
				// Trying to share an already shared directory
				removeMap.push_front(i->first);
			} else if(strnicmp(realPath, i->first, realPath.length()) == 0) {
				// Trying to share a parent directory
				removeMap.push_front(i->first);	
			}
		}
	}

	for(list<string>::const_iterator i = removeMap.begin(); i != removeMap.end(); i++) {
		removeDirectory(*i);
	}
	
	HashManager::HashPauser pauser;	
	
	Directory::Ptr dp = buildTree(realPath, Directory::Ptr());
	string vName = validateVirtual(virtualName);
	dp->setName(vName);

	{
		Lock l(cs);

		shares.insert(std::make_pair(realPath, vName));
		updateIndices(*merge(dp));
		
		setDirty();
	}
	sortReleaseList();
}

ShareManager::Directory::Ptr ShareManager::merge(const Directory::Ptr& directory) {
	for(DirList::const_iterator i = directories.begin(); i != directories.end(); ++i) {
		if(stricmp((*i)->getName(), directory->getName()) == 0) {
			dcdebug("Merging directory %s\n", directory->getName().c_str());
			(*i)->merge(directory);
			return *i;
		}
	}
	
	dcdebug("Adding new directory %s\n", directory->getName().c_str());
	
	directories.push_back(directory);
	return directory;
}

void ShareManager::Directory::merge(const Directory::Ptr& source) {
	for(MapIter i = source->directories.begin(); i != source->directories.end(); ++i) {
		Directory::Ptr subSource = i->second;
		
		MapIter ti = directories.find(subSource->getName());
		if(ti == directories.end()) {
			if(findFile(subSource->getName()) != files.end()) {
				dcdebug("File named the same as directory");
			} else {
				directories.insert(std::make_pair(subSource->getName(), subSource));
				subSource->parent = this;
			}
		} else {
			Directory::Ptr subTarget = ti->second;
			subTarget->merge(subSource);
		}
	}

	// All subdirs either deleted or moved to target...
	source->directories.clear();
	
	for(File::Set::iterator i = source->files.begin(); i != source->files.end(); ++i) {
		if(findFile(i->getName()) == files.end()) {
			if(directories.find(i->getName()) != directories.end()) {
				dcdebug("Directory named the same as file");
			} else {
				 std::pair<File::Set::iterator, bool> added = files.insert(*i);
                   if(added.second) {
				   const_cast<File&>(*added.first).setParent(this);
                  }
			}
		}
	}
}

void ShareManager::removeDirectory(const string& realPath) {
	if(realPath.empty())
		return;

	HashManager::getInstance()->stopHashing(realPath);
	
	Lock l(cs);

	StringMapIter i = shares.find(realPath);
	if(i == shares.end()) {
		return;
	}

	std::string vName = i->second;
	for(DirList::const_iterator j = directories.begin(); j != directories.end(); ) {
		if(stricmp((*j)->getName(), vName) == 0) {
			//directories.erase(j++);
			(*j)->findRemoved();
			directories.erase(j);
		} else {
			++j;
		}
	}

	shares.erase(i);

	HashManager::HashPauser pauser;

	// Readd all directories with the same vName
	for(i = shares.begin(); i != shares.end(); ++i) {
		if(stricmp(i->second, vName) == 0 && checkHidden(i->first)) {
			Directory::Ptr dp = buildTree(i->first, 0);
			dp->setName(i->second);
			merge(dp);
		}
	}
	sortReleaseList();
	rebuildIndices();
	setDirty();
}

void ShareManager::Directory::findRemoved() {
	for(Directory::Map::const_iterator l = directories.begin(); l != directories.end(); ++l) {
		 l->second->findRemoved();
	}
	ShareManager::getInstance()->deleteReleaseDir(name);
}

void ShareManager::renameDirectory(const string& realPath, const string& virtualName)  {
	removeDirectory(realPath);
	addDirectory(realPath, virtualName);
}

ShareManager::DirList::const_iterator ShareManager::getByVirtual(const string& virtualName) const noexcept {
	for(DirList::const_iterator i = directories.begin(); i != directories.end(); ++i) {
		if(stricmp((*i)->getName(), virtualName) == 0) {
			return i;
		}
	}
	return directories.end();
}

int64_t ShareManager::getShareSize(const string& realPath) const noexcept {
	Lock l(cs);
	dcassert(realPath.size()>0);
	StringMap::const_iterator i = shares.find(realPath);

	if(i != shares.end()) {
		DirList::const_iterator j = getByVirtual(i->second);
		if(j != directories.end()) {
			return (*j)->getSize();
		}
	}
	return -1;
}

int64_t ShareManager::getShareSize() const noexcept {
	Lock l(cs);
	int64_t tmp = 0;
	
	for(HashFileMap::const_iterator i = tthIndex.begin(); i != tthIndex.end(); ++i) {
		tmp += i->second->getSize();
	}
	
	return tmp;
}

size_t ShareManager::getSharedFiles() const noexcept {
	Lock l(cs);
	return tthIndex.size();
}

bool ShareManager::isDirShared(const string& directory) {

	string dir = getReleaseDir(directory);
	if (dir.empty())
		return false;

	if (std::binary_search(dirNameList.begin(), dirNameList.end(), dir)) {
		return true;
	} else {
		return false;
	}
}

tstring ShareManager::getDirPath(const string& directory) {
	string dir = getReleaseDir(directory);
	if (dir.empty())
		return Util::emptyStringT;

	string found = Util::emptyString;
	string dirNew;
	for(DirList::const_iterator j = directories.begin(); j != directories.end(); ++j) {
		dirNew = getReleaseDir((*j)->getFullName());
		if (!dirNew.empty()) {
			if (dir == dirNew) {
				found=dirNew;
				break;
			}
		}
		found = (*j)->find(dir);
		if(!found.empty())
			break;
	}

	if (found.empty())
		return Util::emptyStringT;

	StringList ret;
	try {
		ret = getRealPaths(Util::toAdcFile(found));
	} catch(const ShareException&) {
		return Util::emptyStringT;
	}

	if (!ret.empty()) {
		return Text::toT(ret[0]);
	}
	return Util::emptyStringT;
}


string ShareManager::Directory::find(const string& dir) {
	string ret = Util::emptyString;
	string dirNew = ShareManager::getInstance()->getReleaseDir(this->getFullName());

	if (!dirNew.empty()) {
		if (dir == dirNew) {
			return this->getFullName();
		}
	}

	for(Directory::Map::const_iterator l = directories.begin(); l != directories.end(); ++l) {
		ret = l->second->find(dir);
		if(!ret.empty())
			break;
	}
	return ret;
}

string ShareManager::getReleaseDir(const string& aName) {
	//LogManager::getInstance()->message("aName: " + aName);
	string dir=aName;
	if(dir[dir.size() -1] == '\\') 
		dir = dir.substr(0, (dir.size() -1));
	string dirMatch=dir;

	//check if the release name is the last one before checking subdirs
	int dpos = dirMatch.rfind("\\");
	if(dpos != string::npos) {
		dpos++;
		dirMatch = dirMatch.substr(dpos, dirMatch.size()-dpos);
	} else {
		dpos=0;
	}

	if (releaseReg.match(dirMatch) > 0) {
		dir = Text::toLower(dir.substr(dpos, dir.size()));
		return dir;
	}


	//check the subdirs then
	dpos=dir.size();
	dirMatch=dir;
	bool match=false;
	for (;;) {
		if (subDirReg.match(dirMatch) > 0) {
			dpos = dirMatch.rfind("\\");
			if(dpos != string::npos) {
				match=true;
				dirMatch = dirMatch.substr(0,dpos);
			} else {
				break;
			}
		} else {
			break;
		}
	}

	if (!match)
		return Util::emptyString;
	
	//check the release name again without subdirs
	dpos = dirMatch.rfind("\\");
	if(dpos != string::npos) {
		dpos++;
		dirMatch = dirMatch.substr(dpos, dirMatch.size()-dpos);
	} else {
		dpos=0;
	}

	if (releaseReg.match(dirMatch) > 0) {
		dir = Text::toLower(dir.substr(dpos, dir.size()));
		return dir;
	} else {
		return Util::emptyString;
	}


}

void ShareManager::sortReleaseList() {
	Lock l(cs);
	sort(dirNameList.begin(), dirNameList.end());
}

void ShareManager::addReleaseDir(const string& aName) {
	string dir = getReleaseDir(aName);
	if (dir.empty())
		return;

	Lock l(cs);
	dirNameList.push_back(dir);
}

void ShareManager::deleteReleaseDir(const string& aName) {

	string dir = getReleaseDir(aName);
	if (dir.empty())
		return;

	Lock l(cs);
	for(StringList::const_iterator i = dirNameList.begin(); i != dirNameList.end(); ++i) {
		if ((*i) == dir) {
			dirNameList.erase(i);
			return;
		}
	}
}

ShareManager::Directory::Ptr ShareManager::buildTree(const string& aName, const Directory::Ptr& aParent) {
	Directory::Ptr dir = Directory::create(Util::getLastDir(aName), aParent);
	addReleaseDir(dir->getFullName());

	Directory::File::Set::iterator lastFileIter = dir->files.begin();

	FileFindIter end;


#ifdef _WIN32
		for(FileFindIter i(aName + "*"); i != end; ++i) {
#else
	//the fileiter just searches directorys for now, not sure if more 
	//will be needed later
	//for(FileFindIter i(aName + "*"); i != end; ++i) {
	for(FileFindIter i(aName); i != end; ++i) {
#endif
		string name = i->getFileName();

		if(name.empty()) {
			LogManager::getInstance()->message("Invalid file name found while hashing folder "+ aName + ".");
			continue;
		}

		if(BOOLSETTING(SHARE_SKIPLIST_USE_REGEXP)){
			string str1 = SETTING(SKIPLIST_SHARE);
			string str2 = name;
			try {
				boost::regex reg(str1);
				if(boost::regex_search(str2.begin(), str2.end(), reg)){
					continue;
				};
			} catch(...) {
			}
			/*PME regexp;
			regexp.Init(Text::utf8ToAcp(SETTING(SKIPLIST_SHARE)));
			if((regexp.IsValid()) && (regexp.match(Text::utf8ToAcp(name)))) {
				continue;
			}*/
		}else{
			try{
			if( Wildcard::patternMatch( name, SETTING(SKIPLIST_SHARE), '|' ) ){
				continue;
			}
			}catch(...) { }
			
		}

		if(name == "." || name == "..")
			continue;

		//check for forbidden file patterns
		if(BOOLSETTING(REMOVE_FORBIDDEN)) {
			string::size_type nameLen = name.size();
			string fileExt = Util::getFileExt(name);
			if ((stricmp(fileExt.c_str(), ".tdc") == 0) ||
				(stricmp(fileExt.c_str(), ".GetRight") == 0) ||
				(stricmp(fileExt.c_str(), ".temp") == 0) ||
				(stricmp(fileExt.c_str(), ".tmp") == 0) ||
				(stricmp(fileExt.c_str(), ".jc!") == 0) ||	//FlashGet
				(stricmp(fileExt.c_str(), ".dmf") == 0) ||	//Download Master
				(stricmp(fileExt.c_str(), ".!ut") == 0) ||	//uTorrent
				(stricmp(fileExt.c_str(), ".bc!") == 0) ||	//BitComet
				(stricmp(fileExt.c_str(), ".missing") == 0) ||
				(stricmp(fileExt.c_str(), ".bak") == 0) ||
				(stricmp(fileExt.c_str(), ".bad") == 0) ||
				(nameLen > 9 && name.rfind("part.met") == nameLen - 8) ||				
				(name.find("__padding_") == 0) ||			//BitComet padding
				(name.find("__INCOMPLETE__") == 0) ||		//winmx
				(name.find("__incomplete__") == 0)		//winmx
				) {		//kazaa temps
					LogManager::getInstance()->message("Forbidden file will not be shared: " + name + " (" + STRING(SIZE) + ": " + Util::toString(File::getSize(name)) + " " + STRING(B) + ") (" + STRING(DIRECTORY) + ": \"" + aName + "\")");
					continue;
			}
		}
		if(!BOOLSETTING(SHARE_HIDDEN) && i->isHidden())
			continue;
/*
		if(i->isLink())
 			continue;
*/ 			
		if(i->isDirectory()) {
			string newName = aName + name + PATH_SEPARATOR;
			
			dir->setLastWrite(Util::getDateTime(i->getLastWriteTime()));

#ifdef _WIN32
			// don't share Windows directory
			TCHAR path[MAX_PATH];
			::SHGetFolderPath(NULL, CSIDL_WINDOWS, NULL, SHGFP_TYPE_CURRENT, path);
			if(strnicmp(newName, Text::fromT((tstring)path) + PATH_SEPARATOR, _tcslen(path) + 1) == 0)
				continue;
#endif

			if((stricmp(newName, SETTING(TEMP_DOWNLOAD_DIRECTORY)) != 0) && shareFolder(newName)) {
				//ApexDC
				Directory::Ptr tmpDir = buildTree(newName, dir);
				//add the date to the last dir
				tmpDir->setLastWrite(Util::getDateTime(i->getLastWriteTime()));

				if((!BOOLSETTING(DONT_SHARE_EMPTY_DIRS) || tmpDir->countFiles() > 0) && (!BOOLSETTING(ONLY_SHARE_FULL_DIRS) || tmpDir->getFullyHashed())) {
					dir->directories[name] = tmpDir;
				}
			}
		} else {
			// Not a directory, assume it's a file...make sure we're not sharing the settings file...
			if( (stricmp(name.c_str(), "DCPlusPlus.xml") != 0) && 
				(stricmp(name.c_str(), "Favorites.xml") != 0) &&
				(stricmp(Util::getFileExt(name).c_str(), ".dctmp") != 0) &&
				(stricmp(Util::getFileExt(name).c_str(), ".antifrag") != 0) ){

				int64_t size = i->getSize();
				if(BOOLSETTING(NO_ZERO_BYTE) && !(size > 0))
					continue;

				string fileName = aName + name;
				if(stricmp(fileName, SETTING(TLS_PRIVATE_KEY_FILE)) == 0) {
					continue;
				}
				try {
					//ApexDC
					if(HashManager::getInstance()->checkTTH(fileName, size, i->getLastWriteTime())) {
						lastFileIter = dir->files.insert(lastFileIter, Directory::File(name, size, dir, HashManager::getInstance()->getTTH(fileName, size)));
					} else {
						dir->setFullyHashed(false);
					}
				} catch(const HashException&) {
				}
			}
		}
	}
	return dir;
}

bool ShareManager::checkHidden(const string& aName) const {
	FileFindIter ff = FileFindIter(aName.substr(0, aName.size() - 1));

	if (ff != FileFindIter()) {
		return (BOOLSETTING(SHARE_HIDDEN) || !ff->isHidden());
	}

	return true;
}

void ShareManager::updateIndices(Directory& dir) {
	bloom.add(Text::toLower(dir.getName()));
//reset the size to avoid increasing the share size
	//on every refresh.
	dir.size = 0;
	for(Directory::MapIter i = dir.directories.begin(); i != dir.directories.end(); ++i) {
		updateIndices(*i->second);
	}

	dir.size = 0;

	for(Directory::File::Set::iterator i = dir.files.begin(); i != dir.files.end(); ) {
		updateIndices(dir, i++);
	}
}

void ShareManager::rebuildIndices() {
	sharedSize = 0;
	tthIndex.clear();
	bloom.clear();

	for(DirList::const_iterator i = directories.begin(); i != directories.end(); ++i) {
		updateIndices(**i);
	}
}

void ShareManager::updateIndices(Directory& dir, const Directory::File::Set::iterator& i) {
	const Directory::File& f = *i;

	HashFileIter j = tthIndex.find(f.getTTH());
	if(j == tthIndex.end()) {
		dir.size+=f.getSize();
		sharedSize += f.getSize();
	}

	dir.addType(getType(f.getName()));

	tthIndex.insert(make_pair(f.getTTH(), i));
	bloom.add(Text::toLower(f.getName()));
}


int ShareManager::refreshDirs( StringList dirs ){
	
	int result = REFRESH_PATH_NOT_FOUND;
	
	if(refreshing.test_and_set()) {
		LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_IN_PROGRESS));
		return REFRESH_IN_PROGRESS;
	}
		
	{
			//Lock l(cs); atomic flag will protect here
			refreshPaths.clear();
			
			for(StringIter d = dirs.begin(); d != dirs.end(); ++d) {
			
				std::string virt = *d;
			
				for(StringMap::const_iterator j = shares.begin(); j != shares.end(); ++j) {
					if( stricmp( j->second, virt ) == 0 ) {
						refreshPaths.push_back( j->first );
						result = REFRESH_STARTED;
					}
				}
			}

		}
		
		if(result == REFRESH_STARTED)
			result = startRefresh(ShareManager::REFRESH_DIRECTORY | ShareManager::REFRESH_UPDATE);

		if(result == REFRESH_PATH_NOT_FOUND)
			refreshing.clear();

		return result;
	}



int ShareManager::refreshIncoming( ){
	int result = REFRESH_PATH_NOT_FOUND;
	
		if(refreshing.test_and_set()) {
			LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_IN_PROGRESS));
			return REFRESH_IN_PROGRESS;
			}

		{
			//Lock l(cs); atomic flag will protect here
			refreshPaths.clear();

			lastIncomingUpdate = GET_TICK();
			for(StringIter d = incoming.begin(); d != incoming.end(); ++d) {
			
				std::string realpath = *d;
			/*looks kinda stupid but make it like this so realpaths are incoming,
				will avoid same virtual names listed as incoming many times*/
				for(StringMap::const_iterator j = shares.begin(); j != shares.end(); ++j) {
					if( stricmp( j->second, shares.find(realpath)->second ) == 0 ) {
						refreshPaths.push_back( j->first ); //add all matching realpaths to refreshpaths
						result = REFRESH_STARTED;
					}
				}
	
			}

		}
		
		if(result == REFRESH_STARTED)
			result = startRefresh(ShareManager::REFRESH_DIRECTORY | ShareManager::REFRESH_UPDATE);

		if(result == REFRESH_PATH_NOT_FOUND)
			refreshing.clear();

		return result;
	}



int ShareManager::refresh( const string& aDir ){
	int result = REFRESH_PATH_NOT_FOUND;

	if(refreshing.test_and_set()) {
			LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_IN_PROGRESS));
			return REFRESH_IN_PROGRESS;
	}
		{
		//	Lock l(cs); atomic flag will protect here
			refreshPaths.clear();
			

				//loopup the Virtualname selected from share and add it to refreshPaths List
				for(StringMap::const_iterator j = shares.begin(); j != shares.end(); ++j) {
					if( stricmp( j->second, aDir ) == 0 ) {
						refreshPaths.push_back( j->first );
						result = REFRESH_STARTED;
					}
				}
		}

		if(result == REFRESH_STARTED)
			result = startRefresh(ShareManager::REFRESH_DIRECTORY | ShareManager::REFRESH_UPDATE);
		
		if(result == REFRESH_PATH_NOT_FOUND)
			refreshing.clear();

		return result;
	}


int ShareManager::refresh(int aRefreshOptions){
	
	if(refreshing.test_and_set()) {
	LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_IN_PROGRESS));
	return REFRESH_IN_PROGRESS;
	}
		
	startRefresh(aRefreshOptions);
	return REFRESH_STARTED;

}
int ShareManager::startRefresh(int aRefreshOptions)  {
	
	refreshOptions = aRefreshOptions;
	join();

	try {
		start();
		if(refreshOptions & REFRESH_BLOCKING) { 
			join();
		} else {
			setThreadPriority(Thread::LOW);
		}

	} catch(const ThreadException& e) {
		LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_FAILED) + " " + e.getError());
		refreshing.clear();
	}

	return REFRESH_STARTED;
}

StringPairList ShareManager::getDirectories(int refreshOptions) const noexcept {
	Lock l(cs);
	StringPairList ret;
	if(refreshOptions & REFRESH_ALL) {
	for(StringMap::const_iterator i = shares.begin(); i != shares.end(); ++i) {
		ret.push_back(make_pair(i->second, i->first));
	}
	}else if(refreshOptions & REFRESH_DIRECTORY){
	for(StringIterC j = refreshPaths.begin(); j != refreshPaths.end(); ++j) {
		std::string bla = *j;
			// lookup in share the realpaths for refreshpaths
			ret.push_back(make_pair(shares.find(bla)->second, bla));
		}
	}
	return ret;
}

int ShareManager::run() {
	
	StringPairList dirs = getDirectories(refreshOptions);

	if(refreshOptions & REFRESH_ALL) {
		dirNameList.clear();
		lastFullUpdate = GET_TICK();
	}
		HashManager::HashPauser pauser;
				
		LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_INITIATED));
		//sharedSize = 0;
		

		DirList newDirs;
		for(StringPairIter i = dirs.begin(); i != dirs.end(); ++i) {
				if (checkHidden(i->second)) {
					Directory::Ptr dp = buildTree(i->second, Directory::Ptr());
					dp->setName(i->first);
					newDirs.push_back(dp);
				}
		}
		{
		Lock l(cs);

		//only go here when needed
		if(refreshOptions & REFRESH_DIRECTORY){ 
		
		for(StringPairIter i = dirs.begin(); i != dirs.end(); ++i) {
		
			//lookup for the root dirs under the Vname and erase only those.
			for(DirList::const_iterator j = directories.begin(); j != directories.end(); ) {	
				if(stricmp((*j)->getName(), i->first) == 0) {
					//directories.erase(j++);
					(*j)->findRemoved();
					directories.erase(j);
				} else ++j; // in a vector erase all elements are moved to new positions so make sure we dont skip anything.
			}
		}


		} else if(refreshOptions & REFRESH_ALL) {
				directories.clear();
			}

			forceXmlRefresh = true;

			for(DirList::const_iterator i = newDirs.begin(); i != newDirs.end(); ++i) {
				merge(*i);
			}
			
	
			rebuildIndices();
		}


		LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_FINISHED));
	

	if(refreshOptions & REFRESH_UPDATE) {
		ClientManager::getInstance()->infoUpdated();
	}

	//make sure we have a refresh before doing a rebuild
	if(rebuild) {
		HashManager::getInstance()->rebuild();
		LogManager::getInstance()->message(STRING(REBUILD_STARTED));
		rebuild = false;
	}

	forceXmlRefresh = true;

	if(refreshOptions & REFRESH_BLOCKING)
		generateXmlList(true);

	sortReleaseList();
	refreshing.clear();
	return 0;
}
		
void ShareManager::getBloom(ByteVector& v, size_t k, size_t m, size_t h) const {
	dcdebug("Creating bloom filter, k=%u, m=%u, h=%u\n", k, m, h);
	Lock l(cs);
	
	HashBloom bloom;
	bloom.reset(k, m, h);
	for(HashFileMap::const_iterator i = tthIndex.begin(); i != tthIndex.end(); ++i) {
		bloom.add(i->first);
	}
	bloom.copy_to(v);
}

void ShareManager::generateXmlList(bool forced /*false*/) {

	if(forced || forceXmlRefresh || (xmlDirty && (lastXmlUpdate + 15 * 60 * 1000 < GET_TICK() || lastXmlUpdate < lastFullUpdate))) {
		
		if(GeneratingXmlList.test_and_set()) //dont pile up generate calls to the lock, if its already generating return.
			return;
		
		Lock l(cs);
		listN++;

		try {
			string tmp2;
			string indent;

			string newXmlName = Util::getPath(Util::PATH_USER_CONFIG) + "files" + Util::toString(listN) + ".xml.bz2";
			{
				File f(newXmlName, File::WRITE, File::TRUNCATE | File::CREATE);
				// We don't care about the leaves...
				CalcOutputStream<TTFilter<1024*1024*1024>, false> bzTree(&f);
				FilteredOutputStream<BZFilter, false> bzipper(&bzTree);
				CountOutputStream<false> count(&bzipper);
				CalcOutputStream<TTFilter<1024*1024*1024>, false> newXmlFile(&count);

				newXmlFile.write(SimpleXML::utf8Header);
				newXmlFile.write("<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"/\" Generator=\"DC++ " DCVERSIONSTRING "\">\r\n");
				for(DirList::const_iterator i = directories.begin(); i != directories.end(); ++i) {
					(*i)->toXml(newXmlFile, indent, tmp2, true);
				}
				newXmlFile.write("</FileListing>");
				newXmlFile.flush();

				xmlListLen = count.getCount();

				newXmlFile.getFilter().getTree().finalize();
				bzTree.getFilter().getTree().finalize();

				xmlRoot = newXmlFile.getFilter().getTree().getRoot();
				bzXmlRoot = bzTree.getFilter().getTree().getRoot();
			}

			string emptyXmlName = Util::getPath(Util::PATH_USER_CONFIG) + "Emptyfiles.xml.bz2"; // Hide Share Mod
			if(!Util::fileExists(emptyXmlName)) {
				FilteredOutputStream<BZFilter, true> emptyXmlFile(new File(emptyXmlName, File::WRITE, File::TRUNCATE | File::CREATE));
				emptyXmlFile.write(SimpleXML::utf8Header);
				emptyXmlFile.write("<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"/\" Generator=\"DC++ " DCVERSIONSTRING "\">\r\n"); // Hide Share Mod
				emptyXmlFile.write("</FileListing>");
				emptyXmlFile.flush();
			}

			if(bzXmlRef.get()) {
				bzXmlRef.reset();
				File::deleteFile(getBZXmlFile());
			}

			try {
				File::renameFile(newXmlName, Util::getPath(Util::PATH_USER_CONFIG) + "files.xml.bz2");
				newXmlName = Util::getPath(Util::PATH_USER_CONFIG) + "files.xml.bz2";
			} catch(const FileException&) {
				// Ignore, this is for caching only...
			}
			bzXmlRef = unique_ptr<File>(new File(newXmlName, File::READ, File::OPEN));
			setBZXmlFile(newXmlName);
			bzXmlListLen = File::getSize(newXmlName);
		} catch(const Exception&) {
			// No new file lists...
		}
		xmlDirty = false;
		forceXmlRefresh = false;
		lastXmlUpdate = GET_TICK();
		GeneratingXmlList.clear();
	}
}

#define LITERAL(n) n, sizeof(n)-1

void ShareManager::saveXmlList(){
	Lock l(cs);
	string indent;
	//create a backup first incase we get interrupted on creation.
	string newCache = Util::getPath(Util::PATH_USER_CONFIG) + "Share.xml.tmp";
	File ff(newCache, File::WRITE, File::TRUNCATE | File::CREATE);
	BufferedOutputStream<false> xmlFile(&ff);
	//FilteredOutputStream<BZFilter, true> *xmlFile = new FilteredOutputStream<BZFilter, true>(new File(newCache, File::WRITE, File::TRUNCATE | File::CREATE));
	try{
		xmlFile.write(SimpleXML::utf8Header);
		xmlFile.write("<Share>\r\n");

		for(DirList::const_iterator i = directories.begin(); i != directories.end(); ++i) {
			(*i)->toXmlList(xmlFile, indent);
		}
		xmlFile.write("</Share>");
		xmlFile.flush();
		ff.close();
		File::deleteFile(Util::getPath(Util::PATH_USER_CONFIG) + "Share.xml");
		File::renameFile(newCache,  (Util::getPath(Util::PATH_USER_CONFIG) + "Share.xml"));
		File::deleteFile(newCache);
	}catch(Exception&){}

	//delete xmlFile;

	ShareCacheDirty = false;
}

void ShareManager::Directory::toXmlList(OutputStream& xmlFile, string& indent) const{
	string tmp, tmp2;

	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(name, tmp, true));
//	xmlFile->write(LITERAL("\" Path=\""));
//	xmlFile->write(SimpleXML::escape(path, tmp, true));
	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(SimpleXML::escape(lastwrite, tmp, true));
	xmlFile.write(LITERAL("\">\r\n"));

	indent += '\t';
	for(Map::const_iterator i = directories.begin(); i != directories.end(); ++i) {

			i->second->toXmlList(xmlFile, indent);
	
	}

	for(Directory::File::Set::const_iterator i = files.begin(); i != files.end(); ++i) {
		const Directory::File& f = *i;

		xmlFile.write(indent);
		xmlFile.write(LITERAL("<File Name=\""));
		xmlFile.write(SimpleXML::escape(f.getName(), tmp2, true));
		xmlFile.write(LITERAL("\" Size=\""));
		xmlFile.write(Util::toString(f.getSize()));
		xmlFile.write(LITERAL("\" TTH=\""));
		tmp2.clear();
		xmlFile.write(f.getTTH().toBase32(tmp2));
		xmlFile.write(LITERAL("\"/>\r\n"));
	}

	indent.erase(indent.length()-1);
	xmlFile.write(indent);
	xmlFile.write(LITERAL("</Directory>\r\n"));
}


MemoryInputStream* ShareManager::generatePartialList(const string& dir, bool recurse, bool isInSharingHub, bool tthList) const {
	if(dir[0] != '/' || dir[dir.size()-1] != '/')
		return 0;
	
	if(!isInSharingHub) {
				string xml = SimpleXML::utf8Header;
                string tmp;
                xml += "<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"" + SimpleXML::escape(dir, tmp, false) + "\" Generator=\"" APPNAME " " VERSIONSTRING "\">\r\n";
                StringOutputStream sos(xml);
                xml += "</FileListing>";
                return new MemoryInputStream(xml);
	 }

	
	string xml;
	string tmp;
	if (!tthList) {
		xml = SimpleXML::utf8Header;
		xml += "<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"" + SimpleXML::escape(dir, tmp, false) + "\" Generator=\"" APPNAME " " DCVERSIONSTRING "\">\r\n";
	}
	StringOutputStream sos(xml);
	string indent = "\t";

	Lock l(cs);
	if(dir == "/") {
		if (tthList) {
			//no partial tthlists from the whole share
			return 0;
		}
		for(DirList::const_iterator i = directories.begin(); i != directories.end(); ++i) {
			tmp.clear();
			(*i)->toXml(sos, indent, tmp, recurse);
		}
	} else {
		string::size_type i = 1, j = 1;
		
		Directory::Ptr root;
		
		bool first = true;
		while( (i = dir.find('/', j)) != string::npos) {
			if(i == j) {
				j++;
				continue;
			}

			if(first) {
				first = false;
				DirList::const_iterator it = getByVirtual(dir.substr(j, i-j));

				if(it == directories.end())
					return 0;
				root = *it;
				
			
			} else {
				Directory::Map::const_iterator it2 = root->directories.find(dir.substr(j, i-j));
				if(it2 == root->directories.end()) {
					return 0;
				}
				root = it2->second;
			}

			j = i + 1;
		}

		if(!root)
			return 0;

				for(Directory::Map::const_iterator it2 = root->directories.begin(); it2 != root->directories.end(); ++it2) {
					if (!tthList) {
						it2->second->toXml(sos, indent, tmp, recurse);
					} else {
						it2->second->toTTHList(sos, tmp, recurse);
					}
				}
				if (!tthList) {
					root->filesToXml(sos, indent, tmp);
				} else {
					root->toTTHList(sos, tmp, recurse);
				}
			
	
	}

	if (!tthList)
		xml += "</FileListing>";
	if (xml.empty()) {
		dcdebug("Partial NULL");
		return NULL;
	} else {
		return new MemoryInputStream(xml);
	}
}

void ShareManager::Directory::toTTHList(OutputStream& tthList, string& tmp2, bool recursive) const {
	dcdebug("toTTHList2");
	if (recursive) {
		for(Map::const_iterator i = directories.begin(); i != directories.end(); ++i) {
			i->second->toTTHList(tthList, tmp2, recursive);
		}
	}
	for(Directory::File::Set::const_iterator i = files.begin(); i != files.end(); ++i) {
		const Directory::File& f = *i;
		tmp2.clear();
		tthList.write(f.getTTH().toBase32(tmp2));
		tthList.write(LITERAL(" "));
	}
}

void ShareManager::Directory::toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool fullList) const {
	
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(name, tmp2, true));
	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(SimpleXML::escape(lastwrite, tmp2, true));

	if(fullList) {
		xmlFile.write(LITERAL("\">\r\n"));
		indent += '\t';
		
		for(Map::const_iterator i = directories.begin(); i != directories.end(); ++i) {
			i->second->toXml(xmlFile, indent, tmp2, fullList);
		}

		filesToXml(xmlFile, indent, tmp2);
		
		indent.erase(indent.length()-1);
		xmlFile.write(indent);
		xmlFile.write(LITERAL("</Directory>\r\n"));
		
	} else {
		
		if(directories.empty() && files.empty()) {
			xmlFile.write(LITERAL("\" />\r\n"));
		} else {
			xmlFile.write(LITERAL("\" Incomplete=\"1"));
			xmlFile.write(LITERAL("\" Size=\""));
			xmlFile.write(SimpleXML::escape(Util::toString(getSize()), tmp2, true));
		//	xmlFile.write(LITERAL("\" Date=\""));
		//  xmlFile.write(SimpleXML::escape(lastwrite, tmp2, true));
			xmlFile.write(LITERAL("\" />\r\n"));
			}
		
	}
}

void ShareManager::Directory::filesToXml(OutputStream& xmlFile, string& indent, string& tmp2) const {
	dcdebug("filesToXml");
	for(Directory::File::Set::const_iterator i = files.begin(); i != files.end(); ++i) {
		const Directory::File& f = *i;

		xmlFile.write(indent);
		xmlFile.write(LITERAL("<File Name=\""));
		xmlFile.write(SimpleXML::escape(f.getName(), tmp2, true));
		xmlFile.write(LITERAL("\" Size=\""));
		xmlFile.write(Util::toString(f.getSize()));
		xmlFile.write(LITERAL("\" TTH=\""));
		tmp2.clear();
		xmlFile.write(f.getTTH().toBase32(tmp2));
		xmlFile.write(LITERAL("\"/>\r\n"));
	}
}

// These ones we can look up as ints (4 bytes...)...

static const char* typeAudio[] = { ".mp3", ".mp2", ".mid", ".wav", ".ogg", ".wma", ".669", ".aac", ".aif", ".amf", ".ams", ".ape", ".dbm", ".dmf", ".dsm", ".far", ".mdl", ".med", ".mod", ".mol", ".mp1", ".mp4", ".mpa", ".mpc", ".mpp", ".mtm", ".nst", ".okt", ".psm", ".ptm", ".rmi", ".s3m", ".stm", ".ult", ".umx", ".wow" };
static const char* typeCompressed[] = { ".zip", ".ace", ".rar", ".arj", ".hqx", ".lha", ".sea", ".tar", ".tgz", ".uc2" };
static const char* typeDocument[] = { ".htm", ".doc", ".txt", ".nfo", ".pdf", ".chm" };
static const char* typeExecutable[] = { ".exe", ".com" };
static const char* typePicture[] = { ".jpg", ".gif", ".png", ".eps", ".img", ".pct", ".psp", ".pic", ".tif", ".rle", ".bmp", ".pcx", ".jpe", ".dcx", ".emf", ".ico", ".psd", ".tga", ".wmf", ".xif" };
static const char* typeVideo[] = { ".mpg", ".mov", ".asf", ".avi", ".pxp", ".wmv", ".ogm", ".mkv", ".m1v", ".m2v", ".mpe", ".mps", ".mpv", ".ram", ".vob" };

static const string type2Audio[] = { ".au", ".it", ".ra", ".xm", ".aiff", ".flac", ".midi", };
static const string type2Compressed[] = { ".gz" };
static const string type2Picture[] = { ".ai", ".ps", ".pict", ".jpeg", ".tiff" };
static const string type2Video[] = { ".rm", ".divx", ".mpeg", ".mp1v", ".mp2v", ".mpv1", ".mpv2", ".qt", ".rv", ".vivo" };

#define IS_TYPE(x) ( type == (*((uint32_t*)x)) )
#define IS_TYPE2(x) (stricmp(aString.c_str() + aString.length() - x.length(), x.c_str()) == 0)

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

SearchManager::TypeModes ShareManager::getType(const string& aFileName) const noexcept {
	if(aFileName[aFileName.length() - 1] == PATH_SEPARATOR) {
		return SearchManager::TYPE_DIRECTORY;
	}

	if(checkType(aFileName, SearchManager::TYPE_VIDEO))
		return SearchManager::TYPE_VIDEO;
	else if(checkType(aFileName, SearchManager::TYPE_AUDIO))
		return SearchManager::TYPE_AUDIO;
	else if(checkType(aFileName, SearchManager::TYPE_COMPRESSED))
		return SearchManager::TYPE_COMPRESSED;
	else if(checkType(aFileName, SearchManager::TYPE_DOCUMENT))
		return SearchManager::TYPE_DOCUMENT;
	else if(checkType(aFileName, SearchManager::TYPE_EXECUTABLE))
		return SearchManager::TYPE_EXECUTABLE;
	else if(checkType(aFileName, SearchManager::TYPE_PICTURE))
		return SearchManager::TYPE_PICTURE;

	return SearchManager::TYPE_ANY;
}

/**
 * Alright, the main point here is that when searching, a search string is most often found in 
 * the filename, not directory name, so we want to make that case faster. Also, we want to
 * avoid changing StringLists unless we absolutely have to --> this should only be done if a string
 * has been matched in the directory name. This new stringlist should also be used in all descendants,
 * but not the parents...
 */
void ShareManager::Directory::search(SearchResultList& aResults, StringSearch::List& aStrings, int aSearchType, int64_t aSize, int aFileType, Client* aClient, StringList::size_type maxResults) const noexcept {
	// Skip everything if there's nothing to find here (doh! =)
	if(!hasType(aFileType))
		return;

	StringSearch::List* cur = &aStrings;
	unique_ptr<StringSearch::List> newStr;

	// Find any matches in the directory name
	for(StringSearch::List::const_iterator k = aStrings.begin(); k != aStrings.end(); ++k) {
		if(k->match(name)) {
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
		SearchResultPtr sr(new SearchResult(SearchResult::TYPE_DIRECTORY, 0, getFullName(), TTHValue()));
		aResults.push_back(sr);
		ShareManager::getInstance()->setHits(ShareManager::getInstance()->getHits()+1);
	}

	if(aFileType != SearchManager::TYPE_DIRECTORY) {
		for(File::Set::const_iterator i = files.begin(); i != files.end(); ++i) {
			
			if(aSearchType == SearchManager::SIZE_ATLEAST && aSize > i->getSize()) {
				continue;
			} else if(aSearchType == SearchManager::SIZE_ATMOST && aSize < i->getSize()) {
				continue;
			}	
			StringSearch::List::const_iterator j = cur->begin();
			for(; j != cur->end() && j->match(i->getName()); ++j) 
				;	// Empty
			
			if(j != cur->end())
				continue;
			
			// Check file type...
			if(checkType(i->getName(), aFileType)) {
				SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, i->getSize(), getFullName() + i->getName(), i->getTTH()));
				aResults.push_back(sr);
				ShareManager::getInstance()->setHits(ShareManager::getInstance()->getHits()+1);
				if(aResults.size() >= maxResults) {
					break;
				}
			}
		}
	}

	for(Directory::Map::const_iterator l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
			l->second->search(aResults, *cur, aSearchType, aSize, aFileType, aClient, maxResults);
	}
}

void ShareManager::search(SearchResultList& results, const string& aString, int aSearchType, int64_t aSize, int aFileType, Client* aClient, StringList::size_type maxResults) noexcept {
	Lock l(cs);
	if(aFileType == SearchManager::TYPE_TTH) {
		if(aString.compare(0, 4, "TTH:") == 0) {
			TTHValue tth(aString.substr(4));
			HashFileMap::const_iterator i = tthIndex.find(tth);
			if(i != tthIndex.end()) {
				SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, i->second->getSize(), 
					i->second->getParent()->getFullName() + i->second->getName(), i->second->getTTH()));

				results.push_back(sr);
				ShareManager::getInstance()->addHits(1);
			}
		}
		return;
	}
	StringTokenizer<string> t(Text::toLower(aString), '$');
	StringList& sl = t.getTokens();
	if(!bloom.match(sl))
		return;

	StringSearch::List ssl;
	for(StringList::const_iterator i = sl.begin(); i != sl.end(); ++i) {
		if(!i->empty()) {
			ssl.push_back(StringSearch(*i));
		}
	}
	if(ssl.empty())
		return;

	for(DirList::const_iterator j = directories.begin(); (j != directories.end()) && (results.size() < maxResults); ++j) {
		(*j)->search(results, ssl, aSearchType, aSize, aFileType, aClient, maxResults);
	}
}

namespace {
	inline uint16_t toCode(char a, char b) { return (uint16_t)a | ((uint16_t)b)<<8; }
}

ShareManager::AdcSearch::AdcSearch(const StringList& params) : include(&includeX), gt(0), 
	lt(numeric_limits<int64_t>::max()), hasRoot(false), isDirectory(false)
{
	for(StringIterC i = params.begin(); i != params.end(); ++i) {
		const string& p = *i;
		if(p.length() <= 2)
			continue;

		uint16_t cmd = toCode(p[0], p[1]);
		if(toCode('T', 'R') == cmd) {
			hasRoot = true;
			root = TTHValue(p.substr(2));
			return;
		} else if(toCode('A', 'N') == cmd) {
			includeX.push_back(StringSearch(p.substr(2)));		
		} else if(toCode('N', 'O') == cmd) {
			exclude.push_back(StringSearch(p.substr(2)));
		} else if(toCode('E', 'X') == cmd) {
			ext.push_back(p.substr(2));
		} else if(toCode('G', 'R') == cmd) {
			auto exts = AdcHub::parseSearchExts(Util::toInt(p.substr(2)));
			ext.insert(ext.begin(), exts.begin(), exts.end());
		} else if(toCode('R', 'X') == cmd) {
			noExt.push_back(p.substr(2));
		} else if(toCode('G', 'E') == cmd) {
			gt = Util::toInt64(p.substr(2));
		} else if(toCode('L', 'E') == cmd) {
			lt = Util::toInt64(p.substr(2));
		} else if(toCode('E', 'Q') == cmd) {
			lt = gt = Util::toInt64(p.substr(2));
		} else if(toCode('T', 'Y') == cmd) {
			isDirectory = (p[2] == '2');
		}
	}
}

bool ShareManager::AdcSearch::isExcluded(const string& str) {
	for(StringSearch::List::iterator i = exclude.begin(); i != exclude.end(); ++i) {
		if(i->match(str))
			return true;
	}
	return false;
}

bool ShareManager::AdcSearch::hasExt(const string& name) {
	if(ext.empty())
		return true;
	if(!noExt.empty()) {
		ext = StringList(ext.begin(), set_difference(ext.begin(), ext.end(), noExt.begin(), noExt.end(), ext.begin()));
		noExt.clear();
	}
	for(auto i = ext.cbegin(), iend = ext.cend(); i != iend; ++i) {
		if(name.length() >= i->length() && stricmp(name.c_str() + name.length() - i->length(), i->c_str()) == 0)
			return true;
	}
	return false;
}

void ShareManager::Directory::search(SearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults) const noexcept {
	StringSearch::List* cur = aStrings.include;
	StringSearch::List* old = aStrings.include;

	unique_ptr<StringSearch::List> newStr;

	// Find any matches in the directory name
	for(StringSearch::List::const_iterator k = cur->begin(); k != cur->end(); ++k) {
		if(k->match(name) && !aStrings.isExcluded(name)) {
			if(!newStr.get()) {
				newStr = unique_ptr<StringSearch::List>(new StringSearch::List(*cur));
			}
			newStr->erase(remove(newStr->begin(), newStr->end(), *k), newStr->end());
		}
	}

	if(newStr.get() != 0) {
		cur = newStr.get();
	}

	bool sizeOk = (aStrings.gt == 0);
	if( cur->empty() && aStrings.ext.empty() && sizeOk ) {
		// We satisfied all the search words! Add the directory...
		SearchResultPtr sr(new SearchResult(SearchResult::TYPE_DIRECTORY, getSize(), getFullName(), TTHValue()));
		aResults.push_back(sr);
		ShareManager::getInstance()->setHits(ShareManager::getInstance()->getHits()+1);
	}

	if(!aStrings.isDirectory) {
		for(File::Set::const_iterator i = files.begin(); i != files.end(); ++i) {

			if(!(i->getSize() >= aStrings.gt)) {
				continue;
			} else if(!(i->getSize() <= aStrings.lt)) {
				continue;
			}	

			if(aStrings.isExcluded(i->getName()))
				continue;

			StringSearch::List::const_iterator j = cur->begin();
			for(; j != cur->end() && j->match(i->getName()); ++j) 
				;	// Empty

			if(j != cur->end())
				continue;

			// Check file type...
			if(aStrings.hasExt(i->getName())) {

				SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, 
					i->getSize(), getFullName() + i->getName(), i->getTTH()));
				aResults.push_back(sr);
				ShareManager::getInstance()->addHits(1);
				if(aResults.size() >= maxResults) {
					return;
				}
			}
		}
	}

	for(Directory::Map::const_iterator l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
		l->second->search(aResults, aStrings, maxResults);
	}
	aStrings.include = old;
}

void ShareManager::search(SearchResultList& results, const StringList& params, StringList::size_type maxResults) noexcept {
	AdcSearch srch(params);	

	Lock l(cs);

	if(srch.hasRoot) {
		HashFileMap::const_iterator i = tthIndex.find(srch.root);
		if(i != tthIndex.end()) {
			SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, 
				i->second->getSize(), i->second->getParent()->getFullName() + i->second->getName(), 
				i->second->getTTH()));
			results.push_back(sr);
			addHits(1);
		}
		return;
	}

	for(StringSearch::List::const_iterator i = srch.includeX.begin(); i != srch.includeX.end(); ++i) {
		if(!bloom.match(i->getPattern()))
			return;
	}

	for(DirList::const_iterator j = directories.begin(); (j != directories.end()) && (results.size() < maxResults); ++j) {
		(*j)->search(results, srch, maxResults);
	}
}

ShareManager::Directory::Ptr ShareManager::getDirectory(const string& fname) {
	for(StringMapIter mi = shares.begin(); mi != shares.end(); ++mi) {
		if(strnicmp(fname, mi->first, mi->first.length()) == 0) {
			Directory::Ptr d;
			for(DirList::const_iterator i = directories.begin(); i != directories.end(); ++i) {
				if(stricmp((*i)->getName(), mi->second) == 0) {
					d = *i;
				}
			}
			
			if(!d) {
				return Directory::Ptr();
			}

			string::size_type i;
			string::size_type j = mi->first.length();
			while( (i = fname.find(PATH_SEPARATOR, j)) != string::npos) {
				Directory::MapIter dmi = d->directories.find(fname.substr(j, i-j));
				j = i + 1;
				if(dmi == d->directories.end())
					return Directory::Ptr();
				d = dmi->second;
			}
			return d;
		}
	}
	return Directory::Ptr();
}

void ShareManager::on(QueueManagerListener::FileMoved, const string& n) noexcept {
	if(BOOLSETTING(ADD_FINISHED_INSTANTLY)) {
		// Check if finished download is supposed to be shared
		Lock l(cs);
		for(StringMapIter i = shares.begin(); i != shares.end(); i++) {
			if(strnicmp(i->first, n, i->first.size()) == 0 && n[i->first.size() - 1] == PATH_SEPARATOR) {
				try {
					// Schedule for hashing, it'll be added automatically later on...
					HashManager::getInstance()->checkTTH(n, File::getSize(n), 0);
				} catch(const Exception&) {
					// Not a vital feature...
				}
				break;
			}
		}
	}
}

void ShareManager::on(HashManagerListener::TTHDone, const string& fname, const TTHValue& root) noexcept {
	Lock l(cs);
	Directory::Ptr d = getDirectory(fname);
	if(d) {
		Directory::File::Set::const_iterator i = d->findFile(Util::getFileName(fname));
		if(i != d->files.end()) {
			if(root != i->getTTH())
				tthIndex.erase(i->getTTH());
			// Get rid of false constness...
			Directory::File* f = const_cast<Directory::File*>(&(*i));
			f->setTTH(root);
			tthIndex.insert(make_pair(f->getTTH(), i));
		} else {
			string name = Util::getFileName(fname);
			int64_t size = File::getSize(fname);
			Directory::File::Set::iterator it = d->files.insert(Directory::File(name, size, d, root)).first;
			updateIndices(*d, it);
		}
		setDirty(); 
	}
}

void ShareManager::on(TimerManagerListener::Minute, uint64_t tick) noexcept {
	//this will only generate it every 15minutes if its dirty
	//generateXmlList();

	if(SETTING(INCOMING_REFRESH_TIME) > 0 && !incoming.empty()){
			if(lastIncomingUpdate + SETTING(INCOMING_REFRESH_TIME) * 60 * 1000 <= tick) {
			setDirty();
			refreshIncoming();
		}
	}
	if(SETTING(AUTO_REFRESH_TIME) > 0) {
		if(lastFullUpdate + SETTING(AUTO_REFRESH_TIME) * 60 * 1000 <= tick) {
			setDirty();
			refresh(ShareManager::REFRESH_ALL | ShareManager::REFRESH_UPDATE);
		}
	}
}

void ShareManager::Rebuild() {
	//make sure we refreshed before doing a rebuild
	rebuild = true;
	refresh(ShareManager::REFRESH_ALL | ShareManager::REFRESH_UPDATE);
}

bool ShareManager::shareFolder(const string& path, bool thoroughCheck /* = false */) const {
	if(thoroughCheck)	// check if it's part of the share before checking if it's in the exclusions
	{
		bool result = false;
		for(StringMap::const_iterator i = shares.begin(); i != shares.end(); ++i)
		{
			// is it a perfect match
			if((path.size() == i->first.size()) && (stricmp(path, i->first) == 0))
				return true;
			else if (path.size() > i->first.size()) // this might be a subfolder of a shared folder
			{
				string temp = path.substr(0, i->first.size());
				// if the left-hand side matches and there is a \ in the remainder then it is a subfolder
				if((stricmp(temp, i->first) == 0) && (path.find('\\', i->first.size()) != string::npos))
				{
					result = true;
					break;
				}
			}
		}

		if(!result)
			return false;
	}

	// check if it's an excluded folder or a sub folder of an excluded folder
	for(StringIterC j = notShared.begin(); j != notShared.end(); ++j)
	{		
		if(stricmp(path, *j) == 0)
			return false;

		if(thoroughCheck)
		{
			if(path.size() > (*j).size())
			{
				string temp = path.substr(0, (*j).size());
				if((stricmp(temp, *j) == 0) && (path[(*j).size()] == '\\'))
					return false;
			}
		}
	}
	return true;
}

int64_t ShareManager::addExcludeFolder(const string &path) {
	
	HashManager::getInstance()->stopHashing(path);
	
	// make sure this is a sub folder of a shared folder
	bool result = false;
	for(StringMap::const_iterator i = shares.begin(); i != shares.end(); ++i)
	{
		if(path.size() > i->first.size())
		{
			string temp = path.substr(0, i->first.size());
			if(stricmp(temp, i->first) == 0)
			{
				result = true;
				break;
			}
		}
	}

	if(!result)
		return 0;

	// Make sure this not a subfolder of an already excluded folder
	for(StringIterC j = notShared.begin(); j != notShared.end(); ++j)
	{
		if(path.size() >= (*j).size())
		{
			string temp = path.substr(0, (*j).size());
			if(stricmp(temp, *j) == 0)
				return 0;
		}
	}

	// remove all sub folder excludes
	int64_t bytesNotCounted = 0;
	for(StringIter j = notShared.begin(); j != notShared.end(); ++j)
	{
		if(path.size() < (*j).size())
		{
			string temp = (*j).substr(0, path.size());
			if(stricmp(temp, path) == 0)
			{
				bytesNotCounted += Util::getDirSize(*j);
				j = notShared.erase(j);
				j--;
			}
		}
	}

	// add it to the list
	notShared.push_back(path);

	int64_t bytesRemoved = Util::getDirSize(path);

	return (bytesRemoved - bytesNotCounted);
}

int64_t ShareManager::removeExcludeFolder(const string &path, bool returnSize /* = true */) {
	int64_t bytesAdded = 0;
	// remove all sub folder excludes
	for(StringIter j = notShared.begin(); j != notShared.end(); ++j)
	{
		if(path.size() <= (*j).size())
		{
			string temp = (*j).substr(0, path.size());
			if(stricmp(temp, path) == 0)
			{
				if(returnSize) // this needs to be false if the files don't exist anymore
					bytesAdded += Util::getDirSize(*j);
				
				j = notShared.erase(j);
				j--;
			}
		}
	}
	
	return bytesAdded;
}

StringList ShareManager::getVirtualNames() {
	StringList result;



	for(StringMap::const_iterator i = shares.begin(); i != shares.end(); ++i){
		bool exists = false;

		for(StringIter j = result.begin(); j != result.end(); ++j) {
			if( stricmp( *j, i->second ) == 0 ){
				exists = true;
				break;
			}
		}

		if( !exists )
			result.push_back( i->second );
	}

	sort( result.begin(), result.end() );

	return result;
}


} // namespace dcpp

/**
 * @file
 * $Id: ShareManager.cpp 473 2010-01-12 23:17:33Z bigmuscle $
 */
