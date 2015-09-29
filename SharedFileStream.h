/* 
 * Copyright (C) 2003-2005 RevConnect, http://www.revconnect.com
 * Copyright (C) 2011      Big Muscle, http://strongdc.sf.net
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

#ifndef _SHAREDFILESTREAM_H
#define _SHAREDFILESTREAM_H

#include "File.h"
#include "Thread.h"
#include "GetSet.h"

namespace dcpp {

struct SharedFileHandle : File {
	SharedFileHandle(const string& aPath, int access, int mode);
	~SharedFileHandle() noexcept { }

	CriticalSection cs;
	int	ref_cnt;
	string path;
	int mode;
};

class SharedFileStream : public IOStream
{

public:
	typedef unordered_map<string, unique_ptr<SharedFileHandle>, noCaseStringHash, noCaseStringEq> SharedFileHandleMap;

    SharedFileStream(const string& aFileName, int access, int mode);
    ~SharedFileStream();

	size_t write(const void* buf, size_t len) throw(Exception);
	size_t read(void* buf, size_t& len) throw(Exception);

	int64_t getSize() const noexcept;
	void setSize(int64_t newSize) throw(FileException);

	size_t flush() throw(Exception);

    static CriticalSection cs;
	static SharedFileHandleMap readpool;
	static SharedFileHandleMap writepool;

	void setPos(int64_t aPos) noexcept;
private:
	SharedFileHandle* sfh;
	int64_t pos;
};

}

#endif	// _SHAREDFILESTREAM_H