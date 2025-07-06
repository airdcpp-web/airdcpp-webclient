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

#ifndef DCPLUSPLUS_DCPP_SHAREPATH_VALIDATOR_H
#define DCPLUSPLUS_DCPP_SHAREPATH_VALIDATOR_H

#include <airdcpp/core/ActionHook.h>
#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/core/io/File.h>
#include <airdcpp/util/text/StringMatch.h>

#include <airdcpp/core/header/typedefs.h>

namespace dcpp {


// STANDARD_EXCEPTION(ShareValidatorException);

enum ShareValidatorErrorType {
	TYPE_FORBIDDEN_GENERIC,
	TYPE_CONFIG_BOOLEAN,
	TYPE_CONFIG_ADJUSTABLE,
	// TYPE_QUEUE,
	TYPE_EXCLUDED,
	TYPE_HOOK,
};

class ShareValidatorException: public Exception {
public:
	ShareValidatorException(const string& aMessage, ShareValidatorErrorType aType) : Exception(aMessage), type(aType) {

	}

	ShareValidatorErrorType getType() const noexcept {
		return type;
	}

	static bool isReportableError(ShareValidatorErrorType aType) noexcept;
private:
	const ShareValidatorErrorType type;
};

class SharePathValidator {
public:
	ActionHook<nullptr_t, const string&, int64_t> fileValidationHook;
	ActionHook<nullptr_t, const string&> directoryValidationHook;
	ActionHook<nullptr_t, const string&, bool /* aNewParent */> newDirectoryValidationHook;
	ActionHook<nullptr_t, const string&, int64_t, bool /* aNewParent */> newFileValidationHook;

	using RootPointParser = std::function<string(const string&)>;
	SharePathValidator(RootPointParser&& aRootPointParser);

	// Get a list of excluded real paths
	StringSet getExcludedPaths() const noexcept;
	void setExcludedPaths(const StringSet& aPaths) noexcept;

	// Add an excluded path
	// Throws ShareException if validation fails
	void addExcludedPath(const string& aPath);
	bool removeExcludedPath(const string& aPath) noexcept;

	// Prepares the skiplist regex after the pattern has been changed
	void reloadSkiplist();

	// Check if a directory/file name matches skiplist
	bool matchSkipList(const string& aName) const noexcept;

	void saveExcludes(SimpleXML& xml) const noexcept;
	void loadExcludes(SimpleXML& xml) noexcept;

	// Check that the root path is valid to be added in share
	// Use checkSharedName for non-root directories
	void validateRootPath(const string& aRealPath) const;

	// Check the list of new directory path tokens relative to the base path
	// Throws ShareValidatorException/QueueException in case of errors
	// FileException is thrown if some of the directories don't exist
	void validateNewDirectoryPathTokensHooked(const string& aBasePath, const StringList& aTokens, bool aSkipQueueCheck, CallerPtr aCaller) const;

	// Check a single directory/file item
	// Throws ShareValidatorException/QueueException in case of errors
	void validateHooked(const FileItemInfoBase& aFileItem, const string& aPath, bool aSkipQueueCheck, CallerPtr aCaller, bool aIsNew, bool aNewParent) const;

	// Check a new file/directory path
	// Throws ShareValidatorException/QueueException in case of errors
	// FileException is thrown if the file doesn't exist
	void validateNewPathHooked(const string& aPath, bool aSkipQueueCheck, bool aNewParent, CallerPtr aCaller) const;
private:
	// Comprehensive check for a directory/file whether it is valid to be added in share
	// Use validateRootPath for new root directories instead
	// Throws ShareValidatorException in case of errors
	void checkSharedName(const string& aPath, bool aIsDirectory, int64_t aSize = 0) const;

	bool isExcluded(const string& aPath) const noexcept;

	StringMatch skipList;
	string winDir;

	// Excluded paths with exact casing
	// Use refreshMatcherCS for locking
	StringSet excludedPaths;

	RootPointParser rootPointParser;

	mutable SharedMutex cs;
};


} // namespace dcpp

#endif // !defined(SHARE_MANAGER_H)
