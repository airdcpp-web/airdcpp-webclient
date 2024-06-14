/*
 * Copyright (C) 2011-2024 AirDC++ Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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


#ifndef DCPLUSPLUS_DCPP_DBHANDLER_H_
#define DCPLUSPLUS_DCPP_DBHANDLER_H_

#include "stdinc.h"
#include "Exception.h"
#include "Text.h"
#include "Util.h"

#include <functional>

namespace dcpp {

using std::string;

class DbSnapshot {

};

// Most methods throw DbException in case of errors
class DbHandler : boost::noncopyable {
public:
	virtual DbSnapshot* getSnapshot() { return nullptr; }

	virtual void repair(StepFunction stepF, MessageFunction messageF) = 0;
	virtual void open(StepFunction stepF, MessageFunction messageF) = 0;

	virtual void put(void* key, size_t keyLen, void* value, size_t valueLen, DbSnapshot* aSnapshot = nullptr) = 0;
	virtual bool get(void* key, size_t keyLen, size_t initialValueLen, std::function<bool(void* aValue, size_t aValueLen)> loadF, DbSnapshot* aSnapshot = nullptr) = 0;
	virtual void remove(void* aKey, size_t keyLen, DbSnapshot* aSnapshot = nullptr) = 0;

	virtual bool hasKey(void* key, size_t keyLen, DbSnapshot* aSnapshot = nullptr) = 0;

	virtual size_t size(bool thorough, DbSnapshot* aSnapshot = nullptr) = 0;
	virtual int64_t getSizeOnDisk() = 0;

	virtual void remove_if(std::function<bool(void* aKey, size_t keyLen, void* aValue, size_t valueLen)> f, DbSnapshot* aSnapshot = nullptr) = 0;
	virtual void compact() {}

	virtual string getStats() { return "Not supported"; }
	virtual string getRepairFlag() const = 0;

	virtual ~DbHandler() { }

	const string& getFriendlyName() const noexcept { return friendlyName; }
	string getNameLower() const noexcept { return Text::toLower(friendlyName); }
	const string& getPath() const noexcept { return dbPath; }
	uint64_t getCacheSize() const noexcept { return cacheSize; }
protected:
	DbHandler(const string& aPath, const string& aFriendlyName, uint64_t aCacheSize) noexcept : dbPath(Util::validatePath(aPath, true)), friendlyName(aFriendlyName), cacheSize(aCacheSize) {

	}

	const string dbPath;
	const string friendlyName;
	const uint64_t cacheSize;
};

} //dcpp

#endif