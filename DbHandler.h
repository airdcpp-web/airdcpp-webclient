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


#ifndef DCPLUSPLUS_DCPP_DBHANDLER_H_
#define DCPLUSPLUS_DCPP_DBHANDLER_H_

#include "stdinc.h"
#include "Exception.h"
#include "Text.h"
#include "Util.h"

#include <functional>

namespace dcpp {

using std::string;

STANDARD_EXCEPTION(DbException);

class DbHandler {
public:
	virtual void repair(StepFunction stepF, MessageFunction messageF) = 0;
	virtual void open(StepFunction stepF, MessageFunction messageF) = 0;

	virtual void put(void* key, size_t keyLen, void* value, size_t valueLen) = 0;
	virtual bool get(void* key, size_t keyLen, size_t initialValueLen, std::function<bool (void* aValue, size_t aValueLen)> loadF) = 0;
	virtual void remove(void* aKey, size_t keyLen) = 0;

	virtual bool hasKey(void* key, size_t keyLen) = 0;

	virtual size_t size(bool thorough) = 0;

	virtual void remove_if(std::function<bool (void* aKey, size_t keyLen, void* aValue, size_t valueLen)> f) = 0;
	virtual void compact() { }

	virtual string getStats() { return "Not supported"; }

	virtual ~DbHandler() { }

	const string& getFriendlyName() const { return friendlyName; }
	string getNameLower() const { return Text::toLower(friendlyName); }
	const string& getPath() const { return dbPath; }
	uint64_t getCacheSize() const { return cacheSize; }
protected:
	DbHandler(const string& aPath, const string& aFriendlyName, uint64_t aCacheSize) : cacheSize(aCacheSize), dbPath(aPath), friendlyName(aFriendlyName) { 
		if (dbPath.back() != PATH_SEPARATOR)
			dbPath += PATH_SEPARATOR;
	}
	DbHandler(const DbHandler& src); // no copying

	string friendlyName;
	string dbPath;
	uint64_t cacheSize;
};

} //dcpp

#endif