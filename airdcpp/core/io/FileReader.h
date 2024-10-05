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

#ifndef DCPLUSPLUS_DCPP_FILE_READER_H
#define DCPLUSPLUS_DCPP_FILE_READER_H

#include <functional>
#include <string>
#include <vector>

#include <boost/noncopyable.hpp>

namespace dcpp {

using std::function;
using std::pair;
using std::string;
using std::vector;

/** Helper class for reading an entire file */

class FileReader : boost::noncopyable {
public:

	enum Strategy {
		ASYNC = 1,
		SYNC = 0
	};

	typedef function<bool(const void*, size_t)> DataCallback;

	/**
	 * Set up file reader
	 * @param direct Bypass system caches - good for reading files which are not in the cache and should not be there (for example when hashing)
	 * @param blockSize Read block size, 0 = use default
	 */
	FileReader(Strategy preferredStrategy, size_t blockSize = 0) : preferredStrategy(preferredStrategy), blockSize(blockSize) { }

	/**
	 * Read file - callback will be called for each read chunk which may or may not be a multiple of the requested block size.
	 * @param file File name
	 * @param callback Called for each block - the memory is owned by the reader object and
	 * @return The number of bytes actually read
	 * @throw FileException if the read fails
	 */
	size_t read(const string& file, const DataCallback& callback);

private:
	static const size_t DEFAULT_BLOCK_SIZE;

	string file;
	Strategy preferredStrategy;
	size_t blockSize;

	vector<uint8_t> buffer;

	/** Return an aligned buffer which is at least twice the size of ret.second */
	size_t getBlockSize(size_t alignment);
	void* align(void* buf, size_t alignment);

	size_t readAsync(const string& aFile, const DataCallback& callback);
	size_t readSync(const string& aFile, const DataCallback& callback);
};

}

#endif
