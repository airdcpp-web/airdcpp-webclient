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

#include "SharedFileStream.h"

#ifdef _WIN32
# include "Winioctl.h"
#endif

namespace dcpp {

CriticalSection SharedFileStream::cs;
SharedFileStream::SharedFileHandleMap SharedFileStream::readpool;
SharedFileStream::SharedFileHandleMap SharedFileStream::writepool;

SharedFileHandle::SharedFileHandle(const string& aPath, int aAccess, int aMode) : 
	File(aPath, aAccess, aMode), ref_cnt(1), path(aPath), mode(aMode)
{ }

SharedFileStream::SharedFileStream(const string& aFileName, int aAccess, int aMode) {
	Lock l(cs);
	auto& pool = aAccess == File::READ ? readpool : writepool;
	auto p = pool.find(aFileName);
	if (p != pool.end()) {
		sfh = p->second.get();
		sfh->ref_cnt++;
	} else {
	    sfh = new SharedFileHandle(aFileName, aAccess, aMode);
		pool[aFileName] = unique_ptr<SharedFileHandle>(sfh);
	}
}

SharedFileStream::~SharedFileStream() {
	Lock l(cs);

	sfh->ref_cnt--;
	if(sfh->ref_cnt == 0) {
		auto& pool = sfh->mode == File::READ ? readpool : writepool;
		pool.erase(sfh->path);
    }
}

size_t SharedFileStream::write(const void* buf, size_t len) throw(Exception) {
	Lock l(sfh->cs);

	sfh->setPos(pos);
	sfh->write(buf, len);

    pos += len;
	return len;
}

size_t SharedFileStream::read(void* buf, size_t& len) throw(Exception) {
	Lock l(sfh->cs);

	sfh->setPos(pos);
	len = sfh->read(buf, len);

    pos += len;
	return len;
}

int64_t SharedFileStream::getSize() const noexcept {
	Lock l(sfh->cs);
	return sfh->getSize();
}

void SharedFileStream::setSize(int64_t newSize) throw(FileException) {
	Lock l(sfh->cs);
	sfh->setSize(newSize);
}

size_t SharedFileStream::flush() throw(Exception) {
	Lock l(sfh->cs);
	return sfh->flush();
}

void SharedFileStream::setPos(int64_t aPos) noexcept {
	pos = aPos;
}

}
