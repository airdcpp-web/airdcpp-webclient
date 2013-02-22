/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_FILE_H
#define DCPLUSPLUS_DCPP_FILE_H

#include "Streams.h"

#ifdef _WIN32
#include "w.h"
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <fnmatch.h>
#endif

namespace dcpp {

class File : public IOStream {
public:
	enum Mode {
		OPEN = 0x01,
		CREATE = 0x02,
		TRUNCATE = 0x04,
		SHARED = 0x08,
		NO_CACHE_HINT = 0x10,
		RANDOM_ACCESS = 0x20
	};

#ifdef _WIN32
	enum Access {
		READ = GENERIC_READ,
		WRITE = GENERIC_WRITE,
		RW = READ | WRITE
	};

	static uint32_t convertTime(FILETIME* f);

#else // !_WIN32

	enum {
		READ = 0x01,
		WRITE = 0x02,
		RW = READ | WRITE
	};

	// some ftruncate implementations can't extend files like SetEndOfFile,
	// not sure if the client code needs this...
	int extendFile(int64_t len) noexcept;

#endif // !_WIN32

	File(const string& aFileName, int access, int mode, bool isAbsolute = true);

	bool isOpen() const noexcept;
	void close() noexcept;
	int64_t getSize() const noexcept;
	void setSize(int64_t newSize);

	int64_t getPos() const noexcept;
	void setPos(int64_t pos) noexcept;
	void setEndPos(int64_t pos) noexcept;
	void movePos(int64_t pos) noexcept;
	void setEOF();

	size_t read(void* buf, size_t& len);
	size_t write(const void* buf, size_t len);
	size_t flush();

	uint32_t getLastModified() const noexcept;

	static void copyFile(const string& src, const string& target);
	static void renameFile(const string& source, const string& target);
	static void deleteFile(const string& aFileName) noexcept;

	static int64_t getSize(const string& aFileName) noexcept;

	static void ensureDirectory(const string& aFile) noexcept;
	static bool createDirectory(const string& aFile);
	static void removeDirectory(const string& aPath) noexcept;

	static bool isAbsolute(const string& path) noexcept;

	virtual ~File() { File::close(); }

	string read(size_t len);
	string read();
	void write(string&& aString) { write((void*)aString.data(), aString.size()); }
	void write(const string& aString) { write((void*)aString.data(), aString.size()); }
	static StringList findFiles(const string& path, const string& pattern);

protected:
#ifdef _WIN32
	HANDLE h;
#else
	int h;
#endif
};

class FileFindIter {
public:
	/** End iterator constructor */
	FileFindIter();
	/** Begin iterator constructor, path in utf-8 */
	FileFindIter(const string& path);

	~FileFindIter();

	FileFindIter& operator++();
	bool operator!=(const FileFindIter& rhs) const;

	struct DirData
#ifdef _WIN32
		: public WIN32_FIND_DATA
#endif
	{
		DirData();

		string getFileName();
		bool isDirectory();
		bool isHidden();
		bool isLink();
		int64_t getSize();
		uint32_t getLastWriteTime();
		#ifndef _WIN32
			dirent *ent;
			string base;
		#endif
	};

	DirData& operator*() { return data; }
	DirData* operator->() { return &data; }

private:
#ifdef _WIN32
	HANDLE handle;
#else
	DIR* dir;
#endif

	DirData data;
};

} // namespace dcpp

#endif // !defined(FILE_H)