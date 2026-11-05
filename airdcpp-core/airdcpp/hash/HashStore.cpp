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

#include <airdcpp/hash/HashStore.h>
#include <airdcpp/DCPlusPlus.h>
#include <airdcpp/core/io/File.h>
#include <airdcpp/hash/HashedFile.h>
#include <airdcpp/core/io/db/LevelDB.h>
#include <airdcpp/events/LogManager.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/queue/QueueManager.h>
#include <airdcpp/share/ShareManager.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/util/Util.h>


#define FILEINDEX_VERSION 1
#define HASHDATA_VERSION 1

namespace dcpp {

HashStore::HashStore() {
}

HashStore::~HashStore() {
	closeDb();
}

void HashStore::load(StartupLoader& aLoader) {
	// Open the new database
	openDb(aLoader);
}

void HashStore::openDb(StartupLoader& aLoader) {
	auto hashDataPath = AppUtil::getPath(AppUtil::PATH_USER_CONFIG) + "HashData" + PATH_SEPARATOR;
	auto fileIndexPath = AppUtil::getPath(AppUtil::PATH_USER_CONFIG) + "FileIndex" + PATH_SEPARATOR;

	File::ensureDirectory(hashDataPath);
	File::ensureDirectory(fileIndexPath);

	AppUtil::migrate(fileIndexPath, "*");
	AppUtil::migrate(hashDataPath, "*");

	auto cacheSize = static_cast<uint32_t>(Util::convertSize(max(SETTING(DB_CACHE_SIZE), 1), Util::MB));
	auto blockSize = File::getBlockSize(AppUtil::getPath(AppUtil::PATH_USER_CONFIG));

	try {
		// Use the file system block size in here. Using a block size smaller than that reduces the performance significantly especially when writing a lot of data (e.g. when migrating the data)
		// The default cache size of 8 MB is able to hold approximately 256-512 trees with the block size of 16KB which should be enough for most common transfers (should the size be increased with larger block size?)
		// The number of open files doesn't matter here since the tree lookups are very much random (20 is the minimum allowed by LevelDB). The data won't compress so no need to even try it.
		hashDb = make_unique<LevelDB>(hashDataPath, STRING(HASH_DATA), cacheSize, 20, false, max(static_cast<int64_t>(16 * 1024), blockSize));

		// Use a large block size and allow more open files because the reads are nearly sequential in here (but done with multiple threads). 
		// The default database sorting isn't perfect when having files and folders mixed within the same directory but that shouldn't be a big issue (avoid using custom comparison function for now...)
		fileDb = make_unique<LevelDB>(fileIndexPath, STRING(FILE_INDEX), cacheSize, 50, true, 64 * 1024);

		hashDb->open(aLoader.stepF, aLoader.messageF);
		fileDb->open(aLoader.stepF, aLoader.messageF);
	} catch (const DbException& e) {
		// Can't continue without hash database, abort startup
		throw AbortException(e.getError());
	}
}

void HashStore::closeDb() noexcept {
	hashDb.reset(nullptr);
	fileDb.reset(nullptr);
}

void HashStore::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(HASH_DATABASE));
}

void HashStore::addHashedFile(const string& aFilePath, const TigerTree& tt, const HashedFile& fi_) {
	addTree(tt);
	addFile(aFilePath, fi_);
}

void HashStore::addFile(const string& aFilePath, const HashedFile& fi_) {
	auto sz = getFileInfoSize(fi_);
	void* buf = malloc(sz);
	saveFileInfo(buf, fi_);

	try {
		fileDb->put((void*)aFilePath.c_str(), aFilePath.length(), (void*)buf, sz);
	} catch (const DbException& e) {
		free(buf);
		throw HashException(STRING_F(WRITE_FAILED_X, fileDb->getNameLower() % e.getError()));
	}

	free(buf);
}

void HashStore::removeFile(const string& aFilePath) {
	try {
		fileDb->remove((void*)aFilePath.c_str(), aFilePath.length());
	} catch (const DbException& e) {
		throw HashException(STRING_F(WRITE_FAILED_X, fileDb->getNameLower() % e.getError()));
	}
}


void HashStore::renameFileThrow(const string& aOldPath, const string& aNewPath) {
	// Check the old file
	HashedFile hashedFile;
	if (!getFileInfo(aOldPath, hashedFile)) {
		throw HashException("Path " + aOldPath + " doesn't exist in hash database");
	}

	try {
		FileItem newFileInfo(aNewPath);

		// Check the size of the new file
		if (newFileInfo.getSize() != hashedFile.getSize()) {
			throw HashException("Size of " + aOldPath + " (" + Util::toString(hashedFile.getSize()) + ") differs from the size of " + aNewPath + "(" + Util::toString(newFileInfo.getSize()) + ")");
		}

		// Update timestamp for the new database entry
		hashedFile.setTimeStamp(newFileInfo.getLastWriteTime());
	} catch (const FileException& e) {
		throw HashException("Could not open path " + aNewPath + ": " + e.getError());
	}

	// Rename (exact paths; getFileInfo may have rewritten a legacy lower key already)
	removeFile(aOldPath);
	addFile(aNewPath, hashedFile);
}

void HashStore::addTree(const TigerTree& tt) {
	size_t treelen = tt.getLeaves().size() == 1 ? 0 : tt.getLeaves().size() * TTHValue::BYTES;
	auto sz = sizeof(uint8_t) + sizeof(int64_t) + sizeof(int64_t) + treelen;

	//allocate the memory
	void* buf = malloc(sz);


	//set the data
	char* p = (char*)buf;

	uint8_t version = HASHDATA_VERSION;
	memcpy(p, &version, sizeof(uint8_t));
	p += sizeof(uint8_t);

	int64_t fileSize = tt.getFileSize();
	memcpy(p, &fileSize, sizeof(int64_t));
	p += sizeof(int64_t);

	int64_t blockSize = tt.getBlockSize();
	memcpy(p, &blockSize, sizeof(int64_t));
	p += sizeof(int64_t);

	if (treelen > 0)
		memcpy(p, tt.getLeaves()[0].data, treelen);

	//throw HashException(STRING_F(WRITE_FAILED_X, hashDb->getNameLower() % "TEST"));
	try {
		hashDb->put((void*)tt.getRoot().data, sizeof(TTHValue), buf, sz);
	} catch (const DbException& e) {
		free(buf);
		throw HashException(STRING_F(WRITE_FAILED_X, hashDb->getNameLower() % e.getError()));
	}

	free(buf);
}

bool HashStore::getTree(const TTHValue& aRoot, TigerTree& tt_) {
	try {
		return hashDb->get((void*)aRoot.data, sizeof(TTHValue), 100 * 1024, [&](void* aValue, size_t valueLen) {
			return loadTree(aValue, valueLen, aRoot, tt_, true);
		});
	} catch (const DbException& e) {
		log(STRING_F(READ_FAILED_X, hashDb->getNameLower() % e.getError()), LogMessage::SEV_ERROR);
	}

	return false;
}

bool HashStore::hasTree(const TTHValue& aRoot) {
	bool ret = false;
	try {
		ret = hashDb->hasKey((void*)aRoot.data, sizeof(TTHValue));
	} catch (const DbException& e) {
		throw HashException(STRING_F(READ_FAILED_X, hashDb->getNameLower() % e.getError()));
	}

	return ret;
}

bool HashStore::loadTree(const void* src, size_t len, const TTHValue& aRoot, TigerTree& aTree, bool aReportCorruption) {
	if (len < sizeof(uint8_t) + sizeof(int64_t) + sizeof(int64_t))
		return false;

	char* p = (char*)src;

	uint8_t version;
	memcpy(&version, p, sizeof(uint8_t));
	p += sizeof(uint8_t);

	if (version > HASHDATA_VERSION) {
		return false;
	}

	int64_t fileSize;
	memcpy(&fileSize, p, sizeof(int64_t));
	p += sizeof(int64_t);

	int64_t blockSize;
	memcpy(&blockSize, p, sizeof(int64_t));
	p += sizeof(int64_t);

	size_t datalen = len - sizeof(uint8_t) - sizeof(int64_t) - sizeof(int64_t);
	if (datalen > 0) {
		dcassert(datalen % TTHValue::BYTES == 0);
		boost::scoped_array<uint8_t> buf(new uint8_t[datalen]);
		memcpy(&buf[0], p, datalen);
		aTree = TigerTree(fileSize, blockSize, &buf[0]);
		if (aTree.getRoot() != aRoot) {
			if (aReportCorruption) {
				log(STRING_F(TREE_LOAD_FAILED_DB, aRoot.toBase32() % STRING(INVALID_TREE) % "/verifydb"), LogMessage::SEV_ERROR);
			}
			return false;
		}
	} else {
		aTree = TigerTree(fileSize, blockSize, aRoot);
	}
	return true;
}

bool HashStore::loadFileInfo(const void* src, size_t len, HashedFile& aFile) {
	if (len != sizeof(HashedFile) + 1)
		return false;

	char* p = (char*)src;

	uint8_t version;
	memcpy(&version, p, sizeof(uint8_t));
	p += sizeof(uint8_t);

	if (version > FILEINDEX_VERSION) {
		return false;
	}

	uint64_t timeStamp;
	memcpy(&timeStamp, p, sizeof(uint64_t));
	p += sizeof(uint64_t);

	TTHValue root;
	memcpy(&root.data, p, sizeof(TTHValue));
	p += sizeof(TTHValue);

	int64_t fileSize;
	memcpy(&fileSize, p, sizeof(int64_t));
	p += sizeof(int64_t);

	aFile = HashedFile(root, timeStamp, fileSize);
	return true;
}

void HashStore::saveFileInfo(void* dest, const HashedFile& aFile) {
	char* p = (char*)dest;

	uint8_t version = FILEINDEX_VERSION;
	memcpy(p, &version, sizeof(uint8_t));
	p += sizeof(uint8_t);

	uint64_t timeStamp = aFile.getTimeStamp();
	memcpy(p, &timeStamp, sizeof(uint64_t));
	p += sizeof(uint64_t);

	TTHValue root = aFile.getRoot();
	memcpy(p, &root.data, sizeof(TTHValue));
	p += sizeof(TTHValue);

	int64_t fileSize = aFile.getSize();
	memcpy(p, &fileSize, sizeof(int64_t));
	//p += sizeof(int64_t);
}

uint32_t HashStore::getFileInfoSize(const HashedFile& /*aTree*/) {
	return sizeof(uint8_t) + sizeof(uint64_t) + sizeof(TTHValue) + sizeof(int64_t);
}

int64_t HashStore::getRootInfo(const TTHValue& root, InfoType aType) noexcept {
	int64_t ret = 0;
	try {
		hashDb->get((void*)root.data, sizeof(TTHValue), 100 * 1024, [&](void* aValue, size_t /*valueLen*/) {
			char* p = (char*)aValue;

			uint8_t version;
			memcpy(&version, p, sizeof(uint8_t));
			p += sizeof(uint8_t);

			if (version > HASHDATA_VERSION) {
				return false;
			}

			p += (aType == TYPE_FILESIZE ? 0 : sizeof(int64_t));

			memcpy(&ret, p, sizeof(ret));
			return true;
		});
	} catch (const DbException& e) {
		log(STRING_F(READ_FAILED_X, hashDb->getNameLower() % e.getError()), LogMessage::SEV_ERROR);
	}
	return ret;
}

bool HashStore::checkTTH(const string& aFilePath, HashedFile& fi_) noexcept {
	auto initialTime = fi_.getTimeStamp();
	auto initialSize = fi_.getSize();

	if (getFileInfo(aFilePath, fi_)) {
		if (fi_.getTimeStamp() == initialTime && fi_.getSize() == initialSize) {
			return true;
		}
	}

	return false;
}

bool HashStore::getFileInfo(const string& aFilePath, HashedFile& fi_) noexcept {
	try {
		auto load = [&](void* aValue, size_t valueLen) {
			return loadFileInfo(aValue, valueLen, fi_);
		};

		if (fileDb->get((void*)aFilePath.c_str(), aFilePath.length(), sizeof(HashedFile), load)) {
			return true;
		}

		// Legacy transition: keys were Text::toLower(path); rewrite lazily to the exact path
		auto lower = Text::toLower(aFilePath);
		if (lower != aFilePath && fileDb->get((void*)lower.c_str(), lower.length(), sizeof(HashedFile), load)) {
			try {
				auto sz = getFileInfoSize(fi_);
				vector<char> buf(sz);
				saveFileInfo(buf.data(), fi_);
				fileDb->put((void*)aFilePath.c_str(), aFilePath.length(), buf.data(), sz);
				fileDb->remove((void*)lower.c_str(), lower.length());
			} catch (const DbException&) {
				// Keep the hit even if the rewrite fails; migration/optimize can retry later
			}
			return true;
		}
	} catch (const DbException& e) {
		log(STRING_F(READ_FAILED_X, fileDb->getNameLower() % e.getError()), LogMessage::SEV_ERROR);
	}

	return false;
}

void HashStore::migrateCaseKeys() noexcept {
	if (!fileDb) {
		return;
	}

	auto marker = fileDb->getPath() + "CASEKEYS_MIGRATED";
	if (PathUtil::fileExists(marker)) {
		return;
	}

	size_t kept = 0;
	size_t rewritten = 0;
	size_t removed = 0;

	try {
		unique_ptr<DbSnapshot> snapshot(fileDb->getSnapshot());

		// Collect under a stable snapshot; apply in bounded chunks (existing put/remove)
		constexpr size_t chunkSize = 1000;
		vector<pair<string, string>> rewrites; // newKey -> value
		vector<string> deletes;

		auto flush = [&]() {
			for (auto& [newKey, value] : rewrites) {
				// Prefer the first entry if an exact key already exists
				if (!fileDb->hasKey((void*)newKey.c_str(), newKey.length())) {
					fileDb->put((void*)newKey.c_str(), newKey.length(), (void*)value.data(), value.size());
				}
			}
			for (auto& key : deletes) {
				fileDb->remove((void*)key.c_str(), key.length());
			}
			rewrites.clear();
			deletes.clear();
		};

		fileDb->forEach([&](void* aKey, size_t keyLen, void* aValue, size_t valueLen) {
			string key(static_cast<char*>(aKey), keyLen);
			string realPath;
			auto found = ShareManager::getInstance()->findFileByRealPath(key, [&](const ShareDirectory::File& aFile) {
				realPath = aFile.getRealPath();
			});

			if (!found) {
				deletes.push_back(key);
				++removed;
			} else if (realPath != key) {
				rewrites.emplace_back(realPath, string(static_cast<char*>(aValue), valueLen));
				deletes.push_back(key);
				++rewritten;
			} else {
				++kept;
			}

			if (rewrites.size() + deletes.size() >= chunkSize) {
				flush();
			}
			return true;
		}, snapshot.get());

		flush();

		if (!File::createFile(marker)) {
			log("Failed to write FileIndex case-key migration marker", LogMessage::SEV_WARNING);
			return;
		}

		dcdebug("FileIndex migrate: kept=%zu rewritten=%zu removed=%zu\n", kept, rewritten, removed);
		log("FileIndex case-key migration finished (kept=" + Util::toString(kept) +
			", rewritten=" + Util::toString(rewritten) +
			", removed=" + Util::toString(removed) + ")", LogMessage::SEV_INFO);
	} catch (const DbException& e) {
		log(STRING_F(READ_FAILED_X, fileDb->getNameLower() % e.getError()), LogMessage::SEV_ERROR);
		log(STRING(HASHDB_MAINTENANCE_FAILED), LogMessage::SEV_ERROR);
	}
}

void HashStore::optimize(bool doVerify) noexcept {

	int unusedTrees = 0;
	int failedTrees = 0;
	int unusedFiles = 0;
	int validFiles = 0;
	int validTrees = 0;
	int missingTrees = 0;
	int removedFiles = 0;
	int64_t failedSize = 0;

	log(STRING(HASHDB_MAINTENANCE_STARTED), LogMessage::SEV_INFO);

	{
		unordered_set<TTHValue> usedRoots;

		//make sure that the databases stay in sync so that trees added during this operation won't get removed
		unique_ptr<DbSnapshot> fileSnapshot(fileDb->getSnapshot());
		unique_ptr<DbSnapshot> hashSnapshot(hashDb->getSnapshot());

		HashedFile fi;
		string path;

		// lookup each item in file index from the share
		try {
			fileDb->remove_if([&](void* aKey, size_t key_len, void* aValue, size_t valueLen) {
				path = string((const char*)aKey, key_len);
				if (ShareManager::getInstance()->isRealPathShared(path)) {
					if (!loadFileInfo(aValue, valueLen, fi))
						return true;

					usedRoots.emplace(fi.getRoot());
					validFiles++;
					return false;
				}
				else {
					unusedFiles++;
					return true;
				}
				}, fileSnapshot.get());
		} catch (const DbException& e) {
			log(STRING_F(READ_FAILED_X, fileDb->getNameLower() % e.getError()), LogMessage::SEV_ERROR);
			log(STRING(HASHDB_MAINTENANCE_FAILED), LogMessage::SEV_ERROR);
			return;
		}

		//remove trees that aren't shared or queued and optionally check whether each tree can be loaded
		TigerTree tt;
		TTHValue curRoot;
		try {
			hashDb->remove_if([&](void* aKey, size_t key_len, void* aValue, size_t valueLen) {
				memcpy(&curRoot.data, aKey, key_len);
				auto i = usedRoots.find(curRoot);
				if (i == usedRoots.end() && !QueueManager::getInstance()->isFileQueued(curRoot)) {
					//not needed
					unusedTrees++;
					return true;
				}

				if (!doVerify || loadTree(aValue, valueLen, curRoot, tt, false)) {
					//valid tree
					if (i != usedRoots.end())
						usedRoots.erase(i);
					validTrees++;
					return false;
				}

				//failed to load it
				failedTrees++;
				return true;
			}, hashSnapshot.get());
		} catch (const DbException& e) {
			log(STRING_F(READ_FAILED_X, hashDb->getNameLower() % e.getError()), LogMessage::SEV_ERROR);
			log(STRING(HASHDB_MAINTENANCE_FAILED), LogMessage::SEV_ERROR);
			return;
		}

		//remove file entries that don't have a corresponding hash data entry
		missingTrees = static_cast<int>(usedRoots.size()) - failedTrees;
		if (!usedRoots.empty()) {
			try {
				fileDb->remove_if([&](void* /*aKey*/, size_t /*key_len*/, void* aValue, size_t valueLen) {
					loadFileInfo(aValue, valueLen, fi);
					if (usedRoots.contains(fi.getRoot())) {
						failedSize += fi.getSize();
						validFiles--;
						removedFiles++;
						return true;
					}

					return false;
				}, fileSnapshot.get());
			} catch (const DbException& e) {
				log(STRING_F(READ_FAILED_X, fileDb->getNameLower() % e.getError()), LogMessage::SEV_ERROR);
				log(STRING(HASHDB_MAINTENANCE_FAILED), LogMessage::SEV_ERROR);
				return;
			}
		}
	}

	SettingsManager::getInstance()->set(SettingsManager::CUR_REMOVED_FILES, SETTING(CUR_REMOVED_FILES) + unusedFiles + missingTrees);
	if (validFiles == 0 || (static_cast<double>(SETTING(CUR_REMOVED_FILES)) / static_cast<double>(validFiles)) > 0.05) {
		log(STRING_F(COMPACTING_X, fileDb->getNameLower()), LogMessage::SEV_INFO);
		fileDb->compact();
		SettingsManager::getInstance()->set(SettingsManager::CUR_REMOVED_FILES, 0);
	}

	SettingsManager::getInstance()->set(SettingsManager::CUR_REMOVED_TREES, SETTING(CUR_REMOVED_TREES) + unusedTrees + failedTrees);
	if (validTrees == 0 || (static_cast<double>(SETTING(CUR_REMOVED_TREES)) / static_cast<double>(validTrees)) > 0.05) {
		log(STRING_F(COMPACTING_X, hashDb->getNameLower()), LogMessage::SEV_INFO);
		hashDb->compact();
		SettingsManager::getInstance()->set(SettingsManager::CUR_REMOVED_TREES, 0);
	}

	string msg;
	if (unusedFiles > 0 || unusedTrees > 0) {
		msg = STRING_F(HASHDB_MAINTENANCE_UNUSED, unusedFiles % unusedTrees);
	} else {
		msg = STRING(HASHDB_MAINTENANCE_NO_UNUSED);
	}

	log(msg, LogMessage::SEV_INFO);

	if (failedTrees > 0 || missingTrees > 0) {
		if (doVerify) {
			msg = STRING_F(REBUILD_FAILED_ENTRIES_VERIFY, missingTrees % failedTrees);
		} else {
			msg = STRING_F(REBUILD_FAILED_ENTRIES_OPTIMIZE, missingTrees);
		}

		msg += ". " + STRING_F(REBUILD_REFRESH_PROMPT, Util::formatBytes(failedSize));
		log(msg, LogMessage::SEV_ERROR);
	}
}

void HashStore::compact() noexcept {
	log(STRING_F(COMPACTING_X, fileDb->getNameLower()), LogMessage::SEV_INFO);
	fileDb->compact();
	log(STRING_F(COMPACTING_X, hashDb->getNameLower()), LogMessage::SEV_INFO);
	hashDb->compact();
	log("Done", LogMessage::SEV_INFO);
}

string HashStore::getDbStats() noexcept {
	string statMsg;

	statMsg += fileDb->getStats();
	statMsg += "Deleted entries since last compaction: " + Util::toString(SETTING(CUR_REMOVED_FILES)) + " (" + Util::toString(((double)SETTING(CUR_REMOVED_FILES) / (double)fileDb->size(false)) * 100) + "%)";
	statMsg += "\r\n\r\n";

	statMsg += hashDb->getStats();
	statMsg += "Deleted entries since last compaction: " + Util::toString(SETTING(CUR_REMOVED_TREES)) + " (" + Util::toString(((double)SETTING(CUR_REMOVED_TREES) / (double)hashDb->size(false)) * 100) + "%)";
	statMsg += "\r\n\r\n";
	statMsg += "\n\nDisk block size: " + Util::formatBytes(File::getBlockSize(hashDb->getPath())) + "\n\n";
	return statMsg;
}

void HashStore::onScheduleRepair(bool aSchedule) {
	if (aSchedule) {
		File::createFile(hashDb->getRepairFlag());
		File::createFile(fileDb->getRepairFlag());
	} else {
		File::deleteFile(hashDb->getRepairFlag());
		File::deleteFile(fileDb->getRepairFlag());
	}
}

bool HashStore::isRepairScheduled() const noexcept {
	return PathUtil::fileExists(hashDb->getRepairFlag()) && PathUtil::fileExists(fileDb->getRepairFlag());
}

void HashStore::getDbSizes(int64_t& fileDbSize_, int64_t& hashDbSize_) const noexcept {
	fileDbSize_ = fileDb->getSizeOnDisk();
	hashDbSize_ = hashDb->getSizeOnDisk();
}

}
