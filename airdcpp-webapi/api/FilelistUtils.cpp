/*
* Copyright (C) 2011-2021 AirDC++ Project
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

#include <api/FilelistUtils.h>

#include <api/common/Format.h>
#include <api/common/Serializer.h>


namespace webserver {
	const PropertyList FilelistUtils::properties = {
		{ PROP_NAME, "name", TYPE_TEXT, SERIALIZE_TEXT, SORT_CUSTOM },
		{ PROP_TYPE, "type", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_SIZE, "size", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_DATE, "time", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_PATH, "path", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_TTH, "tth", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_DUPE, "dupe", TYPE_NUMERIC_OTHER, SERIALIZE_CUSTOM, SORT_NUMERIC },
		{ PROP_COMPLETE, "complete", TYPE_NUMERIC_OTHER, SERIALIZE_BOOL, SORT_NUMERIC },
	};

	const PropertyItemHandler<FilelistItemInfoPtr> FilelistUtils::propertyHandler(properties,
		FilelistUtils::getStringInfo,
		FilelistUtils::getNumericInfo,
		FilelistUtils::compareItems,
		FilelistUtils::serializeItem
	);

	json FilelistUtils::serializeItem(const FilelistItemInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
			case PROP_TYPE:
			{
				if (!aItem->isDirectory()) {
					return Serializer::serializeFileType(aItem->getAdcPath());
				}

				return Serializer::serializeFolderType(aItem->dir->getContentInfo());
			}
			case PROP_DUPE:
			{
				if (aItem->isDirectory()) {
					return Serializer::serializeDirectoryDupe(aItem->getDupe(), aItem->getAdcPath());
				}

				return Serializer::serializeFileDupe(aItem->getDupe(), aItem->file->getTTH());
			}
			default: dcassert(0); return nullptr;
		}
	}

	int FilelistUtils::compareItems(const FilelistItemInfoPtr& a, const FilelistItemInfoPtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_NAME: {
			if (a->getType() == b->getType()) {
				return Util::DefaultSort(a->getName(), b->getName());
			}

			return a->isDirectory() ? -1 : 1;
		}
		case PROP_TYPE: {
			if (a->getType() != b->getType()) {
				// Directories go first
				return a->getType() == FilelistItemInfo::FILE ? 1 : -1;
			}

			if (a->isDirectory() && b->isDirectory()) {
				return Util::directoryContentSort(a->dir->getContentInfo(), b->dir->getContentInfo());
			}

			return Util::DefaultSort(Util::getFileExt(a->getName()), Util::getFileExt(b->getName()));
		}
		default: dcassert(0); return 0;
		}
	}

	std::string FilelistUtils::getStringInfo(const FilelistItemInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_NAME: return aItem->getName();
		case PROP_PATH: return aItem->getAdcPath();
		case PROP_TYPE: {
			if (aItem->isDirectory()) {
				return Util::formatDirectoryContent(aItem->dir->getContentInfo());
			}

			return Util::formatFileType(aItem->getAdcPath());
		}
		case PROP_TTH: return aItem->getType() == FilelistItemInfo::FILE ? aItem->file->getTTH().toBase32() : Util::emptyString;
		default: dcassert(0); return Util::emptyString;
		}
	}

	double FilelistUtils::getNumericInfo(const FilelistItemInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_SIZE: return (double)aItem->getSize();
		case PROP_DATE: return (double)aItem->getDate();
		case PROP_DUPE: return (double)aItem->getDupe();
		case PROP_COMPLETE: return (double)aItem->isComplete();
		default: dcassert(0); return 0;
		}
	}
}