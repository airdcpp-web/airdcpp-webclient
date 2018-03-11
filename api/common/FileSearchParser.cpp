/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#include <api/common/Deserializer.h>
#include <api/common/FileSearchParser.h>
#include <web-server/JsonUtil.h>

#include <airdcpp/Encoder.h>
#include <airdcpp/SearchManager.h>

namespace webserver {
	SearchPtr FileSearchParser::parseSearch(const json& aJson, bool aIsDirectSearch, const string& aToken) {
		auto priority = Deserializer::deserializePriority(aJson, true);
		if (priority == Priority::DEFAULT) {
			priority = Priority::LOW;
		}

		auto s = make_shared<Search>(priority, aToken);
		parseMatcher(JsonUtil::getRawField("query", aJson), s);
		if (aIsDirectSearch) {
			parseOptions(JsonUtil::getOptionalRawField("options", aJson), s);
		}
		
		return s;
	}

	void FileSearchParser::parseMatcher(const json& aJson, const SearchPtr& aSearch) {
		aSearch->query = JsonUtil::getOptionalFieldDefault<string>("pattern", aJson, Util::emptyString);

		// Filetype
		aSearch->fileType = aSearch->query.size() == 39 && Encoder::isBase32(aSearch->query.c_str()) ? Search::TYPE_TTH : Search::TYPE_ANY;

		auto fileTypeStr = JsonUtil::getOptionalField<string>("file_type", aJson);
		if (fileTypeStr) {
			try {
				SearchManager::getInstance()->getSearchType(parseFileType(*fileTypeStr), aSearch->fileType, aSearch->exts, true);
			} catch (...) {
				throw std::domain_error("Invalid file type");
			}
		}

		if (aSearch->fileType != Search::TYPE_DIRECTORY) {
			// Extensions
			auto optionalExtensions = JsonUtil::getOptionalField<StringList>("extensions", aJson);
			if (optionalExtensions) {
				aSearch->exts = *optionalExtensions;
			}

			// Size
			auto minSize = JsonUtil::getOptionalField<int64_t>("min_size", aJson);
			if (minSize) {
				aSearch->size = *minSize;
				aSearch->sizeType = Search::SIZE_ATLEAST;
			}

			auto maxSize = JsonUtil::getOptionalField<int64_t>("max_size", aJson);
			if (maxSize) {
				aSearch->size = *maxSize;
				aSearch->sizeType = Search::SIZE_ATMOST;
			}
		}

		// Anything to search for?
		if (aSearch->exts.empty() && aSearch->query.empty()) {
			throw std::domain_error("A valid pattern or file extensions must be provided");
		}

		// Date
		aSearch->maxDate = JsonUtil::getOptionalField<time_t>("max_age", aJson);
		aSearch->minDate = JsonUtil::getOptionalField<time_t>("min_age", aJson);

		// Excluded
		aSearch->excluded = JsonUtil::getOptionalFieldDefault<StringList>("excluded", aJson, StringList());

		// Match type
		auto matchTypeStr = JsonUtil::getOptionalFieldDefault<string>("match_type", aJson, "path_partial");
		aSearch->matchType = parseMatchType(matchTypeStr);
	}

	void FileSearchParser::parseOptions(const json& aJson, const SearchPtr& aSearch) {
		aSearch->path = JsonUtil::getOptionalFieldDefault<string>("path", aJson, ADC_ROOT_STR);
		aSearch->maxResults = JsonUtil::getOptionalFieldDefault<int>("max_results", aJson, 5);
		aSearch->returnParents = JsonUtil::getOptionalFieldDefault<bool>("return_parents", aJson, false);
		aSearch->requireReply = true;
	}

	const map<string, string> fileTypeMappings = {
		{ "any", "0" },
		{ "audio", "1" },
		{ "compressed", "2" },
		{ "document", "3" },
		{ "executable", "4" },
		{ "picture", "5" },
		{ "video", "6" },
		{ "directory", "7" },
		{ "tth", "8" },
		{ "file", "9" },
	};

	const string& FileSearchParser::parseFileType(const string& aType) noexcept {
		auto i = fileTypeMappings.find(aType);
		return i != fileTypeMappings.end() ? i->second : aType;
	}

	Search::MatchType FileSearchParser::parseMatchType(const string& aTypeStr) {
		if (aTypeStr == "path_partial") {
			return Search::MATCH_PATH_PARTIAL;
		} else if (aTypeStr == "name_exact") {
			return Search::MATCH_NAME_EXACT;
		} else if (aTypeStr == "name_partial") {
			return Search::MATCH_NAME_PARTIAL;
		}

		throw std::domain_error("Invalid match type");
	}
}
