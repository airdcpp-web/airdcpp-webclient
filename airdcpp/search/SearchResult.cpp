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
#include <airdcpp/search/SearchResult.h>

#include <airdcpp/hub/Client.h>
#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/util/DupeUtil.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/core/classes/ScopedFunctor.h>
#include <airdcpp/search/SearchQuery.h>
#include <airdcpp/util/text/Text.h>
#include <airdcpp/transfer/upload/UploadManager.h>
#include <airdcpp/user/User.h>
#include <airdcpp/util/ValueGenerator.h>

namespace dcpp {

atomic<SearchResultId> searchResultIdCounter { 0 };

SearchResult::SearchResult(const string& aPath) : path(aPath), id(ValueGenerator::rand()), type(Type::DIRECTORY) { }

SearchResult::SearchResult(const HintedUser& aUser, Type aType, uint8_t aTotalSlots, uint8_t aFreeSlots,
	int64_t aSize, const string& aPath, const string& ip, TTHValue aTTH, const string& aToken, time_t aDate, const string& aConnection, const DirectoryContentInfo& aContentInfo) :

	tth(aTTH), path(aPath), IP(ip),
	searchToken(aToken), id(searchResultIdCounter++), size(aSize), totalSlots(aTotalSlots), freeSlots(aFreeSlots),
	contentInfo(aContentInfo), user(aUser), type(aType), date(aDate), connection(aConnection) { }

SearchResult::SearchResult(Type aType, int64_t aSize, const string& aPath, const TTHValue& aTTH, time_t aDate, const DirectoryContentInfo& aContentInfo) :
	tth(aTTH), path(aPath), id(searchResultIdCounter++), size(aSize), totalSlots(UploadManager::getInstance()->getSlots()),
	freeSlots(UploadManager::getInstance()->getFreeSlots()), contentInfo(aContentInfo),
	user(HintedUser(ClientManager::getInstance()->getMe(), Util::emptyString)), type(aType), date(aDate) { }

string SearchResult::toSR(const Client& c) const noexcept {
	// File:		"$SR %s %s%c%s %d/%d%c%s (%s)|"
	// Directory:	"$SR %s %s %d/%d%c%s (%s)|"
	string tmp;
	tmp.reserve(128);
	tmp.append("$SR ", 4);
	tmp.append(Text::fromUtf8(c.getMyNick(), c.get(HubSettings::NmdcEncoding)));
	tmp.append(1, ' ');
	string acpFile = PathUtil::toNmdcFile(Text::fromUtf8(path, c.get(HubSettings::NmdcEncoding)));
	if(type == Type::FILE) {
		tmp.append(acpFile);
		tmp.append(1, '\x05');
		tmp.append(Util::toString(size));
	} else {
		tmp.append(acpFile, 0, acpFile.length() - 1);
	}
	tmp.append(1, ' ');
	tmp.append(Util::toString(freeSlots));
	tmp.append(1, '/');
	tmp.append(Util::toString(totalSlots));
	tmp.append(1, '\x05');
	tmp.append("TTH:" + getTTH().toBase32());
	tmp.append(" (", 2);
	tmp.append(c.getIpPort());
	tmp.append(")|", 2);
	return tmp;
}

AdcCommand SearchResult::toRES(char aType) const noexcept {
	AdcCommand cmd(AdcCommand::CMD_RES, aType);
	cmd.addParam("SI", Util::toString(size));
	cmd.addParam("SL", Util::toString(freeSlots));
	cmd.addParam("FN", path);
	if (type != Type::DIRECTORY) {
		cmd.addParam("TR", getTTH().toBase32());
	}
	cmd.addParam("DM", Util::toString(date));

	if (type == Type::DIRECTORY) {
		cmd.addParam("FI", Util::toString(contentInfo.files));
		cmd.addParam("FO", Util::toString(contentInfo.directories));
	}
	return cmd;
}

string SearchResult::getFileName() const noexcept {
	if(getType() == Type::FILE)
		return PathUtil::getAdcFileName(path); 

	return PathUtil::getAdcLastDir(path);
}

string SearchResult::getSlotString() const noexcept {
	return formatSlots(getFreeSlots(), getTotalSlots());
}

int64_t SearchResult::getConnectionInt() const noexcept {
	return isNMDC() ? static_cast<int64_t>(Util::toDouble(connection)*1024.0*1024.0/8.0) : Util::toInt64(connection);
}

int64_t SearchResult::getSpeedPerSlot() const noexcept {
	return totalSlots > 0 ? getConnectionInt() / totalSlots : 0;
}

bool SearchResult::SpeedSortOrder::operator()(const SearchResultPtr& lhs, const SearchResultPtr& rhs) const noexcept {
	//prefer the one that has free slots
	if (lhs->getFreeSlots() > 0 && rhs->getFreeSlots() == 0)
		return true;

	if (lhs->getFreeSlots() == 0 && rhs->getFreeSlots() > 0)
		return false;


	//both have free slots? compare the per slot speed
	if (lhs->getFreeSlots() && rhs->getFreeSlots())
		return lhs->getFreeSlots() * lhs->getSpeedPerSlot() > rhs->getFreeSlots() * rhs->getSpeedPerSlot();

	//no free slots, choose a random user with the fastest connection available
	return lhs->getConnectionInt() > rhs->getConnectionInt();
}

bool SearchResult::DateOrder::operator()(const SearchResultPtr& a, const SearchResultPtr& b) const noexcept {
	return a->getDate() > 0 && a->getDate() < b->getDate();
}

string SearchResult::formatSlots(size_t aFree, size_t aTotal) noexcept {
	return Util::toString(aFree) + '/' + Util::toString(aTotal);
}

const CID& SearchResult::getCID() const noexcept {
	return user.user->getCID(); 
}

bool SearchResult::isNMDC() const noexcept {
	return user.user->isNMDC(); 
}

void SearchResult::pickResults(SearchResultList& aResults, int aMaxCount) noexcept {
	if (static_cast<int>(aResults.size()) <= aMaxCount) {
		// we can pick all matches
	} else {
		// pick the best matches
		sort(aResults.begin(), aResults.end(), SearchResult::SpeedSortOrder());
		aResults.erase(aResults.begin() + aMaxCount, aResults.end());
	}
}

string SearchResult::getAdcFilePath() const noexcept {
	if (type == Type::DIRECTORY)
		return path;

	return PathUtil::getAdcFilePath(path);
}

bool SearchResult::matches(SearchQuery& aQuery, const string_view& aLocalSearchToken) const noexcept {
	if (!user.user->isNMDC()) {
		// ADC
		if (aLocalSearchToken != searchToken) {
			return false;
		}
	} else {
		// NMDC results must be matched manually

		// File extensions
		if (!aQuery.hasExt(path)) {
			return false;
		}

		// Excluded words
		if (aQuery.isExcluded(path)) {
			return false;
		}

		// TTH
		if (aQuery.root && *aQuery.root != tth) {
			return false;
		}
	}

	// All clients can't handle this correctly
	if (aQuery.itemType == SearchQuery::ItemType::FILE && type != Type::FILE) {
		return false;
	}

	return true;
}

bool SearchResult::getRelevance(SearchQuery& aQuery, RelevanceInfo& relevance_, const string& aLocalSearchToken) const noexcept {
	if (!aLocalSearchToken.empty() && !matches(aQuery, aLocalSearchToken)) {
		return false;
	}

	// Nothing to calculate with TTH searches
	if (aQuery.root) {
		relevance_.matchRelevance = 1;
		relevance_.sourceScoreFactor = 0.01;
		return true;
	}

	// Match path
	SearchQuery::Recursion recursion;
	ScopedFunctor([&] { aQuery.recursion = nullptr; });
	if (!aQuery.matchesAdcPath(path, recursion)) {
		return false;
	}

	// Don't count the levels because they can't be compared with each others
	auto matchRelevance = SearchQuery::getRelevanceScore(aQuery, 0, type == Type::DIRECTORY, getFileName());
	double sourceScoreFactor = 0.01;
	if (aQuery.recursion && aQuery.recursion->isComplete()) {
		// There are subdirectories/files that have more matches than the main directory
		// Don't give too much weight for those even if there are lots of sources
		sourceScoreFactor = 0.001;

		// We don't get the level scores so balance those here
		matchRelevance = max(0.0, matchRelevance - (0.05 * aQuery.recursion->recursionLevel));
	}

	relevance_.matchRelevance = matchRelevance;
	relevance_.sourceScoreFactor = sourceScoreFactor;
	return true;
}

DupeType SearchResult::getDupe() const noexcept {
	if (type == Type::DIRECTORY) {
		return DupeUtil::checkAdcDirectoryDupe(path, size);
	} else {
		return DupeUtil::checkFileDupe(tth);
	}
}

}
