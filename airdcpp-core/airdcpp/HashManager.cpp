/* 
 * Copyright (C) 2001-2021 Jacek Sieka, arnetheduck on gmail point com
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
#include "HashManager.h"

#include "DCPlusPlus.h"
#include "File.h"
#include "FileReader.h"
#include "LogManager.h"
#include "Hasher.h"
#include "QueueManager.h"
#include "ShareManager.h"
#include "ResourceManager.h"
#include "SimpleXMLReader.h"
#include "Util.h"
#include "version.h"
#include "ZUtils.h"

#include "LevelDB.h"

#define FILEINDEX_VERSION 1
#define HASHDATA_VERSION 1

namespace dcpp {

using boost::range::find_if;

HashManager::HashManager() {

}

HashManager::~HashManager() {
	optimizer.join();
}

void HashManager::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(HASHING));
}

bool HashManager::checkTTH(const string& aFileLower, const string& aFileName, HashedFile& fi_) {
	dcassert(Text::isLower(aFileLower));
	if (!store.checkTTH(aFileLower, fi_)) {
		hashFile(aFileName, aFileLower, fi_.getSize());
		return false;
	}

	return true;
}

void HashManager::getFileInfo(const string& aFileLower, const string& aFileName, HashedFile& fi_) {
	dcassert(Text::isLower(aFileLower));
	auto found = store.getFileInfo(aFileLower, fi_);
	if (!found) {
		auto size = File::getSize(aFileName);
		if (size >= 0) {
			hashFile(aFileName, aFileLower, size);
		}

		throw HashException();
	}
}
void HashManager::renameFileThrow(const string& aOldPath, const string& aNewPath) {
	return store.renameFileThrow(aOldPath, aNewPath);
}

void HashManager::HashStore::renameFileThrow(const string& aOldPath, const string& aNewPath) {
	auto oldPathLower = Text::toLower(aOldPath);
	auto newPathLower = Text::toLower(aNewPath);

	// Check the old file
	HashedFile hashedFile;
	if (!getFileInfo(oldPathLower, hashedFile)) {
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

	// Rename
	removeFile(oldPathLower);
	addFile(newPathLower, hashedFile);
}

bool HashManager::getTree(const TTHValue& root, TigerTree& tt) noexcept {
	return store.getTree(root, tt);
}

size_t HashManager::getBlockSize(const TTHValue& root) noexcept {
	return static_cast<size_t>(store.getRootInfo(root, HashStore::TYPE_BLOCKSIZE));
}

int64_t HashManager::getMinBlockSize() noexcept {
	return Hasher::MIN_BLOCK_SIZE;
}

bool HashManager::hashFile(const string& filePath, const string& pathLower, int64_t size) {
	if (isShutdown) { //we cant allow adding more hashers if we are shutting down, it will result in infinite loop
		return false;
	}

	Hasher* h = nullptr;

	WLock l(Hasher::hcs);

	auto deviceId = File::getDeviceId(filePath);
	if (hashers.size() == 1 && !hashers.front()->hasDevices()) {
		//always use the first hasher if it's idle
		h = hashers.front();
	} else {
		auto getLeastLoaded = [](const HasherList& hl) {
			return min_element(hl.begin(), hl.end(), [](const Hasher* h1, const Hasher* h2) { return h1->getBytesLeft() < h2->getBytesLeft(); });
		};

		if (SETTING(HASHERS_PER_VOLUME) == 1) {
			//do we have files for this volume queued already? always use the same one in that case
			auto p = find_if(hashers, [&deviceId](const Hasher* aHasher) { return aHasher->hasDevice(deviceId); });
			if (p != hashers.end()) {
				h = *p;
			} else if (static_cast<int>(hashers.size()) >= SETTING(MAX_HASHING_THREADS)) {
				// can't create new ones
				h = *getLeastLoaded(hashers);
			}
		} else {
			//get the hashers with this volume
			HasherList volHashers;
			copy_if(hashers.begin(), hashers.end(), back_inserter(volHashers), [deviceId](const Hasher* aHasher) { return aHasher->hasDevice(deviceId); });

			if (volHashers.empty() && static_cast<int>(hashers.size()) >= SETTING(MAX_HASHING_THREADS)) {
				//we just need choose from all hashers
				h = *getLeastLoaded(hashers);
			} else if (!volHashers.empty()) {
				//check that the file isn't queued already
				auto p = find_if(volHashers, [&pathLower](const Hasher* aHasher) { return aHasher->hasFile(pathLower); });
				if (p != volHashers.end()) {
					return false;
				}

				auto minLoaded = getLeastLoaded(volHashers);

				//don't create new hashers if the file is less than 10 megabytes and there's a hasher with less than 200MB queued, or the maximum number of threads have been reached for this volume
				if (static_cast<int>(hashers.size()) >= SETTING(MAX_HASHING_THREADS) || (static_cast<int>(volHashers.size()) >= SETTING(HASHERS_PER_VOLUME) && 
					SETTING(HASHERS_PER_VOLUME) > 0) || (size <= Util::convertSize(10, Util::MB) && !volHashers.empty() && (*minLoaded)->getBytesLeft() <= Util::convertSize(200, Util::MB))) {

					//use the least loaded hasher that already has this volume
					h = *minLoaded;
				}
			}
		}

		if (!h) {
			//add a new one
			int id = 0;
			for (auto i: hashers) {
				if (i->hasherID != id)
					break;
				id++;
			}

			log(STRING_F(HASHER_X_CREATED, id), LogMessage::SEV_INFO);
			h = new Hasher(pausers > 0, id);
			hashers.push_back(h);
		}
	}

	//queue the file for hashing
	return h->hashFile(filePath, pathLower, size, deviceId);
}

void HashManager::getFileTTH(const string& aFile, int64_t aSize, bool addStore, TTHValue& tth_, int64_t& sizeLeft_, const bool& aCancel, std::function<void(int64_t, const string&)> updateF/*nullptr*/) {
	auto pathLower = Text::toLower(aFile);
	HashedFile fi(File::getLastModified(aFile), aSize);

	if (!store.checkTTH(pathLower, fi)) {
		File f(aFile, File::READ, File::OPEN);
		auto timestamp = f.getLastModified();
		if (timestamp < 0) {
			throw FileException(STRING(INVALID_MODIFICATION_DATE));
		}

		int64_t bs = max(TigerTree::calcBlockSize(aSize, 10), Hasher::MIN_BLOCK_SIZE);
		TigerTree tt(bs);

		auto start = GET_TICK();
		int64_t tickHashed = 0;

		FileReader fr(FileReader::ASYNC);
		fr.read(aFile, [&](const void* buf, size_t n) -> bool {
			tt.update(buf, n);

			if (updateF) {
				tickHashed += n;

				uint64_t end = GET_TICK();
				if (end - start > 1000) {
					sizeLeft_ -= tickHashed;
					auto lastSpeed = tickHashed * 1000 / (end - start);

					updateF(lastSpeed > 0 ? (sizeLeft_ / lastSpeed) : 0, aFile);

					tickHashed = 0;
					start = end;
				}
			}
			return !aCancel;
		});

		tt.finalize();
		tth_ = tt.getRoot();

		if (addStore && !aCancel) {
			fi = HashedFile(tth_, timestamp, aSize);
			store.addHashedFile(pathLower, tt, fi);
		}
	} else {
		tth_ = fi.getRoot();
	}
}

void HashManager::hasherDone(const string& aFileName, const string& pathLower, const TigerTree& tt, int64_t speed, HashedFile& aFileInfo, int hasherID /*0*/) noexcept {
	try {
		store.addHashedFile(pathLower, tt, aFileInfo);
	} catch (const Exception& e) {
		logHasher(STRING_F(HASHING_FAILED_X, e.getError()), hasherID, true, true);
	}
	
	if(SETTING(LOG_HASHING)) {
		string fn = aFileName;
		if (count(fn.begin(), fn.end(), PATH_SEPARATOR) >= 2) {
			string::size_type i = fn.rfind(PATH_SEPARATOR);
			i = fn.rfind(PATH_SEPARATOR, i - 1);
			fn.erase(0, i);
			fn.insert(0, "...");
		}
	
		if (speed > 0) {
			logHasher(STRING_F(HASHING_FINISHED_X, fn) + " (" + Util::formatBytes(speed) + "/s)", hasherID, false, true);
		} else {
			logHasher(STRING_F(HASHING_FINISHED_X, fn), hasherID, false, true);
		}
	}
}

bool HashManager::addFile(const string& aPath, const HashedFile& fi_) {
	//check that the file exists
	if (File::getSize(aPath) != fi_.getSize()) {
		return false;
	}

	//check that the tree exists
	if (fi_.getSize() < Hasher::MIN_BLOCK_SIZE) {
		TigerTree tt = TigerTree(fi_.getSize(), fi_.getSize(), fi_.getRoot());
		store.addTree(tt);
	} else if (!store.hasTree(fi_.getRoot())) {
		return false;
	}

	store.addFile(Text::toLower(aPath), fi_);
	return true;
}

void HashManager::HashStore::addHashedFile(const string& aFileLower, const TigerTree& tt, const HashedFile& fi_) {
	addTree(tt);
	addFile(aFileLower, fi_);
}

void HashManager::HashStore::addFile(const string& aFileLower, const HashedFile& fi_) {
	auto sz = getFileInfoSize(fi_);
	void* buf = malloc(sz);
	saveFileInfo(buf, fi_);

	try {
		fileDb->put((void*)aFileLower.c_str(), aFileLower.length(), (void*)buf, sz);
	} catch(DbException& e) {
		throw HashException(STRING_F(WRITE_FAILED_X, fileDb->getNameLower() % e.getError()));
	}

	free(buf);
}

void HashManager::HashStore::removeFile(const string& aFilePathLower) {
	try {
		fileDb->remove((void*) aFilePathLower.c_str(), aFilePathLower.length());
	} catch (DbException& e) {
		throw HashException(STRING_F(WRITE_FAILED_X, fileDb->getNameLower() % e.getError()));
	}
}

void HashManager::HashStore::addTree(const TigerTree& tt) {
	size_t treelen = tt.getLeaves().size() == 1 ? 0 : tt.getLeaves().size() * TTHValue::BYTES;
	auto sz = sizeof(uint8_t) + sizeof(int64_t) + sizeof(int64_t) + treelen;

	//allocate the memory
	void* buf = malloc(sz);


	//set the data
	char *p = (char *)buf;

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
	} catch(DbException& e) {
		free(buf);
		throw HashException(STRING_F(WRITE_FAILED_X, hashDb->getNameLower() % e.getError()));
	}

	free(buf);
}

bool HashManager::HashStore::getTree(const TTHValue& aRoot, TigerTree& tt) {
	try {
		return hashDb->get((void*)aRoot.data, sizeof(TTHValue), 100*1024, [&](void* aValue, size_t valueLen) {
			return loadTree(aValue, valueLen, aRoot, tt, true);
		});
	} catch(DbException& e) {
		log(STRING_F(READ_FAILED_X, hashDb->getNameLower() % e.getError()), LogMessage::SEV_ERROR);
	}

	return false;
}

bool HashManager::HashStore::hasTree(const TTHValue& aRoot) {
	bool ret = false;
	try {
		ret = hashDb->hasKey((void*)aRoot.data, sizeof(TTHValue));
	} catch(DbException& e) {
		throw HashException(STRING_F(READ_FAILED_X, hashDb->getNameLower() % e.getError()));
	}
	return ret;
}

bool HashManager::HashStore::loadTree(const void* src, size_t len, const TTHValue& aRoot, TigerTree& aTree, bool aReportCorruption) {
	if (len < sizeof(uint8_t) +sizeof(int64_t) +sizeof(int64_t))
		return false;

	char *p = (char*)src;

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

bool HashManager::HashStore::loadFileInfo(const void* src, size_t len, HashedFile& aFile) {
	if (len != sizeof(HashedFile)+1)
		return false;

	char *p = (char*)src;

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
	memcpy(&root, p, sizeof(TTHValue));
	p += sizeof(TTHValue);

	int64_t fileSize;
	memcpy(&fileSize, p, sizeof(int64_t));
	p += sizeof(int64_t);

	aFile = HashedFile(root, timeStamp, fileSize);
	return true;
}

void HashManager::HashStore::saveFileInfo(void *dest, const HashedFile& aFile) {
	char *p = (char *)dest;

	uint8_t version = FILEINDEX_VERSION;
	memcpy(p, &version, sizeof(uint8_t));
	p += sizeof(uint8_t);

	uint64_t timeStamp = aFile.getTimeStamp();
	memcpy(p, &timeStamp, sizeof(uint64_t));
	p += sizeof(uint64_t);

	TTHValue root = aFile.getRoot();
	memcpy(p, &root, sizeof(TTHValue));
	p += sizeof(TTHValue);

	int64_t fileSize = aFile.getSize();
	memcpy(p, &fileSize, sizeof(int64_t));
	//p += sizeof(int64_t);
}

uint32_t HashManager::HashStore::getFileInfoSize(const HashedFile& /*aTree*/) {
	return sizeof(uint8_t) + sizeof(uint64_t) + sizeof(TTHValue) + sizeof(int64_t);
}

void HashManager::HashStore::loadLegacyTree(File& f, int64_t aSize, int64_t aIndex, int64_t aBlockSize, size_t datLen, const TTHValue& root, TigerTree& tt) {
	try {
		f.setPos(aIndex);
		boost::scoped_array<uint8_t> buf(new uint8_t[datLen]);
		f.read(&buf[0], datLen);
		tt = TigerTree(aSize, aBlockSize, &buf[0]);
		if (!(tt.getRoot() == root))
			throw HashException(STRING(INVALID_TREE));
	} catch (const Exception& e) {
		throw HashException(STRING_F(TREE_LOAD_FAILED, root.toBase32() % e.getError()));
	}
}

int64_t HashManager::HashStore::getRootInfo(const TTHValue& root, InfoType aType) noexcept {
	int64_t ret = 0;
	try {
		hashDb->get((void*)root.data, sizeof(TTHValue), 100*1024, [&](void* aValue, size_t /*valueLen*/) {
			char* p = (char*)aValue;

			uint8_t version;
			memcpy(&version, p, sizeof(uint8_t));
			p += sizeof(uint8_t);

			if (version > FILEINDEX_VERSION) {
				return false;
			}

			p += (aType == TYPE_FILESIZE ? 0 : sizeof(int64_t));

			memcpy(&ret, p, sizeof(ret));
			return true;
		});
	} catch(DbException& e) {
		log(STRING_F(READ_FAILED_X, hashDb->getNameLower() % e.getError()), LogMessage::SEV_ERROR);
	}
	return ret;
}

bool HashManager::HashStore::checkTTH(const string& aFileLower, HashedFile& fi_) noexcept {
	auto initialTime = fi_.getTimeStamp();
	auto initialSize = fi_.getSize();

	if (getFileInfo(aFileLower, fi_)) {
		if (fi_.getTimeStamp() == initialTime && fi_.getSize() == initialSize) {
			return true;
		}
	}

	return false;
}

bool HashManager::HashStore::getFileInfo(const string& aFileLower, HashedFile& fi_) noexcept {
	try {
		return fileDb->get((void*)aFileLower.c_str(), aFileLower.length(), sizeof(HashedFile), [&](void* aValue, size_t valueLen) {
			return loadFileInfo(aValue, valueLen, fi_);
		});
	} catch(const DbException& e) {
		log(STRING_F(READ_FAILED_X, fileDb->getNameLower() % e.getError()), LogMessage::SEV_ERROR);
	}

	return false;
}

void HashManager::HashStore::optimize(bool doVerify) noexcept {
	getInstance()->fire(HashManagerListener::MaintananceStarted());

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
				} else {
					unusedFiles++;
					return true;
				}
			}, fileSnapshot.get());
		} catch(DbException& e) {
			log(STRING_F(READ_FAILED_X, fileDb->getNameLower() % e.getError()), LogMessage::SEV_ERROR);
			log(STRING(HASHDB_MAINTENANCE_FAILED), LogMessage::SEV_ERROR);
			getInstance()->fire(HashManagerListener::MaintananceFinished());
			return;
		}

		//remove trees that aren't shared or queued and optionally check whether each tree can be loaded
		TigerTree tt;
		TTHValue curRoot;
		try {
			hashDb->remove_if([&](void* aKey, size_t key_len, void* aValue, size_t valueLen) {
				memcpy(&curRoot, aKey, key_len);
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
		} catch(DbException& e) {
			log(STRING_F(READ_FAILED_X, hashDb->getNameLower() % e.getError()), LogMessage::SEV_ERROR);
			log(STRING(HASHDB_MAINTENANCE_FAILED), LogMessage::SEV_ERROR);
			getInstance()->fire(HashManagerListener::MaintananceFinished());
			return;
		}

		//remove file entries that don't have a corresponding hash data entry
		missingTrees = usedRoots.size() - failedTrees;
		if (usedRoots.size() > 0) {
			try {
				fileDb->remove_if([&](void* /*aKey*/, size_t /*key_len*/, void* aValue, size_t valueLen) {
					loadFileInfo(aValue, valueLen, fi);
					if (usedRoots.find(fi.getRoot()) != usedRoots.end()) {
						failedSize += fi.getSize();
						validFiles--;
						removedFiles++;
						return true;
					}

					return false;
				}, fileSnapshot.get());
			} catch(DbException& e) {
				log(STRING_F(READ_FAILED_X, fileDb->getNameLower() % e.getError()), LogMessage::SEV_ERROR);
				log(STRING(HASHDB_MAINTENANCE_FAILED), LogMessage::SEV_ERROR);
				getInstance()->fire(HashManagerListener::MaintananceFinished());
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

	getInstance()->fire(HashManagerListener::MaintananceFinished());
}

void HashManager::HashStore::compact() noexcept {
	log(STRING_F(COMPACTING_X, fileDb->getNameLower()), LogMessage::SEV_INFO);
	fileDb->compact();
	log(STRING_F(COMPACTING_X, hashDb->getNameLower()), LogMessage::SEV_INFO);
	hashDb->compact();
	log("Done", LogMessage::SEV_INFO);
}

string HashManager::HashStore::getDbStats() noexcept {
	string statMsg;

	statMsg += fileDb->getStats();
	statMsg += "Deleted entries since last compaction: " + Util::toString(SETTING(CUR_REMOVED_FILES)) + " (" + Util::toString(((double)SETTING(CUR_REMOVED_FILES) / (double)fileDb->size(false))*100) + "%)";
	statMsg += "\r\n\r\n";

	statMsg += hashDb->getStats();
	statMsg += "Deleted entries since last compaction: " + Util::toString(SETTING(CUR_REMOVED_TREES)) + " (" + Util::toString(((double)SETTING(CUR_REMOVED_TREES) / (double)hashDb->size(false))*100) + "%)";
	statMsg += "\r\n\r\n";
	statMsg += "\n\nDisk block size: " + Util::formatBytes(File::getBlockSize(hashDb->getPath())) + "\n\n";
	return statMsg;
}

void HashManager::HashStore::onScheduleRepair(bool schedule) {
	if (schedule) {
		File::createFile(hashDb->getRepairFlag());
		File::createFile(fileDb->getRepairFlag());
	} else {
		File::deleteFile(hashDb->getRepairFlag());
		File::deleteFile(fileDb->getRepairFlag());
	}
}

bool HashManager::HashStore::isRepairScheduled() const noexcept {
	return Util::fileExists(hashDb->getRepairFlag()) && Util::fileExists(fileDb->getRepairFlag());
}

void HashManager::HashStore::getDbSizes(int64_t& fileDbSize_, int64_t& hashDbSize_) const noexcept {
	fileDbSize_ = fileDb->getSizeOnDisk();
	hashDbSize_ = hashDb->getSizeOnDisk();
}

void HashManager::HashStore::openDb(StartupLoader& aLoader) {
	auto hashDataPath = Util::getPath(Util::PATH_USER_CONFIG) + "HashData" + PATH_SEPARATOR;
	auto fileIndexPath = Util::getPath(Util::PATH_USER_CONFIG) + "FileIndex" + PATH_SEPARATOR;

	File::ensureDirectory(hashDataPath);
	File::ensureDirectory(fileIndexPath);

	Util::migrate(fileIndexPath, "*");
	Util::migrate(hashDataPath, "*");

	uint32_t cacheSize = static_cast<uint32_t>(Util::convertSize(max(SETTING(DB_CACHE_SIZE), 1), Util::MB));
	auto blockSize = File::getBlockSize(Util::getPath(Util::PATH_USER_CONFIG));

	try {
		// Use the file system block size in here. Using a block size smaller than that reduces the performance significantly especially when writing a lot of data (e.g. when migrating the data)
		// The default cache size of 8 MB is able to hold approximately 256-512 trees with the block size of 16KB which should be enough for most common transfers (should the size be increased with larger block size?)
		// The number of open files doesn't matter here since the tree lookups are very much random (20 is the minimum allowed by LevelDB). The data won't compress so no need to even try it.
		hashDb.reset(new LevelDB(hashDataPath, STRING(HASH_DATA), cacheSize, 20, false, max(static_cast<int64_t>(16 * 1024), blockSize)));

		// Use a large block size and allow more open files because the reads are nearly sequential in here (but done with multiple threads). 
		// The default database sorting isn't perfect when having files and folders mixed within the same directory but that shouldn't be a big issue (avoid using custom comparison function for now...)
		fileDb.reset(new LevelDB(fileIndexPath, STRING(FILE_INDEX), cacheSize, 50, true, 64 * 1024));


		hashDb->open(aLoader.stepF, aLoader.messageF);
		fileDb->open(aLoader.stepF, aLoader.messageF);
	} catch (const DbException& e) {
		// Can't continue without hash database, abort startup
		throw AbortException(e.getError());
	}
}

void HashManager::HashStore::load(StartupLoader& aLoader) {
	// Open the new database
	openDb(aLoader);
}

HashManager::HashStore::HashStore() {
}

void HashManager::HashStore::closeDb() noexcept {
	hashDb.reset(nullptr);
	fileDb.reset(nullptr);
}

HashManager::HashStore::~HashStore() {
	closeDb();
}

void HashManager::stopHashing(const string& baseDir) noexcept {
	WLock l(Hasher::hcs);
	for (auto h: hashers)
		h->stopHashing(baseDir); 
}

void HashManager::setPriority(Thread::Priority p) noexcept {
	RLock l(Hasher::hcs);
	for (auto h: hashers)
		h->setThreadPriority(p); 
}

HashManager::HashStats HashManager::getStats() const noexcept {
	HashStats stats;

	RLock l(Hasher::hcs);
	for (auto i: hashers) {
		i->getStats(stats.curFile, stats.bytesLeft, stats.filesLeft, stats.speed, stats.filesAdded, stats.bytesAdded);
		if (!i->isPaused()) {
			stats.isPaused = false;
		}

		if (i->isRunning()) {
			stats.hashersRunning++;
		}
	}

	return stats;
}

void HashManager::startMaintenance(bool verify){
	optimizer.startMaintenance(verify); 
}

HashManager::Optimizer::Optimizer() {

}

HashManager::Optimizer::~Optimizer() {
}

void HashManager::Optimizer::startMaintenance(bool aVerify) {
	if (running)
		return;

	verify = aVerify;
	running = true;
	start();
}

int HashManager::Optimizer::run() {
	HashManager::getInstance()->optimize(verify);
	running = false;
	return 0;
}

void HashManager::startup(StartupLoader& aLoader) {
	hashers.push_back(new Hasher(false, 0));
	store.load(aLoader); 
}

void HashManager::shutdown(ProgressFunction progressF) noexcept {
	isShutdown = true;

	{
		WLock l(Hasher::hcs);
		for (auto h : hashers) {
			h->shutdown();
		}
	}

	// Wait for the hashers to shut down
	while (true) {
		{
			RLock l(Hasher::hcs);
			if (hashers.empty()) {
				break;
			}
		}
		Thread::sleep(50);
	}
}

void HashManager::stop() noexcept {
	WLock l(Hasher::hcs);
	for (auto h: hashers) {
		h->stop();
	}
}

void HashManager::removeHasher(const Hasher* aHasher) {
	hashers.erase(remove(hashers.begin(), hashers.end(), aHasher), hashers.end());
}

bool HashManager::pauseHashing() noexcept {
	pausers++;
	if (pausers == 1) {
		RLock l(Hasher::hcs);
		for (auto h : hashers)
			h->pause();
		return isHashingPaused(false);
	}
	return true;
}

void HashManager::resumeHashing(bool forced) {
	if (forced)
		pausers = 0;
	else if (pausers > 0)
		pausers--;

	if (pausers == 0) {
		RLock l(Hasher::hcs);
		for (auto h : hashers)
			h->resume();
	}
}

void HashManager::logHasher(const string& aMessage, int hasherID, bool isError, bool lock) {
	ConditionalRLock l(Hasher::hcs, lock);
	log((hashers.size() > 1 ? "[" + STRING_F(HASHER_X, hasherID) + "] " + ": " : Util::emptyString) + aMessage, isError ? LogMessage::SEV_ERROR : LogMessage::SEV_INFO);
}

bool HashManager::isHashingPaused(bool lock /*true*/) const noexcept {
	ConditionalRLock l(Hasher::hcs, lock);
	return all_of(hashers.begin(), hashers.end(), [](const Hasher* h) { return h->isPaused(); });
}

HashManager::HashPauser::HashPauser() {
	HashManager::getInstance()->pauseHashing();
}

HashManager::HashPauser::~HashPauser() {
	HashManager::getInstance()->resumeHashing();
}

} // namespace dcpp
