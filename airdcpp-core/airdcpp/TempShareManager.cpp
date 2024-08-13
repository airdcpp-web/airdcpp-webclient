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

#include "TempShareManager.h"

#include "HintedUser.h"
#include "TimerManager.h"
#include "Util.h"
#include "ValueGenerator.h"

namespace dcpp {

TempShareInfo::TempShareInfo(const string& aName, const string& aPath, int64_t aSize, const TTHValue& aTTH, const UserPtr& aUser) noexcept :
	id(ValueGenerator::rand()), user(aUser), name(aName), path(aPath), size(aSize), tth(aTTH), timeAdded(GET_TIME()) { }

bool TempShareInfo::hasAccess(const UserPtr& aUser) const noexcept {
	return !user || user == aUser;
}

optional<TempShareToken> TempShareManager::isTempShared(const UserPtr& aUser, const TTHValue& aTTH) const noexcept {
	const auto fp = tempShares.equal_range(aTTH);
	/*auto file = ranges::find_if(fp | pair_to_range | views::values, [&aUser](const auto& f) = > f.hasAccess(aUser));
	if (file.base() != aDirNames.end()) {
		return *file;
	}*/

	for (const auto& file : fp | pair_to_range | views::values) {
		if (file.hasAccess(aUser)) {
			return file.id;
		}
	}

	return nullopt;
}

TempShareInfoList TempShareManager::getTempShares() const noexcept {
	TempShareInfoList ret;
	ranges::copy(tempShares | views::values, back_inserter(ret));
	return ret;
}

pair<TempShareInfo, bool> TempShareManager::addTempShare(const TTHValue& aTTH, const string& aName, const string& aFilePath, int64_t aSize, const UserPtr& aUser) noexcept {
	const auto files = tempShares.equal_range(aTTH);
	for (const auto& file : files | pair_to_range | views::values) {
		if (file.hasAccess(aUser)) {
			return { file, false };
		}
	}

	//didnt exist.. fine, add it.
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

optional<TempShareInfo> TempShareManager::removeTempShare(TempShareToken aId) noexcept {
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