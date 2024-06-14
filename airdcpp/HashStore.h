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

#ifndef DCPLUSPLUS_DCPP_HASH_STORE_H
#define DCPLUSPLUS_DCPP_HASH_STORE_H

#include <functional>
#include "typedefs.h"

#include "DbHandler.h"
#include "MerkleTree.h"
#include "Message.h"

namespace dcpp {

class HashedFile;

class HashStore {
public:
	HashStore();
	~HashStore();

	void addHashedFile(const string& aFilePathLower, const TigerTree& tt, const HashedFile& fi_);
	void addFile(const string& aFilePathLower, const HashedFile& fi_);
	void removeFile(const string& aFilePathLower);

	// Rename a file in the database
	// Throws HashException
	void renameFileThrow(const string& aOldPath, const string& aNewPath);

	void load(StartupLoader& aLoader);

	void optimize(bool doVerify) noexcept;

	bool checkTTH(const string& aFileNameLower, HashedFile& fi_) noexcept;

	void addTree(const TigerTree& tt);
	bool getFileInfo(const string& aFileLower, HashedFile& aFile) noexcept;
	bool getTree(const TTHValue& root, TigerTree& tth);
	bool hasTree(const TTHValue& root);

	enum InfoType {
		TYPE_FILESIZE,
		TYPE_BLOCKSIZE
	};
	int64_t getRootInfo(const TTHValue& aRoot, InfoType aType) noexcept;

	string getDbStats() noexcept;

	void openDb(StartupLoader& aLoader);
	void closeDb() noexcept;

	void onScheduleRepair(bool aSchedule);
	bool isRepairScheduled() const noexcept;

	void getDbSizes(int64_t& fileDbSize_, int64_t& hashDbSize_) const noexcept;
	void compact() noexcept;

	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;
private:
	std::unique_ptr<DbHandler> fileDb;
	std::unique_ptr<DbHandler> hashDb;

	static bool loadTree(const void* src, size_t len, const TTHValue& aRoot, TigerTree& aTree, bool aReportCorruption);

	static bool loadFileInfo(const void* src, size_t len, HashedFile& aFile);
	static void saveFileInfo(void* dest, const HashedFile& aTree);
	static uint32_t getFileInfoSize(const HashedFile& aTree);
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_HASH_STORE_H)