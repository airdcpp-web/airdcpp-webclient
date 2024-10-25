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

#include <airdcpp/share/temp_share/TempShareManager.h>

#include <airdcpp/user/HintedUser.h>
#include <airdcpp/search/SearchResult.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/core/timer/TimerManager.h>
#include <airdcpp/util/Util.h>
#include <airdcpp/util/ValueGenerator.h>

namespace dcpp {

TempShareInfo::TempShareInfo(const string& aName, const string& aPath, int64_t aSize, const TTHValue& aTTH, const UserPtr& aUser) noexcept :
	id(ValueGenerator::rand()), name(aName), user(aUser), realPath(aPath), size(aSize), tth(aTTH), timeAdded(GET_TIME()) { }

bool TempShareInfo::hasAccess(const UserPtr& aUser) const noexcept {
	return !user || user == aUser;
}

TempShareManager::TempShareManager() {
	ShareManager::getInstance()->registerUploadFileProvider(this);
}

TempShareManager::~TempShareManager() {

}

bool TempShareManager::toRealWithSize(const UploadFileQuery& aQuery, string& path_, int64_t& size_, bool& noAccess_) const noexcept {
	for (const auto& item: getTempShares(aQuery.tth)) {
		if (aQuery.enableAccessChecks() && !item.hasAccess(aQuery.user)) {
			noAccess_ = true;
		} else {
			noAccess_ = false;
			path_ = item.realPath;
			size_ = item.size;
			return true;
		}
	}

	return false;
}

void TempShareManager::getRealPaths(const TTHValue& aTTH, StringList& paths_) const noexcept {
	for (const auto& item : getTempShares(aTTH)) {
		paths_.push_back(item.realPath);
	}
}

void TempShareManager::getBloom(ProfileToken, HashBloom& bloom_) const noexcept {
	RLock l(cs);
	for (const auto& item : tempShares | views::values)
		bloom_.add(item.tth);
}

void TempShareManager::getBloomFileCount(ProfileToken, size_t& fileCount_) const noexcept {
	RLock l(cs);
	fileCount_ += tempShares.size();
}

void TempShareManager::search(SearchResultList& results, const TTHValue& aTTH, const ShareSearch& aSearchInfo) const noexcept {
	const auto items = getTempShares(aTTH);
	for (const auto& item : items) {
		if (item.hasAccess(aSearchInfo.optionalUser)) {
			auto sr = make_shared<SearchResult>(SearchResult::Type::FILE, item.size, item.getVirtualPath(), aTTH, item.timeAdded, DirectoryContentInfo::uninitialized());
			results.push_back(sr);
		}
	}
}

optional<TempShareToken> TempShareManager::isTempShared(const UserPtr& aUser, const TTHValue& aTTH) const noexcept {
	RLock l(cs);
	const auto fp = tempShares.equal_range(aTTH);

	for (const auto& file : fp | pair_to_range | views::values) {
		if (file.hasAccess(aUser)) {
			return file.id;
		}
	}

	return nullopt;
}

TempShareInfoList TempShareManager::getTempShares() const noexcept {
	TempShareInfoList ret;
	{
		RLock l(cs);
		ranges::copy(tempShares | views::values, back_inserter(ret));
	}
	return ret;
}

optional<TempShareInfo> TempShareManager::addTempShare(const TTHValue& aTTH, const string& aName, const string& aFilePath, int64_t aSize, ProfileToken aProfile, const UserPtr& aUser) noexcept {
	// Regular shared file?
	if (ShareManager::getInstance()->isFileShared(aTTH, aProfile)) {
		return nullopt;
	}

	optional<pair<TempShareInfo, bool>> addInfo;

	{
		WLock l(cs);
		addInfo.emplace(addTempShareImpl(aTTH, aName, aFilePath, aSize, aUser));
	}

	if (addInfo->second) {
		fire(TempShareManagerListener::TempFileAdded(), addInfo->first);
	}

	return addInfo->first;
}

bool TempShareManager::removeTempShare(TempShareToken aId) noexcept {
	optional<TempShareInfo> removedItem;

	{
		WLock l(cs);
		auto removed = removeTempShareImpl(aId);
		if (!removed) {
			return false;
		}

		removedItem.emplace(*removed);
	}

	fire(TempShareManagerListener::TempFileRemoved(), *removedItem);
	return true;
}

pair<TempShareInfo, bool> TempShareManager::addTempShareImpl(const TTHValue& aTTH, const string& aName, const string& aFilePath, int64_t aSize, const UserPtr& aUser) noexcept {
	const auto files = tempShares.equal_range(aTTH);
	for (const auto& file : files | pair_to_range | views::values) {
		if (file.hasAccess(aUser)) {
			return { file, false };
		}
	}

	// Didn't exist.. fine, add it.
	const auto item = TempShareInfo(aName, aFilePath, aSize, aTTH, aUser);
	tempShares.emplace(aTTH, item);

	return { item, true };
}

TempShareInfoList TempShareManager::getTempShares(const TTHValue& aTTH) const noexcept {
	TempShareInfoList ret;

	{
		const auto files = tempShares.equal_range(aTTH);
		for (const auto& file: files | pair_to_range | views::values) {
			ret.push_back(file);
		}
	}

	return ret;
}

optional<TempShareInfo> TempShareManager::removeTempShareImpl(TempShareToken aId) noexcept {
	optional<TempShareInfo> removedItem;

	const auto i = ranges::find_if(tempShares | views::values, [aId](const TempShareInfo& ti) {
		return ti.id == aId;
	});

	if (i.base() == tempShares.end()) {
		return nullopt;
	}

	removedItem.emplace(std::move(*i));
	tempShares.erase(i.base());

	return removedItem;
}

}