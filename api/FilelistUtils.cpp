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

#include <api/FilelistUtils.h>

#include <api/common/Format.h>

namespace webserver {
	json FilelistUtils::serializeItem(const FilelistItemInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
			case FilelistInfo::PROP_TYPE:
			{
				if (!aItem->isDirectory()) {
					return Serializer::serializeFileType(aItem->getPath());
				}

				return Serializer::serializeFolderType(static_cast<int>(aItem->dir->getFileCount()), static_cast<int>(aItem->dir->getFolderCount()));
			}
			case FilelistInfo::PROP_DUPE:
			{
				if (aItem->isDirectory()) {
					return Serializer::serializeDirectoryDupe(aItem->getDupe(), aItem->getPath());
				}

				return Serializer::serializeFileDupe(aItem->getDupe(), aItem->file->getTTH());
			}
			default: dcassert(0); return nullptr;
		}
	}

	int FilelistUtils::compareItems(const FilelistItemInfoPtr& a, const FilelistItemInfoPtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case FilelistInfo::PROP_NAME: {
			if (a->getType() == b->getType()) {
				return Util::DefaultSort(a->getName(), b->getName());
			}

			return a->isDirectory() ? -1 : 1;
		}
		case FilelistInfo::PROP_TYPE: {
			if (a->getType() != b->getType()) {
				// Directories go first
				return a->getType() == FilelistItemInfo::FILE ? 1 : -1;
			}

			if (!a->isDirectory() && !b->isDirectory()) {
				auto dirsA = a->dir->getFolderCount();
				auto dirsB = b->dir->getFolderCount();
				if (dirsA != dirsB) {
					return compare(dirsA, dirsB);
				}

				auto filesA = a->dir->getFileCount();
				auto filesB = b->dir->getFileCount();

				return compare(filesA, filesB);
			}

			return Util::DefaultSort(Util::getFileExt(a->getName()), Util::getFileExt(b->getName()));
		}
		default: dcassert(0); return 0;
		}
	}

	std::string FilelistUtils::getStringInfo(const FilelistItemInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case FilelistInfo::PROP_NAME: return aItem->getName();
		case FilelistInfo::PROP_PATH: return Util::toAdcFile(aItem->getPath());
		case FilelistInfo::PROP_TYPE: {
			if (aItem->isDirectory()) {
				return Format::formatFolderContent(aItem->dir->getFileCount(), aItem->dir->getFolderCount());
			} else {
				return Format::formatFileType(aItem->getPath());
			}
		}
		case FilelistInfo::PROP_TTH: return aItem->getType() == FilelistItemInfo::FILE ? aItem->file->getTTH().toBase32() : Util::emptyString;
		default: dcassert(0); return Util::emptyString;
		}
	}

	double FilelistUtils::getNumericInfo(const FilelistItemInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case FilelistInfo::PROP_SIZE: return (double)aItem->getSize();
		case FilelistInfo::PROP_DATE: return (double)aItem->getDate();
		case FilelistInfo::PROP_DUPE: return (double)aItem->getDupe();
		default: dcassert(0); return 0;
		}
	}
}