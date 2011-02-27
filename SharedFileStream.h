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

namespace dcpp {

struct SharedFileHandle : File, CriticalSection
{
    int					ref_cnt;

	SharedFileHandle(const string& aFileName, int access, int mode);
	~SharedFileHandle() throw() { }
};

class SharedFileStream : public IOStream
{

public:

    typedef map<string, SharedFileHandle*> SharedFileHandleMap;

    SharedFileStream(const string& aFileName, int access, int mode);
    ~SharedFileStream();

	size_t write(const void* buf, size_t len) throw(Exception);
	size_t read(void* buf, size_t& len) throw(Exception);

	int64_t getSize() const throw();
	void setSize(int64_t newSize) throw(FileException);

	size_t flush() throw(Exception) 
	{
		Lock l(*shared_handle_ptr);
		return shared_handle_ptr->flush();
	}

	void setPos(int64_t _pos) 
	{ 
		pos = _pos; 
	}

    static CriticalSection critical_section;
	static SharedFileHandleMap file_handle_pool;

private:
	SharedFileHandle* shared_handle_ptr;
	int64_t pos;


};

}

#endif	// _SHAREDFILESTREAM_H