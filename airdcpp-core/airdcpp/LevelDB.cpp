/*
 * Copyright (C) 2011-2022 AirDC++ Project
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

#include "File.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "Thread.h"
#include "Util.h"
#include "version.h"

#include <leveldb/comparator.h>
#include <leveldb/cache.h>
#include <leveldb/slice.h>
#include <leveldb/write_batch.h>
#include <leveldb/filter_policy.h>

#define MAX_DB_RETRIES 10

namespace dcpp {

LevelDB::LevelDB(const string& aPath, const string& aFriendlyName, uint64_t cacheSize, int maxOpenFiles, bool useCompression, uint64_t aBlockSize /*4096*/) : 
	DbHandler(aPath, aFriendlyName, cacheSize) {

	readoptions.verify_checksums = false;
	iteroptions.verify_checksums = false;

	iteroptions.fill_cache = false;
	readoptions.fill_cache = true;

	writeoptions.sync = true;

	defaultOptions.env = leveldb::Env::Default();
	defaultOptions.compression = useCompression ? leveldb::kSnappyCompression : leveldb::kNoCompression;
	defaultOptions.max_open_files = maxOpenFiles;
	defaultOptions.block_size = static_cast<size_t>(aBlockSize);
	defaultOptions.block_cache = leveldb::NewLRUCache(static_cast<size_t>(cacheSize));
	defaultOptions.paranoid_checks = false;
	//options.write_buffer_size = cacheSize / 4; // up to two write buffers may be held in memory simultaneously
	defaultOptions.create_if_missing = true;
	defaultOptions.filter_policy = leveldb::NewBloomFilterPolicy(10);
}

string LevelDB::getRepairFlag() const { return dbPath + "REPAIR"; }

void LevelDB::open(StepFunction stepF, MessageFunction messageF) {
	bool forceRepair = Util::fileExists(getRepairFlag());
	if (forceRepair) {
		repair(stepF, messageF);
		File::deleteFile(getRepairFlag());
	}

	auto ret = leveldb::DB::Open(defaultOptions, Text::fromUtf8(dbPath), &db);
	if (!ret.ok()) {
		if (ret.IsIOError()) {
			// most likely there's another instance running or the permissions are wrong
			messageF(STRING_F(DB_OPEN_FAILED_IO, getNameLower() % Text::toUtf8(ret.ToString()) % APPNAME % dbPath % APPNAME), false, true);
			throw DbException();
		} else if (!forceRepair) {
			// the database is corrupted?
			messageF(STRING_F(DB_OPEN_FAILED_REPAIR, getNameLower() % Text::toUtf8(ret.ToString()) % APPNAME), false, false);
			repair(stepF, messageF);

			// try it again
			ret = leveldb::DB::Open(defaultOptions, Text::fromUtf8(dbPath), &db);
		}
	}

	if (!ret.ok()) {
		messageF(STRING_F(DB_OPEN_FAILED, getNameLower() % Text::toUtf8(ret.ToString()) % APPNAME), false, true);
		throw DbException();
	}
}

void LevelDB::repair(StepFunction stepF, MessageFunction messageF) {
	stepF(STRING_F(REPAIRING_X, getNameLower()));
	
	//remove any existing log
	auto logPath = dbPath + "repair.log";
	File::deleteFile(logPath);

	// Set the options
	// Paranoid checks is a bit cruel as it will remove the whole file when corruption is detected... 
	// The verify function should be used instead to fix corruption 

	defaultOptions.env->NewLogger(Text::fromUtf8(logPath), &defaultOptions.info_log);
	//options.paranoid_checks = true;

	auto ret = leveldb::RepairDB(Text::fromUtf8(dbPath), defaultOptions);
	if (!ret.ok()) {
		messageF(STRING_F(DB_REPAIR_FAILED, getNameLower() % Text::toUtf8(ret.ToString()) % dbPath % APPNAME % APPNAME), false, true);
	}

	messageF(STRING_F(DB_X_REPAIRED, friendlyName % logPath), false, false);

	//reset the options
	delete defaultOptions.info_log;
	//options.paranoid_checks = false;
	defaultOptions.info_log = nullptr;
}

LevelDB::~LevelDB() {
	if (db)
		delete db;
	delete defaultOptions.filter_policy;
	delete defaultOptions.block_cache;
}

#define DBACTION(f) (performDbOperation([&] { return f; }))

void LevelDB::put(void* aKey, size_t keyLen, void* aValue, size_t valueLen, DbSnapshot* /*aSnapshot*/ /*nullptr*/) {
	totalWrites++;
	leveldb::Slice key((const char*)aKey, keyLen);
	leveldb::Slice value((const char*)aValue, valueLen);

	// leveldb will replace existing values
	DBACTION(db->Put(writeoptions, key, value));
}

bool LevelDB::get(void* aKey, size_t keyLen, size_t /*initialValueLen*/, std::function<bool(void* aValue, size_t aValueLen)> loadF, DbSnapshot* /*aSnapshot*/ /*nullptr*/) {
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

	ret = "\r\n-=[ Stats for " + getFriendlyName() + " ]=-\n\n" + ret;
	ret += "\r\n\r\nTotal entries: " + Util::toString(size(true, nullptr));
	ret += "\r\nTotal reads: " + Util::toString(totalReads);
	ret += "\r\nTotal Writes: " + Util::toString(totalWrites);
	ret += "\r\nI/O errors: " + Util::toString(ioErrors);
	ret += "\r\nCurrent block size: " + Util::formatBytes(defaultOptions.block_size);
	ret += "\r\nCurrent size on disk: " + Util::formatBytes(getSizeOnDisk());
	ret += "\r\n";
	return ret;
}

bool LevelDB::hasKey(void* aKey, size_t keyLen, DbSnapshot* /*aSnapshot*/ /*nullptr*/) {
	string value;
	leveldb::Slice key((const char*)aKey, keyLen);
	auto ret = db->Get(iteroptions, key, &value);
	return ret.ok();
}

void LevelDB::remove(void* aKey, size_t keyLen, DbSnapshot* /*aSnapshot*/ /*nullptr*/) {
	leveldb::Slice key((const char*)aKey, keyLen);
	DBACTION(db->Delete(writeoptions, key));
}

int64_t LevelDB::getSizeOnDisk() {
	return File::getDirSize(getPath(), false);
}

size_t LevelDB::size(bool thorough, DbSnapshot* aSnapshot /*nullptr*/) {
	if (!thorough && lastSize > 0)
		return lastSize;

	// leveldb doesn't support any easy way to do this
	size_t ret = 0;
	leveldb::ReadOptions options;
	options.fill_cache = false;
	if (aSnapshot)
		options.snapshot = static_cast<LevelSnapshot*>(aSnapshot)->snapshot;

	auto it = unique_ptr<leveldb::Iterator>(db->NewIterator(options));
	for (it->SeekToFirst(); it->Valid(); it->Next()) {
		checkDbError(it->status());
		ret++;
	}

	lastSize = ret;
	return ret;
}

DbSnapshot* LevelDB::getSnapshot() {
	return new LevelSnapshot(db);
}

void LevelDB::remove_if(std::function<bool(void* aKey, size_t key_len, void* aValue, size_t valueLen)> f, DbSnapshot* aSnapshot /*nullptr*/) {
	leveldb::WriteBatch wb;
	leveldb::ReadOptions options;
	options.fill_cache = false;
	options.verify_checksums = false; // it will stop iterating when a checksum mismatch is found otherwise
	if (aSnapshot)
		options.snapshot = static_cast<LevelSnapshot*>(aSnapshot)->snapshot;

	{
		auto it = unique_ptr<leveldb::Iterator>(db->NewIterator(options));
		for (it->SeekToFirst(); it->Valid(); it->Next()) {
			checkDbError(it->status());

			if (f((void*)it->key().data(), it->key().size(), (void*)it->value().data(), it->value().size())) {
				wb.Delete(it->key());
			}
		}
	}

	DBACTION(db->Write(writeoptions, &wb));
}

// free up some space, https://code.google.com/p/leveldb/issues/detail?id=158
// LevelDB will perform some kind of compaction on every startup but it's not as comprehensive as manual one
// The issue has been "fixed" in version 1.13 but it still won't match the manual one (possibly because only ranges that are iterated
// through are compacted but there won't be that many reads to those ranges, not in the file index at least)
void LevelDB::compact() {
	db->CompactRange(nullptr, nullptr);
}

leveldb::Status LevelDB::performDbOperation(function<leveldb::Status()> f) {
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
			Thread::sleep(50);
			continue;
		}

		break;
	}

	checkDbError(ret);
	return ret;
}

void LevelDB::checkDbError(leveldb::Status aStatus) {
	if (aStatus.ok())
		return;

	if (aStatus.IsNotFound())
		return;

	string ret = Text::toUtf8(aStatus.ToString());

#ifdef _WIN32
	if (aStatus.IsCorruption() || aStatus.IsIOError()) {
		if (ret.back() != '.')
			ret += ".";
		ret += " " + STRING_F(DB_ERROR_HINT, STRING(HASHING));
	}
#endif

	throw DbException(ret);
}

} //dcpp