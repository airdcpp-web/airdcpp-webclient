/*
 * Copyright (C) 2001-2022 Jacek Sieka, arnetheduck on gmail point com
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

#include "StreamBase.h"
#include "Util.h"

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#endif

namespace dcpp {

struct FilesystemItem {
	const string name;
	const int64_t size;
	const bool isDirectory;

	string getPath(const string& aBasePath) const noexcept;
};

struct FileItemInfoBase {
	virtual bool isDirectory() const noexcept = 0;
	virtual bool isHidden() const noexcept = 0;
	virtual bool isLink() const noexcept = 0;
	virtual int64_t getSize() const noexcept = 0;
	virtual time_t getLastWriteTime() const noexcept = 0;
};

class File: public IOStream {
public:
	enum Mode {
		OPEN = 0x01,
		CREATE = 0x02,
		TRUNCATE = 0x04,
		SHARED_WRITE = 0x08,
#ifdef _WIN32
		SHARED_DELETE = 0x10
#else
		SHARED_DELETE = 0x00
#endif
	};

#ifdef _WIN32
	enum BufferMode : DWORD {
		BUFFER_SEQUENTIAL = FILE_FLAG_SEQUENTIAL_SCAN,
		BUFFER_RANDOM = FILE_FLAG_RANDOM_ACCESS,
		BUFFER_AUTO = 0,
		BUFFER_NONE = FILE_FLAG_NO_BUFFERING,
		BUFFER_WRITE_THROUGH = FILE_FLAG_WRITE_THROUGH
	};

	enum Access {
		READ = GENERIC_READ,
		WRITE = GENERIC_WRITE,
		RW = READ | WRITE
	};

	static time_t convertTime(const FILETIME* f) noexcept;
	static FILETIME convertTime(time_t f) noexcept;
#else // !_WIN32

	enum {
		READ = 0x01,
		WRITE = 0x02,
		RW = READ | WRITE
	};

	enum BufferMode {
#ifdef HAVE_POSIX_FADVISE
		BUFFER_SEQUENTIAL = POSIX_FADV_SEQUENTIAL,
		BUFFER_RANDOM = POSIX_FADV_RANDOM,
		BUFFER_AUTO = POSIX_FADV_NORMAL,
		BUFFER_NONE = POSIX_FADV_NOREUSE,
		BUFFER_WRITE_THROUGH = POSIX_FADV_NORMAL
#else
		BUFFER_SEQUENTIAL,
		BUFFER_RANDOM,
		BUFFER_AUTO,
		BUFFER_NONE,
		BUFFER_WRITE_THROUGH = BUFFER_AUTO
#endif
	};

	// some ftruncate implementations can't extend files like SetEndOfFile,
	// not sure if the client code needs this...
	int extendFile(int64_t len) noexcept;

#endif // !_WIN32

	File(const string& aFileName, int aAccess, int aMode, BufferMode aBufferMode = BUFFER_AUTO, bool aIsAbsolute = true);
	~File();

	bool isOpen() const noexcept;
	int64_t getSize() const noexcept;
	void setSize(int64_t newSize);

	int64_t getPos() const noexcept;
	void setPos(int64_t pos) noexcept override;
	void setEndPos(int64_t pos) noexcept;
	void movePos(int64_t pos) noexcept;
	void setEOF();

	// Get the path as it appears on disk
	string getRealPath() const;

	size_t read(void* buf, size_t& len) override;
	size_t write(const void* buf, size_t len) override;

	// This has no effect if aForce is false
	// Generally the operating system should decide when the buffered data is written on disk
	size_t flushBuffers(bool aForce = true) override;

	time_t getLastModified() const noexcept;

	static bool createFile(const string& aPath, const string& aContent = Util::emptyString) noexcept;
	static void copyFile(const string& src, const string& target);
	static void renameFile(const string& source, const string& target);
	static void moveDirectory(const string& source, const string& target, const string& aPattern = "*");
	static bool deleteFile(const string& aFileName) noexcept;
	static void deleteFileThrow(const string& aFileName);
	static bool deleteFileEx(const string& aFileName, int maxAttempts = 3) noexcept;

	static time_t getLastModified(const string& path) noexcept;
	static int64_t getSize(const string& aFileName) noexcept;
	static int64_t getBlockSize(const string& aFileName) noexcept;

	// Count size of a directory recursively
	static int64_t getDirSize(const string& aPath, bool recursive, const string& pattern = "*") noexcept;

	static int64_t getFreeSpace(const string& aPath) noexcept;

	typedef set<string, noCaseStringLess> VolumeSet;
	struct DiskInfo {
		const int64_t freeSpace;
		const int64_t totalSpace;
	};

	// Get disk space information (requires disk access)
	static DiskInfo getDiskInfo(const string& aPath) noexcept;

	// Get disk space information from the supplied volumes (avoids disk access)
	// Not that getting disk space information for network folder may take some time
	// especially with a large number of locations
	static DiskInfo getDiskInfo(const string& aTarget, const VolumeSet& aVolumes, bool aIgnoreNetworkPaths) noexcept;

	// Get a set of all mount points
	static VolumeSet getVolumes() noexcept;

	// Parse mount point (requires disk access)
	static string getMountPath(const string& aPath) noexcept;
	static int64_t getDeviceId(const string& aPath) noexcept;

	// Parse mount point from the supplied volumes (avoids disk access)
	static string getMountPath(const string& aPath, const VolumeSet& aVolumes, bool aIgnoreNetworkPaths) noexcept;

	static int ensureDirectory(const string& aFile) noexcept;

	// Similar to ensureDirectory but throws errors
	// Returns false if the directory exists already
	static bool createDirectory(const string& aFile);

	// Remove empty directory
	// Returns false in case of errors (e.g. the directory is not actually empty)
	static bool removeDirectory(const string& aPath) noexcept;

	// Remove the directory even if it's not empty
	// Throws in case of errors
	static void removeDirectoryForced(const string& aPath);

	static std::string makeAbsolutePath(const std::string& filename);
	static std::string makeAbsolutePath(const std::string& path, const std::string& filename);

	static bool isAbsolutePath(const string& aPath) noexcept;
	static bool isHidden(const string& aPath) noexcept;
	static bool isDirectory(const string& aPath) noexcept;

	string readFromEnd(size_t len);
	string read(size_t len);
	string read();
	void write(string&& aString) { write((void*)aString.data(), aString.size()); }
	void write(const string& aString) { write((void*)aString.data(), aString.size()); }

	enum FindFlags {
		TYPE_FILE = 0x01,
		TYPE_DIRECTORY = 0x02,
		FLAG_HIDDEN = 0x04
	};

	static StringList findFiles(const string& path, const string& aNamePattern, int aFindFlags = TYPE_FILE | TYPE_DIRECTORY);

	typedef std::function<void(const FilesystemItem&)> FileIterF;

	// Iterate through content of aPath and handle files matching aNamePattern (use * to match all files)
	// Stops if the handler returns false
	static void forEachFile(const string& aPath, const string& aNamePattern, FileIterF aHandlerF, bool aSkipHidden = true);
#ifdef _WIN32
#define HandleType HANDLE
#else
#define HandleType int
	static bool isLink(const string& aPath) noexcept;
	static time_t getLastWriteTime(const string& aPath) noexcept;
#endif

	HandleType getNativeHandle() const noexcept { return h; }

protected:
	void close() noexcept;

	HandleType h;
};

class FileFindIter {
public:
	// End iterator constructor
	FileFindIter();

	// Begin iterator constructor, path in utf-8. Note that the dirsOnly option isn't fully reliable.
	// It will also work for getting basic information about a single path on Windows, but there may be issues with certain special paths (such as drive letters)
	// This constructor will throw when being used without a pattern on Linux
	FileFindIter(const string& aPath, const string& aPattern = Util::emptyString, bool aDirsOnlyHint = false);

	~FileFindIter();

	FileFindIter& operator++();
	bool operator!=(const FileFindIter& rhs) const;

	struct DirData: FileItemInfoBase {
		DirData();

		string getFileName() const noexcept;
		bool isDirectory() const noexcept override;
		bool isHidden() const noexcept override;
		bool isLink() const noexcept override;
		int64_t getSize() const noexcept override;
		time_t getLastWriteTime() const noexcept override;
		#ifndef _WIN32
			dirent *ent;
			string base;
		#else
			WIN32_FIND_DATA fd;
		#endif
	};

	const DirData& operator*() const noexcept { return data; }
	const DirData* operator->() const noexcept { return &data; }

private:
	FileFindIter& validateCurrent();
#ifdef _WIN32
	HANDLE handle;
#else
	DIR* dir;
	unique_ptr<string> pattern;
#endif

	DirData data;
};

class FileItem : public FileItemInfoBase {
public:
	FileItem(const string& aPath);

	bool isDirectory() const noexcept override;
	bool isHidden() const noexcept override;
	bool isLink() const noexcept override;
	int64_t getSize() const noexcept override;
	time_t getLastWriteTime() const noexcept override;
private:
#ifdef _WIN32
	FileFindIter ff;
#else
	string path;
#endif
};

#ifdef _WIN32
	// on Windows, prefer _wfopen over fopen.
	FILE* dcpp_fopen(const char* filename, const char* mode);
#else
#define dcpp_fopen fopen
#endif

} // namespace dcpp

#endif // !defined(FILE_H)
