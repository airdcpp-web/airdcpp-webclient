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


#ifndef DCPLUSPLUS_DCPP_HAMSTERDB_H_
#define DCPLUSPLUS_DCPP_HAMSTERDB_H_

//#define SNAPPY

#ifdef SNAPPY
#include "../snappy/snappy.h"
#endif

#include "DbHandler.h"
#include "ScopedFunctor.h"
#include "Util.h"
#include "Thread.h"

#include <ham/hamsterdb.hpp>

# ifdef _DEBUG
#   pragma comment(lib, "libhamsterdb-2.1.0d.lib")
# else
#   pragma comment(lib, "libhamsterdb-2.1.0.lib")
# endif

namespace dcpp {

class HamsterDB : public DbHandler {
public:
	HamsterDB(const string& aPath, uint64_t cacheSize, size_t keyLen, bool isFixedLen /*, uint64_t pageSize = 4096*/) : DbHandler(aPath, cacheSize) {
		ham_parameter_t envOptions[] = {
			{HAM_PARAM_CACHESIZE, cacheSize},
			//{HAM_PARAM_PAGESIZE, cacheSize},
			{ 0,0 }
		};

		db = new hamsterdb::db;
		env = new hamsterdb::env;
		try {
			if (Util::fileExists(aPath)) {
				env->open(aPath.c_str(), HAM_CACHE_STRICT, &envOptions[0]);
				db = new hamsterdb::db(env->open_db(1, 0));
			} else {
				ham_parameter_t dbOptions[] = {
					{HAM_PARAM_KEYSIZE, keyLen},
					{ 0,0 }
				};

				env->create(aPath.c_str(), HAM_CACHE_STRICT, 0, &envOptions[0]);
				db = new hamsterdb::db(env->create_db(1, isFixedLen ? 0 : HAM_ENABLE_EXTENDED_KEYS, &dbOptions[0]));
			}
		} catch (hamsterdb::error &e) {
			dcassert(0);
			throw DbException(e.get_string());
		}
	}

	~HamsterDB() {
		db->close();
		env->close();
		delete db;
		delete env;
	}

	void put(void* aKey, size_t keyLen, void* aValue, size_t valueLen) {
		//hamsterdb::key key(aKey, keyLen);
		hamsterdb::key key;
		key.set_size(keyLen);
		key.set_data(aKey);
		auto value = compress(aValue, valueLen, valueLen);
		if (value) {
			aValue = value;
			ScopedFunctor([&] { free(aValue); });
		}

		hamsterdb::record rec(aValue, valueLen);

		Lock l(cs);
		try {
			db->insert(&key, &rec);
		} catch (hamsterdb::error &e) {
			checkDbError(e);
		}
	}

	bool get(void* aKey, size_t keyLen, size_t /*initialValueLen*/, std::function<bool (void* aValue, size_t aValueLen)> loadF) {
		hamsterdb::key key(aKey, keyLen);
		hamsterdb::record rec;

		{
			Lock l(cs);
			try {
				rec = db->find(&key);
			} catch (hamsterdb::error &e) {
				checkDbError(e);
				return false;
			}
		}

		/*void* aValue = malloc(rec.get_size());
		if (!uncompress(rec.get_data(), rec.get_size(), aValue)) {
			memcpy(aValue, rec.get_data(), rec.get_size());
		}*/

		return loadF(rec.get_data(), rec.get_size());
	}

	bool hasKey(void* aKey, size_t keyLen) {
		hamsterdb::key key(aKey, keyLen);

		Lock l(cs);
		try {
			db->find(&key);
			return true;
		} catch (hamsterdb::error &e) {
			checkDbError(e);
		}

		return false;
	}

	size_t size(bool /*thorough*/) {
		return db->get_key_count();
	}

	void remove_if(std::function<bool (void* aKey, size_t key_len, void* aValue, size_t valueLen)> f) {
		hamsterdb::cursor cursor(db, NULL, 0);
		hamsterdb::key key;
		hamsterdb::record rec;

		try {
			while (1) {
				cursor.move_next(&key, &rec);
				if (f(key.get_data(), key.get_size(), rec.get_data(), rec.get_size())) {
					cursor.erase();
				}
			}
		} catch (hamsterdb::error &e) {
			checkDbError(e);
		}
	}
private:
	void checkDbError(hamsterdb::error& e) {
		if (e.get_errno() == HAM_SUCCESS || e.get_errno() == HAM_KEY_NOT_FOUND || e.get_errno() == HAM_DUPLICATE_KEY)
			return;

		dcassert(0);
		throw DbException(e.get_string());
	}

	inline void* compress(void* aValue, size_t aLen, size_t& outLen_) {
	#ifdef SNAPPY
		auto len = snappy::MaxCompressedLength(aLen);
		void* compressed = malloc(len);
		snappy::RawCompress((const char*)aValue, aLen, (char*)compressed, &outLen_);
		return realloc(compressed, outLen_);
	#else
		return nullptr;
	#endif
	}

	/*inline bool Snappy_GetUncompressedLength(const char* input, size_t length,
											 size_t* result) {
	#ifdef SNAPPY
	  return snappy::GetUncompressedLength(input, length, result);
	#else
	  return false;
	#endif
	}*/

	inline bool uncompress(void* input, size_t length, void* output) {
	#ifdef SNAPPY
	  return snappy::RawUncompress((const char*)input, length, (char*)output);
	#else
	  return false;
	#endif
	}

	CriticalSection cs;
	hamsterdb::db* db;
	hamsterdb::env* env;
};

}

#endif