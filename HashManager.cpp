/* 
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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

#include "AirUtil.h"
#include "File.h"
#include "FileReader.h"
#include "LogManager.h"
#include "QueueManager.h"
#include "ShareManager.h"
#include "ResourceManager.h"
#include "ScopedFunctor.h"
#include "SimpleXMLReader.h"
#include "Util.h"
#include "version.h"
#include "ZUtils.h"

//#include "BerkeleyDB.h"
#include "LevelDB.h"
//#include "HamsterDB.h"

#define FILEINDEX_VERSION 1
#define HASHDATA_VERSION 1

namespace dcpp {

using boost::range::find_if;

SharedMutex HashManager::Hasher::hcs;
const int64_t HashManager::MIN_BLOCK_SIZE = 64 * 1024;

HashManager::HashManager() {

}

HashManager::~HashManager() {
	optimizer.join();
}

bool HashManager::checkTTH(const string& aFileLower, const string& aFileName, HashedFile& fi_) {
	dcassert(Text::isLower(aFileLower));
	if (!store.checkTTH(aFileLower, fi_)) {
		hashFile(aFileName, aFileLower, fi_.getSize());
		return false;
	}
	return true;
}

void HashManager::getFileInfo(const string& aFileLower, const string& aFileName, HashedFile& fi_) throw(HashException) {
	dcassert(Text::isLower(aFileLower));
	auto found = store.getFileInfo(aFileLower, fi_);
	if (!found) {
		auto size = File::getSize(aFileName);
		if (size >= 0)
			hashFile(aFileName, aFileLower, size);
		throw HashException();
	}
}

bool HashManager::getTree(const TTHValue& root, TigerTree& tt) noexcept {
	return store.getTree(root, tt);
}

size_t HashManager::getBlockSize(const TTHValue& root) noexcept {
	return static_cast<size_t>(store.getRootInfo(root, HashStore::TYPE_BLOCKSIZE));
}

int64_t HashManager::Hasher::getTimeLeft() const noexcept {
	return lastSpeed > 0 ? (totalBytesLeft / lastSpeed) : 0;
}

bool HashManager::Hasher::getPathVolume(const string& aPath, string& vol_) const noexcept { 
	auto p = find_if(devices | map_keys, [&aPath](const string& aVol) { return aPath.compare(0, aVol.length(), aVol.c_str()) == 0; });
	if (p.base() != devices.end()) {
		vol_ = *p;
		return true;
	}
	return false;
}

bool HashManager::Hasher::hasFile(const string& aPath) const noexcept {
	return w.find(aPath) != w.end();
}

bool HashManager::hashFile(const string& filePath, const string& pathLower, int64_t size) {
	if(aShutdown) //we cant allow adding more hashers if we are shutting down, it will result in infinite loop
		return false;

	Hasher* h = nullptr;

	WLock l(Hasher::hcs);

	//get the volume name
	string vol;
	if (none_of(hashers.begin(), hashers.end(), [&](const Hasher* h) { return h->getPathVolume(pathLower, vol); })) {
		vol = File::getMountPath(pathLower);
	}

	//dcassert(!vol.empty());

	if (hashers.size() == 1 && !hashers.front()->hasDevices()) {
		//always use the first hasher if it's idle
		h = hashers.front();
	} else {
		auto getLeastLoaded = [](const HasherList& hl) {
			return min_element(hl.begin(), hl.end(), [](const Hasher* h1, const Hasher* h2) { return h1->getBytesLeft() < h2->getBytesLeft(); });
		};

		if (SETTING(HASHERS_PER_VOLUME) == 1) {
			//do we have files for this volume queued already? always use the same one in that case
			auto p = find_if(hashers, [&vol](const Hasher* aHasher) { return aHasher->hasDevice(vol); });
			if (p != hashers.end()) {
				h = *p;
			} else if (static_cast<int>(hashers.size()) >= SETTING(MAX_HASHING_THREADS)) {
				// can't create new ones
				h = *getLeastLoaded(hashers);
			}
		} else {
			//get the hashers with this volume
			HasherList volHashers;
			copy_if(hashers.begin(), hashers.end(), back_inserter(volHashers), [&vol](const Hasher* aHasher) { return aHasher->hasDevice(vol); });

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

			LogManager::getInstance()->message(STRING_F(HASHER_X_CREATED, id), LogManager::LOG_INFO);
			h = new Hasher(pausers > 0, id);
			hashers.push_back(h);
		}
	}

	//queue the file for hashing
	return h->hashFile(filePath, pathLower, size, vol);
}

void HashManager::getFileTTH(const string& aFile, int64_t aSize, bool addStore, TTHValue& tth_, int64_t& sizeLeft_, const bool& aCancel, std::function<void(int64_t, const string&)> updateF/*nullptr*/) throw(HashException) {
	auto pathLower = Text::toLower(aFile);
	HashedFile fi(File::getLastModified(aFile), aSize);

	if (!store.checkTTH(pathLower, fi)) {
		File f(aFile, File::READ, File::OPEN);
		int64_t bs = max(TigerTree::calcBlockSize(aSize, 10), MIN_BLOCK_SIZE);
		uint64_t timestamp = f.getLastModified();
		TigerTree tt(bs);

		auto start = GET_TICK();
		int64_t tickHashed = 0;

		FileReader fr(true);
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

		f.close();
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

void HashManager::hashDone(const string& aFileName, const string& pathLower, const TigerTree& tt, int64_t speed, HashedFile& aFileInfo, int hasherID /*0*/) noexcept {
	try {
		store.addHashedFile(pathLower, tt, aFileInfo);
	} catch (const Exception& e) {
		log(STRING_F(HASHING_FAILED_X, e.getError()), hasherID, true, true);
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
			log(STRING_F(HASHING_FINISHED_X, fn) + " (" + Util::formatBytes(speed) + "/s)", hasherID, false, true);
		} else {
			log(STRING_F(HASHING_FINISHED_X, fn), hasherID, false, true);
		}
	}
}

bool HashManager::addFile(const string& aFilePathLower, const HashedFile& fi_) throw(HashException) {
	//check that the file exists
	if (File::getSize(aFilePathLower) != fi_.getSize()) {
		return false;
	}

	//check that the tree exists
	if (fi_.getSize() < MIN_BLOCK_SIZE) {
		TigerTree tt = TigerTree(fi_.getSize(), fi_.getSize(), fi_.getRoot());
		store.addTree(tt);
	} else if (!store.hasTree(fi_.getRoot())) {
		return false;
	}

	store.addFile(aFilePathLower, fi_);
	return true;
}

void HashManager::HashStore::addHashedFile(const string& aFileLower, const TigerTree& tt, const HashedFile& fi_) throw(HashException) {
	addTree(tt);
	addFile(aFileLower, fi_);
}

void HashManager::HashStore::addFile(const string& aFileLower, const HashedFile& fi_) throw(HashException) {
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

void HashManager::renameFile(const string& aOldPath, const string& aNewPath, const HashedFile& fi) throw(HashException) {
	return store.renameFile(aOldPath, aNewPath, fi);
}

void HashManager::HashStore::renameFile(const string& oldPath, const string& newPath, const HashedFile& fi) throw(HashException) {
	auto oldNameLower = Text::toLower(oldPath);
	auto newNameLower = Text::toLower(newPath);
	//HashedFile fi;
	//if (getFileInfo(oldNameLower, fi)) {
		removeFile(oldNameLower);
		addFile(newNameLower, fi);
		//return true;
	//}
}

void HashManager::HashStore::removeFile(const string& aFilePathLower) throw(HashException) {
	try {
		fileDb->remove((void*) aFilePathLower.c_str(), aFilePathLower.length());
	} catch (DbException& e) {
		throw HashException(STRING_F(WRITE_FAILED_X, fileDb->getNameLower() % e.getError()));
	}
}

void HashManager::HashStore::addTree(const TigerTree& tt) throw(HashException) {
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

bool HashManager::HashStore::getTree(const TTHValue& root, TigerTree& tt) {
	try {
		return hashDb->get((void*)root.data, sizeof(TTHValue), 100*1024, [&](void* aValue, size_t valueLen) {
			return loadTree(aValue, valueLen, root, tt, true);
		});
	} catch(DbException& e) {
		LogManager::getInstance()->message(STRING_F(READ_FAILED_X, hashDb->getNameLower() % e.getError()), LogManager::LOG_ERROR);
	}

	return false;
}

bool HashManager::HashStore::hasTree(const TTHValue& root) throw(HashException) {
	bool ret = false;
	try {
		ret = hashDb->hasKey((void*)root.data, sizeof(TTHValue));
	} catch(DbException& e) {
		throw HashException(STRING_F(READ_FAILED_X, hashDb->getNameLower() % e.getError()));
	}
	return ret;
}

bool HashManager::HashStore::loadTree(const void* src, size_t len, const TTHValue& aRoot, TigerTree& aTree, bool reportCorruption) {
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
			if (reportCorruption) {
				LogManager::getInstance()->message(STRING_F(TREE_LOAD_FAILED_DB, aRoot.toBase32() % STRING(INVALID_TREE) % "/verifydb"), LogManager::LOG_ERROR);
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
	p += sizeof(int64_t);
}

uint32_t HashManager::HashStore::getFileInfoSize(const HashedFile& /*aTree*/) {
	return sizeof(uint8_t) + sizeof(uint64_t) + sizeof(TTHValue) + sizeof(int64_t);
}

void HashManager::HashStore::loadLegacyTree(File& f, int64_t aSize, int64_t aIndex, int64_t aBlockSize, size_t datLen, const TTHValue& root, TigerTree& tt) throw(HashException) {
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

int64_t HashManager::HashStore::getRootInfo(const TTHValue& root, InfoType aType) {
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
		LogManager::getInstance()->message(STRING_F(READ_FAILED_X, hashDb->getNameLower() % e.getError()), LogManager::LOG_ERROR);
	}
	return ret;
}

bool HashManager::HashStore::checkTTH(const string& aFileLower, HashedFile& fi) {
	auto initialTime = fi.getTimeStamp();
	auto initialSize = fi.getSize();

	if (getFileInfo(aFileLower, fi)) {
		if (fi.getTimeStamp() == initialTime && fi.getSize() == initialSize) {
			return true;
			//return hasTree(outTTH_);
		}
	}

	return false;
}

bool HashManager::HashStore::getFileInfo(const string& aFileLower, HashedFile& fi_) {
	try {
		return fileDb->get((void*)aFileLower.c_str(), aFileLower.length(), sizeof(HashedFile), [&](void* aValue, size_t valueLen) {
			return loadFileInfo(aValue, valueLen, fi_);
		});
	} catch(DbException& e) {
		LogManager::getInstance()->message(STRING_F(READ_FAILED_X, fileDb->getNameLower() % e.getError()), LogManager::LOG_ERROR);
	}

	return false;
}

void HashManager::HashStore::optimize(bool doVerify) noexcept {
	getInstance()->fire(HashManagerListener::MaintananceStarted());

	int unusedTrees = 0;
	int failedTrees = 0;
	int unusedFiles=0; 
	int validFiles = 0;
	int validTrees = 0;
	int missingTrees = 0;
	int removedFiles = 0;
	int64_t failedSize = 0;

	LogManager::getInstance()->message(STRING(HASHDB_MAINTENANCE_STARTED), LogManager::LOG_INFO);
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
			LogManager::getInstance()->message(STRING_F(READ_FAILED_X, fileDb->getNameLower() % e.getError()), LogManager::LOG_ERROR);
			LogManager::getInstance()->message(STRING(HASHDB_MAINTENANCE_FAILED), LogManager::LOG_ERROR);
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
			LogManager::getInstance()->message(STRING_F(READ_FAILED_X, hashDb->getNameLower() % e.getError()), LogManager::LOG_ERROR);
			LogManager::getInstance()->message(STRING(HASHDB_MAINTENANCE_FAILED), LogManager::LOG_ERROR);
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
				LogManager::getInstance()->message(STRING_F(READ_FAILED_X, fileDb->getNameLower() % e.getError()), LogManager::LOG_ERROR);
				LogManager::getInstance()->message(STRING(HASHDB_MAINTENANCE_FAILED), LogManager::LOG_ERROR);
				getInstance()->fire(HashManagerListener::MaintananceFinished());
				return;
			}
		}
	}

	SettingsManager::getInstance()->set(SettingsManager::CUR_REMOVED_FILES, SETTING(CUR_REMOVED_FILES) + unusedFiles + missingTrees);
	if (validFiles == 0 || (static_cast<double>(SETTING(CUR_REMOVED_FILES)) / static_cast<double>(validFiles)) > 0.05) {
		LogManager::getInstance()->message(STRING_F(COMPACTING_X, fileDb->getNameLower()), LogManager::LOG_INFO);
		fileDb->compact();
		SettingsManager::getInstance()->set(SettingsManager::CUR_REMOVED_FILES, 0);
	}

	SettingsManager::getInstance()->set(SettingsManager::CUR_REMOVED_TREES, SETTING(CUR_REMOVED_TREES) + unusedTrees + failedTrees);
	if (validTrees == 0 || (static_cast<double>(SETTING(CUR_REMOVED_TREES)) / static_cast<double>(validTrees)) > 0.05) {
		LogManager::getInstance()->message(STRING_F(COMPACTING_X, hashDb->getNameLower()), LogManager::LOG_INFO);
		hashDb->compact();
		SettingsManager::getInstance()->set(SettingsManager::CUR_REMOVED_TREES, 0);
	}

	string msg;
	if (unusedFiles > 0 || unusedTrees > 0) {
		msg = STRING_F(HASHDB_MAINTENANCE_UNUSED, unusedFiles % unusedTrees);
	} else {
		msg = STRING(HASHDB_MAINTENANCE_NO_UNUSED);
	}

	LogManager::getInstance()->message(msg, LogManager::LOG_INFO);

	if (failedTrees > 0 || missingTrees > 0) {
		if (doVerify) {
			msg = STRING_F(REBUILD_FAILED_ENTRIES_VERIFY, missingTrees % failedTrees);
		} else {
			msg = STRING_F(REBUILD_FAILED_ENTRIES_OPTIMIZE, missingTrees);
		}

		msg += ". " + STRING_F(REBUILD_REFRESH_PROMPT, Util::formatBytes(failedSize));
		LogManager::getInstance()->message(msg, LogManager::LOG_ERROR);
	}

	getInstance()->fire(HashManagerListener::MaintananceFinished());
}

void HashManager::HashStore::compact() noexcept {
	LogManager::getInstance()->message(STRING_F(COMPACTING_X, fileDb->getNameLower()), LogManager::LOG_INFO);
	fileDb->compact();
	LogManager::getInstance()->message(STRING_F(COMPACTING_X, hashDb->getNameLower()), LogManager::LOG_INFO);
	hashDb->compact();
	LogManager::getInstance()->message("Done", LogManager::LOG_INFO);
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

void HashManager::HashStore::openDb(StepFunction stepF, MessageFunction messageF) throw(DbException) {
	auto hashDataPath = Util::getPath(Util::PATH_USER_CONFIG) + "HashData" + PATH_SEPARATOR;
	auto fileIndexPath = Util::getPath(Util::PATH_USER_CONFIG) + "FileIndex" + PATH_SEPARATOR;

	File::ensureDirectory(hashDataPath);
	File::ensureDirectory(fileIndexPath);

	Util::migrate(fileIndexPath, "*");
	Util::migrate(hashDataPath, "*");

	uint32_t cacheSize = static_cast<uint32_t>(Util::convertSize(max(SETTING(DB_CACHE_SIZE), 1), Util::MB));
	auto blockSize = File::getBlockSize(Util::getPath(Util::PATH_USER_CONFIG));

	// Use the file system block size in here. Using a block size smaller than that reduces the performance significantly especially when writing a lot of data (e.g. when migrating the data)
	// The default cache size of 8 MB is able to hold approximately 256-512 trees with the block size of 16KB which should be enough for most common transfers (should the size be increased with larger block size?)
	// The number of open files doesn't matter here since the tree lookups are very much random (20 is the minimum allowed by LevelDB). The data won't compress so no need to even try it.
	hashDb.reset(new LevelDB(hashDataPath, STRING(HASH_DATA), cacheSize, 20, false, max(static_cast<int64_t>(16*1024), blockSize)));

	// Use a large block size and allow more open files because the reads are nearly sequential in here (but done with multiple threads). 
	// The default database sorting isn't perfect when having files and folders mixed within the same directory but that shouldn't be a big issue (avoid using custom comparison function for now...)
	fileDb.reset(new LevelDB(fileIndexPath, STRING(FILE_INDEX), cacheSize, 50, true, 64*1024));


	hashDb->open(stepF, messageF);
	fileDb->open(stepF, messageF);
}

class HashLoader: public SimpleXMLReader::CallBack {
public:
	HashLoader(HashManager::HashStore& s, const CountedInputStream<false>& countedStream, uint64_t fileSize, ProgressFunction progressF) :
		store(s),
		countedStream(countedStream),
		streamPos(0),
		fileSize(fileSize),
		progressF(progressF),
		version(0),
		inTrees(false),
		inFiles(false),
		inHashStore(false),
		dataFile(nullptr),
		readDataBytes(0),
		migratedFiles(0),
		migratedTrees(0),
		failedTrees(0)
	{ }

	void startTag(const string& name, StringPairList& attribs, bool simple);
	void endTag(const string& name);
	int64_t readDataBytes;

	int failedTrees;
	int migratedTrees;
	int migratedFiles;
private:
	HashManager::HashStore& store;

	const CountedInputStream<false>& countedStream;
	uint64_t streamPos;
	uint64_t fileSize;
	ProgressFunction progressF;

	int version;
	string file;

	bool inTrees;
	bool inFiles;
	bool inHashStore;

	unique_ptr<File> dataFile;
	unordered_map<TTHValue, int64_t> sizeMap;
};

void HashManager::HashStore::load(StepFunction stepF, ProgressFunction progressF, MessageFunction messageF) throw(HashException) {
	auto dataFile = Util::getPath(Util::PATH_USER_CONFIG) + "HashData.dat";
	auto indexFile = Util::getPath(Util::PATH_USER_CONFIG) + "HashIndex.xml";

	//check if we have and old database to convert
	Util::migrate(indexFile);
	Util::migrate(dataFile);

	auto hashDataSize = File::getSize(dataFile);
	auto hashIndexSize = File::getSize(indexFile);

	bool migrating = hashDataSize != -1 && hashIndexSize != -1;
	if (migrating) {
		auto ret = messageF(STRING_F(DB_MIGRATION_INFO, shortVersionString) + "\r\n\r\n" + STRING(WANT_CONTINUE), true, false);
		if (!ret) {
			throw HashException();
		}

		// check the free space for out new databases
		auto volume = File::getMountPath(Util::getPath(Util::PATH_USER_CONFIG));
		if (!volume.empty()) {
			auto freeSpace = File::getFreeSpace(volume);
			if (hashDataSize + hashIndexSize > freeSpace) {
				messageF(STRING_F(DB_MIGRATION_FREE_SPACE, Util::formatBytes(freeSpace) % volume % Util::formatBytes(hashDataSize + hashIndexSize)), false, true);
				throw HashException();
			}
		}
	}


	//open the new database
	try {
		openDb(stepF, messageF);
	} catch (...) {
		throw HashException();
	}


	//migrate the old database file
	if (migrating) {
		stepF(STRING(UPGRADING_HASHDATA));
		try {
			int migratedFiles, migratedTrees, failedTrees;
			{
				File f(indexFile, File::READ, File::OPEN);
				CountedInputStream<false> countedStream(&f);
				HashLoader l(*this, countedStream, hashDataSize + hashIndexSize, progressF);
				SimpleXMLReader(&l).parse(countedStream);
				migratedFiles = l.migratedFiles;
				migratedTrees = l.migratedTrees;
				failedTrees = l.failedTrees;
			}

			File::renameFile(dataFile, dataFile + ".bak");
			File::renameFile(indexFile, indexFile + ".bak");
			messageF(STRING_F(DB_MIGRATION_COMPLETE, migratedFiles % migratedTrees % failedTrees % Util::formatBytes(hashIndexSize) % Util::formatBytes(hashDataSize)), false, false);
		} catch (const Exception&) {
			// ...
		}
	}
}

static const string sHashStore = "HashStore";
static const string sVersion = "Version";
static const string sTrees = "Trees";
static const string sFiles = "Files";
static const string sFile = "File";
static const string sName = "Name";
static const string sSize = "Size";
static const string sHash = "Hash";
static const string sType = "Type";
static const string sTTH = "TTH";
static const string sIndex = "Index";
static const string sBlockSize = "BlockSize";
static const string sTimeStamp = "TimeStamp";
static const string sRoot = "Root";

void HashLoader::startTag(const string& name, StringPairList& attribs, bool simple) {
	auto readIndexBytes = countedStream.getReadBytes();
	if(readIndexBytes != streamPos) {
		streamPos = readIndexBytes;
		progressF(static_cast<float>(readIndexBytes+readDataBytes) / static_cast<float>(fileSize));
	}

	if (!inHashStore && name == sHashStore) {
		version = Util::toInt(getAttrib(attribs, sVersion, 0));
		inHashStore = !simple;
	} else if (inHashStore && (version == 2 || version == 3)) {
		if (inTrees && name == sHash) {
			//migrate old trees

			const string& type = getAttrib(attribs, sType, 0);
			int64_t index = Util::toInt64(getAttrib(attribs, sIndex, 1));
			int64_t blockSize = Util::toInt64(getAttrib(attribs, sBlockSize, 2));
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 3));
			const string& root = getAttrib(attribs, sRoot, 4);
			if (!root.empty() && type == sTTH && (index >= 8 || index == HashManager::SMALL_TREE) && blockSize >= 1024) {
				auto tth = TTHValue(root);
				try {
					if (!dataFile) {
						dataFile.reset(new File(Util::getPath(Util::PATH_USER_CONFIG) + "HashData.dat", File::READ, File::OPEN | File::SHARED_WRITE, File::BUFFER_RANDOM));
					}

					if (dataFile) {
						TigerTree tt;
						if (index == HashManager::SMALL_TREE) {
							tt = TigerTree(size, blockSize, tth);
						} else {
							size_t datalen = TigerTree::calcBlocks(size, blockSize) * TTHValue::BYTES;
							readDataBytes += datalen;
							store.loadLegacyTree(*dataFile.get(), size, index, blockSize, datalen, tth, tt);
						}

						store.addTree(tt);
						sizeMap.emplace(tth, size);
						migratedTrees++;
					}
				} catch (const Exception& /*e*/) {
					failedTrees++;
				}
			}
		} else if (inFiles && name == sFile) {
			file = getAttrib(attribs, sName, 0);
			auto timeStamp = Util::toUInt32(getAttrib(attribs, sTimeStamp, 1));
			const string& root = getAttrib(attribs, sRoot, 2);

			if (!file.empty() && timeStamp > 0 && !root.empty()) {
				string fileLower = Text::toLower(file);
				auto tth = TTHValue(root);
				auto p = sizeMap.find(tth);
				if (p != sizeMap.end()) {
					auto fi = HashedFile(tth, timeStamp, p->second);
					store.addFile(fileLower, fi);
					migratedFiles++;
				}
			}
		} else if (name == sTrees) {
			inTrees = !simple;
		} else if (name == sFiles) {
			inFiles = !simple;
		}
	}
}

void HashLoader::endTag(const string& name) {
	if (name == sFile) {
		file.clear();
	}
}

HashManager::HashStore::HashStore() {
}

void HashManager::HashStore::closeDb() {
	hashDb.reset(nullptr);
	fileDb.reset(nullptr);
}

HashManager::HashStore::~HashStore() {
	closeDb();
}

bool HashManager::Hasher::hashFile(const string& fileName, const string& filePathLower, int64_t size, const string& devID) noexcept {
	//always locked
	auto ret = w.emplace_sorted(filePathLower, fileName, size, devID);
	if (ret.second) {
		devices[(*ret.first).devID]++; 
		totalBytesLeft += size;
		s.signal();
		return true;
	}

	return false;
}

bool HashManager::Hasher::pause() noexcept {
	paused = true;
	return paused;
}

void HashManager::Hasher::resume() {
	paused = false;
	t_resume();
}

bool HashManager::Hasher::isPaused() const noexcept {
	return paused;
}

void HashManager::Hasher::removeDevice(const string& aID) noexcept {
	dcassert(!aID.empty());
	auto dp = devices.find(aID);
	if (dp != devices.end()) {
		dp->second--;
		if (dp->second == 0)
			devices.erase(dp);
	}
}

void HashManager::Hasher::stopHashing(const string& baseDir) noexcept {
	for (auto i = w.begin(); i != w.end();) {
		if (Util::strnicmp(baseDir, i->filePath, baseDir.length()) == 0) {
			totalBytesLeft -= i->fileSize;
			removeDevice(i->devID);
			i = w.erase(i);
		} else {
			++i;
		}
	}
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

void HashManager::getStats(string& curFile, int64_t& bytesLeft, size_t& filesLeft, int64_t& speed, int& hasherCount) const noexcept {
	RLock l(Hasher::hcs);
	hasherCount = hashers.size();
	for (auto i: hashers)
		i->getStats(curFile, bytesLeft, filesLeft, speed);
}

void HashManager::startMaintenance(bool verify){
	optimizer.startMaintenance(verify); 
}

HashManager::Optimizer::Optimizer() : running(false) {

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

void HashManager::startup(StepFunction stepF, ProgressFunction progressF, MessageFunction messageF) throw(HashException) {
	hashers.push_back(new Hasher(false, 0));
	store.load(stepF, progressF, messageF); 
}

void HashManager::stop() noexcept {
	WLock l(Hasher::hcs);
	for (auto h: hashers)
		h->clear();
}

void HashManager::Hasher::shutdown() { 
	closing = true; 
	clear();
	if(paused) 
		resume(); 
	s.signal(); 
}

void HashManager::shutdown(ProgressFunction progressF) noexcept {
	aShutdown = true;

	{
		WLock l(Hasher::hcs);
		for (auto h: hashers) {
			h->shutdown();
		}
	}

	// Wait for the hashers to shut down
	while(true) {
		{
			RLock l(Hasher::hcs);
			if(hashers.empty()) {
				break;
			}
		}
		Thread::sleep(50);
	}
}

void HashManager::Hasher::clear() noexcept {
	w.clear();
	devices.clear();
	totalBytesLeft = 0;
}

void HashManager::Hasher::getStats(string& curFile, int64_t& bytesLeft, size_t& filesLeft, int64_t& speed) const noexcept {
	curFile = currentFile;
	filesLeft += w.size();
	if (running)
		filesLeft++;
	bytesLeft += totalBytesLeft;
	speed += lastSpeed;
}

void HashManager::Hasher::instantPause() {
	if(paused) {
		t_suspend();
	}
}

HashManager::Hasher::Hasher(bool isPaused, int aHasherID) : paused(isPaused), hasherID(aHasherID), totalBytesLeft(0), lastSpeed(0) {
	start();
}

void HashManager::log(const string& aMessage, int hasherID, bool isError, bool lock) {
	ConditionalRLock l(Hasher::hcs, lock);
	LogManager::getInstance()->message((hashers.size() > 1 ? "[" + STRING_F(HASHER_X, hasherID) + "] " + ": " : Util::emptyString) + aMessage, isError ? LogManager::LOG_ERROR : LogManager::LOG_INFO);
}

int HashManager::Hasher::run() {
	setThreadPriority(Thread::IDLE);

	string fname;
	for(;;) {
		s.wait();
		instantPause(); //suspend the thread...
		if(closing) {
			WLock l(hcs);
			HashManager::getInstance()->removeHasher(this);
			break;
		}
		
		int64_t originalSize = 0;
		bool failed = true;
		bool dirChanged = false;
		string curDevID, pathLower;
		{
			WLock l(hcs);
			if(!w.empty()) {
				auto& wi = w.front();
				dirChanged = initialDir.empty() || compare(Util::getFilePath(wi.filePath), Util::getFilePath(fname)) != 0;
				currentFile = fname = move(wi.filePath);
				curDevID = move(wi.devID);
				pathLower = move(wi.filePathLower);
				originalSize = wi.fileSize;
				dcassert(!curDevID.empty());
				w.pop_front();
			} else {
				fname.clear();
			}
		}
		running = true;

		HashedFile fi;
		if(!fname.empty()) {
			int64_t sizeLeft = originalSize;
			try {
				if (initialDir.empty()) {
					initialDir = Util::getFilePath(fname);
				}

				if (dirChanged)
					sfv.loadPath(Util::getFilePath(fname));
				uint64_t start = GET_TICK();
				File f(fname, File::READ, File::OPEN);

				// size changed since adding?
				int64_t size = f.getSize();
				sizeLeft = size;
				totalBytesLeft += size - originalSize;

				int64_t bs = max(TigerTree::calcBlockSize(size, 10), MIN_BLOCK_SIZE);
				uint64_t timestamp = f.getLastModified();
				TigerTree tt(bs);

				CRC32Filter crc32;

				auto fileCRC = sfv.hasFile(Util::getFileName(pathLower));

				uint64_t lastRead = GET_TICK();
 
                FileReader fr(true);
				fr.read(fname, [&](const void* buf, size_t n) -> bool {
					if(SETTING(MAX_HASH_SPEED)> 0) {
						uint64_t now = GET_TICK();
						uint64_t minTime = n * 1000LL / Util::convertSize(SETTING(MAX_HASH_SPEED), Util::MB);
 
						if(lastRead + minTime > now) {
							Thread::sleep(minTime - (now - lastRead));
						}
						lastRead = lastRead + minTime;
					} else {
						lastRead = GET_TICK();
					}
					tt.update(buf, n);
				
					if(fileCRC)
						crc32(buf, n);

					sizeLeft -= n;
					uint64_t end = GET_TICK();

					if(totalBytesLeft > 0)
						totalBytesLeft -= n;
					if(end > start)
						lastSpeed = (size - sizeLeft)*1000 / (end -start);

					return !closing;
				});

				f.close();
				tt.finalize();

				failed = fileCRC && crc32.getValue() != *fileCRC;

				uint64_t end = GET_TICK();
				int64_t averageSpeed = 0;

				if (!failed) {
					sizeHashed += size;
					dirSizeHashed += size;

					dirFilesHashed++;
					filesHashed++;
				}

				if(end > start) {
					hashTime += (end - start);
					dirHashTime += (end - start);
					averageSpeed = size * 1000 / (end - start);
				}

				if(failed) {
					getInstance()->log(STRING(ERROR_HASHING) + fname + ": " + STRING(ERROR_HASHING_CRC32), hasherID, true, true);
					getInstance()->fire(HashManagerListener::HashFailed(), fname, fi);
				} else {
					fi = HashedFile(tt.getRoot(), timestamp, size);
					getInstance()->hashDone(fname, pathLower, tt, averageSpeed, fi, hasherID);
					//tth = tt.getRoot();
				}
			} catch(const FileException& e) {
				totalBytesLeft -= sizeLeft;
				getInstance()->log(STRING(ERROR_HASHING) + " " + fname + ": " + e.getError(), hasherID, true, true);
				getInstance()->fire(HashManagerListener::HashFailed(), fname, fi);
				failed = true;
			}
		
		}

		auto onDirHashed = [&] () -> void {
			if ((SETTING(HASHERS_PER_VOLUME) == 1 || w.empty()) && (dirFilesHashed > 1 || !failed)) {
				if (dirFilesHashed == 1) {
					getInstance()->log(STRING_F(HASHING_FINISHED_FILE, currentFile % 
						Util::formatBytes(dirSizeHashed) % 
						Util::formatTime(dirHashTime / 1000, true) % 
						(Util::formatBytes(dirHashTime > 0 ? ((dirSizeHashed * 1000) / dirHashTime) : 0) + "/s" )), hasherID, false, false);
				} else {
					getInstance()->log(STRING_F(HASHING_FINISHED_DIR, Util::getFilePath(initialDir) % 
						dirFilesHashed %
						Util::formatBytes(dirSizeHashed) % 
						Util::formatTime(dirHashTime / 1000, true) % 
						(Util::formatBytes(dirHashTime > 0 ? ((dirSizeHashed * 1000) / dirHashTime) : 0) + "/s" )), hasherID, false, false);
				}
			}

			dirsHashed++;
			dirHashTime = 0;
			dirSizeHashed = 0;
			dirFilesHashed = 0;
			initialDir.clear();
		};

		bool deleteThis = false;
		{
			WLock l(hcs);
			if (!fname.empty())
				removeDevice(curDevID);

			if (w.empty()) {
				if (sizeHashed > 0) {
					if (dirsHashed == 0) {
						onDirHashed();
						//LogManager::getInstance()->message(STRING(HASHING_FINISHED_TOTAL_PLAIN), LogManager::LOG_INFO);
					} else {
						onDirHashed();
						getInstance()->log(STRING_F(HASHING_FINISHED_TOTAL, filesHashed % Util::formatBytes(sizeHashed) % dirsHashed % 
							Util::formatTime(hashTime / 1000, true) % 
							(Util::formatBytes(hashTime > 0 ? ((sizeHashed * 1000) / hashTime) : 0)  + "/s" )), hasherID, false, false);
					}
				} else if(!fname.empty()) {
					//all files failed to hash?
					getInstance()->log(STRING(HASHING_FINISHED), hasherID, false, false);

					//always clear the directory so that the will be a fresh start when more files are added for hashing
					initialDir.clear();
				}

				hashTime = 0;
				sizeHashed = 0;
				dirsHashed = 0;
				filesHashed = 0;
				deleteThis = hasherID != 0;
				sfv.unload();
			} else if (!AirUtil::isParentOrExact(initialDir, w.front().filePath)) {
				onDirHashed();
			}

			currentFile.clear();
		}

		if (!failed && !fname.empty())
			getInstance()->fire(HashManagerListener::TTHDone(), fname, fi);

		if (deleteThis) {
			//check again if we have added new items while this was unlocked

			WLock l(hcs);
			if (w.empty()) {
				//Nothing more to has, delete this hasher
				getInstance()->removeHasher(this);
				break;
			}
		}

		running = false;
	}

	delete this;
	return 0;
}

void HashManager::removeHasher(Hasher* aHasher) {
	hashers.erase(remove(hashers.begin(), hashers.end(), aHasher), hashers.end());
}

HashManager::Hasher::WorkItem::WorkItem(WorkItem&& rhs) noexcept {
	devID.swap(rhs.devID);
	filePath.swap(rhs.filePath);
	filePathLower.swap(rhs.filePathLower);
	fileSize = rhs.fileSize;
}

HashManager::Hasher::WorkItem& HashManager::Hasher::WorkItem::operator=(WorkItem&& rhs) noexcept {
	devID.swap(rhs.devID);
	filePath.swap(rhs.filePath);
	filePathLower.swap(rhs.filePathLower);
	fileSize = rhs.fileSize;
	return *this; 
}

HashManager::HashPauser::HashPauser() {
	HashManager::getInstance()->pauseHashing();
}

HashManager::HashPauser::~HashPauser() {
	HashManager::getInstance()->resumeHashing();
}

bool HashManager::pauseHashing() noexcept {
	pausers++;
	if (pausers == 1) {
		RLock l (Hasher::hcs);
		for (auto h: hashers)
			h->pause();
		return isHashingPaused(false);
	}
	return true;
}

void HashManager::resumeHashing(bool forced) {
	if (forced )
		pausers = 0;
	else if (pausers > 0)
		pausers--;

	if (pausers == 0) {
		RLock l(Hasher::hcs);
		for(auto h: hashers)
			h->resume();
	}
}

bool HashManager::isHashingPaused(bool lock /*true*/) const noexcept {
	ConditionalRLock l(Hasher::hcs, lock);
	return all_of(hashers.begin(), hashers.end(), [](const Hasher* h) { return h->isPaused(); });
}

} // namespace dcpp
