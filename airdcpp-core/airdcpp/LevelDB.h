/*
 * Copyright (C) 2011-2017 AirDC++ Project
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

#include <leveldb/status.h>
#include <leveldb/db.h>
#include <leveldb/env.h>
#include <leveldb/options.h>

namespace dcpp {

class LevelDB : public DbHandler {
public:
	DbSnapshot* getSnapshot();

	LevelDB(const string& aPath, const string& aFriendlyName, uint64_t cacheSize, int maxOpenFiles, bool useCompression, uint64_t aBlockSize = 4096);
	~LevelDB();

	void put(void* aKey, size_t keyLen, void* aValue, size_t valueLen, DbSnapshot* aSnapshot /*nullptr*/);
	bool get(void* aKey, size_t keyLen, size_t /*initialValueLen*/, std::function<bool(void* aValue, size_t aValueLen)> loadF, DbSnapshot* aSnapshot /*nullptr*/);
	void remove(void* aKey, size_t keyLen, DbSnapshot* aSnapshot /*nullptr*/);
	bool hasKey(void* aKey, size_t keyLen, DbSnapshot* aSnapshot /*nullptr*/);

	string getStats();

	size_t size(bool /*thorough*/, DbSnapshot* aSnapshot /*nullptr*/);
	int64_t getSizeOnDisk();

	void remove_if(std::function<bool(void* aKey, size_t key_len, void* aValue, size_t valueLen)> f, DbSnapshot* aSnapshot /*nullptr*/);
	void compact();
	void repair(StepFunction stepF, MessageFunction messageF);
	void open(StepFunction stepF, MessageFunction messageF);
private:
	class LevelSnapshot : public DbSnapshot {
	public:
		LevelSnapshot(leveldb::DB* aDb) : snapshot(aDb->GetSnapshot()), db(*aDb) {

		}

		~LevelSnapshot() { 
			db.ReleaseSnapshot(snapshot);
		}

		leveldb::DB& db;
		const leveldb::Snapshot* snapshot;
	};

	string getRepairFlag() const;
	leveldb::Status performDbOperation(function<leveldb::Status()> f);
	void checkDbError(leveldb::Status aStatus);

	leveldb::DB* db = nullptr;

	//DB options
	leveldb::Options defaultOptions;

	// options used when reading from the database
	leveldb::ReadOptions readoptions;

	// options used when iterating over values of the database
	leveldb::ReadOptions iteroptions;

	// options used when writing to the database
	leveldb::WriteOptions writeoptions;

	uint64_t totalReads = 0;
	uint64_t totalWrites = 0;
	uint64_t ioErrors = 0;
	size_t lastSize = 0;
};

} //dcpp

#endif