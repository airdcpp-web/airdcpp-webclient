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

#include "stdinc.h"
#include "DCPlusPlus.h"

#include "DCPlusPlus.h"

#include "SharedFileStream.h"

#ifdef _WIN32
# include "Winioctl.h"
#endif

namespace dcpp {

CriticalSection SharedFileStream::critical_section;
SharedFileStream::SharedFileHandleMap SharedFileStream::file_handle_pool;

SharedFileHandle::SharedFileHandle(const string& aFileName, int access, int mode) : 
	File(aFileName, access, mode)
{
#ifdef _WIN32
	if(!SETTING(ANTI_FRAG))
	{
		// avoid allocation of large ranges of zeroes for unused segments
		DWORD bytesReturned;
		DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &bytesReturned, NULL);
	}
#endif
}

SharedFileStream::SharedFileStream(const string& aFileName, int access, int mode)
{
	Lock l(critical_section);

	if(file_handle_pool.count(aFileName) > 0)
	{
	    shared_handle_ptr = file_handle_pool[aFileName];
		shared_handle_ptr->ref_cnt++;
	}
	else
	{
	    shared_handle_ptr = new SharedFileHandle(aFileName, access, mode);
	    shared_handle_ptr->ref_cnt = 1;
		file_handle_pool[aFileName] = shared_handle_ptr;
	}
}

SharedFileStream::~SharedFileStream()
{
	Lock l(critical_section);

	shared_handle_ptr->ref_cnt--;
	
	if(!shared_handle_ptr->ref_cnt)
	{
        for(SharedFileHandleMap::iterator i = file_handle_pool.begin();
        							i != file_handle_pool.end();
                                    i++)
		{
			if(i->second == shared_handle_ptr)
			{
            	file_handle_pool.erase(i);
				delete shared_handle_ptr;
                return;
            }
        }

		dcassert(0);
    }
}

size_t SharedFileStream::write(const void* buf, size_t len) throw(Exception)
{
	Lock l(*shared_handle_ptr);

	shared_handle_ptr->setPos(pos);
	shared_handle_ptr->write(buf, len);

    pos += len;
	return len;
}

size_t SharedFileStream::read(void* buf, size_t& len) throw(Exception) {
	Lock l(*shared_handle_ptr);

	shared_handle_ptr->setPos(pos);
	len = shared_handle_ptr->read(buf, len);

    pos += len;
	return len;
}

int64_t SharedFileStream::getSize() const noexcept
{
	Lock l(*shared_handle_ptr);
	return shared_handle_ptr->getSize();
}

void SharedFileStream::setSize(int64_t newSize) throw(FileException)
{
	Lock l(*shared_handle_ptr);
	shared_handle_ptr->setSize(newSize);
}

}
