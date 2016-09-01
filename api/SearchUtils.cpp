/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#include <api/SearchUtils.h>

#include <api/common/Format.h>
#include <api/common/Serializer.h>


namespace webserver {
	const PropertyList SearchUtils::properties = {
		{ PROP_NAME, "name", TYPE_TEXT, SERIALIZE_TEXT, SORT_CUSTOM },
		{ PROP_RELEVANCE, "relevance", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_HITS, "hits", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_USERS, "users", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_TYPE, "type", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_SIZE, "size", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_DATE, "time", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_PATH, "path", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_CONNECTION, "connection", TYPE_SPEED, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_SLOTS, "slots", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_TTH, "tth", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_DUPE, "dupe", TYPE_NUMERIC_OTHER, SERIALIZE_CUSTOM, SORT_NUMERIC },
	};

	const PropertyItemHandler<SearchResultInfoPtr> SearchUtils::propertyHandler = {
		properties,
		SearchUtils::getStringInfo, SearchUtils::getNumericInfo, SearchUtils::compareResults, SearchUtils::serializeResult
	};

	json SearchUtils::serializeResult(const SearchResultInfoPtr& aResult, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_TYPE: {
			if (!aResult->isDirectory()) {
				return Serializer::serializeFileType(aResult->sr->getPath());
			} else {
				return Serializer::serializeFolderType(aResult->sr->getFileCount(), aResult->sr->getFolderCount());
			}
		}
		case PROP_SLOTS: {
			int free = 0, total = 0;
			aResult->getSlots(free, total);
			return Serializer::serializeSlots(free, total);
		}
		case PROP_USERS: {
			
			return {
				{ "count", aResult->getHits() + 1 },
				{ "user", Serializer::serializeHintedUser(aResult->sr->getUser()) }
			};
		}
		case PROP_DUPE:
		{
			if (aResult->isDirectory()) {
				return Serializer::serializeDirectoryDupe(aResult->getDupe(), aResult->sr->getPath());
			}

			return Serializer::serializeFileDupe(aResult->getDupe(), aResult->sr->getTTH());
		}
		default: dcassert(0); return nullptr;
		}
	}

	int SearchUtils::compareResults(const SearchResultInfoPtr& a, const SearchResultInfoPtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_NAME: {
			if (a->sr->getType() == b->sr->getType())
				return Util::DefaultSort(a->sr->getFileName(), b->sr->getFileName());
			else
				return (a->sr->getType() == SearchResult::TYPE_DIRECTORY) ? -1 : 1;
		}
		case PROP_TYPE: {
			if (a->sr->getType() != b->sr->getType()) {
				// Directories go first
				return a->sr->getType() == SearchResult::TYPE_FILE ? 1 : -1;
			}

			if (a->sr->getType() != SearchResult::TYPE_FILE && b->sr->getType() != SearchResult::TYPE_FILE) {
				// Directories
				auto dirsA = a->sr->getFolderCount();
				auto dirsB = b->sr->getFolderCount();
				if (dirsA != dirsB) {
					return compare(dirsA, dirsB);
				}

				auto filesA = a->sr->getFileCount();
				auto filesB = b->sr->getFileCount();

				return compare(filesA, filesB);
			}

			return Util::DefaultSort(Util::getFileExt(a->sr->getPath()), Util::getFileExt(b->sr->getPath()));
		}
		case PROP_SLOTS: {
			if (a->sr->getFreeSlots() == b->sr->getFreeSlots())
				return compare(a->sr->getTotalSlots(), b->sr->getTotalSlots());
			else
				return compare(a->sr->getFreeSlots(), b->sr->getFreeSlots());
		}
		case PROP_USERS: {
			if (a->getHits() != b->getHits()) {
				return compare(a->getHits(), b->getHits());
			}

			return Util::DefaultSort(Format::formatNicks(a->sr->getUser()), Format::formatNicks(b->sr->getUser()));
		}
		default: dcassert(0); return 0;
		}
	}
	std::string SearchUtils::getStringInfo(const SearchResultInfoPtr& aResult, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_NAME: return aResult->sr->getFileName();
		case PROP_PATH: return Util::toAdcFile(aResult->sr->getPath());
		case PROP_USERS: return Format::formatNicks(aResult->sr->getUser());
		case PROP_TYPE: {
			if (aResult->sr->getType() == SearchResult::TYPE_DIRECTORY) {
				return Format::formatFolderContent(aResult->sr->getFileCount(), aResult->sr->getFolderCount());
			}
			else {
				return Format::formatFileType(aResult->sr->getPath());
			}
		}
		case PROP_SLOTS: {
			int freeSlots = 0, totalSlots = 0;
			aResult->getSlots(freeSlots, totalSlots);
			return SearchResult::formatSlots(freeSlots, totalSlots);
		}
		case PROP_TTH: return aResult->sr->getTTH().toBase32();
		default: dcassert(0); return Util::emptyString;
		}
	}
	double SearchUtils::getNumericInfo(const SearchResultInfoPtr& aResult, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_SIZE: return (double)aResult->sr->getSize();
		case PROP_HITS: return (double)aResult->getHits();
		case PROP_CONNECTION: return aResult->getConnectionSpeed();
		case PROP_RELEVANCE : return aResult->getTotalRelevance();
		case PROP_DATE: return (double)aResult->getOldestDate();
		case PROP_DUPE: return (double)aResult->getDupe();
		default: dcassert(0); return 0;
		}
	}
}