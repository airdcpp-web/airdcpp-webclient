/*
* Copyright (C) 2011-2017 AirDC++ Project
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

	const PropertyItemHandler<GroupedSearchResultPtr> SearchUtils::propertyHandler = {
		properties,
		SearchUtils::getStringInfo, SearchUtils::getNumericInfo, SearchUtils::compareResults, SearchUtils::serializeResult
	};

	json SearchUtils::serializeResult(const GroupedSearchResultPtr& aResult, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_TYPE: {
			if (!aResult->isDirectory()) {
				return Serializer::serializeFileType(aResult->getPath());
			}

			return Serializer::serializeFolderType(aResult->getContentInfo());
		}
		case PROP_SLOTS: {
			auto slots = aResult->getSlots();
			return Serializer::serializeSlots(slots.free, slots.total);
		}
		case PROP_USERS: {
			return {
				{ "count", aResult->getHits() },
				{ "user", Serializer::serializeHintedUser(aResult->getBaseUser()) }
			};
		}
		case PROP_DUPE:
		{
			if (aResult->isDirectory()) {
				return Serializer::serializeDirectoryDupe(aResult->getDupe(), aResult->getPath());
			}

			return Serializer::serializeFileDupe(aResult->getDupe(), aResult->getTTH());
		}
		default: dcassert(0); return nullptr;
		}
	}

	int SearchUtils::compareResults(const GroupedSearchResultPtr& a, const GroupedSearchResultPtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_NAME: {
			if (a->isDirectory() == b->isDirectory()) {
				return Util::DefaultSort(a->getFileName(), b->getFileName());
			}

			return a->isDirectory() ? -1 : 1;
		}
		case PROP_TYPE: {
			if (a->isDirectory() != b->isDirectory()) {
				// Directories go first
				return a->isDirectory() ? -1 : 1;
			}

			if (a->isDirectory() && b->isDirectory()) {
				return Util::directoryContentSort(a->getContentInfo(), b->getContentInfo());
			}

			return Util::DefaultSort(Util::getFileExt(a->getPath()), Util::getFileExt(b->getPath()));
		}
		case PROP_SLOTS: {
			auto slotsA = a->getSlots();
			auto slotsB = b->getSlots();

			if (slotsA.free == slotsB.free) {
				return compare(slotsA.total, slotsB.total);
			}

			return compare(slotsA.free, slotsB.free);
		}
		case PROP_USERS: {
			if (a->getHits() != b->getHits()) {
				return compare(a->getHits(), b->getHits());
			}

			return Util::DefaultSort(Format::formatNicks(a->getBaseUser()), Format::formatNicks(b->getBaseUser()));
		}
		default: dcassert(0); return 0;
		}
	}
	std::string SearchUtils::getStringInfo(const GroupedSearchResultPtr& aResult, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_NAME: return aResult->getFileName();
		case PROP_PATH: return Util::toAdcFile(aResult->getPath());
		case PROP_USERS: return Format::formatNicks(aResult->getBaseUser());
		case PROP_TYPE: {
			if (aResult->isDirectory()) {
				return Util::formatDirectoryContent(aResult->getContentInfo());
			}

			return Util::formatFileType(aResult->getPath());
		}
		case PROP_SLOTS: {
			auto slots = aResult->getSlots();
			return SearchResult::formatSlots(slots.free, slots.total);
		}
		case PROP_TTH: return aResult->isDirectory() ? Util::emptyString : aResult->getTTH().toBase32();
		default: dcassert(0); return Util::emptyString;
		}
	}
	double SearchUtils::getNumericInfo(const GroupedSearchResultPtr& aResult, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_SIZE: return (double)aResult->getSize();
		case PROP_HITS: return (double)aResult->getHits();
		case PROP_CONNECTION: return aResult->getConnectionSpeed();
		case PROP_RELEVANCE : return aResult->getTotalRelevance();
		case PROP_DATE: return (double)aResult->getOldestDate();
		case PROP_DUPE: return (double)aResult->getDupe();
		default: dcassert(0); return 0;
		}
	}
}