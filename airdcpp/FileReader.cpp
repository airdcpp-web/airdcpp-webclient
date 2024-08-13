/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#include "stdinc.h"

#include "FileReader.h"

#include "debug.h"
#include "File.h"
#include "Exception.h"
#include "PathUtil.h"
#include "Text.h"
#include "Util.h"
#include "SystemUtil.h"

namespace dcpp {

using std::make_pair;
using std::swap;

namespace {
static const size_t READ_FAILED = static_cast<size_t>(-1);
}

// Benchmarking: https://bugs.launchpad.net/dcplusplus/+bug/1909861/comments/9
const size_t FileReader::DEFAULT_BLOCK_SIZE = 1024 * 1024;

size_t FileReader::read(const string& aPath, const DataCallback& callback) {
	size_t ret = READ_FAILED;

	if (preferredStrategy == ASYNC) {
		ret = readAsync(aPath, callback);
	}

	if (ret == READ_FAILED) {
		ret = readSync(aPath, callback);
	}

	return ret;
}

/** Read entire file, never returns READ_FAILED */
size_t FileReader::readSync(const string& aPath, const DataCallback& callback) {
	buffer.resize(getBlockSize(0));

	auto buf = &buffer[0];
	File f(aPath, File::READ, File::OPEN | File::SHARED_WRITE, File::BUFFER_SEQUENTIAL);

#ifdef F_NOCACHE
	// macOS
	// Avoid memory caching (fadvise is not available)
	fcntl(f.getNativeHandle(), F_NOCACHE, 1);
#endif

	size_t total = 0;
	size_t n = buffer.size();
	bool go = true;
	while (f.read(buf, n) > 0 && go) {
		go = callback(buf, n);

#ifdef POSIX_FADV_DONTNEED
		// Allow read bytes to be purged from the memory cache
		if (posix_fadvise(f.getNativeHandle(), total, n, POSIX_FADV_DONTNEED) != 0) {
			throw FileException(SystemUtil::translateError(errno));
		}
#endif

		total += n;
		n = buffer.size();
	}

	return total;
}

size_t FileReader::getBlockSize(size_t alignment) {
	auto block = blockSize < DEFAULT_BLOCK_SIZE ? DEFAULT_BLOCK_SIZE : blockSize;
	if(alignment > 0) {
		block = ((block + alignment - 1) / alignment) * alignment;
	}

	return block;
}

void* FileReader::align(void *buf, size_t alignment) {
	return alignment == 0 ? buf
		: reinterpret_cast<void*>(((reinterpret_cast<size_t>(buf) + alignment - 1) / alignment) * alignment);
}

#ifdef _WIN32

struct Handle : boost::noncopyable {
	Handle(HANDLE h) : h(h) { }
	~Handle() { ::CloseHandle(h); }

	operator HANDLE() { return h; }

	HANDLE h;
};

size_t FileReader::readAsync(const string& aPath, const DataCallback& callback) {
	DWORD sector = 0, y;

	auto tfile = Text::toT(aPath);

	if (!::GetDiskFreeSpace(PathUtil::getFilePath(tfile).c_str(), &y, &sector, &y, &y)) {
		dcdebug("Failed to get sector size: %s\n", SystemUtil::translateError(::GetLastError()).c_str());
		return READ_FAILED;
	}

	auto tmp = ::CreateFile(tfile.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, NULL);

	if (tmp == INVALID_HANDLE_VALUE) {
		dcdebug("Failed to open unbuffered file: %s\n", SystemUtil::translateError(::GetLastError()).c_str());
		return READ_FAILED;
	}

	Handle h(tmp);

	DWORD bufSize = static_cast<DWORD>(getBlockSize(sector));
	buffer.resize(bufSize * 2 + sector);

	auto buf = align(&buffer[0], sector);

	DWORD hn = 0;
	DWORD rn = 0;
	uint8_t* hbuf = static_cast<uint8_t*>(buf) + bufSize;
	uint8_t* rbuf = static_cast<uint8_t*>(buf);
	OVERLAPPED over = { 0 };

	// Read the first block
	auto res = ::ReadFile(h, hbuf, bufSize, NULL, &over);
	auto err = ::GetLastError();

	if(!res && err != ERROR_IO_PENDING) {
		if(err != ERROR_HANDLE_EOF) {
			dcdebug("First overlapped read failed: %s\n", SystemUtil::translateError(::GetLastError()).c_str());
			return READ_FAILED;
		}

		return 0;
	}

	// Finish the read and see how it went
	if(!GetOverlappedResult(h, &over, &hn, TRUE)) {
		err = ::GetLastError();
		if(err != ERROR_HANDLE_EOF) {
			dcdebug("First overlapped read failed: %s\n", SystemUtil::translateError(::GetLastError()).c_str());
			return READ_FAILED;
		}
	}
	over.Offset = hn;

	bool go = true;
	for (; hn == bufSize && go;) {
		// Start a new overlapped read
		res = ::ReadFile(h, rbuf, bufSize, NULL, &over);
		err = ::GetLastError();

		// Process the previously read data
		go = callback(hbuf, hn);

		if (!res && err != ERROR_IO_PENDING) {
			if(err != ERROR_HANDLE_EOF) {
				throw FileException(SystemUtil::translateError(err));
			}

			rn = 0;
		} else {
			// Finish the new read
			if (!GetOverlappedResult(h, &over, &rn, TRUE)) {
				err = ::GetLastError();
				if(err != ERROR_HANDLE_EOF) {
					throw FileException(SystemUtil::translateError(err));
				}

				rn = 0;
			}
		}

		*((uint64_t*)&over.Offset) += rn;

		swap(rbuf, hbuf);
		swap(rn, hn);
	}

	if(hn != 0) {
		// Process leftovers
		callback(hbuf, hn);
	}

	return *((size_t*)&over.Offset);
}

#else

size_t FileReader::readAsync(const string& file, const DataCallback& callback) {
	// Not implemented yet
	// https://bugs.launchpad.net/dcplusplus/+bug/1909861
	return READ_FAILED;
}

#endif
}
