/* 
 * Copyright (C) 2003-2005 RevConnect, http://www.revconnect.com
 * Copyright (C) 2011      Big Muscle, http://strongdc.sf.net
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

#ifndef _SHAREDFILESTREAM_H
#define _SHAREDFILESTREAM_H

#include <airdcpp/CriticalSection.h>
#include <airdcpp/File.h>
#include <airdcpp/GetSet.h>

namespace dcpp {

struct SharedFileHandle : File {
	SharedFileHandle(const string& aPath, int access, int mode);
	~SharedFileHandle() noexcept = default;

	CriticalSection cs;
	int	ref_cnt;
	string path;
	int mode;
};

class SharedFileStream : public IOStream
{

public:
	using SharedFileHandleMap = unordered_map<string, unique_ptr<SharedFileHandle>, noCaseStringHash, noCaseStringEq>;

    SharedFileStream(const string& aFileName, int access, int mode);
    ~SharedFileStream() override;

	size_t write(const void* buf, size_t len) override;
	size_t read(void* buf, size_t& len) override;

	int64_t getSize() const noexcept override;
	void setSize(int64_t newSize);

	size_t flushBuffers(bool aForce) override;

    static CriticalSection cs;
	static SharedFileHandleMap readpool;
	static SharedFileHandleMap writepool;

	void setPos(int64_t aPos) noexcept override;
private:
	SharedFileHandle* sfh;
	int64_t pos;
};

}

#endif	// _SHAREDFILESTREAM_H