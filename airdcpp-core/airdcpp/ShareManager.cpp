/*
 * Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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

#include "AirUtil.h"
#include "Bundle.h"
#include "BZUtils.h"
#include "ClientManager.h"
#include "ErrorCollector.h"
#include "File.h"
#include "FilteredFile.h"
#include "LogManager.h"
#include "HashManager.h"
#include "ResourceManager.h"
#include "ScopedFunctor.h"
#include "SearchResult.h"
#include "SharePathValidator.h"
#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "Transfer.h"
#include "UserConnection.h"

#include "version.h"

#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/algorithm/count_if.hpp>
#include <boost/algorithm/cxx11/all_of.hpp>

#include "concurrency.h"

namespace dcpp {

using std::string;
using boost::adaptors::filtered;
using boost::range::find_if;
using boost::range::for_each;
using boost::range::copy;
using boost::algorithm::copy_if;
using boost::range::remove_if;

#define SHARE_CACHE_VERSION "3"

#ifdef ATOMIC_FLAG_INIT
atomic_flag ShareManager::refreshing = ATOMIC_FLAG_INIT;
#else
atomic_flag ShareManager::refreshing;
#endif

ShareManager::ShareManager() : bloom(new ShareBloom(1 << 20)), validator(new SharePathValidator())
{ 
	SettingsManager::getInstance()->addListener(this);
	HashManager::getInstance()->addListener(this);

	File::ensureDirectory(Util::getPath(Util::PATH_SHARECACHE));
#ifdef _DEBUG
#ifdef _WIN32
	{
		auto emoji = Text::wideToUtf8(L"\U0001F30D");

		DualString d1(emoji);
		dcassert(d1.getNormal() == emoji);
		dcassert(d1.getLower() == emoji);
	}

	{
		auto character = _T("\u00D6"); // Ö
		DualString d2(Text::wideToUtf8(character));
		dcassert(d2.getNormal() != d2.getLower());
	}
#endif
#endif
}

ShareManager::~ShareManager() {
	HashManager::getInstance()->removeListener(this);
	SettingsManager::getInstance()->removeListener(this);
}

// Note that settings are loaded before this function is called
// This function shouldn't initialize anything that is needed by the startup wizard
void ShareManager::startup(function<void(const string&)> splashF, function<void(float)> progressF) noexcept {
	bool refreshed = false;
	if(!loadCache(progressF)) {
		if (splashF)
			splashF(STRING(REFRESHING_SHARE));
		refresh(false, TYPE_STARTUP_BLOCKING, progressF);
		refreshed = true;
	}

	addAsyncTask([=] {
		if (!refreshed)
			fire(ShareManagerListener::ShareLoaded());

		TimerManager::getInstance()->addListener(this);

		if (SETTING(STARTUP_REFRESH) && !refreshed)
			refresh(false, TYPE_STARTUP_DELAYED);
	});
}

void ShareManager::abortRefresh() noexcept {
	//abort buildtree and refresh, we are shutting down.
	aShutdown = true;
}

void ShareManager::shutdown(function<void(float)> progressF) noexcept {
	saveXmlList(progressF);

	try {
		RLock l (cs);
		//clear refs so we can delete filelists.
		auto lists = File::findFiles(Util::getPath(Util::PATH_USER_CONFIG), "files?*.xml.bz2", File::TYPE_FILE);
		for(auto& f: shareProfiles) {
			if(f->getProfileList() && f->getProfileList()->bzXmlRef.get()) 
				f->getProfileList()->bzXmlRef.reset(); 
		}

		for_each(lists, File::deleteFile);
	} catch(...) { }

	TimerManager::getInstance()->removeListener(this);
	join();
}

void ShareManager::setProfilesDirty(const ProfileTokenSet& aProfiles, bool aIsMajorChange /*false*/) noexcept {
	if (!aProfiles.empty()) {
		RLock l(cs);
		for(const auto token: aProfiles) {
			auto i = find(shareProfiles.begin(), shareProfiles.end(), token);
			if(i != shareProfiles.end()) {
				if (aIsMajorChange)
					(*i)->getProfileList()->setForceXmlRefresh(true);
				(*i)->getProfileList()->setXmlDirty(true);
				(*i)->setProfileInfoDirty(true);
			}
		}
	}

	for (const auto token : aProfiles) {
		fire(ShareManagerListener::ProfileUpdated(), token, aIsMajorChange);
	}
}

ShareManager::Directory::Directory(DualString&& aRealName, const ShareManager::Directory::Ptr& aParent, time_t aLastWrite, const RootDirectory::Ptr& aRoot) :
	parent(aParent.get()),
	root(aRoot),
	lastWrite(aLastWrite),
	realName(move(aRealName))
{
}

ShareManager::Directory::~Directory() { 
	for_each(files, DeleteFunction());
}

void ShareManager::Directory::updateModifyDate() {
	lastWrite = dcpp::File::getLastModified(getRealPath());
}

void ShareManager::Directory::getContentInfo(int64_t& size_, size_t& files_, size_t& folders_) const noexcept {
	for(const auto& d: directories) {
		d->getContentInfo(size_, files_, folders_);
	}

	folders_ += directories.size();
	size_ += size;
	files_ += files.size();
}

int64_t ShareManager::Directory::getLevelSize() const noexcept {
	return size;
}

int64_t ShareManager::Directory::getTotalSize() const noexcept {
	int64_t tmp = size;
	for (const auto& d : directories) {
		tmp += d->getTotalSize();
	}

	return tmp;
}

string ShareManager::Directory::getAdcPath() const noexcept {
	if (parent) {
		return parent->getAdcPath() + realName.getNormal() + ADC_SEPARATOR;
	}

	if (!root) {
		// Root may not be available for subdirectories that are being refreshed
		return ADC_SEPARATOR_STR;
	}

	return ADC_SEPARATOR + root->getName() + ADC_SEPARATOR;
}

string ShareManager::Directory::getVirtualName() const noexcept {
	if (root) {
		return root->getName();
	}

	return realName.getNormal();
}

const string& ShareManager::Directory::getVirtualNameLower() const noexcept {
	if (root) {
		return root->getNameLower();
	}

	return realName.getLower();
}

StringList ShareManager::getRealPaths(const TTHValue& root) const noexcept {
	StringList ret;

	RLock l(cs);
	const auto i = tthIndex.equal_range(const_cast<TTHValue*>(&root)); 
	for (auto f = i.first; f != i.second; ++f) {
		ret.push_back((*f).second->getRealPath());
	}

	const auto k = tempShares.find(root);
	if (k != tempShares.end()) {
		ret.push_back(k->second.path);
	}

	return ret;
}


bool ShareManager::isTTHShared(const TTHValue& tth) const noexcept {
	RLock l(cs);
	return tthIndex.find(const_cast<TTHValue*>(&tth)) != tthIndex.end();
}

void ShareManager::Directory::increaseSize(int64_t aSize, int64_t& totalSize_) noexcept {
	size += aSize; 
	totalSize_ += aSize;
	//dcassert(accumulate(files.begin(), files.end(), (int64_t)0, [](int64_t aTotal, const File* aFile) { return aTotal + aFile->getSize(); }) == size);
}

void ShareManager::Directory::decreaseSize(int64_t aSize, int64_t& totalSize_) noexcept {
	size -= aSize; 
	totalSize_ -= aSize;
	dcassert(size >= 0 && totalSize_ >= 0);
}

string ShareManager::Directory::getRealPath(const string& path) const noexcept {
	if (parent) {
		return parent->getRealPath(realName.getNormal() + PATH_SEPARATOR_STR + path);
	}

	if (!root) {
		// Root may not be available for subdirectories that are being refreshed
		return path;
	}

	return root->getPath() + path;
}

bool ShareManager::Directory::isRoot() const noexcept {
	return root ? true : false;
}

bool ShareManager::Directory::hasProfile(const ProfileTokenSet& aProfiles) const noexcept {
	if (root && root->hasRootProfile(aProfiles)) {
		return true;
	}

	if (parent) {
		return parent->hasProfile(aProfiles);
	}

	return false;
}


void ShareManager::Directory::copyRootProfiles(ProfileTokenSet& profiles_, bool aSetCacheDirty) const noexcept {
	if (root) {
		boost::copy(root->getRootProfiles(), inserter(profiles_, profiles_.begin()));
		if (aSetCacheDirty)
			root->setCacheDirty(true);
	}

	if (parent)
		parent->copyRootProfiles(profiles_, aSetCacheDirty);
}

bool ShareManager::RootDirectory::hasRootProfile(const ProfileTokenSet& aProfiles) const noexcept {
	for(const auto ap: aProfiles) {
		if (rootProfiles.find(ap) != rootProfiles.end())
			return true;
	}
	return false;
}

bool ShareManager::Directory::hasProfile(const OptionalProfileToken& aProfile) const noexcept {
	if(!aProfile || (root && root->hasRootProfile(*aProfile))) {
		return true;
	} 
	
	if (parent) {
		return parent->hasProfile(aProfile);
	}

	return false;
}

bool ShareManager::RootDirectory::hasRootProfile(ProfileToken aProfile) const noexcept {
	return rootProfiles.find(aProfile) != rootProfiles.end();
}

ShareManager::RootDirectory::RootDirectory(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming, time_t aLastRefreshTime) noexcept :
	path(aRootPath), cacheDirty(false), virtualName(make_unique<DualString>(aVname)), 
	incoming(aIncoming), rootProfiles(aProfiles), lastRefreshTime(aLastRefreshTime) {

}

ShareManager::RootDirectory::Ptr ShareManager::RootDirectory::create(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming, time_t aLastRefreshTime) noexcept {
	return shared_ptr<RootDirectory>(new RootDirectory(aRootPath, aVname, aProfiles, aIncoming, aLastRefreshTime));
}

void ShareManager::RootDirectory::addRootProfile(ProfileToken aProfile) noexcept {
	rootProfiles.emplace(aProfile);
}

bool ShareManager::RootDirectory::removeRootProfile(ProfileToken aProfile) noexcept {
	rootProfiles.erase(aProfile);
	return rootProfiles.empty();
}

string ShareManager::toVirtual(const TTHValue& tth, ProfileToken aProfile) const {
	
	RLock l(cs);

	FileList* fl = getFileList(aProfile);
	if(tth == fl->getBzXmlRoot()) {
		return Transfer::USER_LIST_NAME_BZ;
	} else if(tth == fl->getXmlRoot()) {
		return Transfer::USER_LIST_NAME;
	}

	auto i = tthIndex.find(const_cast<TTHValue*>(&tth)); 
	if (i != tthIndex.end()) {
		return i->second->getAdcPath();
	}

	//nothing found throw;
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

FileList* ShareManager::getFileList(ProfileToken aProfile) const {
	const auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
	if(i != shareProfiles.end()) {
		dcassert((*i)->getProfileList());
		return (*i)->getProfileList();
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

pair<int64_t, string> ShareManager::getFileListInfo(const string& virtualFile, ProfileToken aProfile) {
	if(virtualFile == "MyList.DcLst") 
		throw ShareException("NMDC-style lists no longer supported, please upgrade your client");

	if(virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
		FileList* fl = generateXmlList(aProfile);
		return { fl->getBzXmlListLen(), fl->getFileName() };
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

void ShareManager::toRealWithSize(const string& aVirtualFile, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_) {
	if(aVirtualFile.compare(0, 4, "TTH/") == 0) {
		TTHValue tth(aVirtualFile.substr(4));

		RLock l(cs);
		if(any_of(aProfiles.begin(), aProfiles.end(), [](ProfileToken s) { return s != SP_HIDDEN; })) {
			const auto flst = tthIndex.equal_range(const_cast<TTHValue*>(&tth));
			for(auto f = flst.first; f != flst.second; ++f) {
				noAccess_ = false; //we may throw if the file doesn't exist on the disk so always reset this to prevent invalid access denied messages
				auto profiles = aProfiles;
				if (f->second->getParent()->hasProfile(profiles)) {
					path_ = f->second->getRealPath();
					size_ = f->second->getSize();
					return;
				} else {
					noAccess_ = true;
				}
			}
		}

		const auto files = tempShares.equal_range(tth);
		for (auto i = files.first; i != files.second; ++i) {
			noAccess_ = false;
			if (i->second.hasAccess(aUser)) {
				path_ = i->second.path;
				size_ = i->second.size;
				return;
			} else {
				noAccess_ = true;
			}
		}
	} else {
		Directory::List dirs;

		RLock l (cs);
		findVirtuals<ProfileTokenSet>(aVirtualFile, aProfiles, dirs);

		auto fileName = Text::toLower(Util::getAdcFileName(aVirtualFile));
		for(const auto& d: dirs) {
			auto it = d->files.find(fileName);
			if(it != d->files.end()) {
				path_ = (*it)->getRealPath();
				size_ = (*it)->getSize();
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

MemoryInputStream* ShareManager::getTree(const string& virtualFile, ProfileToken aProfile) const noexcept {
	TigerTree tree;
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		if(!HashManager::getInstance()->getTree(TTHValue(virtualFile.substr(4)), tree))
			return nullptr;
	} else {
		try {
			TTHValue tth = getListTTH(virtualFile, aProfile);
			HashManager::getInstance()->getTree(tth, tree);
		} catch(const Exception&) {
			return nullptr;
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
		const Directory::File* f = i->second;
		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", f->getAdcPath());
		cmd.addParam("SI", Util::toString(f->getSize()));
		cmd.addParam("TR", f->getTTH().toBase32());
		return cmd;
	}

	//not found throw
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

bool ShareManager::isTempShared(const UserPtr& aUser, const TTHValue& tth) const noexcept {
	RLock l(cs);
	const auto fp = tempShares.equal_range(tth);
	for (auto i = fp.first; i != fp.second; ++i) {
		if (i->second.hasAccess(aUser)) {
			return true;
		}
	}
	return false;
}

bool ShareManager::hasTempShares() const noexcept {
	return !tempShares.empty(); 
}

TempShareInfoList ShareManager::getTempShares() const noexcept {
	TempShareInfoList ret;

	{
		RLock l(cs);
		boost::copy(tempShares | map_values, back_inserter(ret));
	}

	return ret;
}


TempShareInfo::TempShareInfo(const string& aName, const string& aPath, int64_t aSize, const TTHValue& aTTH, const UserPtr& aUser) noexcept :
	id(Util::rand()), user(aUser), name(aName), path(aPath), size(aSize), tth(aTTH), timeAdded(GET_TIME()) { }

bool TempShareInfo::hasAccess(const UserPtr& aUser) const noexcept {
	return !user || user == aUser;
}

optional<TempShareInfo> ShareManager::addTempShare(const TTHValue& aTTH, const string& aName, const string& aFilePath, int64_t aSize, ProfileToken aProfile, const UserPtr& aUser) noexcept {
	// Regular shared file?
	if (isFileShared(aTTH, aProfile)) {
		return nullopt;
	} 
	
	const auto item = TempShareInfo(aName, aFilePath, aSize, aTTH, aUser);
	{
		WLock l(cs);
		const auto files = tempShares.equal_range(aTTH);
		for (auto i = files.first; i != files.second; ++i) {
			if (i->second.hasAccess(aUser)) {
				return i->second;
			}
		}

		//didnt exist.. fine, add it.
		tempShares.emplace(aTTH, item);
	}

	fire(ShareManagerListener::TempFileAdded(), item);
	return item;
}

bool ShareManager::removeTempShare(const UserPtr& aUser, const TTHValue& tth) noexcept {
	optional<TempShareInfo> removedItem;

	{
		WLock l(cs);
		const auto files = tempShares.equal_range(tth);
		for (auto i = files.first; i != files.second; ++i) {
			if (i->second.user == aUser) {
				removedItem.emplace(i->second);
				tempShares.erase(i);
				break;
			}
		}
	}

	if (removedItem) {
		fire(ShareManagerListener::TempFileRemoved(), *removedItem);
		return true;
	}


	return false;
}

bool ShareManager::removeTempShare(TempShareToken aId) noexcept {
	optional<TempShareInfo> removedItem;

	{
		WLock l(cs);
		const auto i = find_if(tempShares | map_values, [aId](const TempShareInfo& ti) {
			return ti.id == aId;
		});

		if (i.base() == tempShares.end()) {
			return false;
		}

		removedItem.emplace(*i);
		tempShares.erase(i.base());
	}

	fire(ShareManagerListener::TempFileRemoved(), *removedItem);
	return true;
}

void ShareManager::getRealPaths(const string& aVirtualPath, StringList& realPaths_, const OptionalProfileToken& aProfile) const {
	if (aVirtualPath.empty())
		throw ShareException("empty virtual path");

	if (aVirtualPath == ADC_ROOT_STR) {
		getRootPaths(realPaths_);
		return;
	}

	Directory::List dirs;

	RLock l(cs);
	findVirtuals<OptionalProfileToken>(aVirtualPath, aProfile, dirs);

	if (aVirtualPath.back() == ADC_SEPARATOR) {
		// Directory
		for (const auto& d : dirs) {
			realPaths_.push_back(d->getRealPath());
		}
	} else {
		// File
		auto fileName = Text::toLower(Util::getAdcFileName(aVirtualPath));
		for(const auto& d: dirs) {
			auto it = d->files.find(fileName);
			if(it != d->files.end()) {
				realPaths_.push_back((*it)->getRealPath());
				return;
			}
		}
	}
}

bool ShareManager::isRealPathShared(const string& aPath) const noexcept {
	RLock l (cs);
	auto d = findDirectory(Util::getFilePath(aPath));
	if (d) {
		if (!aPath.empty() && aPath.back() == PATH_SEPARATOR) {
			// It's a directory
			return true;
		}

		// It's a file
		auto it = d->files.find(Text::toLower(Util::getFileName(aPath)));
		if(it != d->files.end()) {
			return true;
		}
	}

	return false;
}

string ShareManager::realToVirtualAdc(const string& aPath, const OptionalProfileToken& aToken) const noexcept{
	RLock l(cs);
	auto d = findDirectory(Util::getFilePath(aPath));
	if (!d || !d->hasProfile(aToken)) {
		return Util::emptyString;
	}

	auto vPathAdc = d->getAdcPath();
	if (aPath.back() == PATH_SEPARATOR) {
		// Directory
		return vPathAdc;
	}

	// It's a file
	return vPathAdc + ADC_SEPARATOR_STR + Util::getFileName(aPath);
}

string ShareManager::validateVirtualName(const string& aVirt) const noexcept {
	string tmp = aVirt;
	string::size_type idx = 0;

	while( (idx = tmp.find_first_of("\\/"), idx) != string::npos) {
		tmp[idx] = '_';
	}
	return tmp;
}

void ShareManager::loadProfile(SimpleXML& aXml, const string& aName, ProfileToken aToken) {
	auto sp = std::make_shared<ShareProfile>(aName, aToken);
	shareProfiles.push_back(sp);

	aXml.stepIn();
	while(aXml.findChild("Directory")) {
		auto realPath = Util::validatePath(aXml.getChildData(), true);
		if(realPath.empty()) {
			continue;
		}

		const auto& loadedVirtualName = aXml.getChildAttrib("Virtual");

		auto p = rootPaths.find(realPath);
		if (p != rootPaths.end()) {
			p->second->getRoot()->addRootProfile(aToken);
		} else {
			auto incoming = aXml.getBoolChildAttrib("Incoming");
			auto lastRefreshTime = aXml.getLongLongChildAttrib("LastRefreshTime");

			// Validate in case we have changed the rules
			auto vName = validateVirtualName(loadedVirtualName.empty() ? Util::getLastDir(realPath) : loadedVirtualName);
			Directory::createRoot(realPath, vName, { aToken }, incoming, 0, rootPaths, lowerDirNameMap, *bloom.get(), lastRefreshTime);
		}
	}

	aXml.resetCurrentChild();

	if (sp->isDefault()) {
		validator->loadExcludes(aXml);
	}

	aXml.stepOut();
}

void ShareManager::load(SimpleXML& aXml) {
	aXml.resetCurrentChild();
	if(aXml.findChild("Share")) {
		const auto& name = aXml.getChildAttrib("Name");
		loadProfile(aXml, !name.empty() ? name : STRING(DEFAULT), aXml.getIntChildAttrib("Token"));
	}

	aXml.resetCurrentChild();
	while (aXml.findChild("ShareProfile")) {
		const auto& token = aXml.getIntChildAttrib("Token");
		const auto& name = aXml.getChildAttrib("Name");
		if (token != SP_HIDDEN && !name.empty()) {
			loadProfile(aXml, name, token);
		}
	}
}

void ShareManager::on(SettingsManagerListener::LoadCompleted, bool) noexcept {
	validator->reloadSkiplist();

	{
		// Check share profiles
		if (!getShareProfile(SETTING(DEFAULT_SP))) {
			if (shareProfiles.empty()) {
				auto sp = std::make_shared<ShareProfile>(STRING(DEFAULT), SETTING(DEFAULT_SP));
				shareProfiles.push_back(sp);
			} else {
				SettingsManager::getInstance()->set(SettingsManager::DEFAULT_SP, shareProfiles.front()->getToken());
			}
		}

		auto hiddenProfile = std::make_shared<ShareProfile>(STRING(SHARE_HIDDEN), SP_HIDDEN);
		shareProfiles.push_back(hiddenProfile);
	}

	{
		// Validate loaded paths
		auto rootPathsCopy = rootPaths;
		for (const auto& dp : rootPathsCopy) {
			if (find_if(rootPathsCopy | map_keys, [&dp](const string& aPath) {
				return AirUtil::isSubLocal(dp.first, aPath);
			}).base() != rootPathsCopy.end()) {
				removeDirName(*dp.second.get(), lowerDirNameMap);
				rootPaths.erase(dp.first);

				LogManager::getInstance()->message("The directory " + dp.first + " was not loaded: parent of this directory is shared in another profile, which is not supported in this client version.", LogMessage::SEV_WARNING);
			}
		}
	}
}

ShareProfilePtr ShareManager::getShareProfile(ProfileToken aProfile, bool allowFallback /*false*/) const noexcept {
	RLock l (cs);
	const auto p = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
	if (p != shareProfiles.end()) {
		return *p;
	} else if (allowFallback) {
		dcassert(aProfile != SETTING(DEFAULT_SP));
		return *shareProfiles.begin();
	}
	return nullptr;
}

OptionalProfileToken ShareManager::getProfileByName(const string& aName) const noexcept {
	RLock l(cs);
	if (aName.empty()) {
		return SETTING(DEFAULT_SP);
	}

	auto p = find_if(shareProfiles, [&](const ShareProfilePtr& aProfile) { return Util::stricmp(aProfile->getPlainName(), aName) == 0; });
	if (p == shareProfiles.end())
		return nullopt;
	return (*p)->getToken();
}

ShareManager::Directory::Ptr ShareManager::Directory::createNormal(DualString&& aRealName, const Ptr& aParent, time_t aLastWrite, Directory::MultiMap& dirNameMap_, ShareBloom& bloom) noexcept {
	auto dir = Ptr(new Directory(move(aRealName), aParent, aLastWrite, nullptr));

	if (aParent) {
		auto added = aParent->directories.insert_sorted(dir).second;
		if (!added) {
			return nullptr;
		}
	}

	addDirName(dir, dirNameMap_, bloom);
	return dir;
}

ShareManager::Directory::Ptr ShareManager::Directory::createRoot(const string& aRootPath, const string& aVname, const ProfileTokenSet& aProfiles, bool aIncoming, 
	time_t aLastWrite, Map& rootPaths_, Directory::MultiMap& dirNameMap_, ShareBloom& bloom, time_t aLastRefreshTime) noexcept
{
	auto dir = Ptr(new Directory(Util::getLastDir(aRootPath), nullptr, aLastWrite, RootDirectory::create(aRootPath, aVname, aProfiles, aIncoming, aLastRefreshTime)));

	dcassert(rootPaths_.find(dir->getRealPath()) == rootPaths_.end());
	rootPaths_[dir->getRealPath()] = dir;

	addDirName(dir, dirNameMap_, bloom);
	return dir;
}

bool ShareManager::Directory::setParent(const Directory::Ptr& aDirectory, const Directory::Ptr& aParent) noexcept {
	aDirectory->parent = aParent.get();
	if (aParent) {
		auto inserted = aParent->directories.insert_sorted(aDirectory).second;
		if (!inserted) {
			dcassert(0);
			return false;
		}

		aParent->updateModifyDate();
	}

	return true;
}

void ShareManager::Directory::cleanIndices(Directory& aDirectory, int64_t& sharedSize_, File::TTHMap& tthIndex_, Directory::MultiMap& dirNames_) noexcept {
	aDirectory.cleanIndices(sharedSize_, tthIndex_, dirNames_);

	if (aDirectory.parent) {
		aDirectory.parent->directories.erase_key(aDirectory.realName.getLower());
		aDirectory.parent = nullptr;
	}
}

void ShareManager::Directory::File::updateIndices(ShareBloom& bloom_, int64_t& sharedSize_, TTHMap& tthIndex_) noexcept {
	parent->increaseSize(size, sharedSize_);
#ifdef _DEBUG
	checkAddedTTHDebug(this, tthIndex_);
#endif

	tthIndex_.emplace(const_cast<TTHValue*>(&tth), this);
	bloom_.add(name.getLower());
}

void ShareManager::Directory::cleanIndices(int64_t& sharedSize_, HashFileMap& tthIndex_, Directory::MultiMap& dirNames_) noexcept {
	for (auto& d : directories) {
		d->cleanIndices(sharedSize_, tthIndex_, dirNames_);
	}

	//remove from the name map
	removeDirName(*this, dirNames_);

	//remove all files
	for (const auto& f : files) {
		f->cleanIndices(sharedSize_, tthIndex_);
	}
}

void ShareManager::Directory::File::cleanIndices(int64_t& sharedSize_, File::TTHMap& tthIndex_) noexcept {
	parent->decreaseSize(size, sharedSize_);

	auto flst = tthIndex_.equal_range(const_cast<TTHValue*>(&tth));
	auto p = find(flst | map_values, this);
	if (p.base() != flst.second)
		tthIndex_.erase(p.base());
	else
		dcassert(0);
}

static const string SDIRECTORY = "Directory";
static const string SFILE = "File";
static const string SNAME = "Name";
static const string SSIZE = "Size";
static const string DATE = "Date";
static const string SHARE = "Share";
static const string SVERSION = "Version";

struct ShareManager::ShareLoader : public SimpleXMLReader::ThreadedCallBack, public ShareManager::RefreshInfo {
	ShareLoader(const string& aPath, const ShareManager::Directory::Ptr& aOldRoot, ShareManager::ShareBloom& aBloom) :
		ShareManager::RefreshInfo(aPath, aOldRoot, 0, aBloom),
		ThreadedCallBack(aOldRoot->getRoot()->getCacheXmlPath()),
		curDirPath(aOldRoot->getRoot()->getPath()),
		curDirPathLower(Text::toLower(aOldRoot->getRoot()->getPath()))
	{ 
		cur = newShareDirectory;
	}


	void startTag(const string& aName, StringPairList& attribs, bool simple) {
		if(compare(aName, SDIRECTORY) == 0) {
			const string& name = getAttrib(attribs, SNAME, 0);
			const string& date = getAttrib(attribs, DATE, 1);

			if(!name.empty()) {
				curDirPath += name + PATH_SEPARATOR;

				cur = ShareManager::Directory::createNormal(name, cur, Util::toTimeT(date), lowerDirNameMapNew, bloom);
				if (!cur) {
					throw Exception("Duplicate directory name");
				}

				curDirPathLower += cur->realName.getLower() + PATH_SEPARATOR;
			}

			if(simple) {
				if(cur) {
					cur = cur->getParent();
				}
			}
		} else if (cur && compare(aName, SFILE) == 0) {
			const string& fname = getAttrib(attribs, SNAME, 0);
			if(fname.empty()) {
				dcdebug("Invalid file found: %s\n", fname.c_str());
				return;
			}

			try {
				DualString name(fname);
				HashedFile fi;
				HashManager::getInstance()->getFileInfo(curDirPathLower + name.getLower(), curDirPath + fname, fi);
				addFile(move(name), cur, fi, tthIndexNew, bloom, addedSize);
			} catch(Exception& e) {
				hashSize += File::getSize(curDirPath + fname);
				dcdebug("Error loading file list %s \n", e.getError().c_str());
			}
		} else if (compare(aName, SHARE) == 0) {
			int version = Util::toInt(getAttrib(attribs, SVERSION, 0));
			if (version > Util::toInt(SHARE_CACHE_VERSION))
				throw Exception("Newer cache version"); //don't load those...

			cur->setLastWrite(Util::toTimeT(getAttrib(attribs, DATE, 2)));
		}
	}
	void endTag(const string& name) {
		if(compare(name, SDIRECTORY) == 0) {
			if(cur) {
				curDirPath = Util::getParentDir(curDirPath);
				curDirPathLower = Util::getParentDir(curDirPathLower);
				cur = cur->getParent();
			}
		}
	}

private:
	friend struct SizeSort;

	ShareManager::Directory::Ptr cur;

	string curDirPathLower;
	string curDirPath;
};

typedef shared_ptr<ShareManager::ShareLoader> ShareLoaderPtr;
typedef vector<ShareLoaderPtr> LoaderList;

bool ShareManager::loadCache(function<void(float)> progressF) noexcept{
	HashManager::HashPauser pauser;

	Util::migrate(Util::getPath(Util::PATH_SHARECACHE), "ShareCache_*");

	// Get all cache XMLs
	StringList fileList = File::findFiles(Util::getPath(Util::PATH_SHARECACHE), "ShareCache_*", File::TYPE_FILE);

	if (fileList.empty()) {
		return rootPaths.empty();
	}

	LoaderList cacheLoaders;

	// Create loaders
	for (const auto& p : fileList) {
		if (Util::getFileExt(p) == ".xml") {
			// Find the corresponding directory pointer for this path
			auto rp = find_if(rootPaths | map_values, [&p](const Directory::Ptr& aDir) {
				return Util::stricmp(aDir->getRoot()->getCacheXmlPath(), p) == 0; 
			});

			if (rp.base() != rootPaths.end()) {
				try {
					auto loader = std::make_shared<ShareLoader>(rp.base()->first, *rp, *bloom.get());
					cacheLoaders.emplace_back(loader);
					continue;
				} catch (...) {}
			}
		}

		// No use for this cache file
		File::deleteFile(p);
	}

	{
		const auto dirCount = cacheLoaders.size();

		//ll.sort(SimpleXMLReader::ThreadedCallBack::SizeSort());

		// Parse the actual cache files
		atomic<long> loaded(0);
		bool hasFailedCaches = false;

		try {
			parallel_for_each(cacheLoaders.begin(), cacheLoaders.end(), [&](ShareLoaderPtr& i) {
				//LogManager::getInstance()->message("Thread: " + Util::toString(::GetCurrentThreadId()) + "Size " + Util::toString(loader.size), LogMessage::SEV_INFO);
				auto& loader = *i;
				try {
					SimpleXMLReader(&loader).parse(*loader.file);
				} catch (SimpleXMLException& e) {
					LogManager::getInstance()->message(STRING_F(LOAD_FAILED_X, loader.xmlPath % e.getError()), LogMessage::SEV_ERROR);
					hasFailedCaches = true;
					File::deleteFile(loader.xmlPath);
				} catch (...) {
					hasFailedCaches = true;
					File::deleteFile(loader.xmlPath);
				}

				if (progressF) {
					progressF(static_cast<float>(loaded++) / static_cast<float>(dirCount));
				}
			});
		} catch (std::exception& e) {
			hasFailedCaches = true;
			LogManager::getInstance()->message("Loading the share cache failed: " + string(e.what()), LogMessage::SEV_INFO);
		}

		if (hasFailedCaches) {
			// Refresh all
			return false;
		}
	}

	// Apply the changes
	int64_t hashSize = 0;

	for (const auto& l : cacheLoaders) {
		applyRefreshChanges(*l, hashSize, nullptr);
	}

#ifdef _DEBUG
	//validateDirectoryTreeDebug();
#endif

	if (hashSize > 0) {
		LogManager::getInstance()->message(STRING_F(FILES_ADDED_FOR_HASH_STARTUP, Util::formatBytes(hashSize)), LogMessage::SEV_INFO);
	}

	return true;
}

void ShareManager::save(SimpleXML& aXml) {
	RLock l(cs);
	for(const auto& sp: shareProfiles | filtered(ShareProfile::NotHidden())) {
		auto isDefault = sp->getToken() == SETTING(DEFAULT_SP);

		aXml.addTag(isDefault ? "Share" : "ShareProfile"); // Keep the old Share tag around for compatibility with other clients
		aXml.addChildAttrib("Token", sp->getToken());
		aXml.addChildAttrib("Name", sp->getPlainName());
		aXml.stepIn();

		for(const auto& d: rootPaths | map_values | filtered(Directory::HasRootProfile(sp->getToken()))) {
			aXml.addTag("Directory", d->getRealPath());
			aXml.addChildAttrib("Virtual", d->getRoot()->getName());
			aXml.addChildAttrib("Incoming", d->getRoot()->getIncoming());
			aXml.addChildAttrib("LastRefreshTime", d->getRoot()->getLastRefreshTime());
		}

		if (isDefault) {
			// Excludes are global so they need to be saved only once
			validator->saveExcludes(aXml);
		}

		aXml.stepOut();
	}
}

void ShareManager::Directory::countStats(time_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles_, size_t& lowerCaseFiles_, size_t& totalStrLen_) const noexcept{
	for(auto& d: directories) {
		d->countStats(totalAge_, totalDirs_, totalSize_, totalFiles_, lowerCaseFiles_, totalStrLen_);
	}

	for (const auto& f: files) {
		totalSize_ += f->getSize();
		totalAge_ += f->getLastWrite();
		totalStrLen_ += f->name.getLower().length();
		if (f->name.lowerCaseOnly()) {
			lowerCaseFiles_++;
		}
	}

	totalStrLen_ += realName.getLower().length();
	totalDirs_ += directories.size();
	totalFiles_ += files.size();
}

void ShareManager::countStats(time_t& totalAge_, size_t& totalDirs_, int64_t& totalSize_, size_t& totalFiles_, size_t& lowerCaseFiles_, size_t& totalStrLen_, size_t& roots_) const noexcept{
	RLock l(cs);
	for (const auto& d : rootPaths | map_values) {
		totalDirs_++;
		roots_++;
		d->countStats(totalAge_, totalDirs_, totalSize_, totalFiles_, lowerCaseFiles_, totalStrLen_);
	}
}

optional<ShareManager::ShareItemStats> ShareManager::getShareItemStats() const noexcept {
	unordered_set<decltype(tthIndex)::key_type> uniqueTTHs;

	{
		RLock l(cs);
		for (auto tth : tthIndex | map_keys) {
			uniqueTTHs.insert(tth);
		}
	}

	ShareItemStats stats;
	stats.profileCount = shareProfiles.size() - 1; // remove hidden
	stats.uniqueFileCount = uniqueTTHs.size();

	time_t totalAge = 0;
	countStats(totalAge, stats.totalDirectoryCount, stats.totalSize, stats.totalFileCount, stats.lowerCaseFiles, stats.totalNameSize, stats.rootDirectoryCount);

	if (stats.uniqueFileCount == 0 || stats.totalDirectoryCount == 0) {
		return nullopt;
	}

	stats.averageFileAge = GET_TIME() - static_cast<time_t>(Util::countAverage(totalAge, stats.totalFileCount));
	stats.averageNameLength = Util::countAverage(stats.totalNameSize, stats.totalFileCount + stats.totalDirectoryCount);
	return stats;
}

ShareManager::ShareSearchStats ShareManager::getSearchMatchingStats() const noexcept {
	auto upseconds = static_cast<double>(GET_TICK()) / 1000.00;

	ShareSearchStats stats;

	stats.totalSearches = totalSearches;
	stats.totalSearchesPerSecond = Util::countAverage(totalSearches, upseconds);
	stats.recursiveSearches = recursiveSearches;
	stats.recursiveSearchesResponded = recursiveSearchesResponded;
	stats.filteredSearches = filteredSearches;
	stats.unfilteredRecursiveSearchesPerSecond = (recursiveSearches - filteredSearches) / upseconds;

	stats.averageSearchMatchMs = static_cast<uint64_t>(Util::countAverage(recursiveSearchTime, recursiveSearches - filteredSearches));
	stats.averageSearchTokenCount = Util::countAverage(searchTokenCount, recursiveSearches - filteredSearches);
	stats.averageSearchTokenLength = Util::countAverage(searchTokenLength, searchTokenCount);

	stats.autoSearches = autoSearches;
	stats.tthSearches = tthSearches;

	return stats;
}

string ShareManager::printStats() const noexcept {
	auto optionalItemStats = getShareItemStats();
	if (!optionalItemStats) {
		return "No files shared";
	}

	auto itemStats = *optionalItemStats;

	string ret = boost::str(boost::format(
"\r\n\r\n-=[ Share statistics ]=-\r\n\r\n\
Share profiles: %d\r\n\
Shared paths: %d\r\n\
Total share size: %s\r\n\
Total shared files: %d (of which %d%% are lowercase)\r\n\
Unique TTHs: %d (%d%%)\r\n\
Total shared directories: %d (%d files per directory)\r\n\
Average age of a file: %s\r\n\
Average name length of a shared item: %d bytes (total size %s)")

		% itemStats.profileCount
		% itemStats.rootDirectoryCount
		% Util::formatBytes(itemStats.totalSize)
		% itemStats.totalFileCount % Util::countPercentage(itemStats.lowerCaseFiles, itemStats.totalFileCount)
		% itemStats.uniqueFileCount % Util::countPercentage(itemStats.uniqueFileCount, itemStats.totalFileCount)
		% itemStats.totalDirectoryCount % Util::countAverage(itemStats.totalFileCount, itemStats.totalDirectoryCount)
		% Util::formatTime(itemStats.averageFileAge, false, true)
		% itemStats.averageNameLength
		% Util::formatBytes(itemStats.totalNameSize)
	);

	auto searchStats = getSearchMatchingStats();
	ret += boost::str(boost::format(
"\r\n\r\n-=[ Search statistics ]=-\r\n\r\n\
Total incoming searches: %d (%d per second)\r\n\
Incoming text searches: %d (of which %d were matched per second)\r\n\
Filtered text searches: %d%% (%d%% of the matched ones returned results)\r\n\
Average search tokens (non-filtered only): %d (%d bytes per token)\r\n\
Auto searches (text, ADC only): %d%%\r\n\
Average time for matching a recursive search: %d ms\r\n\
TTH searches: %d%% (hash bloom mode: %s)")

		% searchStats.totalSearches % searchStats.totalSearchesPerSecond
		% searchStats.recursiveSearches % searchStats.unfilteredRecursiveSearchesPerSecond
		% Util::countPercentage(searchStats.filteredSearches, searchStats.recursiveSearches) % Util::countPercentage(searchStats.recursiveSearchesResponded, searchStats.recursiveSearches - searchStats.filteredSearches)
		% searchStats.averageSearchTokenCount  % searchStats.averageSearchTokenLength
		% Util::countAverage(searchStats.autoSearches, searchStats.recursiveSearches)
		% searchStats.averageSearchMatchMs
		% Util::countPercentage(searchStats.tthSearches, searchStats.totalSearches)
		% (SETTING(BLOOM_MODE) != SettingsManager::BLOOM_DISABLED ? "Enabled" : "Disabled") // bloom mode
	);

	return ret;
}

void ShareManager::validateRootPath(const string& aRealPath, bool aMatchCurrentRoots) const {
	validator->validateRootPath(aRealPath);

	if (aMatchCurrentRoots) {
		RLock l(cs);
		for (const auto& p: rootPaths) {
			auto rootProfileNames = ShareProfile::getProfileNames(p.second->getRoot()->getRootProfiles(), shareProfiles);
			if (AirUtil::isParentOrExactLocal(p.first, aRealPath)) {
				if (Util::stricmp(p.first, aRealPath) != 0) {
					// Subdirectory of an existing directory is not allowed
					throw ShareException(STRING_F(DIRECTORY_PARENT_SHARED, Util::listToString(rootProfileNames)));
				}

				throw ShareException(STRING(DIRECTORY_SHARED));
			}

			if (AirUtil::isSubLocal(p.first, aRealPath)) {
				throw ShareException(STRING_F(DIRECTORY_SUBDIRS_SHARED, Util::listToString(rootProfileNames)));
			}
		}
	}
}

void ShareManager::getRoots(const OptionalProfileToken& aProfile, Directory::List& dirs_) const noexcept {
	copy(rootPaths | map_values | filtered(Directory::HasRootProfile(aProfile)), back_inserter(dirs_));
}

void ShareManager::getRootsByVirtual(const string& aVirtualName, const OptionalProfileToken& aProfile, Directory::List& dirs_) const noexcept {
	for(const auto& d: rootPaths | map_values | filtered(Directory::HasRootProfile(aProfile))) {
		if(Util::stricmp(d->getRoot()->getName(), aVirtualName) == 0) {
			dirs_.push_back(d);
		}
	}
}

void ShareManager::getRootsByVirtual(const string& aVirtualName, const ProfileTokenSet& aProfiles, Directory::List& dirs_) const noexcept {
	for(const auto& d: rootPaths | map_values) {
		// Compare name
		if (Util::stricmp(d->getRoot()->getNameLower(), aVirtualName) != 0) {
			continue;
		}

		// Find any matching profile
		if (ShareProfile::hasCommonProfiles(d->getRoot()->getRootProfiles(), aProfiles)) {
			dirs_.push_back(d);
		}
	}
}

void ShareManager::Directory::getProfileInfo(ProfileToken aProfile, int64_t& totalSize, size_t& filesCount) const noexcept {
	totalSize += size;
	filesCount += files.size();

	for(const auto& d: directories) {
		d->getProfileInfo(aProfile, totalSize, filesCount);
	}
}

void ShareManager::getProfileInfo(ProfileToken aProfile, int64_t& size, size_t& files) const noexcept {
	auto sp = getShareProfile(aProfile);
	if (!sp)
		return;

	if (sp->getProfileInfoDirty()) {
		{
			RLock l(cs);
			for (const auto& d : rootPaths | map_values) {
				if (d->getRoot()->hasRootProfile(aProfile)) {
					d->getProfileInfo(aProfile, size, files);
				}
			}
		}

		sp->setSharedFiles(files);
		sp->setShareSize(size);
		sp->setProfileInfoDirty(false);
	}

	size = sp->getShareSize();
	files = sp->getSharedFiles();
}

int64_t ShareManager::getTotalShareSize(ProfileToken aProfile) const noexcept {
	int64_t ret = 0;

	{
		RLock l(cs);
		for (const auto& d : rootPaths | map_values) {
			if (d->getRoot()->hasRootProfile(aProfile)) {
				ret += d->getTotalSize();
			}
		}
	}

	return ret;
}

bool ShareManager::isAdcDirectoryShared(const string& aAdcPath) const noexcept{
	Directory::List dirs;

	{
		RLock l(cs);
		getDirectoriesByAdcName(aAdcPath, dirs);
	}

	return !dirs.empty();
}

DupeType ShareManager::isAdcDirectoryShared(const string& aAdcPath, int64_t aSize) const noexcept{
	Directory::List dirs;

	{
		RLock l(cs);
		getDirectoriesByAdcName(aAdcPath, dirs);
	}

	if (dirs.empty())
		return DUPE_NONE;

	return dirs.front()->getTotalSize() == aSize ? DUPE_SHARE_FULL : DUPE_SHARE_PARTIAL;
}

StringList ShareManager::getAdcDirectoryPaths(const string& aAdcPath) const noexcept{
	StringList ret;
	Directory::List dirs;

	{
		RLock l(cs);
		getDirectoriesByAdcName(aAdcPath, dirs);
	}

	for (const auto& dir : dirs) {
		ret.push_back(dir->getRealPath());
	}

	return ret;
}

void ShareManager::getDirectoriesByAdcName(const string& aAdcPath, Directory::List& dirs_) const noexcept {
	if (aAdcPath.size() < 3)
		return;

	// get the last meaningful directory to look up
	auto nameInfo = AirUtil::getAdcDirectoryName(aAdcPath);

	auto nameLower = Text::toLower(nameInfo.first);
	const auto directories = lowerDirNameMap.equal_range(&nameLower);
	if (directories.first == directories.second)
		return;

	for (auto s = directories.first; s != directories.second; ++s) {
		if (nameInfo.second != string::npos) {
			// confirm that we have the subdirectory as well
			auto dir = s->second->findDirectoryByPath(aAdcPath.substr(nameInfo.second), ADC_SEPARATOR);
			if (dir) {
				dirs_.push_back(dir);
			}
		} else {
			dirs_.push_back(s->second);
		}
	}
}

ShareManager::Directory::Ptr ShareManager::Directory::findDirectoryByPath(const string& aPath, char separator) const noexcept {
	dcassert(!aPath.empty());

	auto p = aPath.find(separator);
	auto d = directories.find(Text::toLower(p != string::npos ? aPath.substr(0, p) : aPath));
	if (d != directories.end()) {
		if (p == aPath.size() || p == aPath.size() - 1)
			return *d;

		return (*d)->findDirectoryByPath(aPath.substr(p+1), separator);
	}

	return nullptr;
}

ShareManager::Directory::Ptr ShareManager::Directory::findDirectoryByName(const string& aName) const noexcept {
	auto p = directories.find(Text::toLower(aName));
	return p != directories.end() ? *p : nullptr;
}

bool ShareManager::isFileShared(const TTHValue& aTTH) const noexcept{
	RLock l (cs);
	return tthIndex.find(const_cast<TTHValue*>(&aTTH)) != tthIndex.end();
}

bool ShareManager::isFileShared(const TTHValue& aTTH, ProfileToken aProfile) const noexcept{
	RLock l (cs);
	const auto files = tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
	for(auto i = files.first; i != files.second; ++i) {
		if(i->second->getParent()->hasProfile(aProfile)) {
			return true;
		}
	}

	return false;
}

bool ShareManager::RefreshInfo::checkContent(const Directory::Ptr& aDirectory) noexcept {
	if (SETTING(SKIP_EMPTY_DIRS_SHARE) && aDirectory->getDirectories().empty() && aDirectory->files.empty()) {
		// Remove from parent
		Directory::cleanIndices(*aDirectory.get(), addedSize, tthIndexNew, lowerDirNameMapNew);
		return false;
	}

	return true;
}

ShareManager::ShareBuilder::ShareBuilder(const string& aPath, const Directory::Ptr& aOldRoot, time_t aLastWrite, ShareBloom& bloom_, bool& shutdown_, SharePathValidator& aPathValidator) :
	shutdown(shutdown_), pathValidator(aPathValidator), RefreshInfo(aPath, aOldRoot, aLastWrite, bloom_) {

}

bool ShareManager::ShareBuilder::buildTree() noexcept {
	try {
		buildTree(path, Text::toLower(path), newShareDirectory);
	} catch (const std::bad_alloc&) {
		LogManager::getInstance()->message(STRING_F(DIR_REFRESH_FAILED, path % STRING(OUT_OF_MEMORY)), LogMessage::SEV_ERROR);
		return false;
	} catch (...) {
		LogManager::getInstance()->message(STRING_F(DIR_REFRESH_FAILED, path % STRING(UNKNOWN_ERROR)), LogMessage::SEV_ERROR);
		return false;
	}

	return true;
}

void ShareManager::ShareBuilder::buildTree(const string& aPath, const string& aPathLower, const Directory::Ptr& aParent) {
	ErrorCollector errors;
	FileFindIter end;
	for(FileFindIter i(aPath, "*"); i != end && !shutdown; ++i) {
		const auto name = i->getFileName();
		if(name.empty()) {
			return;
		}

		const auto isDirectory = i->isDirectory();
		if (!isDirectory) {
			errors.increaseTotal();
		}

		DualString dualName(name);
		auto curPath = aPath + name + (isDirectory ? PATH_SEPARATOR_STR : Util::emptyString);
		auto curPathLower = aPathLower + dualName.getLower() + (isDirectory ? PATH_SEPARATOR_STR : Util::emptyString);

		try {
			pathValidator.validate(i, curPath, false);
		} catch (const ShareException& e) {
			if (SETTING(REPORT_BLOCKED_SHARE)) {
				if (isDirectory) {
					LogManager::getInstance()->message(STRING_F(SHARE_DIRECTORY_BLOCKED, curPath % e.getError()), LogMessage::SEV_INFO);
				} else {
					errors.add(e.getError(), name, false);
				}
			}

			continue;
		} catch (...) {
			continue;
		}

		if (isDirectory) {
			auto curDir = Directory::createNormal(move(dualName), aParent, i->getLastWriteTime(), lowerDirNameMapNew, bloom);
			if (curDir) {
				buildTree(curPath, curPathLower, curDir);
				checkContent(curDir);
			}
		} else {
			// Not a directory, assume it's a file...
			int64_t size = i->getSize();
			try {
				HashedFile fi(i->getLastWriteTime(), size);
				if(HashManager::getInstance()->checkTTH(aPathLower + dualName.getLower(), aPath + name, fi)) {
					addFile(move(dualName), aParent, fi, tthIndexNew, bloom, addedSize);
				} else {
					hashSize += size;
				}
			} catch(const HashException&) {
			}
		}
	}

	auto msg = errors.getMessage();
	if (!msg.empty()) {
		LogManager::getInstance()->message(STRING_F(SHARE_FILES_BLOCKED, aPath % msg), LogMessage::SEV_INFO);
	}
}

#ifdef _DEBUG
void ShareManager::checkAddedDirNameDebug(const Directory::Ptr& aDir, Directory::MultiMap& aDirNames) noexcept {
	auto directories = aDirNames.equal_range(const_cast<string*>(&aDir->getVirtualNameLower()));
	auto findByPtr = find(directories | map_values, aDir);
	auto findByPath = find_if(directories | map_values, [&](const Directory::Ptr& d) {
		return d->getRealPath() == aDir->getRealPath();
	});

	dcassert(findByPtr.base() == directories.second);
	dcassert(findByPath.base() == directories.second);
}

void ShareManager::checkAddedTTHDebug(const Directory::File* aFile, HashFileMap& aTTHIndex) noexcept {
	auto flst = aTTHIndex.equal_range(const_cast<TTHValue*>(&aFile->getTTH()));
	auto p = find(flst | map_values, aFile);
	dcassert(p.base() == flst.second);
}

void ShareManager::validateDirectoryTreeDebug() noexcept {
	OrderedStringSet directories, files;

	auto start = GET_TICK();
	{
		RLock l(cs);
		for (const auto& d : rootPaths | map_values) {
			validateDirectoryRecursiveDebug(d, directories, files);
		}
	}
	auto end = GET_TICK();
	dcdebug("Share tree checked in " U64_FMT " ms\n", end - start);

	StringList filesDiff, directoriesDiff;
	if (files.size() != tthIndex.size()) {
		OrderedStringSet indexed;
		for (const auto& f : tthIndex | map_values) {
			indexed.insert(f->getRealPath());
		}

		set_symmetric_difference(files.begin(), files.end(), indexed.begin(), indexed.end(), back_inserter(filesDiff));
	}

	if (directories.size() != lowerDirNameMap.size()) {
		OrderedStringSet indexed;
		for (const auto& d : lowerDirNameMap | map_values) {
			indexed.insert(d->getRealPath());
		}

		set_symmetric_difference(directories.begin(), directories.end(), indexed.begin(), indexed.end(), back_inserter(directoriesDiff));
	}

	dcassert(directoriesDiff.empty() && filesDiff.empty());
}

void ShareManager::validateDirectoryRecursiveDebug(const Directory::Ptr& aDir, OrderedStringSet& directoryPaths_, OrderedStringSet& filePaths_) noexcept {
	{
		auto res = directoryPaths_.insert(aDir->getRealPath());
		dcassert(res.second);
	}

	{
		Directory::List dirs;
		getDirectoriesByAdcName(aDir->getAdcPath(), dirs);

		dcassert(count_if(dirs.begin(), dirs.end(), [&](const Directory::Ptr& d) {
			return d->getRealPath() == aDir->getRealPath();
		}) == 1);

		dcassert(bloom->match(aDir->getVirtualNameLower()));
	}

	int64_t realDirectorySize = 0;
	for (const auto& f : aDir->files) {
		auto flst = tthIndex.equal_range(const_cast<TTHValue*>(&f->getTTH()));
		dcassert(boost::count_if(flst | map_values, [&](const Directory::File* aFile) {
			return aFile->getRealPath() == f->getRealPath();
		}) == 1);

		dcassert(bloom->match(f->name.getLower()));
		auto res = filePaths_.insert(f->getRealPath());
		dcassert(res.second);
		realDirectorySize += f->getSize();
	}

	auto cachedDirectorySize = aDir->getLevelSize();
	dcassert(cachedDirectorySize == realDirectorySize);

	for (const auto& d : aDir->getDirectories()) {
		validateDirectoryRecursiveDebug(d, directoryPaths_, filePaths_);
	}
}

#endif

ShareManager::RefreshResult ShareManager::refreshVirtualName(const string& aVirtualName) noexcept {
	StringList refreshDirs;

	{
		RLock l(cs);
		for(const auto& d: rootPaths | map_values) {
			if (Util::stricmp(d->getRoot()->getNameLower(), aVirtualName) == 0) {
				refreshDirs.push_back(d->getRealPath());
			}
		}
	}

	return addRefreshTask(REFRESH_DIRS, refreshDirs, TYPE_MANUAL, aVirtualName);
}


ShareManager::RefreshResult ShareManager::refresh(bool aIncoming, RefreshType aType, function<void(float)> progressF /*nullptr*/) noexcept {
	StringList dirs;

	{
		RLock l (cs);
		for (const auto& d: rootPaths | map_values) {
			if (aIncoming && !d->getRoot()->getIncoming())
				continue;

			dirs.push_back(d->getRoot()->getPath());
		}
	}

	return addRefreshTask(aIncoming ? REFRESH_INCOMING : REFRESH_ALL, dirs, aType, Util::emptyString, progressF);
}

struct ShareTask : public Task {
	ShareTask(const RefreshPathList& aDirs, const string& aDisplayName, ShareManager::RefreshType aRefreshType) : dirs(aDirs), displayName(aDisplayName), type(aRefreshType) { }
	RefreshPathList dirs;
	string displayName;
	ShareManager::RefreshType type;
};

void ShareManager::addAsyncTask(AsyncF aF) noexcept {
	tasks.add(ASYNC, make_unique<AsyncTask>(aF));
	if (!refreshing.test_and_set()) {
		start();
	}
}

ShareManager::RefreshResult ShareManager::refreshPaths(const StringList& aPaths, const string& aDisplayName /*Util::emptyString*/, function<void(float)> aProgressF /*nullptr*/) noexcept {
	for (const auto& path : aPaths) {
		auto d = findDirectory(path);
		if (!d && !allowShareDirectory(path)) {
			return RefreshResult::REFRESH_PATH_NOT_FOUND;
		}
	}

	return addRefreshTask(REFRESH_DIRS, aPaths, RefreshType::TYPE_MANUAL, aDisplayName, aProgressF);
}

void ShareManager::validateRefreshTask(StringList& dirs_) noexcept {
	Lock l(tasks.cs);
	auto& tq = tasks.getTasks();

	//remove directories that have already been queued for refreshing
	for (const auto& i : tq) {
		if (i.first != ASYNC) {
			auto t = static_cast<ShareTask*>(i.second.get());
			dirs_.erase(boost::remove_if(dirs_, [t](const string& p) {
				return boost::find(t->dirs, p) != t->dirs.end();
			}), dirs_.end());
		}
	}
}

void ShareManager::reportPendingRefresh(TaskType aTaskType, const RefreshPathList& aDirectories, const string& aDisplayName) const noexcept {
	string msg;
	switch (aTaskType) {
		case(REFRESH_ALL) :
			msg = STRING(REFRESH_QUEUED);
			break;
		case(REFRESH_DIRS) :
			if (!aDisplayName.empty()) {
				msg = STRING_F(VIRTUAL_REFRESH_QUEUED, aDisplayName);
			} else if (aDirectories.size() == 1) {
				msg = STRING_F(DIRECTORY_REFRESH_QUEUED, *aDirectories.begin());
			}
			break;
		case(ADD_DIR) :
			if (aDirectories.size() == 1) {
				msg = STRING_F(ADD_DIRECTORY_QUEUED, *aDirectories.begin());
			} else {
				msg = STRING_F(ADD_DIRECTORIES_QUEUED, aDirectories.size());
			}
					  break;
		case(REFRESH_INCOMING) :
			msg = STRING(INCOMING_REFRESH_QUEUED);
			break;
		default:
			break;
	};

	if (!msg.empty()) {
		LogManager::getInstance()->message(msg, LogMessage::SEV_INFO);
	}
}

ShareManager::RefreshResult ShareManager::addRefreshTask(TaskType aTaskType, const StringList& aDirs, RefreshType aRefreshType, const string& aDisplayName, function<void(float)> aProgressF) noexcept {
	if (aDirs.empty()) {
		return RefreshResult::REFRESH_PATH_NOT_FOUND;
	}

	auto dirs = aDirs;
	validateRefreshTask(dirs);

	if (dirs.empty()) {
		return RefreshResult::REFRESH_ALREADY_QUEUED;
	}

	RefreshPathList paths;
	for (auto& path : dirs) {
		setRefreshState(path, RefreshState::STATE_PENDING, false);
		paths.insert(path);
	}

	fire(ShareManagerListener::RefreshQueued(), aTaskType, paths);
	tasks.add(aTaskType, make_unique<ShareTask>(paths, aDisplayName, aRefreshType));

	if(refreshing.test_and_set()) {
		if (aRefreshType != TYPE_STARTUP_DELAYED) {
			//this is always called from the task thread...
			reportPendingRefresh(aTaskType, paths, aDisplayName);
		}
		return RefreshResult::REFRESH_IN_PROGRESS;
	}

	if (aRefreshType == TYPE_STARTUP_BLOCKING && aTaskType == REFRESH_ALL) {
		runTasks(aProgressF);
	} else {
		try {
			start();
		} catch(const ThreadException& e) {
			LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_FAILED) + " " + e.getError(), LogMessage::SEV_WARNING);
			refreshing.clear();
		}
	}

	return RefreshResult::REFRESH_STARTED;
}

void ShareManager::getRootPaths(StringList& paths_) const noexcept {
	RLock l(cs);
	boost::copy(rootPaths | map_keys, back_inserter(paths_));
}

void ShareManager::setDefaultProfile(ProfileToken aNewDefault) noexcept {
	auto oldDefault = SETTING(DEFAULT_SP);

	{
		WLock l(cs);
		// Put the default profile on top
		auto p = find(shareProfiles, aNewDefault);
		rotate(shareProfiles.begin(), p, shareProfiles.end());
	}

	SettingsManager::getInstance()->set(SettingsManager::DEFAULT_SP, aNewDefault);

	fire(ShareManagerListener::DefaultProfileChanged(), oldDefault, aNewDefault);
	fire(ShareManagerListener::ProfileUpdated(), aNewDefault, true);
	fire(ShareManagerListener::ProfileUpdated(), oldDefault, true);
}

void ShareManager::addProfiles(const ShareProfileInfo::List& aProfiles) noexcept {
	for (auto& sp : aProfiles) {
		addProfile(std::make_shared<ShareProfile>(sp->name, sp->token));
	}
}

void ShareManager::removeProfiles(const ShareProfileInfo::List& aProfiles) noexcept{
	for (auto& sp : aProfiles) {
		removeProfile(sp->token);
	}
}

void ShareManager::renameProfiles(const ShareProfileInfo::List& aProfiles) noexcept {
	for (auto& sp : aProfiles) {
		auto p = getShareProfile(sp->token);
		if (p) {
			p->setPlainName(sp->name);
			updateProfile(p);
		}
	}
}

void ShareManager::addProfile(const ShareProfilePtr& aProfile) noexcept {
	{
		WLock l(cs);

		// Hidden profile should always be the last one
		shareProfiles.insert(shareProfiles.end() - 1, aProfile);
	}

	fire(ShareManagerListener::ProfileAdded(), aProfile->getToken());
}

void ShareManager::updateProfile(const ShareProfilePtr& aProfile) noexcept {
	fire(ShareManagerListener::ProfileUpdated(), aProfile->getToken(), true);
}

bool ShareManager::removeProfile(ProfileToken aToken) noexcept {
	StringList removedPaths;

	{
		WLock l(cs);
		// Remove all directories
		for (auto& root : rootPaths) {
			auto profiles = root.second->getRoot()->getRootProfiles();
			profiles.erase(aToken);
			root.second->getRoot()->setRootProfiles(profiles);

			if (profiles.empty()) {
				removedPaths.push_back(root.first);
			}
		}

		// Remove profile
		auto i = find(shareProfiles.begin(), shareProfiles.end(), aToken);
		if (i == shareProfiles.end()) {
			return false;
		}

		shareProfiles.erase(remove(shareProfiles.begin(), shareProfiles.end(), aToken), shareProfiles.end());
	}
	
	fire(ShareManagerListener::ProfileRemoved(), aToken); //removeRootDirectories() might take a while so fire listener first.
	removeRootDirectories(removedPaths);
	return true;
}

bool ShareManager::addRootDirectory(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept {
	dcassert(!aDirectoryInfo->profiles.empty());
	const auto& path = aDirectoryInfo->path;

	{
		WLock l(cs);
		auto i = rootPaths.find(path);
		if (i != rootPaths.end()) {
			return false;
		} else {
			dcassert(find_if(rootPaths | map_keys, IsParentOrExact(path, PATH_SEPARATOR)).base() == rootPaths.end());

			// It's a new parent, will be handled in the task thread
			Directory::createRoot(path, aDirectoryInfo->virtualName, aDirectoryInfo->profiles, aDirectoryInfo->incoming, File::getLastModified(path), rootPaths, lowerDirNameMap, *bloom.get(), 0);
		}
	}

	fire(ShareManagerListener::RootCreated(), path);
	addRefreshTask(ADD_DIR, { path }, TYPE_MANUAL);

	return true;
}

void ShareManager::addRootDirectories(const ShareDirectoryInfoList& aNewDirs) noexcept {
	for(const auto& d: aNewDirs) {
		addRootDirectory(d);
	}
}

bool ShareManager::removeRootDirectory(const string& aPath) noexcept {
	ProfileTokenSet dirtyProfiles;

	{
		WLock l(cs);
		auto k = rootPaths.find(aPath);
		if (k == rootPaths.end()) {
			return false;
		}

		auto sd = k->second;

		dirtyProfiles = k->second->getRoot()->getRootProfiles();

		rootPaths.erase(k);

		// Remove the root
		Directory::cleanIndices(*sd, sharedSize, tthIndex, lowerDirNameMap);
		File::deleteFile(sd->getRoot()->getCacheXmlPath());
	}

	HashManager::getInstance()->stopHashing(aPath);

	LogManager::getInstance()->message(STRING_F(SHARED_DIR_REMOVED, aPath), LogMessage::SEV_INFO);

	fire(ShareManagerListener::RootRemoved(), aPath);
	setProfilesDirty(dirtyProfiles, true);
	return true;
}

void ShareManager::removeRootDirectories(const StringList& aRemoveDirs) noexcept{

	for(const auto& path: aRemoveDirs) {
		removeRootDirectory(path);
	}


#ifdef _DEBUG
	validateDirectoryTreeDebug();
#endif
}

bool ShareManager::updateRootDirectory(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept {
	dcassert(!aDirectoryInfo->profiles.empty());
	RootDirectory::Ptr rootDirectory;
	ProfileTokenSet dirtyProfiles = aDirectoryInfo->profiles;

	{
		WLock l(cs);
		auto p = rootPaths.find(aDirectoryInfo->path);
		if (p != rootPaths.end()) {
			auto vName = validateVirtualName(aDirectoryInfo->virtualName);
			rootDirectory = p->second->getRoot();

			// Make sure that all removed profiles are set dirty as well
			dirtyProfiles.insert(rootDirectory->getRootProfiles().begin(), rootDirectory->getRootProfiles().end());

			removeDirName(*p->second, lowerDirNameMap);
			rootDirectory->setName(vName);
			addDirName(p->second, lowerDirNameMap, *bloom.get());

			rootDirectory->setIncoming(aDirectoryInfo->incoming);
			rootDirectory->setRootProfiles(aDirectoryInfo->profiles);
		} else {
			return false;
		}
	}

	setProfilesDirty(dirtyProfiles, true);

	fire(ShareManagerListener::RootUpdated(), aDirectoryInfo->path);

	return true;
}

void ShareManager::updateRootDirectories(const ShareDirectoryInfoList& changedDirs) noexcept {
	for(const auto& dirInfo: changedDirs) {
		updateRootDirectory(dirInfo);
	}

#ifdef _DEBUG
	validateDirectoryTreeDebug();
#endif
}

void ShareManager::reportTaskStatus(uint8_t aTask, const RefreshPathList& directories, bool finished, int64_t aHashSize, const string& displayName, RefreshType aRefreshType) const noexcept {
	string msg;
	switch (aTask) {
		case(REFRESH_ALL):
			msg = finished ? STRING(FILE_LIST_REFRESH_FINISHED) : STRING(FILE_LIST_REFRESH_INITIATED);
			break;
		case(REFRESH_DIRS):
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
		case(ADD_BUNDLE):
			if (finished)
				msg = STRING_F(BUNDLE_X_SHARED, displayName); //show the whole path so that it can be opened from the system log
			break;
	};

	if (!msg.empty()) {
		if (aHashSize > 0) {
			msg += " " + STRING_F(FILES_ADDED_FOR_HASH, Util::formatBytes(aHashSize));
		} else if (aRefreshType == TYPE_SCHEDULED && !SETTING(LOG_SCHEDULED_REFRESHES)) {
			return;
		}
		LogManager::getInstance()->message(msg, LogMessage::SEV_INFO);
	}
}

int ShareManager::run() {
	runTasks();
	return 0;
}

ShareManager::RefreshInfo::~RefreshInfo() {

}

ShareManager::RefreshInfo::RefreshInfo(const string& aPath, const Directory::Ptr& aOldShareDirectory, time_t aLastWrite, ShareBloom& bloom_) :
	path(aPath), oldShareDirectory(aOldShareDirectory), bloom(bloom_) {

	// Use a different directory for building the tree
	if (aOldShareDirectory && aOldShareDirectory->getRoot()) {
		newShareDirectory = Directory::createRoot(aPath, aOldShareDirectory->getVirtualName(), aOldShareDirectory->getRoot()->getRootProfiles(), aOldShareDirectory->getRoot()->getIncoming(),
			aLastWrite, rootPathsNew, lowerDirNameMapNew, bloom_, aOldShareDirectory->getRoot()->getLastRefreshTime());
	} else {
		// We'll set the parent later
		newShareDirectory = Directory::createNormal(Util::getLastDir(aPath), nullptr, aLastWrite, lowerDirNameMapNew, bloom_);
	}
}

void ShareManager::runTasks(function<void (float)> progressF /*nullptr*/) noexcept {
	unique_ptr<HashManager::HashPauser> pauser = nullptr;
	ScopedFunctor([this] { refreshing.clear(); });

	for (;;) {
		TaskQueue::TaskPair t;
		if (!tasks.getFront(t))
			break;
		ScopedFunctor([this] { tasks.pop_front(); });

		if (t.first == ASYNC) {
			auto task = static_cast<AsyncTask*>(t.second);
			task->f();
			continue;
		}

		auto task = static_cast<ShareTask*>(t.second);
		if (task->type == TYPE_STARTUP_DELAYED)
			Thread::sleep(5000); // let the client start first

		setThreadPriority(task->type == TYPE_MANUAL ? Thread::NORMAL : Thread::IDLE);

		refreshRunning = true;
		ScopedFunctor([this] { refreshRunning = false; });

		if (!pauser) {
			pauser.reset(new HashManager::HashPauser());
		}

		auto dirs = task->dirs;

		// Handle the removed paths
		for (const auto& d : task->dirs) {
			if (dirs.find(d) == dirs.end()) {
				setRefreshState(d, RefreshState::STATE_NORMAL, true);
			}
		}

		if (dirs.empty()) {
			continue;
		}

		ShareBuilderSet refreshDirs;

		ShareBloom* refreshBloom = t.first == REFRESH_ALL ? new ShareBloom(1 << 20) : bloom.get();

		// Get refresh infos for each path
		{
			RLock l (cs);
			for(auto& refreshPath: dirs) {
				auto directory = findDirectory(refreshPath);
				refreshDirs.insert(std::make_shared<ShareBuilder>(refreshPath, directory, File::getLastModified(refreshPath), *refreshBloom, aShutdown, *validator.get()));
			}
		}

		reportTaskStatus(t.first, dirs, false, 0, task->displayName, task->type);
		if (t.first == REFRESH_INCOMING) {
			lastIncomingUpdate = GET_TICK();
		} else if (t.first == REFRESH_ALL) {
			lastFullUpdate = GET_TICK();
			lastIncomingUpdate = GET_TICK();
		}

		// Refresh
		atomic<long> progressCounter(0);

		int64_t totalHash = 0;
		ProfileTokenSet dirtyProfiles;

		auto doRefresh = [&](const ShareBuilderPtr& i) {
			auto& ri = *i.get();

			setRefreshState(ri.path, RefreshState::STATE_RUNNING, false);

			// Build the tree
			auto succeed = ri.buildTree();

			// Don't save cache with an incomplete tree
			if (aShutdown)
				return;

			// Apply the changes
			if (succeed) {
				WLock l(cs);
				applyRefreshChanges(ri, totalHash, &dirtyProfiles);
			}

			// Finish up
			setRefreshState(ri.path, RefreshState::STATE_NORMAL, succeed);
			if (progressF) {
				progressF(static_cast<float>(progressCounter++) / static_cast<float>(refreshDirs.size()));
			}
		};

		try {
			if (SETTING(REFRESH_THREADING) == SettingsManager::MULTITHREAD_ALWAYS || (SETTING(REFRESH_THREADING) == SettingsManager::MULTITHREAD_MANUAL && (task->type == TYPE_MANUAL || task->type == TYPE_STARTUP_BLOCKING))) {
				TaskScheduler s;
				parallel_for_each(refreshDirs.begin(), refreshDirs.end(), doRefresh);
			} else {
				for_each(refreshDirs, doRefresh);
			}
		} catch (std::exception& e) {
			LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_FAILED) + string(e.what()), LogMessage::SEV_INFO);
			continue;
		}

		if (aShutdown)
			break;

		if(t.first == REFRESH_ALL) {
			// Reset the bloom so that removed files are nulled (which won't happen with partial refreshes)

			WLock l(cs);
			bloom.reset(refreshBloom);
		}

		setProfilesDirty(dirtyProfiles, task->type == TYPE_MANUAL || t.first == REFRESH_ALL || t.first == ADD_BUNDLE);
		reportTaskStatus(t.first, dirs, true, totalHash, task->displayName, task->type);

		fire(ShareManagerListener::RefreshCompleted(), t.first, dirs);
	}

#ifdef _DEBUG
	validateDirectoryTreeDebug();
#endif
}

void ShareManager::RefreshInfo::mergeRefreshChanges(Directory::MultiMap& lowerDirNameMap_, Directory::Map& rootPaths_, HashFileMap& tthIndex_, int64_t& totalHash_, int64_t& totalAdded_, ProfileTokenSet* dirtyProfiles_) noexcept {
#ifdef _DEBUG
	for (const auto& d: lowerDirNameMapNew | map_values) {
		checkAddedDirNameDebug(d, lowerDirNameMap_);
	}

	for (const auto& f : tthIndexNew | map_values) {
		checkAddedTTHDebug(f, tthIndex_);
	}
#endif

	lowerDirNameMap_.insert(lowerDirNameMapNew.begin(), lowerDirNameMapNew.end());
	tthIndex_.insert(tthIndexNew.begin(), tthIndexNew.end());

	for (const auto& rp : rootPathsNew) {
		//dcassert(rootPaths_.find(rp.first) == rootPaths_.end());
		rootPaths_[rp.first] = rp.second;
	}

	totalHash_ += hashSize;
	totalAdded_ += addedSize;

	if (dirtyProfiles_) {
		newShareDirectory->copyRootProfiles(*dirtyProfiles_, true);
	}

	// Save some memory
	lowerDirNameMapNew.clear();
	tthIndexNew.clear();
	oldShareDirectory = nullptr;
	newShareDirectory = nullptr;
}

void ShareManager::setRefreshState(const string& aRefreshPath, RefreshState aState, bool aUpdateRefreshTime) noexcept {
	RootDirectory::Ptr rootDir;

	{
		RLock l(cs);
		auto p = find_if(rootPaths | map_values, [&](const Directory::Ptr& aDir) {
			return AirUtil::isParentOrExactLocal(aDir->getRoot()->getPath(), aRefreshPath);
		});

		if (p.base() == rootPaths.end()) {
			return;
		}

		rootDir = (*p)->getRoot();
	}

	// We want to fire a root update also when refreshing subdirectories (as the size/content may have changed)
	// but don't change the refresh state
	if (aRefreshPath == rootDir->getPath()) {
		rootDir->setRefreshState(aState);
		if (aUpdateRefreshTime) {
			rootDir->setLastRefreshTime(GET_TIME());
		}
	}

	fire(ShareManagerListener::RootRefreshState(), rootDir->getPath());
}

bool ShareManager::applyRefreshChanges(RefreshInfo& ri, int64_t& totalHash_, ProfileTokenSet* aDirtyProfiles) {
	Directory::Ptr parent = nullptr;

	// Recursively remove the content of this dir from TTHIndex and directory name map
	if (ri.oldShareDirectory) {
		// Root removed while refreshing?
		if (ri.oldShareDirectory->isRoot() && rootPaths.find(ri.path) == rootPaths.end()) {
			return false;
		}

		parent = ri.oldShareDirectory->getParent();

		// Remove the old directory
		Directory::cleanIndices(*ri.oldShareDirectory, sharedSize, tthIndex, lowerDirNameMap);
	}

	// Set the parent for refreshed subdirectories
	// (previous directory should always be available for roots)
	if (!ri.oldShareDirectory || !ri.oldShareDirectory->isRoot()) {
		// All content was removed?
		if (!ri.checkContent(ri.newShareDirectory)) {
			return false;
		}

		if (!parent) {
			// Create new parent
			parent = getDirectory(Util::getParentDir(ri.path));
			if (!parent) {
				return false;
			}
		}

		// Set the parent
		if (!Directory::setParent(ri.newShareDirectory, parent)) {
			return false;
		}
	}

	ri.mergeRefreshChanges(lowerDirNameMap, rootPaths, tthIndex, totalHash_, sharedSize, aDirtyProfiles);
	dcdebug("Share changes applied for the directory %s\n", ri.path.c_str());
	return true;
}

void ShareManager::on(TimerManagerListener::Minute, uint64_t aTick) noexcept {
	if(lastSave == 0 || lastSave + 15*60*1000 <= aTick) {
		saveXmlList();
	}

	if(SETTING(AUTO_REFRESH_TIME) > 0 && lastFullUpdate + SETTING(AUTO_REFRESH_TIME) * 60 * 1000 <= aTick) {
		lastIncomingUpdate = aTick;
		lastFullUpdate = aTick;
		refresh(false, TYPE_SCHEDULED);
	} else if(SETTING(INCOMING_REFRESH_TIME) > 0 && lastIncomingUpdate + SETTING(INCOMING_REFRESH_TIME) * 60 * 1000 <= aTick) {
		lastIncomingUpdate = aTick;
		refresh(true, TYPE_SCHEDULED);
	}
}

ShareDirectoryInfoPtr ShareManager::getRootInfo(const Directory::Ptr& aDir) const noexcept {
	auto& rootDir = aDir->getRoot();

	size_t fileCount = 0, folderCount = 0;
	int64_t size = 0;
	aDir->getContentInfo(size, fileCount, folderCount);

	auto info = std::make_shared<ShareDirectoryInfo>(aDir->getRealPath());
	info->profiles = rootDir->getRootProfiles();
	info->incoming = rootDir->getIncoming();
	info->size = size;
	info->contentInfo = DirectoryContentInfo(folderCount, fileCount);
	info->virtualName = rootDir->getName();
	info->refreshState = static_cast<uint8_t>(rootDir->getRefreshState());
	info->lastRefreshTime = rootDir->getLastRefreshTime();
	return info;
}

ShareDirectoryInfoPtr ShareManager::getRootInfo(const string& aPath) const noexcept {
	RLock l(cs);
	auto p = rootPaths.find(aPath);
	if (p != rootPaths.end()) {
		return getRootInfo(p->second);
	}

	return nullptr;
}

ShareDirectoryInfoList ShareManager::getRootInfos() const noexcept {
	ShareDirectoryInfoList ret;

	RLock l (cs);
	for(const auto& d: rootPaths | map_values) {
		ret.push_back(getRootInfo(d));
	}

	return ret;
}
		
void ShareManager::getBloom(HashBloom& bloom_) const noexcept {
	RLock l(cs);
	for(const auto tth: tthIndex | map_keys)
		bloom_.add(*tth);

	for(const auto& tth: tempShares | map_keys)
		bloom_.add(tth);
}

string ShareManager::generateOwnList(ProfileToken aProfile) {
	FileList* fl = generateXmlList(aProfile, true);
	return fl->getFileName();
}


//forwards the calls to createFileList for creating the filelist that was reguested.
FileList* ShareManager::generateXmlList(ProfileToken aProfile, bool forced /*false*/) {
	FileList* fl = nullptr;

	{
		RLock l(cs);
		const auto i = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
		if(i == shareProfiles.end()) {
			throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
		}

		fl = (*i)->getProfileList();
	}


	{
		Lock lFl(fl->cs);
		if (fl->allowGenerateNew(forced)) {
			auto tmpName = fl->getFileName().substr(0, fl->getFileName().length() - 4);
			try {
				{
					File f(tmpName, File::RW, File::TRUNCATE | File::CREATE, File::BUFFER_SEQUENTIAL, false);

					toFilelist(f, ADC_ROOT_STR, aProfile, true);

					fl->setXmlListLen(f.getSize());

					File bz(fl->getFileName(), File::WRITE, File::TRUNCATE | File::CREATE, File::BUFFER_SEQUENTIAL, false);
					// We don't care about the leaves...
					CalcOutputStream<TTFilter<1024 * 1024 * 1024>, false> bzTree(&bz);
					FilteredOutputStream<BZFilter, false> bzipper(&bzTree);
					CalcOutputStream<TTFilter<1024 * 1024 * 1024>, false> newXmlFile(&bzipper);

					newXmlFile.write(f.read());
					newXmlFile.flushBuffers(false);

					newXmlFile.getFilter().getTree().finalize();
					bzTree.getFilter().getTree().finalize();

					fl->setXmlRoot(newXmlFile.getFilter().getTree().getRoot());
					fl->setBzXmlRoot(bzTree.getFilter().getTree().getRoot());
				}

				fl->saveList();
				fl->generationFinished(false);
			} catch (const Exception& e) {
				// No new file lists...
				LogManager::getInstance()->message(STRING_F(SAVE_FAILED_X, fl->getFileName() % e.getError()), LogMessage::SEV_ERROR);
				fl->generationFinished(true);

				// do we have anything to send?
				if (fl->getCurrentNumber() == 0) {
					throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
				}
			}

			File::deleteFile(tmpName);
		}
	}
	return fl;
}

MemoryInputStream* ShareManager::generatePartialList(const string& aVirtualPath, bool aRecursive, const OptionalProfileToken& aProfile) const noexcept {
	if (aVirtualPath.front() != ADC_SEPARATOR || aVirtualPath.back() != ADC_SEPARATOR) {
		return 0;
	}

	string xml = Util::emptyString;

	{
		StringOutputStream sos(xml);
		toFilelist(sos, aVirtualPath, aProfile, aRecursive);
	}

	if (xml.empty()) {
		dcdebug("Partial NULL");
		return nullptr;
	} else {
		dcdebug("Partial list generated (%s)\n", aVirtualPath.c_str());
		return new MemoryInputStream(xml);
	}
}

void ShareManager::toFilelist(OutputStream& os_, const string& aVirtualPath, const OptionalProfileToken& aProfile, bool aRecursive) const {
	FilelistDirectory listRoot(Util::emptyString, 0);
	Directory::List childDirectories;

	RLock l(cs);
	dcdebug("Generating filelist for %s \n", aVirtualPath.c_str());

	// Get the directories
	if (aVirtualPath == ADC_ROOT_STR) {
		getRoots(aProfile, childDirectories);
	} else {
		try {
			// We need to save the root directories as well for listing the files directly inside them
			findVirtuals<OptionalProfileToken>(aVirtualPath, aProfile, listRoot.shareDirs);
		} catch (...) {
			return;
		}

		for (const auto& d : listRoot.shareDirs) {
			copy(d->getDirectories(), back_inserter(childDirectories));
			listRoot.date = max(listRoot.date, d->getLastWrite());
		}
	}

	// Prepare the data
	for (const auto& d : childDirectories) {
		d->toFileList(listRoot, aRecursive);
		listRoot.date = max(listRoot.date, d->getLastWrite()); // In case the date is not set yet
	}

	// Write the XML
	string tmp, indent = "\t";

	os_.write(SimpleXML::utf8Header);
	os_.write("<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() +
		"\" Base=\"" + SimpleXML::escape(aVirtualPath, tmp, false) +
		"\" BaseDate=\"" + Util::toString(listRoot.date) +
		"\" Generator=\"" + shortVersionString + "\">\r\n");

	for (const auto ld : listRoot.listDirs | map_values) {
		ld->toXml(os_, indent, tmp, aRecursive);
	}
	listRoot.filesToXml(os_, indent, tmp, !aRecursive);

	os_.write("</FileListing>");
}

void ShareManager::Directory::toFileList(FilelistDirectory& aListDir, bool aRecursive) {
	FilelistDirectory* newListDir = nullptr;
	auto pos = aListDir.listDirs.find(const_cast<string*>(&getVirtualNameLower()));
	if (pos != aListDir.listDirs.end()) {
		newListDir = pos->second;
		newListDir->date = max(newListDir->date, lastWrite);
	} else {
		newListDir = new FilelistDirectory(getVirtualName(), lastWrite);
		aListDir.listDirs.emplace(const_cast<string*>(&newListDir->name), newListDir);
	}

	newListDir->shareDirs.push_back(this);

	if (aRecursive) {
		for (const auto& d: directories) {
			d->toFileList(*newListDir, aRecursive);
		}
	}
}

ShareManager::FilelistDirectory::FilelistDirectory(const string& aName, time_t aDate) : name(aName), date(aDate) { }

ShareManager::FilelistDirectory::~FilelistDirectory() {
	for_each(listDirs | map_values, DeleteFunction());
}

#define LITERAL(n) n, sizeof(n)-1
void ShareManager::FilelistDirectory::toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool aRecursive) const {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(name, tmp2, true));
	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(Util::toString(date));

	if(aRecursive) {
		xmlFile.write(LITERAL("\">\r\n"));

		indent += '\t';
		for(const auto& d: listDirs | map_values) {
			d->toXml(xmlFile, indent, tmp2, aRecursive);
		}

		filesToXml(xmlFile, indent, tmp2, !aRecursive);

		indent.erase(indent.length()-1);
		xmlFile.write(indent);
		xmlFile.write(LITERAL("</Directory>\r\n"));
	} else {
		size_t fileCount = 0, directoryCount = 0;
		int64_t totalSize = 0;
		for (const auto& d : shareDirs) {
			d->getContentInfo(totalSize, fileCount, directoryCount);
		}

		xmlFile.write(LITERAL("\" Size=\""));
		xmlFile.write(Util::toString(totalSize));

		if (fileCount == 0 && directoryCount == 0) {
			xmlFile.write(LITERAL("\" />\r\n"));
		} else {
			xmlFile.write(LITERAL("\" Incomplete=\"1"));

			if (directoryCount > 0) {
				xmlFile.write(LITERAL("\" Directories=\""));
				xmlFile.write(Util::toString(directoryCount));
			}

			if (fileCount > 0) {
				xmlFile.write(LITERAL("\" Files=\""));
				xmlFile.write(Util::toString(fileCount));
			}

			xmlFile.write(LITERAL("\"/>\r\n"));
		}
	}
}

void ShareManager::FilelistDirectory::filesToXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const {
	bool filesAdded = false;
	int dupeFiles = 0;
	for(auto di = shareDirs.begin(); di != shareDirs.end(); ++di) {
		if (filesAdded) {
			for(const auto& fi: (*di)->files) {
				//go through the dirs that we have added already
				if (none_of(shareDirs.begin(), di, [&fi](const Directory::Ptr& d) { return d->files.find(fi->name.getLower()) != d->files.end(); })) {
					fi->toXml(xmlFile, indent, tmp2, addDate);
				} else {
					dupeFiles++;
				}
			}
		} else if (!(*di)->files.empty()) {
			filesAdded = true;
			for(const auto& f: (*di)->files)
				f->toXml(xmlFile, indent, tmp2, addDate);
		}
	}

	if (dupeFiles > 0 && SETTING(FL_REPORT_FILE_DUPES) && shareDirs.size() > 1) {
		StringList paths;
		for (const auto& d : shareDirs)
			paths.push_back(d->getRealPath());

		LogManager::getInstance()->message(STRING_F(DUPLICATE_FILES_DETECTED, dupeFiles % Util::toString(", ", paths)), LogMessage::SEV_WARNING);
	}
}

void ShareManager::Directory::filesToXmlList(OutputStream& xmlFile, string& indent, string& tmp2) const {
	for(const auto& f: files) {
		xmlFile.write(indent);
		xmlFile.write(LITERAL("<File Name=\""));
		xmlFile.write(SimpleXML::escape(f->name.lowerCaseOnly() ? f->name.getLower() : f->name.getNormal(), tmp2, true));
		xmlFile.write(LITERAL("\"/>\r\n"));
	}
}

ShareManager::Directory::File::File(DualString&& aName, const Directory::Ptr& aParent, const HashedFile& aFileInfo) : 
	size(aFileInfo.getSize()), parent(aParent.get()), tth(aFileInfo.getRoot()), lastWrite(aFileInfo.getTimeStamp()), name(move(aName)) {
	
}

ShareManager::Directory::File::~File() {

}

void ShareManager::Directory::File::toXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDate) const {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<File Name=\""));
	xmlFile.write(SimpleXML::escape(name.lowerCaseOnly() ? name.getLower() : name.getNormal(), tmp2, true));
	xmlFile.write(LITERAL("\" Size=\""));
	xmlFile.write(Util::toString(size));
	xmlFile.write(LITERAL("\" TTH=\""));
	tmp2.clear();
	xmlFile.write(getTTH().toBase32(tmp2));

	if (addDate) {
		xmlFile.write(LITERAL("\" Date=\""));
		xmlFile.write(Util::toString(lastWrite));
	}
	xmlFile.write(LITERAL("\"/>\r\n"));
}

string ShareManager::RootDirectory::getCacheXmlPath() const noexcept {
	return Util::getPath(Util::PATH_SHARECACHE) + "ShareCache_" + Util::validateFileName(path) + ".xml";
}

void ShareManager::RootDirectory::setName(const string& aName) noexcept {
	virtualName.reset(new DualString(aName));
}

#define LITERAL(n) n, sizeof(n)-1

void ShareManager::saveXmlList(function<void(float)> progressF /*nullptr*/) noexcept {

	if(xml_saving)
		return;

	xml_saving = true;

	if (progressF)
		progressF(0);

	int cur = 0;
	Directory::List dirtyDirs;

	{
		RLock l(cs);
		boost::algorithm::copy_if(rootPaths | map_values, back_inserter(dirtyDirs), [](const Directory::Ptr& aDir) { return aDir->getRoot()->getCacheDirty() && !aDir->getParent(); });

		try {
			parallel_for_each(dirtyDirs.begin(), dirtyDirs.end(), [&](const Directory::Ptr& d) {
				string path = d->getRoot()->getCacheXmlPath();
				try {
					{
						string indent, tmp;

						//create a backup first in case we get interrupted on creation.
						File ff(path + ".tmp", File::WRITE, File::TRUNCATE | File::CREATE);
						BufferedOutputStream<false> xmlFile(&ff);

						xmlFile.write(SimpleXML::utf8Header);
						xmlFile.write(LITERAL("<Share Version=\"" SHARE_CACHE_VERSION));
						xmlFile.write(LITERAL("\" Path=\""));
						xmlFile.write(SimpleXML::escape(d->getRoot()->getPath(), tmp, true));

						xmlFile.write(LITERAL("\" Date=\""));
						xmlFile.write(SimpleXML::escape(Util::toString(d->getLastWrite()), tmp, true));
						xmlFile.write(LITERAL("\">\r\n"));
						indent += '\t';

						for (const auto& child : d->getDirectories()) {
							child->toXmlList(xmlFile, indent, tmp);
						}
						d->filesToXmlList(xmlFile, indent, tmp);

						xmlFile.write(LITERAL("</Share>"));
					}

					File::deleteFile(path);
					File::renameFile(path + ".tmp", path);
				} catch (Exception& e) {
					LogManager::getInstance()->message(STRING_F(SAVE_FAILED_X, path % e.getError()), LogMessage::SEV_WARNING);
				}

				d->getRoot()->setCacheDirty(false);
				if (progressF) {
					cur++;
					progressF(static_cast<float>(cur) / static_cast<float>(dirtyDirs.size()));
				}
			});
		} catch (std::exception& e) {
			LogManager::getInstance()->message("Saving the share cache failed: " + string(e.what()), LogMessage::SEV_INFO);
		}
	}

	xml_saving = false;
	lastSave = GET_TICK();
}

void ShareManager::Directory::toXmlList(OutputStream& xmlFile, string& indent, string& tmp) {
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(realName.lowerCaseOnly() ? realName.getLower() : realName.getNormal(), tmp, true));

	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(SimpleXML::escape(Util::toString(lastWrite), tmp, true));
	xmlFile.write(LITERAL("\">\r\n"));

	indent += '\t';
	filesToXmlList(xmlFile, indent, tmp);

	for(const auto& d: directories) {
		d->toXmlList(xmlFile, indent, tmp);
	}

	indent.erase(indent.length()-1);
	xmlFile.write(indent);
	xmlFile.write(LITERAL("</Directory>\r\n"));
}

MemoryInputStream* ShareManager::generateTTHList(const string& dir, bool recurse, ProfileToken aProfile) const noexcept {
	
	if(aProfile == SP_HIDDEN)
		return nullptr;
	
	string tths;
	string tmp;
	StringOutputStream sos(tths);
	Directory::List result;

	try{
		RLock l(cs);
		findVirtuals<ProfileToken>(dir, aProfile, result); 
		for(const auto& it: result) {
			//dcdebug("result name %s \n", (*it)->getRoot()->getName(aProfile));
			it->toTTHList(sos, tmp, recurse);
		}
	} catch(...) {
		return nullptr;
	}

	if (tths.size() == 0) {
		dcdebug("Partial NULL");
		return nullptr;
	} else {
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
		tthList.write(f->getTTH().toBase32(tmp2));
		tthList.write(LITERAL(" "));
	}
}

bool ShareManager::addDirectoryResult(const Directory* aDir, SearchResultList& aResults, const OptionalProfileToken& aProfile, SearchQuery& srch) const noexcept {
	const string path = srch.addParents ? Util::getAdcParentDir(aDir->getAdcPath()) : aDir->getAdcPath();

	// Have we added it already?
	auto p = find_if(aResults, [&path](const SearchResultPtr& sr) { return sr->getAdcPath() == path; });
	if (p != aResults.end())
		return false;

	// Get all directories with this path
	Directory::List result;

	try {
		findVirtuals<OptionalProfileToken>(path, aProfile, result);
	} catch(...) {
		dcassert(path.empty());
	}

	// Count date and content information
	time_t date = 0;
	int64_t size = 0;
	size_t files = 0, folders = 0;
	for(const auto& d: result) {
		d->getContentInfo(size, files, folders);
		date = max(date, d->getLastWrite());
	}

	if (srch.matchesDate(date)) {
		auto sr = make_shared<SearchResult>(SearchResult::TYPE_DIRECTORY, size, path, TTHValue(), date, DirectoryContentInfo(folders, files));
		aResults.push_back(sr);
		return true;
	}

	return false;
}

void ShareManager::Directory::File::addSR(SearchResultList& aResults, bool aAddParent) const noexcept {
	if (aAddParent) {
		auto sr = make_shared<SearchResult>(parent->getAdcPath());
		aResults.push_back(sr);
	} else {
		auto sr = make_shared<SearchResult>(SearchResult::TYPE_FILE,
			size, getAdcPath(), getTTH(), getLastWrite(), DirectoryContentInfo());

		aResults.push_back(sr);
	}
}

void ShareManager::nmdcSearch(SearchResultList& l, const string& nmdcString, int aSearchType, int64_t aSize, int aFileType, StringList::size_type maxResults, bool aHideShare) noexcept{
	auto query = SearchQuery(nmdcString, static_cast<Search::SizeModes>(aSearchType), aSize, static_cast<Search::TypeModes>(aFileType), maxResults);
	adcSearch(l, query, aHideShare ? SP_HIDDEN : SETTING(DEFAULT_SP), CID(), ADC_ROOT_STR, false);
}

/**
* Alright, the main point here is that when searching, a search string is most often found in
* the filename, not directory name, so we want to make that case faster. Also, we want to
* avoid changing StringLists unless we absolutely have to --> this should only be done if a string
* has been matched in the directory name. This new stringlist should also be used in all descendants,
* but not the parents...
*/

void ShareManager::Directory::search(SearchResultInfo::Set& results_, SearchQuery& aStrings, int aLevel) const noexcept{
	const auto& dirName = getVirtualNameLower();
	if (aStrings.isExcludedLower(dirName)) {
		return;
	}

	auto old = aStrings.recursion;

	unique_ptr<SearchQuery::Recursion> rec = nullptr;

	// Find any matches in the directory name
	// Subdirectories of fully matched items won't match anything
	if (aStrings.matchesAnyDirectoryLower(dirName)) {
		bool positionsComplete = aStrings.positionsComplete();
		if (aStrings.itemType != SearchQuery::TYPE_FILE && positionsComplete && aStrings.gt == 0 && aStrings.matchesDate(lastWrite)) {
			// Full match
			results_.insert(Directory::SearchResultInfo(this, aStrings, aLevel));
			//if (aStrings.matchType == SearchQuery::MATCH_FULL_PATH) {
			//	return;
			//}
		} 
		
		if (aStrings.matchType == Search::MATCH_PATH_PARTIAL) {
			bool hasValidResult = positionsComplete;
			if (!hasValidResult) {
				// Partial match; ignore if all matches are less than 3 chars in length
				const auto& positions = aStrings.getLastPositions();
				for (size_t j = 0; j < positions.size(); ++j) {
					if (positions[j] != string::npos && aStrings.include.getPatterns()[j].size() > 2) {
						hasValidResult = true;
						break;
					}
				}
			}

			if (hasValidResult) {
				rec.reset(new SearchQuery::Recursion(aStrings, dirName));
				aStrings.recursion = rec.get();
			}
		}
	}

	// Moving up
	aLevel++;
	if (aStrings.recursion) {
		aStrings.recursion->increase(dirName.length());
	}

	// Match files
	if(aStrings.itemType != SearchQuery::TYPE_DIRECTORY) {
		for(const auto& f: files) {
			if (!aStrings.matchesFileLower(f->name.getLower(), f->getSize(), f->getLastWrite())) {
				continue;
			}

			results_.insert(Directory::SearchResultInfo(f, aStrings, aLevel));
			if (aStrings.addParents)
				break;
		}
	}

	// Match directories
	for(const auto& d: directories) {
		d->search(results_, aStrings, aLevel);
	}

	// Moving to a lower level
	if (aStrings.recursion) {
		aStrings.recursion->decrease(dirName.length());
	}

	aStrings.recursion = old;
}

void ShareManager::adcSearch(SearchResultList& results, SearchQuery& srch, const OptionalProfileToken& aProfile, const CID& cid, const string& aDir, bool aIsAutoSearch) {
	dcassert(!aDir.empty());

	totalSearches++;
	if (aProfile == SP_HIDDEN) {
		return;
	}

	RLock l(cs);
	if(srch.root) {
		tthSearches++;
		const auto i = tthIndex.equal_range(const_cast<TTHValue*>(&(*srch.root)));
		for(auto& f: i | map_values) {
			if (f->hasProfile(aProfile) && AirUtil::isParentOrExactAdc(aDir, f->getAdcPath())) {
				f->addSR(results, srch.addParents);
				return;
			}
		}

		const auto files = tempShares.equal_range(*srch.root);
		for(const auto& f: files | map_values) {
			if(!f.user || f.user->getCID() == cid) {
				//TODO: fix the date?
				auto sr = make_shared<SearchResult>(SearchResult::TYPE_FILE, f.size, "/tmp/" + f.name, *srch.root, f.timeAdded, DirectoryContentInfo());
				results.push_back(sr);
			}
		}
		return;
	}

	recursiveSearches++;
	if (aIsAutoSearch)
		autoSearches++;

	for (const auto& p : srch.include.getPatterns()) {
		if (!bloom->match(p.str())) {
			filteredSearches++;
			return;
		}
	}

	// Get the search roots
	Directory::List roots;
	if (aDir == ADC_ROOT_STR) {
		getRoots(aProfile, roots);
	} else {
		findVirtuals<OptionalProfileToken>(aDir, aProfile, roots);
	}

	auto start = GET_TICK();

	// go them through recursively
	Directory::SearchResultInfo::Set resultInfos;
	for (const auto& d: roots) {
		d->search(resultInfos, srch, 0);
	}

	// update statistics
	auto end = GET_TICK();
	recursiveSearchTime += end - start;
	searchTokenCount += srch.include.count();
	for (const auto& p : srch.include.getPatterns()) 
		searchTokenLength += p.size();


	// pick the results to return
	for (auto i = resultInfos.begin(); (i != resultInfos.end()) && (results.size() < srch.maxResults); ++i) {
		auto& info = *i;
		if (info.getType() == Directory::SearchResultInfo::DIRECTORY) {
			addDirectoryResult(info.directory, results, aProfile, srch);
		} else {
			info.file->addSR(results, srch.addParents);
		}
	}

	if (!results.empty())
		recursiveSearchesResponded++;
}

void ShareManager::addDirName(const Directory::Ptr& aDir, Directory::MultiMap& aDirNames, ShareBloom& aBloom) noexcept {
	const auto& nameLower = aDir->getVirtualNameLower();

#ifdef _DEBUG
	checkAddedDirNameDebug(aDir, aDirNames);
#endif
	aDirNames.emplace(const_cast<string*>(&nameLower), aDir);
	aBloom.add(nameLower);
}

void ShareManager::removeDirName(const Directory& aDir, Directory::MultiMap& aDirNames) noexcept {
	auto directories = aDirNames.equal_range(const_cast<string*>(&aDir.getVirtualNameLower()));
	auto p = find_if(directories | map_values, [&aDir](const Directory::Ptr& d) { return d.get() == &aDir; });
	if (p.base() == aDirNames.end()) {
		dcassert(0);
		return;
	}

	aDirNames.erase(p.base());
}

void ShareManager::shareBundle(const BundlePtr& aBundle) noexcept {
	if (aBundle->isFileBundle()) {
		try {
			HashedFile fi;
			HashManager::getInstance()->getFileInfo(Text::toLower(aBundle->getTarget()), aBundle->getTarget(), fi);
			onFileHashed(aBundle->getTarget(), fi);

			LogManager::getInstance()->message(STRING_F(SHARED_FILE_ADDED, aBundle->getTarget()), LogMessage::SEV_INFO);
		} catch (...) { dcassert(0); }

		return;
	}

	auto path = aBundle->getTarget();
	addRefreshTask(ADD_BUNDLE, { aBundle->getTarget() }, RefreshType::TYPE_BUNDLE, aBundle->getTarget());
}

bool ShareManager::allowShareDirectory(const string& aRealPath) const noexcept {
	try {
		validatePath(aRealPath, false);
		return true;
	} catch (const Exception&) { }

	return false;
}

void ShareManager::validatePath(const string& aRealPath, bool aSkipQueueCheck) const {
	StringList tokens;
	Directory::Ptr baseDirectory = nullptr;

	{
		RLock l(cs);
		baseDirectory = findDirectory(aRealPath, tokens);
	}

	if (!baseDirectory) {
		throw ShareException(STRING(DIRECTORY_NOT_FOUND));
	}

	// Validate missing tokens
	validator->validatePathTokens(baseDirectory->getRealPath(), tokens, aSkipQueueCheck);
}

ShareManager::Directory::Ptr ShareManager::findDirectory(const string& aRealPath, StringList& remainingTokens_) const noexcept {
	auto mi = find_if(rootPaths | map_keys, IsParentOrExact(aRealPath, PATH_SEPARATOR)).base();
	if (mi == rootPaths.end()) {
		return nullptr;
	}

	auto curDir = mi->second;

	remainingTokens_ = StringTokenizer<string>(aRealPath.substr(mi->first.length()), PATH_SEPARATOR).getTokens();

	bool hasMissingToken = false;
	remainingTokens_.erase(std::remove_if(remainingTokens_.begin(), remainingTokens_.end(), [&](const string& currentName) {
		if (!hasMissingToken) {
			auto d = curDir->findDirectoryByName(currentName);
			if (d) {
				curDir = d;
				return true;
			}

			hasMissingToken = true;
		}

		return false;
	}), remainingTokens_.end());

	return curDir;
}

ShareManager::Directory::Ptr ShareManager::getDirectory(const string& aRealPath) noexcept {
	StringList tokens;

	// Find the existing directories
	auto curDir = findDirectory(aRealPath, tokens);
	if (!curDir) {
		return curDir;
	}

	// Validate the remaining tokens
	try {
		validator->validatePathTokens(curDir->getRealPath(), tokens, false);
	} catch (const Exception&) {
		return nullptr;
	}

	// Create missing directories
	for (const auto& curName : tokens) {
		curDir->updateModifyDate();
		curDir = Directory::createNormal(DualString(curName), curDir, File::getLastModified(curDir->getRealPath()), lowerDirNameMap, *bloom.get());
	}

	return curDir;
}

ShareManager::Directory::Ptr ShareManager::findDirectory(const string& aRealPath) const noexcept {
	StringList tokens;
	auto curDir = findDirectory(aRealPath, tokens);
	return tokens.empty() ? curDir : nullptr;
}

void ShareManager::onFileHashed(const string& fname, HashedFile& fileInfo) noexcept {
	ProfileTokenSet dirtyProfiles;
	{
		WLock l(cs);
		auto d = getDirectory(Util::getFilePath(fname));
		if (!d) {
			return;
		}

		addFile(Util::getFileName(fname), d, fileInfo, tthIndex, *bloom.get(), sharedSize, &dirtyProfiles);
	}

	setProfilesDirty(dirtyProfiles, false);
}

void ShareManager::addFile(DualString&& aName, const Directory::Ptr& aDir, const HashedFile& aFileInfo, HashFileMap& tthIndex_, ShareBloom& aBloom_, int64_t& sharedSize_, ProfileTokenSet* dirtyProfiles_) noexcept {
	{
		auto i = aDir->files.find(aName.getLower());
		if (i != aDir->files.end()) {
			// Get rid of false constness...
			(*i)->cleanIndices(sharedSize_, tthIndex_);
			delete *i;
			aDir->files.erase(i);
		}
	}

	auto it = aDir->files.insert_sorted(new Directory::File(move(aName), aDir, aFileInfo)).first;
	(*it)->updateIndices(aBloom_, sharedSize_, tthIndex_);

	if (dirtyProfiles_) {
		aDir->copyRootProfiles(*dirtyProfiles_, true);
	}
}

ShareProfileList ShareManager::getProfiles() const noexcept {
	RLock l(cs);
	return shareProfiles; 
}

ShareProfileInfo::List ShareManager::getProfileInfos() const noexcept {
	ShareProfileInfo::List ret;

	RLock l(cs);
	for (const auto& sp : shareProfiles | filtered(ShareProfile::NotHidden())) {
		auto p = std::make_shared<ShareProfileInfo>(sp->getPlainName(), sp->getToken());
		if (p->token == SETTING(DEFAULT_SP)) {
			p->isDefault = true;
			ret.emplace(ret.begin(), p);
		} else {
			ret.emplace_back(p);
		}
	}
	
	return ret;
}

GroupedDirectoryMap ShareManager::getGroupedDirectories() const noexcept {
	GroupedDirectoryMap ret;
	
	{
		RLock l (cs);
		for (const auto& d: rootPaths | map_values) {
			const auto& currentPath = d->getRoot()->getPath();
			auto virtualName = d->getRoot()->getName();

			ret[virtualName].insert(currentPath);
		}
	}

	return ret;
}

StringSet ShareManager::getExcludedPaths() const noexcept {
	return validator->getExcludedPaths();
}

void ShareManager::addExcludedPath(const string& aPath) {
	validator->addExcludedPath(aPath);
	fire(ShareManagerListener::ExcludeAdded(), aPath);
}

bool ShareManager::removeExcludedPath(const string& aPath) noexcept {
	if (validator->removeExcludedPath(aPath)) {
		fire(ShareManagerListener::ExcludeRemoved(), aPath);
		return true;
	}

	return false;
}

void ShareManager::reloadSkiplist() {
	validator->reloadSkiplist();
}

void ShareManager::setExcludedPaths(const StringSet& aPaths) noexcept {
	validator->setExcludedPaths(aPaths);
}

/*bool ShareManager::validate(FileFindIter& aIter, const string& aPath) const noexcept {
	return validator->validate(aIter, aPath, Text::toLower(aPath), false);
}*/

} // namespace dcpp
