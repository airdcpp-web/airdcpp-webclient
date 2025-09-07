/*
 * Copyright (C) 2011-2024 AirDC++ Project
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

#include <airdcpp/util/DupeUtil.h>

#include <airdcpp/queue/QueueManager.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/util/PathUtil.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/trim.hpp>

namespace dcpp {

boost::regex DupeUtil::releaseRegBasic = boost::regex(DupeUtil::getReleaseRegBasic());
boost::regex DupeUtil::releaseRegChat = boost::regex(getReleaseRegLong(true));
boost::regex DupeUtil::subDirRegPlain = boost::regex(getSubDirReg(), boost::regex::icase);

StringList DupeUtil::getAdcDirectoryDupePaths(DupeType aType, const string& aAdcPath) {
	StringList ret;
	if (isShareDupe(aType)) {
		ret = ShareManager::getInstance()->getAdcDirectoryDupePaths(aAdcPath);
	} else {
		ret = QueueManager::getInstance()->getAdcDirectoryDupePaths(aAdcPath);
	}

	return ret;
}

StringList DupeUtil::getFileDupePaths(DupeType aType, const TTHValue& aTTH) {
	StringList ret;
	if (isShareDupe(aType)) {
		ret = ShareManager::getInstance()->getRealPaths(aTTH);
	} else {
		ret = QueueManager::getInstance()->getTargets(aTTH);
	}

	std::sort(ret.begin(), ret.end());
	ret.erase(unique(ret.begin(), ret.end()), ret.end()); // Duplicate paths can be added by different share providers
	return ret;
}

bool DupeUtil::isShareOnlyDupe(DupeType aType) noexcept { 
	return aType == DUPE_SHARE_FULL || aType == DUPE_SHARE_PARTIAL; 
}

bool DupeUtil::isQueueOnlyDupe(DupeType aType) noexcept {
	return aType == DUPE_QUEUE_FULL || aType == DUPE_QUEUE_PARTIAL;
}

bool DupeUtil::isFinishedOnlyDupe(DupeType aType) noexcept {
	return aType == DUPE_FINISHED_FULL || aType == DUPE_FINISHED_PARTIAL;
}

bool DupeUtil::isShareDupe(DupeType aType, bool aAllowPartial) noexcept {
	if (aType == DUPE_SHARE_FULL) {
		return true;
	}

	if (aAllowPartial && (aType == DUPE_SHARE_PARTIAL || aType == DUPE_SHARE_QUEUE_FINISHED || aType == DUPE_SHARE_QUEUE || aType == DUPE_SHARE_FINISHED)) {
		return true;
	}

	return false;
}

bool DupeUtil::isQueueDupe(DupeType aType, bool aAllowPartial) noexcept {
	if (aType == DUPE_QUEUE_FULL) {
		return true;
	}

	if (aAllowPartial && (aType == DUPE_QUEUE_PARTIAL || aType == DUPE_SHARE_QUEUE_FINISHED || aType == DUPE_SHARE_QUEUE || aType == DUPE_QUEUE_FINISHED)) {
		return true;
	}

	return false;
}

bool DupeUtil::isFinishedDupe(DupeType aType, bool aAllowPartial) noexcept {
	if (aType == DUPE_FINISHED_FULL) {
		return true;
	}

	if (aAllowPartial && (aType == DUPE_FINISHED_PARTIAL || aType == DUPE_SHARE_QUEUE_FINISHED || aType == DUPE_SHARE_FINISHED || aType == DUPE_QUEUE_FINISHED)) {
		return true;
	}

	return false;
}

DupeType DupeUtil::checkAdcDirectoryDupe(const string& aAdcPath, int64_t aSize) {
	auto dupe = ShareManager::getInstance()->getAdcDirectoryDupe(aAdcPath, aSize);
	if (dupe != DUPE_NONE) {
		return dupe;
	}

	return QueueManager::getInstance()->getAdcDirectoryDupe(aAdcPath, aSize);
}

DupeType DupeUtil::checkFileDupe(const TTHValue& aTTH) {
	if (ShareManager::getInstance()->isFileShared(aTTH)) {
		return DUPE_SHARE_FULL;
	}

	return QueueManager::getInstance()->isFileQueued(aTTH);
}

bool DupeUtil::allowOpenDirectoryDupe(DupeType aType) noexcept {
	return aType != DUPE_NONE;
}

bool DupeUtil::allowOpenFileDupe(DupeType aType) noexcept {
	return aType != DUPE_NONE && aType != DUPE_QUEUE_FULL;
}

void DupeUtil::init() {
	// releaseRegBasic.assign(getReleaseRegBasic());
	// releaseRegChat.assign(getReleaseRegLong(true));
	// subDirRegPlain.assign(getSubDirReg(), boost::regex::icase);
}

bool DupeUtil::isRelease(const string& aString) {
	try {
		return boost::regex_match(aString, releaseRegBasic);
	} catch (...) {}

	return false;
}

const string DupeUtil::getReleaseRegLong(bool chat) noexcept {
	if (chat)
		return R"(((?<=\s)|^)(?=\S*[A-Z]\S*)(([A-Z0-9]|\w[A-Z0-9])[A-Za-z0-9-]*)(\.|_|(-(?=\S*\d{4}\S+)))(\S+)-(\w{2,})(?=(\W)?\s|$))";
	else
		return R"((?=\S*[A-Z]\S*)(([A-Z0-9]|\w[A-Z0-9])[A-Za-z0-9-]*)(\.|_|(-(?=\S*\d{4}\S+)))(\S+)-(\w{2,}))";
}

const string DupeUtil::getReleaseRegBasic() noexcept {
	return R"(((?=\S*[A-Za-z]\S*)[A-Z0-9]\S{3,})-([A-Za-z0-9_]{2,}))";
}

const string DupeUtil::getSubDirReg() noexcept {
	return R"((((S(eason)?)|DVD|CD|(D|DIS(K|C))).?([0-9](0-9)?))|Sample.?|Proof.?|Cover.?|.{0,5}Sub(s|pack)?)";
}

string DupeUtil::getReleaseDir(const string& aDir, bool aCut, const char aSeparator) noexcept {
	auto p = getDirectoryName(PathUtil::getFilePath(aDir, aSeparator), aSeparator);
	if (aCut) {
		return p.first;
	}

	// return with the path
	return p.second == string::npos ? aDir : aDir.substr(0, p.second);
}

pair<string, string::size_type> DupeUtil::getDirectoryName(const string& aPath, char aSeparator) noexcept {
	if (aPath.size() < 3)
		return { aPath, false };

	//get the directory to search for
	bool isSub = false;
	string::size_type i = aPath.back() == aSeparator ? aPath.size() - 2 : aPath.size() - 1, j;
	for (;;) {
		j = aPath.find_last_of(aSeparator, i);
		if (j == string::npos) {
			j = 0;
			break;
		}

		if (!boost::regex_match(aPath.substr(j + 1, i - j), subDirRegPlain)) {
			j++;
			break;
		}

		isSub = true;
		i = j - 1;
	}

	return { aPath.substr(j, i - j + 1), isSub ? i + 2 : string::npos };
}

string DupeUtil::getTitle(const string& searchTerm) noexcept {
	auto ret = Text::toLower(searchTerm);

	//Remove group name
	size_t pos = ret.rfind('-');
	if (pos != string::npos)
		ret = ret.substr(0, pos);

	//replace . with space
	pos = 0;
	while ((pos = ret.find_first_of("._", pos)) != string::npos) {
		ret.replace(pos, 1, " ");
	}

	//remove words after year/episode
	boost::regex reg;
	reg.assign("(((\\[)?((19[0-9]{2})|(20[0-1][0-9]))|(s[0-9]([0-9])?(e|d)[0-9]([0-9])?)|(Season(\\.)[0-9]([0-9])?)).*)");

	boost::match_results<string::const_iterator> result;
	if (boost::regex_search(ret, result, reg, boost::match_default)) {
		ret = ret.substr(0, result.position());
	}

	//boost::regex_replace(ret, reg, Util::emptyStringT, boost::match_default | boost::format_sed);

	//remove extra words
	string extrawords [] = { "multisubs", "multi", "dvdrip", "dvdr", "real proper", "proper", "ultimate directors cut", "directors cut", "dircut", "x264", "pal", "complete", "limited", "ntsc", "bd25",
		"bd50", "bdr", "bd9", "retail", "bluray", "nordic", "720p", "1080p", "read nfo", "dts", "hdtv", "pdtv", "hddvd", "repack", "internal", "custom", "subbed", "unrated", "recut",
		"extended", "dts51", "finsub", "swesub", "dksub", "nosub", "remastered", "2disc", "rf", "fi", "swe", "stv", "r5", "festival", "anniversary edition", "bdrip", "ac3", "xvid",
		"ws", "int" };
	pos = 0;
	ret += ' ';
	auto arrayLength = sizeof (extrawords) / sizeof (*extrawords);
	while (pos < arrayLength) {
		boost::replace_all(ret, " " + extrawords[pos] + " ", " ");
		pos++;
	}

	//trim spaces from the end
	boost::trim_right(ret);
	return ret;
}

DupeType DupeUtil::parseDirectoryContentDupe(const DupeSet& aDupeSet) noexcept {
	if (aDupeSet.empty() || ranges::all_of(aDupeSet, [](auto d) { return d == DupeType::DUPE_NONE; })) {
		// None
		return DUPE_NONE;
	}

	// Full dupes
	if (ranges::all_of(aDupeSet, [](auto d) { return d == DupeType::DUPE_SHARE_FULL; })) {
		return DUPE_SHARE_FULL;
	}

	if (ranges::all_of(aDupeSet, [](auto d) { return d == DupeType::DUPE_QUEUE_FULL; })) {
		return DUPE_QUEUE_FULL;
	}

	if (ranges::all_of(aDupeSet, [](auto d) { return d == DupeType::DUPE_FINISHED_FULL; })) {
		return DUPE_FINISHED_FULL;
	}

	// Partial dupes
	if (ranges::all_of(aDupeSet, [](auto d) { return DupeUtil::isShareOnlyDupe(d) || d == DupeType::DUPE_NONE; })) {
		return DUPE_SHARE_PARTIAL;
	}

	if (ranges::all_of(aDupeSet, [](auto d) { return DupeUtil::isQueueOnlyDupe(d) || d == DupeType::DUPE_NONE; })) {
		return DUPE_QUEUE_PARTIAL;
	}

	if (ranges::all_of(aDupeSet, [](auto d) { return DupeUtil::isFinishedOnlyDupe(d) || d == DupeType::DUPE_NONE; })) {
		return DUPE_FINISHED_PARTIAL;
	}

	// Mixed
	if (ranges::all_of(aDupeSet, [](auto d) { return DupeUtil::isFinishedOnlyDupe(d) || DupeUtil::isQueueOnlyDupe(d) || d == DUPE_QUEUE_FINISHED || d == DupeType::DUPE_NONE; })) {
		return DUPE_QUEUE_FINISHED;
	}

	if (ranges::all_of(aDupeSet, [](auto d) { return DupeUtil::isFinishedOnlyDupe(d) || DupeUtil::isShareOnlyDupe(d) || d == DUPE_SHARE_FINISHED || d == DupeType::DUPE_NONE; })) {
		return DUPE_SHARE_FINISHED;
	}

	if (ranges::all_of(aDupeSet, [](auto d) { return DupeUtil::isQueueOnlyDupe(d) || DupeUtil::isShareOnlyDupe(d) || d == DUPE_SHARE_QUEUE || d == DupeType::DUPE_NONE; })) {
		return DUPE_SHARE_QUEUE;
	}

	return DUPE_SHARE_QUEUE_FINISHED;
}

}
