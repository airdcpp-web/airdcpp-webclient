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


#ifndef DCPLUSPLUS_DCPP_BDB_H_
#define DCPLUSPLUS_DCPP_BDB_H_

#include "DbHandler.h"
#include "ScopedFunctor.h"
#include "LogManager.h"
#include "Util.h"

#include "../bdb/include/db.h"
#include "../bdb/include/db_cxx.h"

# ifdef _DEBUG
#   pragma comment(lib, "libdb53sd.lib")
#	pragma comment(lib, "libdb_stl53sd.lib")
# else
#   pragma comment(lib, "libdb53s.lib")
#	pragma comment(lib, "libdb_stl53s.lib")
# endif

#define MAX_DB_RETRIES 5

namespace dcpp {

class BerkeleyDB : public DbHandler {
public:
	static void errorF(const DbEnv* /*env*/, const char* prefix, const char* errorMsg) {
		LogManager::getInstance()->message("Database error in " + string(prefix) + ": " + string(errorMsg), LogManager::LOG_ERROR);
	}

	BerkeleyDB(const string& aPath, uint64_t cacheSize, uint64_t aBlockSize = 4096) : DbHandler(aPath, cacheSize) {
		//set the flags 
		u_int32_t db_flags = DB_CREATE /*| DB_READ_UNCOMMITTED*/;
		u_int32_t env_flags = DB_PRIVATE | DB_THREAD | DB_CREATE |
			DB_INIT_MPOOL | DB_AUTO_COMMIT | DB_INIT_LOCK;

		try {
			dbEnv = new DbEnv(DB_CXX_NO_EXCEPTIONS);
			dbEnv->set_errcall(errorF); //show all errors on startup

			auto ret = dbEnv->set_cachesize(0, cacheSize, 1);
			dcassert(ret == 0);

			dbEnv->open(Util::getFilePath(aPath).c_str(), env_flags, 0);


			uint32_t gb, b;
			dbEnv->get_memory_max(&gb, &b);

			ret = dbEnv->set_lk_detect(DB_LOCK_DEFAULT);
			dcassert(ret == 0);
			// Redirect debugging information to std::cerr
			//dbEnv->set_error_stream(&std::cerr);

			auto fileName = Util::getFileName(aPath);

			db = new Db(dbEnv, DB_CXX_NO_EXCEPTIONS);
			db->set_errpfx(fileName.c_str());

			/*if (Util::wasUncleanShutdown) {
				flags |= 
			}*/

			/*ret = db->verify(fileName.c_str(), NULL, NULL, 0);
			if (ret != 0 && ret != 2) {
				string error(db_strerror(ret));
				checkDbError(ret);
			}*/

			db->open(NULL, // Transaction pointer 
				fileName.c_str(), // Database file name 
				NULL, // Optional logical database name
				DB_BTREE, // Database access method
				db_flags, // Open flags
				0); // File mode (using defaults)

		} catch(DbException &e) {
			throw DbException(e.what());
		} catch(Exception& e) {
			throw DbException(e.what());
		}
	}

	~BerkeleyDB() {
		try {
			// Close the database
			db->close(0);
			dbEnv->close(0);
		} catch(DbException& /*e*/) {
			//...
		} catch(std::exception& /*e*/) {
			//...
		}

		delete db;
		delete dbEnv;
	}

	void put(void* aKey, size_t keyLen, void* aValue, size_t valueLen) {
		Dbt key(aKey, keyLen);
		key.set_flags(DB_DBT_USERMEM);

		Dbt data;
		data.set_flags(DB_DBT_USERMEM);
		data.set_ulen(valueLen);
		data.set_size(valueLen);
		data.set_data(aValue);

		auto ret = performDbOperation([&] { return db->put(NULL, &key, &data, DB_NOOVERWRITE); });
		checkDbError(ret);
	}

	bool get(void* aKey, size_t keyLen, size_t valueLen, std::function<bool (void* aValue, size_t aValueLen)> loadF) {
		Dbt key(aKey, keyLen);
		key.set_flags(DB_DBT_USERMEM);

		Dbt data;
		data.set_flags(DB_DBT_USERMEM);

		//set a reasonable value to start with
		data.set_ulen(valueLen);

		void* buf = malloc(valueLen);
		data.set_data(buf);
		ScopedFunctor([&buf] { free(buf); });


		auto ret = performDbOperation([&] { return db->get(NULL, &key, &data, 0); });
		if (ret == DB_BUFFER_SMALL) {
			//enlarge the buffer
			data.set_ulen(data.get_size());
			realloc(buf, data.get_size());

			ret = performDbOperation([&] { return db->get(NULL, &key, &data, 0); });
		}

		if (ret == DB_NOTFOUND) {
			return false;
		}

		checkDbError(ret);
		return loadF(buf, data.get_size());
	}

	/*static string statRet;
	static void printF(const DbEnv*, const char* msg) {
		statRet += msg;
		statRet += "\n";
	}

	string getStats() { 
		string ret;

		dbEnv->set_msgcall(printF);

		statRet += "\n\nGENERAL STATS\n\n";
		dbEnv->stat_print(0);
		statRet += "\n\nMEMORY STATS\n\n";
		dbEnv->memp_stat_print(0);
		statRet += "\n\nLOCKING STATS\n\n";
		dbEnv->mutex_stat_print(0);
		statMsg += "\n\nHASHDATA STATS\n\n";
		hashDb->stat_print(0);
		statMsg += "\n\nFILEINDEX STATS\n\n";
		fileDb->stat_print(0);

		dbEnv->set_msgcall(NULL);
		//hashDb->set_msgcall(NULL);

		ret.swap(statRet);
		return ret;
	}*/

	bool hasKey(void* aKey, size_t keyLen) {
		Dbt key(aKey, keyLen);
		key.set_flags(DB_DBT_USERMEM);

		auto ret = performDbOperation([&] { return db->exists(NULL, &key, 0); });
		return ret == 0; 
	}

	size_t size(bool thorough) {
		DB_BTREE_STAT* stats;
		ScopedFunctor([&stats] { free(stats); });

		auto ret = db->stat(NULL, &stats, thorough ? 0 : DB_FAST_STAT);
		checkDbError(ret);

		auto itemCount = stats->bt_ndata;

		return itemCount;
		//return 2000;
	}

	void remove_if(std::function<bool (void* aKey, size_t keyLen, void* aValue, size_t valueLen)> f) {
		/*leveldb::WriteBatch wb;
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

		db->Write(writeoptions, &wb);*/
	}
private:
	int performDbOperation(function<int ()> f) {
		int attempts = 0;
		int ret = 0;
		for (;;) {
			auto ret = f();
			if (ret == DB_LOCK_DEADLOCK) {
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

	void checkDbError(int err) {
		if (err != 0 && err != DB_NOTFOUND && err != DB_KEYEXIST) {
			dcassert(0);
			string error(db_strerror(err));
			throw DbException(error);
		}
	}

	Db* db;
	DbEnv* dbEnv;
};

} //dcpp

#endif