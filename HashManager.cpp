/* 
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#include "Util.h"
#include "ResourceManager.h"
#include "SimpleXML.h"
#include "LogManager.h"
#include "File.h"
#include "FileReader.h"
#include "ZUtils.h"
#include "ShareManager.h"
#include "AirUtil.h"
#include "ScopedFunctor.h"

# ifdef _DEBUG
#   pragma comment(lib, "libdb53sd.lib")
#	pragma comment(lib, "libdb_stl53sd.lib")
# else
#   pragma comment(lib, "libdb53s.lib")
#	pragma comment(lib, "libdb_stl53s.lib")
# endif

namespace dcpp {

using boost::range::find_if;

SharedMutex HashManager::Hasher::hcs;
const int64_t HashManager::MIN_BLOCK_SIZE = 64 * 1024;

HashManager::HashManager(): /*nextSave(0),*/ pausers(0), aShutdown(false) {
	//TimerManager::getInstance()->addListener(this);
}

HashManager::~HashManager() { 
}

bool HashManager::checkTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp, TTHValue& outTTH_) {
	auto nameLower = Text::toLower(aFileName);
	if (!store.checkTTH(nameLower, aSize, aTimeStamp, outTTH_)) {
		hashFile(aFileName, move(nameLower), aSize);
		return false;
	}
	return true;
}

void HashManager::getFileInfo(const string& aFileName, HashedFile& fi_) {
	auto nameLower = Text::toLower(aFileName);
	auto found = store.getFileInfo(nameLower, fi_);
	if (!found) {
		auto size = File::getSize(aFileName);
		hashFile(aFileName, move(nameLower), size);
		throw HashException();
	}
}

bool HashManager::getTree(const TTHValue& root, TigerTree& tt) {
	return store.getTree(root, tt);
}

size_t HashManager::getBlockSize(const TTHValue& root) {
	return static_cast<size_t>(store.getRootInfo(root, HashStore::TYPE_BLOCKSIZE));
}

int64_t HashManager::Hasher::getTimeLeft() const {
	return lastSpeed > 0 ? (totalBytesLeft / lastSpeed) : 0;
}

bool HashManager::Hasher::getPathVolume(const string& aPath, string& vol_) const { 
	auto p = find_if(devices | map_keys, [&aPath](const string& aVol) { return strncmp(aPath.c_str(), aVol.c_str(), aVol.length()) == 0; });
	if (p.base() != devices.end()) {
		vol_ = *p;
		return true;
	}
	return false;
}

void HashManager::hashFile(const string& filePath, string&& pathLower, int64_t size) {
	if(aShutdown) //we cant allow adding more hashers if we are shutting down, it will result in infinite loop
		return;

	Hasher* h = nullptr;

	WLock l(Hasher::hcs);

	//get the volume name
	string vol;
	if (none_of(hashers.begin(), hashers.end(), [&](const Hasher* h) { return h->getPathVolume(pathLower, vol); })) {
		TCHAR* buf = new TCHAR[pathLower.length()];
		GetVolumePathName(Text::toT(pathLower).c_str(), buf, pathLower.length());
		vol = Text::fromT(buf);
		delete[] buf;
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
			} else if (hashers.size() >= SETTING(MAX_HASHING_THREADS)) {
				// can't create new ones
				h = *getLeastLoaded(hashers);
			}
		} else {
			//get the hashers with this volume
			HasherList volHashers;
			copy_if(hashers.begin(), hashers.end(), back_inserter(volHashers), [&vol](const Hasher* aHasher) { return aHasher->hasDevice(vol); });

			if (volHashers.empty() && hashers.size() >= SETTING(MAX_HASHING_THREADS)) {
				//we just need choose from all hashers
				h = *getLeastLoaded(hashers);
			} else {
				auto minLoaded = getLeastLoaded(volHashers);

				//don't create new hashers if the file is less than 10 megabytes and there's a hasher with less than 200MB queued, or the maximum number of threads have been reached for this volume
				if (hashers.size() >= SETTING(MAX_HASHING_THREADS) || volHashers.size() >= SETTING(HASHERS_PER_VOLUME) || (size <= 10*1024*1024 && !volHashers.empty() && (*minLoaded)->getBytesLeft() <= 200*1024*1024)) {
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
	h->hashFile(filePath, move(pathLower), size, move(vol));
}

void HashManager::getFileTTH(const string& aFile, int64_t aSize, bool addStore, TTHValue& tth_, int64_t& sizeLeft_, const bool& aCancel, std::function<void (int64_t, const string&)> updateF/*nullptr*/) {
	auto pathLower = move(Text::toLower(aFile));
	if (!store.checkTTH(pathLower, aSize, AirUtil::getLastWrite(aFile), tth_)) {
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
			auto fi = HashedFile(tth_, timestamp, aSize);
			store.addFile(move(pathLower), tt, fi);
		}
	}
}

void HashManager::hashDone(const string& aFileName, string&& pathLower, const TigerTree& tt, int64_t speed, HashedFile& aFileInfo, int hasherID /*0*/) {
	try {
		store.addFile(move(pathLower), tt, aFileInfo);
	} catch (const Exception& e) {
		log(STRING(HASHING_FAILED) + " " + e.getError(), hasherID, true, true);
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
			log(STRING(HASHING_FINISHED) + " " + fn + " (" + Util::formatBytes(speed) + "/s)", hasherID, false, true);
		} else {
			log(STRING(HASHING_FINISHED) + " " + fn, hasherID, false, true);
		}
	}
}

void HashManager::HashStore::addFile(string&& aFileLower,  const TigerTree& tt, HashedFile& fi_) {
	addTree(tt);
	fileIndex[aFileLower] =  fi_;
}


void HashManager::HashStore::addTree(const TigerTree& tt) noexcept {
	//hashData.insert(make_pair(tt.getRoot(), tt));


	auto tth = tt.getRoot();
	Dbt key(&tth, sizeof(TTHValue));


	Dbt data;

	size_t treelen = tt.getLeaves().size() == 1 ? 0 : tt.getLeaves().size() * TTHValue::BYTES;
	auto sz = sizeof(int64_t) + sizeof(int64_t) + sizeof(treelen) + treelen;

	data.set_ulen(sz);
	data.set_size(sz);

	//allocate the memory
	void* buf = malloc(data.get_ulen());
	data.set_flags(DB_DBT_USERMEM);


	//set the data
	char *p = (char *)buf;


	int64_t fileSize = tt.getFileSize();
	memcpy(p, &fileSize, sizeof(int64_t));
	p += sizeof(int64_t);

	int64_t blockSize = tt.getBlockSize();
	memcpy(p, &blockSize, sizeof(int64_t));
	p += sizeof(int64_t);

	memcpy(p, &treelen, sizeof(size_t));
	p += sizeof(size_t);

	if (treelen > 0)
		memcpy(p, tt.getLeaves()[0].data, treelen);

	data.set_data(buf);

	int ret = 0;
	//put in the database
	try {
		ret = hashDb->put(NULL, &key, &data, DB_NOOVERWRITE);
	} catch(DbException &e) {
		hashDb->err(e.get_errno(), "Error!");
	} catch(std::exception &e) {
		hashDb->errx("Error! %s", e.what());
	}

	isDbError(ret);

	free(buf);
}

#define INITIAL_BUF 15*1024
bool HashManager::HashStore::getTree(const TTHValue& root, TigerTree& tt) const {
	auto tth = root;
	Dbt key(&tth, sizeof(TTHValue));
	key.set_flags(DB_DBT_USERMEM);

	Dbt data;
	data.set_flags(DB_DBT_USERMEM);

	//set a reasonable value to start with
	data.set_ulen(INITIAL_BUF);

	void* buf = malloc(INITIAL_BUF);
	data.set_data(buf);
	ScopedFunctor([&buf] { free(buf); });


	auto ret = hashDb->get(NULL, &key, &data, 0);
	if (ret == DB_BUFFER_SMALL) {
		//enlarge the buffer
		data.set_ulen(data.get_size());
		realloc(buf, data.get_size());

		ret = hashDb->get(NULL, &key, &data, 0);
	}

	if (isDbError(ret))
		return false;

	loadTree(root, tt, data.get_data());
	return true;


	/*auto p = hashData.find(root);
	if (p != hashData.end()) {
		tt = p->second;
		return true;
	}

	return false;*/
}

void HashManager::HashStore::loadTree(const TTHValue& aRoot, TigerTree& aTree, const void *src) {
	char *p = (char*)src;

	int64_t fileSize;
	memcpy(&fileSize, p, sizeof(int64_t));
	p += sizeof(int64_t);

	int64_t blockSize;
	memcpy(&blockSize, p, sizeof(int64_t));
	p += sizeof(int64_t);

	//size_t datalen = TigerTree::calcBlocks(fileSize, blockSize) * TTHValue::BYTES;
	size_t datalen;
	memcpy(&datalen, p, sizeof(size_t));
	p += sizeof(size_t);

	if (datalen > 0) {
		boost::scoped_array<uint8_t> buf(new uint8_t[datalen]);
		memcpy(&buf[0], p, datalen);
		aTree = TigerTree(fileSize, blockSize, &buf[0]);
	} else {
		aTree = TigerTree(fileSize, blockSize, aRoot);
	}
}

void HashManager::HashStore::loadFileInfo(HashedFile& aFile, const void *src) {
	char *p = (char*)src;

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
}

void HashManager::HashStore::saveFileInfo(void *dest, const HashedFile& aFile) {
	char *p = (char *)dest;

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

u_int32_t HashManager::HashStore::getFileInfoSize(const HashedFile& /*aTree*/) {
	return sizeof(uint64_t) + sizeof(TTHValue) + sizeof(int64_t);
}

/*void HashManager::HashStore::saveTree(void *dest, const TigerTree& aTree) {
	char *p = (char *)dest;

	int64_t fileSize = aTree.getFileSize();
	memcpy(p, &fileSize, sizeof(int64_t));
	p += sizeof(int64_t);

	int64_t blockSize = aTree.getBlockSize();
	memcpy(p, &blockSize, sizeof(int64_t));
	p += sizeof(int64_t);

	size_t datalen = aTree.getLeaves().size() * TTHValue::BYTES;

	//size_t datalen = aTree.getLeaves().size() == 1 ? 0 : aTree.getLeaves().size() * TTHValue::BYTES;
	memcpy(p, &datalen, sizeof(size_t));
	p += sizeof(size_t);

	//if (datalen > 0)
		memcpy(p, aTree.getLeaves()[0].data, datalen);
}

void HashManager::HashStore::loadTree(TigerTree& aTree, const void *src) {
	char *p = (char*)src;

	int64_t fileSize;
	memcpy(&fileSize, p, sizeof(int64_t));
	p += sizeof(int64_t);

	int64_t blockSize;
	memcpy(&blockSize, p, sizeof(int64_t));
	p += sizeof(int64_t);

	//size_t datalen = TigerTree::calcBlocks(fileSize, blockSize) * TTHValue::BYTES;
	size_t datalen;
	memcpy(&datalen, p, sizeof(size_t));
	p += sizeof(size_t);

	if (datalen > TTHValue::BYTES) {
		boost::scoped_array<uint8_t> buf(new uint8_t[datalen]);
		memcpy(&buf[0], p, datalen);
		aTree = TigerTree(fileSize, blockSize, &buf[0]);
	} else {
		TTHValue root;
		memcpy(&root, p, sizeof(TTHValue));
		aTree = TigerTree(fileSize, blockSize, root);
	}
}

u_int32_t HashManager::HashStore::getTreeSize(const TigerTree& aTree) {
	return sizeof(aTree.getFileSize()) + sizeof(aTree.getFileSize()) + sizeof(size_t) + (aTree.getLeaves().size() * TTHValue::BYTES);
}*/

bool HashManager::HashStore::loadLegacyTree(File& f, const TreeInfo& ti, const TTHValue& root, TigerTree& tt, bool rebuilding) {
	if (ti.getIndex() == SMALL_TREE) {
		tt = TigerTree(ti.getSize(), ti.getBlockSize(), root);
		return true;
	}
	try {
		f.setPos(ti.getIndex());
		size_t datalen = TigerTree::calcBlocks(ti.getSize(), ti.getBlockSize()) * TTHValue::BYTES;
		boost::scoped_array<uint8_t> buf(new uint8_t[datalen]);
		f.read(&buf[0], datalen);
		tt = TigerTree(ti.getSize(), ti.getBlockSize(), &buf[0]);
		if (!(tt.getRoot() == root))
			throw HashException(STRING(INVALID_TREE));
	} catch (const Exception& e) {
		if (!rebuilding)
			LogManager::getInstance()->message(STRING_F(TREE_LOAD_FAILED, root.toBase32() % e.getError()), LogManager::LOG_ERROR);
		return false;
	}

	return true;
}

int64_t HashManager::HashStore::getRootInfo(const TTHValue& root, InfoType aType) const {
	auto tth = root;
	int64_t value;

	Dbt key(&tth, sizeof(TTHValue));
	key.set_flags(DB_DBT_USERMEM);

	Dbt data;
	data.set_data(&value);
	data.set_flags(DB_DBT_PARTIAL | DB_DBT_USERMEM);
	data.set_dlen(sizeof(int64_t));
	data.set_doff(aType == TYPE_FILESIZE ? 0 : sizeof(int64_t));
	data.set_ulen(sizeof(int64_t));


	auto ret = hashDb->get(NULL, &key, &data, 0);
	if (isDbError(ret))
		return 0;

	return value;

	//auto i = hashData.find(root);
	//return i == hashData.end() ? 0 : static_cast<size_t>(i->second.getBlockSize());
}

bool HashManager::HashStore::checkTTH(const string& aFileLower, int64_t aSize, uint32_t aTimeStamp, TTHValue& outTTH_) {
	auto i = fileIndex.find(aFileLower);
	if (i != fileIndex.end()) {
		if (i->second.getTimeStamp() == aTimeStamp && i->second.getSize() == aSize) {
			outTTH_ = i->second.getRoot();
			return true;
		}
	}

	return false;
}

bool HashManager::HashStore::getFileInfo(const string& aFileLower, HashedFile& fi_) {
	auto p = fileIndex.find(aFileLower);
	if (p != fileIndex.end()) {
		fi_ = p->second;
		return true;
	}

	return false;
}

void HashManager::HashStore::rebuild() {
	try {
		int unusedTrees = 0;
		int failedTrees = 0;
		int unusedFiles=0; 
		int64_t failedSize = 0;

		unordered_set<TTHValue> usedRoots;
		{
			/*
			
			//DB map doesn't allow erasing while looping, do it in the hard way

			Dbc *cursorp;
			fileDb->cursor(NULL, &cursorp, 0); 

			string curPath;
			HashedFile fi;

			Dbt key(&curPath, UNC_MAX_PATH);
			key.set_flags(DB_DBT_USERMEM);
			key.set_ulen(UNC_MAX_PATH);

			size_t bufSize = sizeof(uint64_t) + sizeof(TTHValue) + sizeof(int64_t);

			Dbt data(&fi, bufSize);
			data.set_ulen(bufSize);
			data.set_flags(DB_DBT_USERMEM);
			//void* buf = malloc(bufSize);
			//data.set_data(buf);

			int ret;
			while ((ret = cursorp->get(&key, &data, DB_PREV)) == 0) {
				if (ShareManager::getInstance()->isRealPathShared(curPath)) {
					usedRoots.insert(fi.getRoot());
				} else {
					cursorp->del(0);
				}
			}

			if (ret != 0 && ret != DB_NOTFOUND) {
				auto error = db_strerror(ret);
				dcassert(0);
			}

			//free(buf);
			cursorp->close();*/

			StringList toErase;
			for (auto& i: fileIndex) {
				if (ShareManager::getInstance()->isRealPathShared(i.first)) {
					usedRoots.insert(i.second.getRoot());
				} else {
					unusedFiles++;
					toErase.push_back(i.first);
				}
			}

			for (auto& i: toErase) {
				fileIndex.erase(i);
			}
		}

		{
			Dbc *cursorp;
			hashDb->cursor(NULL, &cursorp, 0); 

			TTHValue curRoot;
			TigerTree tt;

			Dbt key(&curRoot, sizeof(TTHValue));
			key.set_flags(DB_DBT_USERMEM);
			key.set_ulen(sizeof(TTHValue));

			Dbt data;
			size_t bufSize = 1024*1024;
			data.set_ulen(bufSize);
			data.set_flags(DB_DBT_USERMEM);
			void* buf = malloc(bufSize);
			data.set_data(buf);

			int ret;
			while ((ret = cursorp->get(&key, &data, DB_PREV)) == 0) {
				if (usedRoots.find(curRoot) == usedRoots.end()) {
					auto ret = cursorp->del(0);
					dcassert(ret == 0);
					unusedTrees++;
				} else {
					//check if the data is valid
					/*loadTree(curRoot, tt, data.get_data());
					tt.calcRoot();
					if (tt.getRoot() == curRoot)	
						continue;

					//failed
					failedSize += tt.getFileSize();
					failedTrees++;
					usedRoots.erase(curRoot);*/
				}
			}

			isDbError(ret);

			free(buf);
			cursorp->close();
		}


		string msg;
		if (unusedFiles > 0 || unusedTrees > 0) {
			msg = STRING_F(HASH_REBUILT_UNUSED, unusedFiles % unusedTrees);
		} else {
			msg = STRING(HASH_REBUILT_NO_UNUSED);
		}

		if (failedTrees > 0) {
			msg += ". ";
			msg += STRING_F(REBUILD_FAILED_ENTRIES, failedTrees % Util::formatBytes(failedSize));
		}

		LogManager::getInstance()->message(msg, failedTrees > 0 ? LogManager::LOG_ERROR : LogManager::LOG_INFO);
	} catch (const Exception& e) {
		LogManager::getInstance()->message(STRING(HASHING_FAILED) + " " + e.getError(), LogManager::LOG_ERROR);
	}
}

bool HashManager::HashStore::isDbError(int err) const {
	if (err == 0)
		return false;
	
	if (err != DB_NOTFOUND && err != DB_KEYEXIST) {
		string error(db_strerror(err));
		LogManager::getInstance()->message("Database error:" + error, LogManager::LOG_ERROR);
		dcassert(0);
	}
	return true;
}

void HashManager::HashStore::updateCacheSize() {
	if (SETTING(DB_CACHE_AUTOSET)) {
		/*DB_BTREE_STAT *statp;
		int ret;
        if ((ret = fileDb->stat(NULL, &statp, DB_FAST_STAT)) != 0) {
			return;
        }

		auto tableSize = statp->bt_nkeys;
		free(statp);*/

		//auto indexSize = fileIndex.size(false);
		//size_t newSize = max((indexSize*120) / (1024*1024), static_cast<size_t>(1));
		//SettingsManager::getInstance()->set(SettingsManager::DB_CACHE_SIZE, static_cast<int>(newSize));
	}
}


string statMsg;
void printF(const DbEnv* /*env*/, const char* msg) {
	statMsg += msg;
	statMsg += "\n";
}

string HashManager::HashStore::getDbStats() {
	string ret;

	dbEnv.set_msgcall(printF);

	statMsg += "\n\nGENERAL STATS\n\n";
	dbEnv.stat_print(0);
	statMsg += "\n\nMEMORY STATS\n\n";
	dbEnv.memp_stat_print(0);
	statMsg += "\n\nLOCKING STATS\n\n";
	dbEnv.mutex_stat_print(0);
	/*statMsg += "\n\nHASHDATA STATS\n\n";
	hashDb->stat_print(0);
	statMsg += "\n\nFILEINDEX STATS\n\n";
	fileDb->stat_print(0);*/

	dbEnv.set_msgcall(NULL);
	//hashDb->set_msgcall(NULL);

	ret.swap(statMsg);
	return ret;
}

string HashManager::HashStore::getIndexFile() { return Util::getPath(Util::PATH_USER_CONFIG) + "HashIndex.xml"; }

class HashLoader: public SimpleXMLReader::CallBack {
public:
	HashLoader(HashManager::HashStore& s, const CountedInputStream<false>& countedStream, uint64_t fileSize, function<void (float)> progressF) :
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
		migrating(false)
	{ }

	void startTag(const string& name, StringPairList& attribs, bool simple);
	void endTag(const string& name);
	
	bool migrating;
private:
	HashManager::HashStore& store;

	const CountedInputStream<false>& countedStream;
	uint64_t streamPos;
	uint64_t fileSize;
	function<void (float)> progressF;

	int version;
	string file;

	bool inTrees;
	bool inFiles;
	bool inHashStore;

	unique_ptr<File> dataFile;
};

void HashManager::HashStore::load(function<void (float)> progressF) {
	//migrate the old database file
	Util::migrate(Util::getPath(Util::PATH_USER_CONFIG) + "HashStore.db");

	uint32_t cacheSize = static_cast<uint32_t>(max(SETTING(DB_CACHE_SIZE), 1));

	//set the flags 
	u_int32_t db_flags = DB_CREATE /*| DB_READ_UNCOMMITTED*/;
	u_int32_t env_flags = DB_PRIVATE | DB_THREAD | DB_CREATE |
		DB_INIT_MPOOL | DB_AUTO_COMMIT | DB_INIT_LOCK;

	try {
		dbEnv.open(Util::getPath(Util::PATH_USER_CONFIG).c_str(), env_flags, 0);
		dbEnv.set_cachesize(0, cacheSize*1024*1024, 1);
		dbEnv.set_lk_detect(DB_LOCK_DEFAULT);

		// Redirect debugging information to std::cerr
		dbEnv.set_error_stream(&std::cerr);

		hashDb = new Db(&dbEnv, DB_CXX_NO_EXCEPTIONS);
		hashDb->open(NULL, // Transaction pointer 
			"HashData.db", // Database file name 
			NULL, // Optional logical database name
			DB_HASH, // Database access method
			db_flags, // Open flags
			0); // File mode (using defaults)

		fileDb = new Db(&dbEnv, DB_CXX_NO_EXCEPTIONS);
		fileDb->open(NULL, // Transaction pointer 
			"FileIndex.db", // Database file name 
			NULL, // Optional logical database name
			DB_BTREE, // Database access method
			db_flags, // Open flags
			0); // File mode (using defaults)

		fileIndex.set_db_handle(fileDb, &dbEnv);
		//hashData.set_db_handle(hashDb, &dbEnv);
	} catch(DbException &e) {
		LogManager::getInstance()->message("Failed to open the hash database: " + string(e.what()), LogManager::LOG_ERROR);
	} catch(Exception& e) {
		LogManager::getInstance()->message("Failed to open the hash database: " + string(e.getError()), LogManager::LOG_ERROR);
	}

	/*dbstl::DbstlElemTraits<TigerTree> *pTigerTraits = dbstl::DbstlElemTraits<TigerTree>::instance();
	pTigerTraits->set_size_function(getTreeSize);
	pTigerTraits->set_copy_function(saveTree);
	pTigerTraits->set_restore_function(loadTree);*/

	dbstl::DbstlElemTraits<HashedFile> *pTigerTraits = dbstl::DbstlElemTraits<HashedFile>::instance();
	pTigerTraits->set_size_function(getFileInfoSize);
	pTigerTraits->set_copy_function(saveFileInfo);
	pTigerTraits->set_restore_function(loadFileInfo);

	if (SettingsManager::lanMode)
		return;

	try {
		Util::migrate(getIndexFile());
		bool migrating = false;

		{
			File f(getIndexFile(), File::READ, File::OPEN);
			CountedInputStream<false> countedStream(&f);
			HashLoader l(*this, countedStream, f.getSize(), progressF);
			SimpleXMLReader(&l).parse(countedStream);

			migrating = l.migrating;
		}

		if (migrating) {
			auto dataFile = Util::getPath(Util::PATH_USER_CONFIG) + "HashData.dat";
			auto indexFile = Util::getPath(Util::PATH_USER_CONFIG) + "HashIndex.xml";

			File::renameFile(dataFile, dataFile + ".bak");
			File::renameFile(indexFile, indexFile + ".bak");
		}
	} catch (const Exception&) {
		// ...
	}

	//countNextSave();
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
	//ScopedFunctor([this] {
		auto readBytes = countedStream.getReadBytes();
		if(readBytes != streamPos) {
			streamPos = readBytes;
			progressF(static_cast<float>(readBytes) / static_cast<float>(fileSize));
		}
	//});

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
					if (!migrating) {
						migrating = true;
						if (!dataFile) {
							dataFile.reset(new File(Util::getPath(Util::PATH_USER_CONFIG) + "HashData.dat", File::READ, File::OPEN | File::SHARED | File::RANDOM_ACCESS));
						}
					}

					if (dataFile) {
						TigerTree tt;
						if (store.loadLegacyTree(*dataFile.get(), HashManager::HashStore::TreeInfo(size, index, blockSize), tth, tt)) {
							store.addTree(tt);
						}
					}
				} catch (const Exception&) {
					//..
				}
			}
		} else if (inFiles && name == sFile) {
			file = getAttrib(attribs, sName, 0);
			auto timeStamp = Util::toUInt32(getAttrib(attribs, sTimeStamp, 1));
			const string& root = getAttrib(attribs, sRoot, 2);

			if (!file.empty() && timeStamp > 0 && !root.empty()) {
				string fileLower = Text::toLower(file);
				auto size = store.getRootInfo(TTHValue(root), HashManager::HashStore::TYPE_FILESIZE);
				if (size > 0)
					store.fileIndex[fileLower] = HashedFile(TTHValue(root), timeStamp, size);
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

HashManager::HashStore::HashStore() :
	/*dirty(false),*/ dbEnv(DB_CXX_NO_EXCEPTIONS) {
}

HashManager::HashStore::~HashStore() {
	try {
		// Close the database
		hashDb->close(0);
		fileDb->close(0);

		dbEnv.close(0);
	} catch(DbException& /*e*/) {
		//...
	} catch(std::exception& /*e*/) {
		//...
	}

	delete fileDb;
	delete hashDb;
}

void HashManager::Hasher::hashFile(const string& fileName, string&& filePathLower, int64_t size, string&& devID) {
	//always locked
	auto wi = move(WorkItem(fileName, move(filePathLower), size, move(devID)));

	bool added = false;
	if (w.empty()) {
		w.push_back(wi);
		added = true;
	} else if (HashSortOrder()(w.back(), wi)) {
		// When adding large amounts of files from ShareManager, they should be sorted already. Prefer using push_back because of resource optimization
		if(compare(w.back().filePath, fileName) != 0) {
			w.push_back(wi);
			added = true;
		}
	} else {
		auto hqr = equal_range(w.begin(), w.end(), wi, HashSortOrder());
		if (hqr.first == hqr.second) {
			//it doesn't exist yet
			w.insert(hqr.first, move(wi));
			added = true;
		}
	}

	if (added) {
		devices[wi.devID]++; 
		totalBytesLeft += size;
		s.signal();
	}
}

bool HashManager::Hasher::pause() {
	paused = true;
	return paused;
}

void HashManager::Hasher::resume() {
	paused = false;
	t_resume();
}

bool HashManager::Hasher::isPaused() const {
	return paused;
}

void HashManager::Hasher::removeDevice(const string& aID) {
	dcassert(!aID.empty());
	auto dp = devices.find(aID);
	if (dp != devices.end()) {
		dp->second--;
		if (dp->second == 0)
			devices.erase(dp);
	}
}

void HashManager::Hasher::stopHashing(const string& baseDir) {
	for (auto i = w.begin(); i != w.end();) {
		if (strnicmp(baseDir, i->filePath, baseDir.length()) == 0) {
			totalBytesLeft -= i->fileSize;
			removeDevice(i->devID);
			i = w.erase(i);
		} else {
			++i;
		}
	}
}

void HashManager::stopHashing(const string& baseDir) {
	WLock l(Hasher::hcs);
	for (auto h: hashers)
		h->stopHashing(baseDir); 
}

void HashManager::setPriority(Thread::Priority p) {
	RLock l(Hasher::hcs);
	for (auto h: hashers)
		h->setThreadPriority(p); 
}

void HashManager::getStats(string& curFile, int64_t& bytesLeft, size_t& filesLeft, int64_t& speed, int& hasherCount) {
	RLock l(Hasher::hcs);
	hasherCount = hashers.size();
	for (auto i: hashers)
		i->getStats(curFile, bytesLeft, filesLeft, speed);
}

void HashManager::rebuild() {
	hashers.front()->scheduleRebuild(); 
}

void HashManager::startup(function<void (float)> progressF) {
	hashers.push_back(new Hasher(false, 0));
	store.load(progressF); 
}

void HashManager::stop() {
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

void HashManager::Hasher::scheduleRebuild() { 
	rebuild = true; 
	s.signal(); 
	if(paused) 
		t_resume(); 
}

void HashManager::shutdown(function<void (float)> progressF) {
	aShutdown = true;
	//TimerManager::getInstance()->removeListener(this);

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

	store.updateCacheSize();
}

void HashManager::Hasher::clear() {
	w.clear();
	devices.clear();
	totalBytesLeft = 0;
}

void HashManager::Hasher::getStats(string& curFile, int64_t& bytesLeft, size_t& filesLeft, int64_t& speed) {
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

HashManager::Hasher::Hasher(bool isPaused, int aHasherID) : closing(false), running(false), paused(isPaused), rebuild(false), /*saveData(false),*/ totalBytesLeft(0), lastSpeed(0), sizeHashed(0), hashTime(0), dirsHashed(0),
	filesHashed(0), dirFilesHashed(0), dirSizeHashed(0), dirHashTime(0), hasherID(aHasherID) { 

	start();
	if (isPaused)
		t_suspend();
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

		if(rebuild) {
			HashManager::getInstance()->doRebuild();
			rebuild = false;
			continue;
		}
		
		bool failed = true;
		bool dirChanged = false;
		string curDevID, pathLower;
		{
			WLock l(hcs);
			if(!w.empty()) {
				auto& wi = w.front();
				dirChanged = compare(Util::getFilePath(wi.filePath), Util::getFilePath(fname)) != 0;
				currentFile = fname = move(wi.filePath);
				curDevID = move(wi.devID);
				pathLower = move(wi.filePathLower);
				dcassert(!curDevID.empty());
				w.pop_front();
			} else {
				fname.clear();
			}
		}
		running = true;

		HashedFile fi;
		if(!fname.empty()) {
			try {
				if (initialDir.empty()) {
					initialDir = Util::getFilePath(fname);
				}

				if (dirChanged)
					sfv.loadPath(Util::getFilePath(fname));
				uint64_t start = GET_TICK();
				File f(fname, File::READ, File::OPEN);
				int64_t size = f.getSize();
				int64_t bs = max(TigerTree::calcBlockSize(size, 10), MIN_BLOCK_SIZE);
				uint64_t timestamp = f.getLastModified();
				int64_t sizeLeft = size;
				TigerTree tt(bs);

				CRC32Filter crc32;

				auto fileCRC = sfv.hasFile(Util::getFileName(pathLower));

				uint64_t lastRead = GET_TICK();
 
                FileReader fr(true);
				fr.read(fname, [&](const void* buf, size_t n) -> bool {
					uint64_t now = GET_TICK();
					if(SETTING(MAX_HASH_SPEED)> 0) {
						uint64_t minTime = n * 1000LL / (SETTING(MAX_HASH_SPEED) * 1024LL * 1024LL);
 
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

					if(totalBytesLeft > 0)
						totalBytesLeft -= n;
					if(now > start)
						lastSpeed = (size - sizeLeft)*1000 / (now -start);

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
					HashManager::getInstance()->log(STRING(ERROR_HASHING) + fname + ": " + STRING(ERROR_HASHING_CRC32), hasherID, true, true);
					HashManager::getInstance()->fire(HashManagerListener::HashFailed(), fname, fi);
				} else {
					fi = HashedFile(tt.getRoot(), timestamp, size);
					HashManager::getInstance()->hashDone(fname, move(pathLower), tt, averageSpeed, fi, hasherID);
					//tth = tt.getRoot();
				}
			} catch(const FileException& e) {
				HashManager::getInstance()->log(STRING(ERROR_HASHING) + " " + fname + ": " + e.getError(), hasherID, true, true);
				HashManager::getInstance()->fire(HashManagerListener::HashFailed(), fname, fi);
			}
		
		}

		auto onDirHashed = [&] () -> void {
			if ((SETTING(HASHERS_PER_VOLUME) == 1 || w.empty()) && (dirFilesHashed > 1 || !failed)) {
				if (dirFilesHashed == 1) {
					HashManager::getInstance()->log(STRING_F(HASHING_FINISHED_FILE, currentFile % 
						Util::formatBytes(dirSizeHashed) % 
						Util::formatTime(dirHashTime / 1000, true) % 
						(Util::formatBytes(dirHashTime > 0 ? ((dirSizeHashed * 1000) / dirHashTime) : 0) + "/s" )), hasherID, false, false);
				} else {
					HashManager::getInstance()->log(STRING_F(HASHING_FINISHED_DIR, Util::getFilePath(initialDir) % 
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
						HashManager::getInstance()->log(STRING_F(HASHING_FINISHED_TOTAL, filesHashed % Util::formatBytes(sizeHashed) % dirsHashed % 
							Util::formatTime(hashTime / 1000, true) % 
							(Util::formatBytes(hashTime > 0 ? ((sizeHashed * 1000) / hashTime) : 0)  + "/s" )), hasherID, false, false);
					}
				}
				hashTime = 0;
				sizeHashed = 0;
				dirsHashed = 0;
				filesHashed = 0;
				deleteThis = hasherID != 0;
			} else if (!AirUtil::isParentOrExact(initialDir, w.front().filePath)) {
				onDirHashed();
			}

			currentFile.clear();
		}

		if (!failed && !fname.empty())
			HashManager::getInstance()->fire(HashManagerListener::TTHDone(), fname, fi);

		if (deleteThis) {
			//check again if we have added new items while this was unlocked

			WLock l(hcs);
			if (w.empty()) {
				//Nothing more to has, delete this hasher
				HashManager::getInstance()->removeHasher(this);
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

bool HashManager::Hasher::HashSortOrder::operator()(const WorkItem& left, const WorkItem& right) const {
	// Case-sensitive (faster), it is rather unlikely that case changes, and if it does it's harmless.
	auto comp = compare(Util::getFilePath(left.filePathLower), Util::getFilePath(right.filePathLower));
	if (comp == 0) {
		return compare(left.filePathLower, right.filePathLower) < 0;
	}
	return comp < 0;
}

HashManager::HashPauser::HashPauser() {
	HashManager::getInstance()->pauseHashing();
}

HashManager::HashPauser::~HashPauser() {
	HashManager::getInstance()->resumeHashing();
}

bool HashManager::pauseHashing() {
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

bool HashManager::isHashingPaused(bool lock /*true*/) const {
	ConditionalRLock l(Hasher::hcs, lock);
	return all_of(hashers.begin(), hashers.end(), [](const Hasher* h) { return h->isPaused(); });
}

void HashManager::doRebuild() {
	// its useless to allow hashing with other threads during rebuild. ( TODO: Disallow resuming and show something in hashprogress )
	HashPauser pause;
	store.rebuild();
}

} // namespace dcpp