/*
 * Copyright (C) 2011-2013 AirDC++ Project
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
#include "LevelDB.h"
#include "Util.h"
#include "version.h"
#include "ResourceManager.h"
#include "File.h"
#include "LogManager.h"

#include <leveldb/comparator.h>
#include <leveldb/filter_policy.h>
#include <leveldb/cache.h>
#include <leveldb/slice.h>
#include <leveldb/write_batch.h>

#define MAX_DB_RETRIES 10

namespace dcpp {

LevelDB::LevelDB(const string& aPath, const string& aFriendlyName, uint64_t cacheSize, int maxOpenFiles, bool useCompression, uint64_t aBlockSize /*4096*/) : DbHandler(aPath, aFriendlyName, cacheSize), 
	totalWrites(0), totalReads(0), ioErrors(0), db(nullptr) {

	readoptions.verify_checksums = false;
	iteroptions.verify_checksums = false;

	iteroptions.fill_cache = false;
	readoptions.fill_cache = true;

	options.env = leveldb::Env::Default();
	options.compression = useCompression ? leveldb::kSnappyCompression : leveldb::kNoCompression;
	options.max_open_files = maxOpenFiles;
	options.block_size = aBlockSize;
	options.block_cache = leveldb::NewLRUCache(cacheSize);
	options.paranoid_checks = false;
	//options.write_buffer_size = cacheSize / 4; // up to two write buffers may be held in memory simultaneously
	options.filter_policy = leveldb::NewBloomFilterPolicy(10);
	options.create_if_missing = true;
}

string LevelDB::getRepairFlag() const { return dbPath + "REPAIR"; }

void LevelDB::open(StepFunction stepF, MessageFunction messageF) {
	bool forceRepair = Util::fileExists(getRepairFlag());
	if (forceRepair) {
		repair(stepF, messageF);
		File::deleteFile(getRepairFlag());
	}

	auto ret = leveldb::DB::Open(options, Text::fromUtf8(dbPath), &db);
	if (!ret.ok()) {
		if (ret.IsIOError()) {
			// most likely there's another instance running or the permissions are wrong
			messageF(STRING_F(DB_OPEN_FAILED_IO, getNameLower() % Text::toUtf8(ret.ToString()) % APPNAME % dbPath % APPNAME), false, true);
			exit(0);
		} else if (!forceRepair) {
			// the database is corrupted?
			messageF(STRING_F(DB_OPEN_FAILED_REPAIR, getNameLower() % Text::toUtf8(ret.ToString()) % APPNAME), false, false);
			repair(stepF, messageF);

			// try it again
			ret = leveldb::DB::Open(options, Text::fromUtf8(dbPath), &db);
		}
	}

	if (!ret.ok()) {
		messageF(STRING_F(DB_OPEN_FAILED, getNameLower() % Text::toUtf8(ret.ToString()) % APPNAME), false, true);
		exit(0);
	}
}

void LevelDB::repair(StepFunction stepF, MessageFunction messageF) {
	stepF(STRING_F(REPAIRING_X, getNameLower()));
	
	//remove any existing log
	string logPath = dbPath + "repair.log";
	File::deleteFile(logPath);
	options.env->NewLogger(Text::fromUtf8(logPath), &options.info_log);

	auto ret = leveldb::RepairDB(Text::fromUtf8(dbPath), options);
	if (!ret.ok()) {
		messageF(STRING_F(DB_REPAIR_FAILED, getNameLower() % Text::toUtf8(ret.ToString()) % dbPath % APPNAME % APPNAME), false, true);
	}

	LogManager::getInstance()->message(STRING_F(DB_X_REPAIRED, friendlyName % logPath), LogManager::LOG_INFO);

	//reset the log
	delete options.info_log;
	options.info_log = nullptr;
}

LevelDB::~LevelDB() {
	if (db)
		delete db;
	delete options.filter_policy;
	delete options.block_cache;
}

#define DBACTION(f) (performDbOperation([&] { return f; }))

void LevelDB::put(void* aKey, size_t keyLen, void* aValue, size_t valueLen) {
	totalWrites++;
	leveldb::Slice key((const char*)aKey, keyLen);
	leveldb::Slice value((const char*)aValue, valueLen);

	// leveldb doesn't support unique values, so check and delete existing ones manually before inserting (those shouldn't exist ofter and bloom filter should handle false searches efficiently)
	string tmp;
	auto ret = DBACTION(db->Get(iteroptions, key, &tmp));
	if (!ret.IsNotFound()) {
		DBACTION(db->Delete(writeoptions, key));
	}

	DBACTION(db->Put(writeoptions, key, value));
}

bool LevelDB::get(void* aKey, size_t keyLen, size_t /*initialValueLen*/, std::function<bool (void* aValue, size_t aValueLen)> loadF) {
	totalReads++;
	string value;
	leveldb::Slice key((const char*)aKey, keyLen);
	auto ret = DBACTION(db->Get(readoptions, key, &value));
	if (ret.ok()) {
		return loadF((void*)value.data(), value.size());
	}

	return false;
}

string LevelDB::getStats() { 
	string ret;
	string value = "leveldb.stats";
	leveldb::Slice prop(value.c_str(), value.length());
	db->GetProperty(prop, &ret);
	ret += "\r\n\r\nTotal reads: " + Util::toString(totalReads);
	ret += "\r\nTotal Writes: " + Util::toString(totalWrites);
	ret += "\r\nI/O errors: " + Util::toString(ioErrors);
	ret += "\r\nCurrent block size: " + Util::formatBytes(options.block_size);
	ret += "\r\n\r\n";
	return ret;
}

bool LevelDB::hasKey(void* aKey, size_t keyLen) {

	/*string value;
	leveldb::Slice key((const char*)aKey, keyLen);
	auto ret = db->Get(iteroptions, key, &value);
	return ret.ok();*/

	auto it = unique_ptr<leveldb::Iterator>(db->NewIterator(iteroptions));

	leveldb::Slice key((const char*)aKey, keyLen);

	performDbOperation([&] {
		it->Seek(key);
		return it->status();
	});

	if (it->Valid() && options.comparator->Compare(key, it->key()) == 0)
		return true;

	checkDbError(it->status());
	return false;
}

void LevelDB::remove(void* aKey, size_t keyLen) {
	leveldb::Slice key((const char*)aKey, keyLen);
	DBACTION(db->Delete(writeoptions, key));
}

size_t LevelDB::size(bool /*thorough*/) {
	// leveldb doesn't support any easy way to do this
	size_t ret = 0;
	leveldb::ReadOptions options;
	options.fill_cache = false;
	auto it = unique_ptr<leveldb::Iterator>(db->NewIterator(options));
	for (it->SeekToFirst(); it->Valid(); it->Next()) {
		ret++;
	}

	checkDbError(it->status());
	return ret;
}

DbSnapshot* LevelDB::getSnapshot() {
	return new LevelSnapshot(db);
}

void LevelDB::remove_if(std::function<bool (void* aKey, size_t key_len, void* aValue, size_t valueLen)> f, DbSnapshot* aSnapshot /*nullptr*/) {
	// leveldb doesn't support erasing with an iterator, do it in the hard way
	leveldb::WriteBatch wb;
	leveldb::ReadOptions options;
	options.fill_cache = false;
	options.verify_checksums = false;
	options.snapshot = static_cast<LevelSnapshot*>(aSnapshot)->snapshot;

	{
		auto it = unique_ptr<leveldb::Iterator>(db->NewIterator(options));

		//random access file errors may also happen here....
		for (it->SeekToFirst(); it->Valid(); performDbOperation([&] { it->Next(); return it->status(); })) {
			if (f((void*)it->key().data(), it->key().size(), (void*)it->value().data(), it->value().size())) {
				wb.Delete(it->key());
			}
		}

		checkDbError(it->status());
	}

	db->Write(writeoptions, &wb);
}

// free up some space, https://code.google.com/p/leveldb/issues/detail?id=158
// LevelDB will perform some kind of compaction on every startup but it's not as comprehensive as manual one
void LevelDB::compact() {
	db->CompactRange(NULL, NULL);
}


// avoid errors like this: "IO error: C:\Program Files (x86)\AirDC\HashData\056183.sst: Could not create random access file. "
leveldb::Status LevelDB::performDbOperation(function<leveldb::Status ()> f) {
	int attempts = 0;
	leveldb::Status ret;
	for (;;) {
		ret = f();
		if (ret.IsIOError()) {
			ioErrors++;
			attempts++;
			if (attempts == MAX_DB_RETRIES) {
				break;
			}
			Sleep(50);
			continue;
		}

		break;
	}

	checkDbError(ret);
	return ret;
}

void LevelDB::checkDbError(leveldb::Status aStatus) {
	if (aStatus.ok() || aStatus.IsNotFound())
		return;

	string ret = Text::toUtf8(aStatus.ToString());
	if (aStatus.IsCorruption() || aStatus.IsIOError()) {
		if (ret.back() != '.')
			ret += ".";
		ret += " " + STRING(DB_ERROR_HINT);
	}

	throw DbException(ret);
}

} //dcpp