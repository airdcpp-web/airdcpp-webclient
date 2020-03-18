/*
 * Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_EXCEPTION_H
#define DCPLUSPLUS_DCPP_EXCEPTION_H

#include <string>

#include "debug.h"

namespace dcpp {

using std::string;


class Exception : public std::exception
{
public:
	Exception() { }
	Exception(const string& aError) : errorString(aError), errorCode(0) { dcdrun(if(errorString.size()>0)) dcdebug("Thrown: %s\n", errorString.c_str()); }
	Exception(const string& aError, int eCode) : errorString(aError), errorCode(eCode) { dcdrun(if (errorString.size()>0)) dcdebug("Thrown: %s\n", errorString.c_str()); }

	//should start using some codes for the exceptions since most of them are translated...
	enum {
		NONE = 0,
		TTH_INCONSISTENCY = 1
	};

	virtual const char* what() const noexcept { return getError().c_str(); }
	
	virtual ~Exception() noexcept { }
	virtual const string& getError() const noexcept { return errorString; }
	virtual const int& getErrorCode() const noexcept { return errorCode; }
protected:
	string errorString;
	int errorCode;
};

#ifdef _DEBUG

#define STANDARD_EXCEPTION(name) class name : public Exception { \
public:\
	name() : Exception(#name) { } \
	name(const string& aError) : Exception(#name ": " + aError) { } \
	name(const string& aError, int eCode) : Exception(#name ": " + aError, eCode) { } \
	virtual ~name() noexcept { } \
}

#else // _DEBUG

#define STANDARD_EXCEPTION(name) class name : public Exception { \
public:\
	name() : Exception() { } \
	name(const string& aError) : Exception(aError) { } \
	name(const string& aError, int eCode) : Exception(aError, eCode) { } \
	virtual ~name() noexcept { } \
}
#endif

STANDARD_EXCEPTION(AbortException);
STANDARD_EXCEPTION(CryptoException);
STANDARD_EXCEPTION(DbException);
STANDARD_EXCEPTION(DupeException);
STANDARD_EXCEPTION(FileException);
STANDARD_EXCEPTION(HashException);
STANDARD_EXCEPTION(MonitorException);
STANDARD_EXCEPTION(ParseException);
STANDARD_EXCEPTION(QueueException);
STANDARD_EXCEPTION(SearchTypeException);
STANDARD_EXCEPTION(ShareException);
STANDARD_EXCEPTION(SimpleXMLException);
STANDARD_EXCEPTION(ThreadException);

} // namespace dcpp

#endif // !defined(EXCEPTION_H)