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

namespace dcpp {

class LevelDB : public DbHandler {
public:
	LevelDB(const string& aPath, uint64_t cacheSize, bool cacheReads) : DbHandler(aPath, cacheSize) {
		dbEnv = nullptr;
		//readoptions.verify_checksums = true;

		//iteroptions.verify_checksums = true;
		iteroptions.fill_cache = cacheReads;


		options.block_cache = leveldb::NewLRUCache(cacheSize / 2);
		//options.write_buffer_size = cacheSize / 4; // up to two write buffers may be held in memory simultaneously
		options.filter_policy = leveldb::NewBloomFilterPolicy(10);
		options.create_if_missing = true;

		auto ret = leveldb::DB::Open(options, dbPath, &db);
		if (!ret.ok()) {
			//LogManager::getInstance()->message("Failed to open the hash database: " + ret.ToString(), LogManager::LOG_ERROR);
			dcassert(0);
		}
	}

	~LevelDB() {
		delete db;
		if (dbEnv)
			delete dbEnv;
	}

	void put(void* aKey, size_t keyLen, void* aValue, size_t valueLen) {
		leveldb::Slice key((const char*)aKey, keyLen);
		leveldb::Slice value((const char*)aValue, valueLen);

		auto ret = db->Put(writeoptions, key, value);
		if (!ret.ok()) {
			auto error = ret.ToString();
			dcassert(0);
		}
	}

	void* get(void* aKey, size_t keyLen, size_t /*valueLen*/) {
		auto value = new string();
		leveldb::Slice key((const char*)aKey, keyLen);
		auto ret = db->Get(iteroptions, key, value);
		if (ret.ok()) {
			//void* aValue = malloc(value->size());
			//memcpy(aValue, (void*)value->data(), value->size());
			//return aValue;
			return (void*)value->data();
		}

		return nullptr;
		/*auto it = unique_ptr<leveldb::Iterator>(db->NewIterator(iteroptions));

		leveldb::Slice key((const char*)aKey, keyLen);
		it->Seek(key);
		if (it->Valid()) {
			auto v = it->value();
			void* aValue = malloc(v.size());
			memcpy(aValue, (void*)v.data(), v.size());
			return aValue;
		}

		return nullptr;*/
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
		it->Seek(key);
		leveldb::Options options;
		return it->Valid() && options.comparator->Compare(key, it->key()) == 0;
	}

	size_t size(bool /*thorough*/) {
		size_t ret = 0;
		leveldb::ReadOptions options;
		options.fill_cache = false;
		auto it = unique_ptr<leveldb::Iterator>(db->NewIterator(options));
		for (it->SeekToFirst(); it->Valid(); it->Next()) {
			ret++;
		}

		return ret;
	}

	void remove_if(std::function<bool (void* aKey, size_t key_len, void* aValue)> f) {
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
		}

		db->Write(writeoptions, &wb);
	}
private:
	leveldb::DB* db;
	leveldb::Env* dbEnv;

	//DB options
	leveldb::Options options;

	// options used when reading from the database
	//leveldb::ReadOptions readoptions;

	// options used when iterating over values of the database
	leveldb::ReadOptions iteroptions;

	// options used when writing to the database
	leveldb::WriteOptions writeoptions;
};

} //dcpp

#endif