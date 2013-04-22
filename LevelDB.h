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


#ifndef DCPLUSPLUS_DCPP_LEVELDB_H_
#define DCPLUSPLUS_DCPP_LEVELDB_H_

#include "DbHandler.h"

#include <leveldb/comparator.h>
#include <leveldb/db.h>
#include <leveldb/env.h>
#include <leveldb/options.h>
#include <leveldb/filter_policy.h>
#include <leveldb/cache.h>
#include <leveldb/slice.h>
#include <leveldb/status.h>
#include <leveldb/write_batch.h>

#define MAX_DB_RETRIES 5

namespace dcpp {

class LevelDB : public DbHandler {
public:
	LevelDB(const string& aPath, uint64_t cacheSize, bool cacheReads, uint64_t aBlockSize = 4096) : DbHandler(aPath, cacheSize) {
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

	~LevelDB() {
		delete db;
		delete options.filter_policy;
		delete options.block_cache;
		if (dbEnv)
			delete dbEnv;
	}

	void put(void* aKey, size_t keyLen, void* aValue, size_t valueLen) {
		leveldb::Slice key((const char*)aKey, keyLen);
		leveldb::Slice value((const char*)aValue, valueLen);

		string tmp;
		auto ret = performDbOperation([&] { return db->Get(iteroptions, key, &tmp); });
		if (ret.IsNotFound()) {
			ret = db->Put(writeoptions, key, value);
		}
		checkDbError(ret);
	}

	void* get(void* aKey, size_t keyLen, size_t /*valueLen*/) {
		auto value = new string();
		leveldb::Slice key((const char*)aKey, keyLen);
		auto ret = performDbOperation([&] { return db->Get(readoptions, key, value); });
		if (ret.ok()) {
			//void* aValue = malloc(value->size());
			//memcpy(aValue, (void*)value->data(), value->size());
			//return aValue;
			return (void*)value->data();
		}

		checkDbError(ret);
		return nullptr;
	}

	string getStats() { 
		string ret;
		string value = "leveldb.stats";
		leveldb::Slice prop(value.c_str(), value.length());
		db->GetProperty(prop, &ret);
		return ret;
	}

	bool hasKey(void* aKey, size_t keyLen) {

		/*string value;
		leveldb::Slice key((const char*)aKey, keyLen);
		auto ret = db->Get(iteroptions, key, &value);
		return ret.ok();*/

		auto it = unique_ptr<leveldb::Iterator>(db->NewIterator(iteroptions));

		leveldb::Slice key((const char*)aKey, keyLen);

		int attempts = 0;
		for (;;) {
			it->Seek(key);
			if (it->status().IsIOError()) {
				attempts++;
				if (attempts == MAX_DB_RETRIES) {
					break;
				}
				continue;
			}

			break;
		}

		if (it->Valid() && options.comparator->Compare(key, it->key()) == 0)
			return true;

		checkDbError(it->status());
		return false;
	}

	size_t size(bool /*thorough*/) {
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

	void remove_if(std::function<bool (void* aKey, size_t key_len, void* aValue)> f) {
		// leveldb doesn't support erasing with an iterator, do it in the hard way
		leveldb::WriteBatch wb;
		leveldb::ReadOptions options;
		options.fill_cache = false;

		{
			auto it = unique_ptr<leveldb::Iterator>(db->NewIterator(options));
			for (it->SeekToFirst(); it->Valid(); it->Next()) {
				if (f((void*)it->key().data(), it->key().size(), (void*)it->value().data())) {
					wb.Delete(it->key());
				}
			}

			checkDbError(it->status());
		}

		db->Write(writeoptions, &wb);
	}
private:
	leveldb::Status performDbOperation(function<leveldb::Status ()> f) {
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

	void checkDbError(leveldb::Status aStatus) {
		if (aStatus.ok() || aStatus.IsNotFound())
			return;

		throw DbException(aStatus.ToString());
	}

	leveldb::DB* db;
	leveldb::Env* dbEnv;

	//DB options
	leveldb::Options options;

	// options used when reading from the database
	leveldb::ReadOptions readoptions;

	// options used when iterating over values of the database
	leveldb::ReadOptions iteroptions;

	// options used when writing to the database
	leveldb::WriteOptions writeoptions;
};

} //dcpp

#endif