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

#include <leveldb/comparator.h>
#include <leveldb/filter_policy.h>
#include <leveldb/cache.h>
#include <leveldb/slice.h>
#include <leveldb/write_batch.h>

#define MAX_DB_RETRIES 5

namespace dcpp {

LevelDB::LevelDB(const string& aPath, uint64_t cacheSize, uint64_t aBlockSize /*4096*/) : DbHandler(aPath, cacheSize) {
	dbEnv = nullptr;
	//readoptions.verify_checksums = true;

	//iteroptions.verify_checksums = true;
	iteroptions.fill_cache = false;
	readoptions.fill_cache = true;

	options.block_size = aBlockSize;
	options.block_cache = leveldb::NewLRUCache(cacheSize);
	//options.write_buffer_size = cacheSize / 4; // up to two write buffers may be held in memory simultaneously
	options.filter_policy = leveldb::NewBloomFilterPolicy(10);
	options.create_if_missing = true;

	auto ret = leveldb::DB::Open(options, dbPath, &db);
	checkDbError(ret);
}

LevelDB::~LevelDB() {
	delete db;
	delete options.filter_policy;
	delete options.block_cache;
	if (dbEnv)
		delete dbEnv;
}

#define DBACTION(f) (performDbOperation([&] { return f; }))

void LevelDB::put(void* aKey, size_t keyLen, void* aValue, size_t valueLen) {
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

void LevelDB::remove_if(std::function<bool (void* aKey, size_t key_len, void* aValue, size_t valueLen)> f) {
	// leveldb doesn't support erasing with an iterator, do it in the hard way
	leveldb::WriteBatch wb;
	leveldb::ReadOptions options;
	options.fill_cache = false;

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
	db->CompactRange(NULL, NULL); //free up some space, https://code.google.com/p/leveldb/issues/detail?id=158
}


// avoid errors like this: "IO error: C:\Program Files (x86)\AirDC\HashData\056183.sst: Could not create random access file. "
leveldb::Status LevelDB::performDbOperation(function<leveldb::Status ()> f) {
	int attempts = 0;
	leveldb::Status ret;
	for (;;) {
		ret = f();
		if (ret.IsIOError()) {
			attempts++;
			if (attempts == MAX_DB_RETRIES) {
				break;
			}
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

	throw DbException(aStatus.ToString());
}

} //dcpp