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
#include "ShareProfileManager.h"

#include "LogManager.h"
#include "ResourceManager.h"
#include "SimpleXML.h"
#include "Streams.h"
#include "Transfer.h"
#include "UserConnection.h"

#include "version.h"

#include "concurrency.h"

namespace dcpp {

using ranges::find_if;
using ranges::for_each;

ShareProfileManager::ShareProfileManager(ProfileCallback&& aOnRemoveProfile) : onRemoveProfile(aOnRemoveProfile) {
}

ShareProfileManager::~ShareProfileManager() {
}

void ShareProfileManager::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(SHARE));
}

void ShareProfileManager::shutdown(function<void(float)> progressF) noexcept {
	removeCachedFilelists();
}

void ShareProfileManager::removeCachedFilelists() noexcept {
	try {
		RLock l(cs);
		//clear refs so we can delete filelists.
		auto lists = File::findFiles(AppUtil::getPath(AppUtil::PATH_USER_CONFIG), "files?*.xml.bz2", File::TYPE_FILE);
		for (auto& profile: shareProfiles) {
			if (profile->getProfileList() && profile->getProfileList()->bzXmlRef.get()) {
				profile->getProfileList()->bzXmlRef.reset();
			}
		}

		for_each(lists, File::deleteFile);
	}
	catch (...) {}
}

FileList* ShareProfileManager::getProfileFileListUnsafe(ProfileToken aProfile) const {
	auto p = getShareProfileUnsafe(aProfile);
	if (p) {
		dcassert(p->getProfileList());
		return p->getProfileList();
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

TTHValue ShareProfileManager::getListTTH(const string& aVirtualFile, ProfileToken aProfile) const {
	RLock l(cs);
	if (aVirtualFile == Transfer::USER_LIST_NAME_BZ) {
		return getProfileFileListUnsafe(aProfile)->getBzXmlRoot();
	} else if (aVirtualFile == Transfer::USER_LIST_NAME_EXTRACTED) {
		return getProfileFileListUnsafe(aProfile)->getXmlRoot();
	}

	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

ShareProfilePtr ShareProfileManager::loadProfile(SimpleXML& aXml, bool aIsDefault) {
	const auto& token = aXml.getIntChildAttrib("Token");
	const auto& loadedName = aXml.getChildAttrib("Name");

	if (!aIsDefault) {
		if (loadedName.empty() || token == SP_HIDDEN) {
			return nullptr;
		}
	}

	auto name = !loadedName.empty() ? loadedName : STRING(DEFAULT);

	auto sp = std::make_shared<ShareProfile>(name, token);
	shareProfiles.push_back(sp);
	return sp;
}

void ShareProfileManager::ensureDefaultProfiles() noexcept {
	// Default profile
	if (!getShareProfile(SETTING(DEFAULT_SP))) {
		if (shareProfiles.empty()) {
			auto sp = std::make_shared<ShareProfile>(STRING(DEFAULT), SETTING(DEFAULT_SP));
			shareProfiles.push_back(sp);
		}
		else {
			SettingsManager::getInstance()->set(SettingsManager::DEFAULT_SP, shareProfiles.front()->getToken());
		}
	}

	// Hidden profile
	auto hiddenProfile = std::make_shared<ShareProfile>(STRING(SHARE_HIDDEN), SP_HIDDEN);
	shareProfiles.push_back(hiddenProfile);
}

ShareProfilePtr ShareProfileManager::getShareProfile(ProfileToken aProfile, bool aAllowFallback /*false*/) const noexcept {
	RLock l (cs);
	const auto p = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
	if (p != shareProfiles.end()) {
		return *p;
	} else if (aAllowFallback) {
		dcassert(aProfile != SETTING(DEFAULT_SP));
		return *shareProfiles.begin();
	}
	return nullptr;
}

ShareProfilePtr ShareProfileManager::getShareProfileUnsafe(ProfileToken aProfile) const noexcept {
	auto p = find(shareProfiles.begin(), shareProfiles.end(), aProfile);
	return p != shareProfiles.end() ? *p : nullptr;
}

OptionalProfileToken ShareProfileManager::getProfileByName(const string& aName) const noexcept {
	RLock l(cs);
	if (aName.empty()) {
		return SETTING(DEFAULT_SP);
	}

	auto p = find_if(shareProfiles, [&](const ShareProfilePtr& aProfile) { return Util::stricmp(aProfile->getPlainName(), aName) == 0; });
	if (p == shareProfiles.end())
		return nullopt;
	return (*p)->getToken();
}

void ShareProfileManager::saveProfile(const ShareProfilePtr& aProfile, SimpleXML& aXml) {
	auto isDefault = aProfile->getToken() == SETTING(DEFAULT_SP);

	aXml.addTag(isDefault ? "Share" : "ShareProfile"); // Keep the old Share tag around for compatibility with other clients
	aXml.addChildAttrib("Token", aProfile->getToken());
	aXml.addChildAttrib("Name", aProfile->getPlainName());
}

void ShareProfileManager::setDefaultProfile(ProfileToken aNewDefault) noexcept {
	auto oldDefault = SETTING(DEFAULT_SP);

	{
		WLock l(cs);
		// Put the default profile on top
		auto p = ranges::find(shareProfiles, aNewDefault, &ShareProfile::getToken);
		rotate(shareProfiles.begin(), p, shareProfiles.end());
	}

	SettingsManager::getInstance()->set(SettingsManager::DEFAULT_SP, aNewDefault);

	fire(ShareProfileManagerListener::DefaultProfileChanged(), oldDefault, aNewDefault);
	fire(ShareProfileManagerListener::ProfileUpdated(), aNewDefault, true);
	fire(ShareProfileManagerListener::ProfileUpdated(), oldDefault, true);
}

void ShareProfileManager::addProfiles(const ShareProfileInfo::List& aProfiles) noexcept {
	for (auto& sp : aProfiles) {
		addProfile(std::make_shared<ShareProfile>(sp->name, sp->token));
	}
}

void ShareProfileManager::removeProfiles(const ShareProfileInfo::List& aProfiles) noexcept{
	for (auto& sp : aProfiles) {
		removeProfile(sp->token);
	}
}

void ShareProfileManager::renameProfiles(const ShareProfileInfo::List& aProfiles) noexcept {
	for (auto& sp : aProfiles) {
		auto p = getShareProfile(sp->token);
		if (p) {
			p->setPlainName(sp->name);
			updateProfile(p);
		}
	}
}

void ShareProfileManager::addProfile(const ShareProfilePtr& aProfile) noexcept {
	{
		WLock l(cs);

		// Hidden profile should always be the last one
		shareProfiles.insert(shareProfiles.end() - 1, aProfile);
	}

	fire(ShareProfileManagerListener::ProfileAdded(), aProfile->getToken());
}

void ShareProfileManager::updateProfile(const ShareProfilePtr& aProfile) noexcept {
	fire(ShareProfileManagerListener::ProfileUpdated(), aProfile->getToken(), true);
}

bool ShareProfileManager::removeProfile(ProfileToken aToken) noexcept {
	auto profile = getShareProfileUnsafe(aToken);
	if (!profile) {
		return false;
	}

	// Remove from the roots
	onRemoveProfile(profile);

	{
		WLock l(cs);
		// Remove profile
		shareProfiles.erase(remove(shareProfiles.begin(), shareProfiles.end(), aToken), shareProfiles.end());
	}
	
	fire(ShareProfileManagerListener::ProfileRemoved(), aToken);
	return true;
}

void ShareProfileManager::setProfilesDirty(const ProfileTokenSet& aProfiles, bool aIsMajorChange /*false*/) noexcept {
	if (!aProfiles.empty()) {
		RLock l(cs);
		for (const auto token : aProfiles) {
			auto p = getShareProfileUnsafe(token);
			if (p) {
				p->setDirty(aIsMajorChange);
			}
		}
	}

	for (const auto token : aProfiles) {
		fire(ShareProfileManagerListener::ProfileUpdated(), token, aIsMajorChange);
	}
}

ShareProfileList ShareProfileManager::getProfiles() const noexcept {
	RLock l(cs);
	return shareProfiles;
}

ShareProfileInfo::List ShareProfileManager::getProfileInfos() const noexcept {
	ShareProfileInfo::List ret;

	RLock l(cs);
	for (const auto& sp : shareProfiles | views::filter(ShareProfile::NotHidden())) {
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

} // namespace dcpp
