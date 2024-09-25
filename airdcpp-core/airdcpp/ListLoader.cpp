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

#include <airdcpp/ListLoader.h>

#include <airdcpp/DupeUtil.h>
#include <airdcpp/PathUtil.h>
#include <airdcpp/ResourceManager.h>
#include <airdcpp/SimpleXML.h>


namespace dcpp {

using ranges::for_each;
using ranges::find_if;

ListLoader::ListLoader(DirectoryListing* aList, const string& aBase,
	bool aUpdating, time_t aListDownloadDate) :
	list(aList), cur(aList->getRoot().get()), base(aBase), updating(aUpdating),
	partialList(aList->getPartialList()), listDownloadDate(aListDownloadDate) {
}

void ListLoader::validateName(const string_view& aName) {
	if (aName.empty()) {
		throw SimpleXMLException("Name attribute missing");
	}

	if (aName == "." || aName == "..") {
		throw SimpleXMLException("Forbidden filename");
	}

	if (aName.find(ADC_SEPARATOR) != string::npos) {
		throw SimpleXMLException("Filenames can't contain path separators");
	}
}

static const string sFileListing = "FileListing";
static const string sBase = "Base";
static const string sBaseDate = "BaseDate";
static const string sGenerator = "Generator";
static const string sDirectory = "Directory";
static const string sIncomplete = "Incomplete";
static const string sDirectories = "Directories";
static const string sFiles = "Files";
static const string sFile = "File";
static const string sName = "Name";
static const string sSize = "Size";
static const string sTTH = "TTH";
static const string sDate = "Date";

void ListLoader::loadFile(StringPairList& attribs, bool) {
	const string& n = getAttrib(attribs, sName, 0);
	validateName(n);

	const string& s = getAttrib(attribs, sSize, 1);
	if (s.empty())
		return;

	auto size = Util::toInt64(s);

	const string& h = getAttrib(attribs, sTTH, 2);
	if (h.empty())
		return;

	TTHValue tth(h); /// @todo verify validity?

	auto f = make_shared<DirectoryListing::File>(cur, n, size, tth, Util::parseRemoteFileItemDate(getAttrib(attribs, sDate, 3)));
	cur->files.push_back(f);
}

DirectoryListing::Directory::DirType ListLoader::parseDirectoryType(bool aIncomplete, const DirectoryContentInfo& aContentInfo) noexcept {
	if (!aIncomplete) {
		return DirectoryListing::Directory::TYPE_NORMAL;
	}

	if (aContentInfo.directories > 0)
		return DirectoryListing::Directory::TYPE_INCOMPLETE_CHILD;
	
	return DirectoryListing::Directory::TYPE_INCOMPLETE_NOCHILD;
}

void ListLoader::loadDirectory(StringPairList& attribs, bool) {
	const string& name = getAttrib(attribs, sName, 0);
	validateName(name);

	bool incomplete = getAttrib(attribs, sIncomplete, 1) == "1";
	auto& directoriesStr = getAttrib(attribs, sDirectories, 2);
	auto& filesStr = getAttrib(attribs, sFiles, 3);

	auto contentInfo(DirectoryContentInfo::empty());
	if (!incomplete || !filesStr.empty() || !directoriesStr.empty()) {
		contentInfo = DirectoryContentInfo(Util::toInt(directoriesStr), Util::toInt(filesStr));
	}

	const string& size = getAttrib(attribs, sSize, 2);
	const string& date = getAttrib(attribs, sDate, 3);

	DirectoryListing::DirectoryPtr d = nullptr;
	if (updating) {
		dirsLoaded++;

		auto i = cur->directories.find(&name);
		if (i != cur->directories.end()) {
			d = i->second;
		}
	}

	if (!d) {
		auto type = parseDirectoryType(incomplete, contentInfo);
		d = DirectoryListing::Directory::create(cur, name, type, listDownloadDate, contentInfo, size, Util::parseRemoteFileItemDate(date));
	} else {
		if (!incomplete) {
			d->setComplete();
		}
		d->setRemoteDate(Util::parseRemoteFileItemDate(date));
	}
	cur = d.get();
}

void ListLoader::loadListing(StringPairList& attribs, bool) {
	if (updating) {
		const string& b = getAttrib(attribs, sBase, 2);
		dcassert(PathUtil::isAdcDirectoryPath(base));

		// Validate the parsed base path
		{
			if (Util::stricmp(b, base) != 0) {
				throw AbortException("The base directory specified in the file list (" + b + ") doesn't match with the expected base (" + base + ")");
			}
		}

		cur = list->createBaseDirectory(base, listDownloadDate).get();

		dcassert(list->findDirectoryUnsafe(base));

		const string& baseDate = getAttrib(attribs, sBaseDate, 3);
		cur->setRemoteDate(Util::parseRemoteFileItemDate(baseDate));
	}

	// Set the root complete only after we have finished loading 
	// This will prevent possible problems, such as GUI counting the size of this folder

	inListing = true;
}

void ListLoader::startTag(const string& aName, StringPairList& attribs, bool aSimple) {
	if(list->getClosing()) {
		throw AbortException();
	}

	if (inListing) {
		if (aName == sFile) {
			loadFile(attribs, aSimple);
		} else if (aName == sDirectory) {
			loadDirectory(attribs, aSimple);

			if (aSimple) {
				// To handle <Directory Name="..." />
				endTag(aName);
			}
		}
	} else if (aName == sFileListing) {
		loadListing(attribs, aSimple);

		if (aSimple) {
			// To handle <FileListing Base="..." />
			endTag(aName);
		}
	}
}

void ListLoader::endTag(const string& aName) {
	if(inListing) {
		if(aName == sDirectory) {
			cur = cur->getParent();
		} else if (aName == sFileListing) {
			// Cur should be the loaded base path now

			cur->setComplete();

			if (list->loadHooks && list->loadHooks->hasSubscribers()) {
				list->updateStatus(STRING(RUNNING_HOOKS));
				runHooksRecursive(list->getRoot());
			}

			// Content info is not loaded for the base path
			cur->setContentInfo(cur->getContentInfoRecursive(false));

			inListing = false;
		}
	}
}

void ListLoader::runHooksRecursive(const DirectoryListing::DirectoryPtr& aDir) noexcept {
	if (!list->loadHooks || list->closing) {
		return;
	}

	// Directories

	std::erase_if(aDir->directories, [this](auto& dp) {
		auto error = list->loadHooks->directoryLoadHook.runHooksError(this, dp.second, *list);
		if (error) {
			dcdebug("Hook rejection for filelist directory %s (%s)\n", dp.second->getAdcPathUnsafe().c_str(), ActionHookRejection::formatError(error).c_str());
		}

		return error;
	});

	// Files
	std::erase_if(aDir->files, [this](const auto& f) {
		if (auto error = list->loadHooks->fileLoadHook.runHooksError(this, f, *list)) {
			dcdebug("Hook rejection for filelist file %s (%s)\n", f->getAdcPathUnsafe().c_str(), ActionHookRejection::formatError(error).c_str());
			return true;
		}

		return false;
	});

	// Children
	if (aDir->findCompleteChildren()) {
		parallel_for_each(aDir->directories.begin(), aDir->directories.end(), [this](const auto& d) {
			runHooksRecursive(d.second);
		});
	}
}

} // namespace dcpp
