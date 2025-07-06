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
#include <airdcpp/core/io/File.h>

#include <airdcpp/util/AppUtil.h>
#include <airdcpp/core/classes/Exception.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/util/SystemUtil.h>
#include <airdcpp/core/thread/Thread.h>

#ifdef _WIN32
#include <airdcpp/core/header/w.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <errno.h>
#include <dirent.h>
#include <fnmatch.h>
#include <utime.h>
#endif

#ifdef F_NOCACHE
#include <fcntl.h>
#endif

#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif

#ifdef _DEBUG
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#endif

namespace dcpp {

#ifdef _WIN32
File::File(const string& aFileName, int aAccess, int aMode, BufferMode aBufferMode, bool aIsAbsolute /*true*/) {
	dcassert(aAccess == WRITE || aAccess == READ || aAccess == (READ | WRITE));

	int m = 0;
	if (aMode & OPEN) {
		if (aMode & CREATE) {
			m = (aMode & TRUNCATE) ? CREATE_ALWAYS : OPEN_ALWAYS;
		} else {
			m = (aMode & TRUNCATE) ? TRUNCATE_EXISTING : OPEN_EXISTING;
		}
	} else {
		if (aMode & CREATE) {
			m = (aMode & TRUNCATE) ? CREATE_ALWAYS : CREATE_NEW;
		} else {
			dcassert(0);
		}
	}

	DWORD shared = FILE_SHARE_READ | (aMode & SHARED_WRITE ? (FILE_SHARE_WRITE) : 0);
	if (aMode & SHARED_DELETE)
		shared |= FILE_SHARE_DELETE;

	DWORD dwFlags = aBufferMode /* | FILE_FLAG_OPEN_REPARSE_POINT*/; // For symlink support (can't be used anywhere yet)
	string path = aFileName;
	if (aIsAbsolute) {
		path = PathUtil::formatPath(aFileName);
	}

	auto isDirectoryPath = path.back() == PATH_SEPARATOR;
	if (isDirectoryPath)
		dwFlags |= FILE_FLAG_BACKUP_SEMANTICS;

	h = ::CreateFile(Text::toT(path).c_str(), aAccess, shared, NULL, m, dwFlags, NULL);
	if(h == INVALID_HANDLE_VALUE) {
		throw FileException(SystemUtil::translateError(GetLastError()));
	}

#ifdef _DEBUG
	dcassert(isDirectory(aFileName) == isDirectoryPath);

	// Strip possible network path prefix
	auto fileName = aFileName.size() > 2 && aFileName.substr(0, 2) == "\\\\" ? aFileName.substr(2) : aFileName;
	auto realPath = getRealPath();

	// Avoid issues on Linux...
	dcassert(
		compare(fileName, realPath) == 0 || 
		Util::stricmp(fileName, realPath) != 0 // Shortcut/symlink
	);
#endif
}

time_t File::getLastModified() const noexcept {
	FILETIME f = {0};
	::GetFileTime(h, NULL, NULL, &f);
	return convertTime(&f);
}

bool File::isDirectory(const string& aPath) noexcept {
	// FileFindIter doesn't work for drive paths
	DWORD attr = GetFileAttributes(Text::toT(PathUtil::formatPath(aPath)).c_str());
	return attr != 0xFFFFFFFF && (attr & FILE_ATTRIBUTE_DIRECTORY) > 0;
}

time_t File::convertTime(const FILETIME* f) noexcept {
	SYSTEMTIME s = { 1970, 1, 0, 1, 0, 0, 0, 0 };
	FILETIME f2 = {0};
	if(::SystemTimeToFileTime(&s, &f2)) {
		ULARGE_INTEGER a,b;
		a.LowPart = f->dwLowDateTime;
		a.HighPart = f->dwHighDateTime;
		b.LowPart = f2.dwLowDateTime;
		b.HighPart = f2.dwHighDateTime;
		return (static_cast<time_t>(a.QuadPart) - static_cast<time_t>(b.QuadPart)) / (10000000LL); // 100ns > s
	}
	return 0;
}

FILETIME File::convertTime(time_t f) noexcept {
	FILETIME ft;

	ft.dwLowDateTime = (DWORD)f;
	ft.dwHighDateTime = (DWORD)(f >> 32);
	return ft;
}

bool File::isOpen() const noexcept {
	return h != INVALID_HANDLE_VALUE;
}

void File::close() noexcept {
	if(isOpen()) {
		CloseHandle(h);
		h = INVALID_HANDLE_VALUE;
	}
}

int64_t File::getSize() const noexcept {
	LARGE_INTEGER x;

	if(!::GetFileSizeEx(h, &x)) 
		return -1;

	return x.QuadPart;
}

int64_t File::getPos() const noexcept {
	LONG x = 0;
	DWORD l = ::SetFilePointer(h, 0, &x, FILE_CURRENT);

	return (int64_t)l | ((int64_t)x)<<32;
}

void File::setSize(int64_t newSize) {
	int64_t pos = getPos();
	setPos(newSize);
	setEOF();
	setPos(pos);
}
void File::setPos(int64_t pos) noexcept {
	LONG x = (LONG) (pos>>32);
	::SetFilePointer(h, (DWORD)(pos & 0xffffffff), &x, FILE_BEGIN);
}
void File::setEndPos(int64_t pos) noexcept {
	LONG x = (LONG) (pos>>32);
	::SetFilePointer(h, (DWORD)(pos & 0xffffffff), &x, FILE_END);
}

void File::movePos(int64_t pos) noexcept {
	LONG x = (LONG) (pos>>32);
	::SetFilePointer(h, (DWORD)(pos & 0xffffffff), &x, FILE_CURRENT);
}

size_t File::read(void* buf, size_t& len) {
	DWORD x;
	if(!::ReadFile(h, buf, (DWORD)len, &x, NULL)) {
		throw FileException(SystemUtil::translateError(GetLastError()));
	}
	len = x;
	return x;
}

size_t File::write(const void* buf, size_t len) {
	DWORD x;
	if(!::WriteFile(h, buf, (DWORD)len, &x, NULL)) {
		throw FileException(SystemUtil::translateError(GetLastError()));
	}
	dcassert(x == len);
	return x;
}
void File::setEOF() {
	dcassert(isOpen());
	if(!SetEndOfFile(h)) {
		throw FileException(SystemUtil::translateError(GetLastError()));
	}
}

string File::getRealPath() const {
	TCHAR buf[UNC_MAX_PATH];
	auto ret = GetFinalPathNameByHandle(h, buf, UNC_MAX_PATH, FILE_NAME_OPENED);
	if (!ret) {
		throw FileException(SystemUtil::translateError(GetLastError()));
	}

	auto path = Text::fromT(buf);

	// Extended-length path prefix is always added
	// Remove for consistency
	if (path.size() > 8 && path.compare(0, 8, "\\\\?\\UNC\\") == 0) {
		return path.substr(8);
	} else if (path.size() > 4 && path.compare(0, 4, "\\\\?\\") == 0) {
		return path.substr(4);
	}

	return path;
}

size_t File::flushBuffers(bool aForce) {
	if (!aForce) {
		return 0;
	}

#ifdef _DEBUG
	auto start = boost::posix_time::microsec_clock::universal_time();;
#endif

	if(isOpen() && !FlushFileBuffers(h))
		throw FileException(SystemUtil::translateError(GetLastError()));

#ifdef _DEBUG
	dcdebug("File %s was flushed in " I64_FMT " ms\n", getRealPath().c_str(), (boost::posix_time::microsec_clock::universal_time() - start).total_milliseconds());
#endif

	return 0;
}

void File::renameFile(const string& source, const string& target) {
	if(!::MoveFileEx(Text::toT(PathUtil::formatPath(source)).c_str(), Text::toT(PathUtil::formatPath(target)).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
		throw FileException(SystemUtil::translateError(GetLastError()));
	}
}

void File::copyFile(const string& src, const string& target) {
	if(!::CopyFile(Text::toT(PathUtil::formatPath(src)).c_str(), Text::toT(PathUtil::formatPath(target)).c_str(), FALSE)) {
		throw FileException(SystemUtil::translateError(GetLastError()));
	}
}

time_t File::getLastModified(const string& aPath) noexcept {
	if (aPath.empty())
		return 0;

	FileFindIter ff = FileFindIter(aPath);
	if (ff != FileFindIter()) {
		return ff->getLastWriteTime();
	}

	return 0;
}

bool File::isHidden(const string& aPath) noexcept {
	if (aPath.empty())
		return 0;

	FileFindIter ff = FileFindIter(aPath);
	if (ff != FileFindIter()) {
		return ff->isHidden();
	}

	return false;
}

void File::deleteFileThrow(const string& aFileName) {
	if (!::DeleteFile(Text::toT(PathUtil::formatPath(aFileName)).c_str())) {
		throw FileException(SystemUtil::translateError(GetLastError()));
	}
}

bool File::removeDirectory(const string& aPath) noexcept {
	return ::RemoveDirectory(Text::toT(PathUtil::formatPath(aPath)).c_str()) > 0 ? true : false;
}

int64_t File::getSize(const string& aFileName) noexcept {
	auto i = FileFindIter(aFileName);
	return i != FileFindIter() ? i->getSize() : -1;

}

int File::ensureDirectory(const string& aFile) noexcept {
	int result = 0;

	// Skip the first dir...
	auto start = aFile.find_first_of("\\/");
	if(start == string::npos)
		return ERROR_INVALID_NAME;

	start++;
	while((start = aFile.find_first_of("\\/", start)) != string::npos) {
		result = ::CreateDirectory(Text::toT(PathUtil::formatPath(aFile.substr(0, start+1))).c_str(), NULL);
		start++;
	}

	return result;
}

bool File::createDirectory(const string& aFile) {
	auto result = ensureDirectory(aFile);
	if(result == 0) {
		result = GetLastError();
		if(result == ERROR_ALREADY_EXISTS)
			return false;

		throw FileException(SystemUtil::translateError(result));
	}

	return true;
}

bool File::isAbsolutePath(const string& path) noexcept {
	return path.size() > 2 && (path[1] == ':' || path[0] == '/' || path[0] == '\\');
}

int64_t File::getDeviceId(const string& aPath) noexcept {
	DWORD dwSerialNumber;
	auto ret = GetVolumeInformation(Text::toT(getMountPath(aPath)).c_str(), NULL, 0, &dwSerialNumber, NULL, NULL, NULL, 0);
	return ret > 0 ? dwSerialNumber : -1;
}

string File::getMountPath(const string& aPath) noexcept {
	unique_ptr<TCHAR[]> buf(new TCHAR[aPath.length()+1]);
	GetVolumePathName(Text::toT(PathUtil::formatPath(aPath)).c_str(), buf.get(), aPath.length());
	return Text::fromT(buf.get());
}

File::DiskInfo File::getDiskInfo(const string& aPath) noexcept {
	int64_t freeSpace = -1, totalSpace = -1;
	GetDiskFreeSpaceEx(Text::toT(PathUtil::formatPath(aPath)).c_str(), NULL, (PULARGE_INTEGER)&totalSpace, (PULARGE_INTEGER)&freeSpace);
	return { freeSpace, totalSpace };
}

int64_t File::getBlockSize(const string& aFileName) noexcept {
	DWORD sectorBytes, clusterSectors, tmp2, tmp3;
	auto ret = GetDiskFreeSpace(Text::toT(PathUtil::formatPath(aFileName)).c_str(), &clusterSectors, &sectorBytes, &tmp2, &tmp3);
	return ret > 0 ? static_cast<int64_t>(sectorBytes)*static_cast<int64_t>(clusterSectors) : 4096;
}

std::string File::makeAbsolutePath(const std::string& aPath, const std::string& aFilename) {
	auto ret = Text::toT(aPath);
	ret.reserve(UNC_MAX_PATH);
	auto res = _wfullpath(&ret[0], Text::toT(aFilename).c_str(), UNC_MAX_PATH);
	if (!res) {
		dcassert(0);
		return aPath + aFilename;
	}

	return Text::fromT(res);
}


#else // !_WIN32

File::File(const string& aFileName, int access, int mode, BufferMode aBufferMode, bool /*isAbsolute*/) {
	dcassert(access == WRITE || access == READ || access == (READ | WRITE));

	int m = 0;
	if(access == READ)
		m |= O_RDONLY;
	else if(access == WRITE)
		m |= O_WRONLY;
	else
		m |= O_RDWR;

	if(mode & CREATE) {
		m |= O_CREAT;
	}
	if(mode & TRUNCATE) {
		m |= O_TRUNC;
	}

#ifdef O_DIRECT
	if (aBufferMode == BUFFER_NONE) {
		m |= O_DIRECT;
	}
#endif
	h = open(aFileName.c_str(), m, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if(h == -1)
		throw FileException(SystemUtil::translateError(errno));

#ifdef HAVE_POSIX_FADVISE
	if (aBufferMode != BUFFER_NONE) {
		if (posix_fadvise(h, 0, 0, aBufferMode) != 0) {
			throw FileException(SystemUtil::translateError(errno));
		}
	}
#endif

#ifdef F_NOCACHE
	// macOS
	if (aBufferMode == BUFFER_NONE) {
		fcntl(h, F_NOCACHE, 1);
	}
#endif

#ifdef _DEBUG
	auto isDirectoryPath = aFileName.back() == PATH_SEPARATOR;
	dcassert(isDirectory(aFileName) == isDirectoryPath);
#endif
}

time_t File::getLastModified() const noexcept {
	struct stat s;
	if (::fstat(h, &s) == -1)
		return 0;

	return s.st_mtime;
}

string File::getRealPath() const {
	char buf[PATH_MAX + 1];

	int ret;
#ifdef F_GETPATH
	ret = fcntl(h, F_GETPATH, buf);
#else
	auto procPath = "/proc/self/fd/" + Util::toString(h);
	ret = ::readlink(procPath.c_str(), buf, sizeof(buf));
#endif

	if (ret == -1) {
		throw FileException(SystemUtil::translateError(errno));
	}

	return string(buf);
}

bool File::isOpen() const noexcept {
	return h != -1;
}

void File::close() noexcept {
	if(h != -1) {
		::close(h);
		h = -1;
	}
}

int64_t File::getSize() const noexcept {
	struct stat s;
	if(::fstat(h, &s) == -1)
		return -1;

	return (int64_t)s.st_size;
}

int64_t File::getPos() const noexcept {
	return (int64_t)lseek(h, 0, SEEK_CUR);
}

void File::setPos(int64_t pos) noexcept {
	lseek(h, (off_t)pos, SEEK_SET);
}

void File::setEndPos(int64_t pos) noexcept {
	lseek(h, (off_t)pos, SEEK_END);
}

void File::movePos(int64_t pos) noexcept {
	lseek(h, (off_t)pos, SEEK_CUR);
}

size_t File::read(void* buf, size_t& len) {
	ssize_t result = ::read(h, buf, len);
	if (result == -1) {
		throw FileException(SystemUtil::translateError(errno));
	}
	len = result;
	return (size_t)result;
}

size_t File::write(const void* buf, size_t len) {
	ssize_t result;
	char* pointer = (char*)buf;
	ssize_t left = len;

	while (left > 0) {
		result = ::write(h, pointer, left);
		if (result == -1) {
			if (errno != EINTR) {
				throw FileException(SystemUtil::translateError(errno));
			}
		} else {
			pointer += result;
			left -= result;
		}
	}
	return len;
}

// some ftruncate implementations can't extend files like SetEndOfFile,
// not sure if the client code needs this...
int File::extendFile(int64_t len) noexcept {
	char zero;

	if( (lseek(h, (off_t)len, SEEK_SET) != -1) && (::write(h, &zero,1) != -1) ) {
		return ftruncate(h,(off_t)len);
	}
	return -1;
}

void File::setEOF() {
	int64_t pos;
	int64_t eof;
	int ret;

	pos = (int64_t)lseek(h, 0, SEEK_CUR);
	eof = (int64_t)lseek(h, 0, SEEK_END);
	if (eof < pos)
		ret = extendFile(pos);
	else
		ret = ftruncate(h, (off_t)pos);
	lseek(h, (off_t)pos, SEEK_SET);
	if (ret == -1)
		throw FileException(SystemUtil::translateError(errno));
}

void File::setSize(int64_t newSize) {
	int64_t pos = getPos();
	setPos(newSize);
	setEOF();
	setPos(pos);
}

size_t File::flushBuffers(bool aForce) {
	if (!aForce) {
		return 0;
	}

	if(isOpen() && fsync(h) == -1)
		throw FileException(SystemUtil::translateError(errno));
	return 0;
}

/**
 * ::rename seems to have problems when source and target is on different partitions
 * from "man 2 rename":
 * EXDEV oldpath and newpath are not on the same mounted filesystem. (Linux permits a
 * filesystem to be mounted at multiple points, but rename(2) does not
 * work across different mount points, even if the same filesystem is mounted on both.)
*/
void File::renameFile(const string& source, const string& target) {
	int ret = ::rename(source.c_str(), target.c_str());
	if(ret != 0 && errno == EXDEV) {
		copyFile(source, target);
		deleteFile(source);
	} else if (ret != 0) {
		throw FileException(SystemUtil::translateError(errno));
	}
}

// This doesn't assume all bytes are written in one write call, it is a bit safer
void File::copyFile(const string& source, const string& target) {
	const size_t BUF_SIZE = 64 * 1024;
	boost::scoped_array<char> buffer(new char[BUF_SIZE]);
	size_t count = BUF_SIZE;
	File src(source, File::READ, 0);
	File dst(target, File::WRITE, File::CREATE | File::TRUNCATE);

	while(src.read(&buffer[0], count) > 0) {
		char* p = &buffer[0];
		while(count > 0) {
			size_t ret = dst.write(p, count);
			p += ret;
			count -= ret;
		}
		count = BUF_SIZE;
	}
}

void File::deleteFileThrow(const string& aFileName) {
	auto result = ::unlink(aFileName.c_str());
	if (result == -1) {
		throw FileException(SystemUtil::translateError(result));
	}
}

int64_t File::getSize(const string& aFileName) noexcept {
	struct stat s;
	if(stat(aFileName.c_str(), &s) == -1)
		return -1;

	return s.st_size;
}

bool File::createDirectory(const string& aFile) {
	auto result = ensureDirectory(aFile);
	if (result != 0) {
		if (result == EEXIST)
			return false;

		throw FileException(SystemUtil::translateError(result));
	}

	return true;
}

int File::ensureDirectory(const string& aFile) noexcept {
	int result = 0;

	string::size_type start = 0;
	while ((start = aFile.find_first_of('/', start)) != string::npos) {
		result = mkdir(aFile.substr(0, start+1).c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
		start++;
	}

	return result;
}

bool File::isAbsolutePath(const string& path) noexcept {
	return path.size() > 1 && path[0] == '/';
}

File::DiskInfo File::getDiskInfo(const string& aFileName) noexcept {
	struct statvfs sfs;
	if (statvfs(aFileName.c_str(), &sfs) == -1) {
		return { -1LL, -1LL };
	}

	int64_t freeSpace = (int64_t)sfs.f_bsize * (int64_t)sfs.f_bavail;
	int64_t totalSpace = (int64_t)sfs.f_bsize * (int64_t)sfs.f_blocks;
	return { freeSpace, totalSpace };
}

int64_t File::getBlockSize(const string& aFileName) noexcept {
	struct stat statbuf;
	if (stat(aFileName.c_str(), &statbuf) == -1) {
		return 4096;
	}

	return statbuf.st_size;
}

string File::getMountPath(const string& aPath) noexcept {
	auto volumes = File::getVolumes();
	return getMountPath(aPath, volumes, false);
}


int64_t File::getDeviceId(const string& aPath) noexcept {
	struct stat statbuf;
	if (stat(aPath.c_str(), &statbuf) == -1) {
		return -1;
	}

	return (int64_t)statbuf.st_dev;
}

time_t File::getLastModified(const string& aPath) noexcept {
	struct stat statbuf;
	if (stat(aPath.c_str(), &statbuf) == -1) {
		return 0;
	}

	return statbuf.st_mtime;
}

bool File::removeDirectory(const string& aPath) noexcept {
	return rmdir(aPath.c_str()) == 0;
}

bool File::isHidden(const string& aPath) noexcept {
	return aPath.find("/.") != string::npos;
}

bool File::isDirectory(const string& aPath) noexcept {
	struct stat inode;
	if (stat(aPath.c_str(), &inode) == -1) return false;
	return S_ISDIR(inode.st_mode);
}

bool File::isLink(const string& aPath) noexcept {
	struct stat inode;
	if (lstat(aPath.c_str(), &inode) == -1) return false;
	return S_ISLNK(inode.st_mode);
}

time_t File::getLastWriteTime(const string& aPath) noexcept {
	struct stat inode;
	if (stat(aPath.c_str(), &inode) == -1) return 0;
	return inode.st_mtime;
}

std::string File::makeAbsolutePath(const std::string& aPath, const std::string& aFilename) {
	string ret;
	ret.reserve(PATH_MAX + 1);

	string input = aFilename + aPath;

	// Note: realpath fails with non-existing files/directories but there's no better alternative
	auto res = realpath(input.c_str(), &ret[0]);
	if (!res) {
		return aPath + aFilename;
	}

	return string(res);
}


#endif // !_WIN32

File::~File() {
	File::close();
}

std::string File::makeAbsolutePath(const std::string& aFilename) {
	if (isAbsolutePath(aFilename)) {
		return aFilename;
	}

	return makeAbsolutePath(AppUtil::getAppFilePath(), aFilename);
}

void File::removeDirectoryForced(const string& aPath) {
	for (FileFindIter i(aPath, "*"); i != FileFindIter(); ++i) {
		if (i->isDirectory()) {
			removeDirectoryForced(aPath + i->getFileName() + PATH_SEPARATOR);
		} else {
			try {
				deleteFileThrow(aPath + i->getFileName());
			} catch (const FileException& e) {
				throw FileException(e.getError() + "(" + aPath + i->getFileName() + ")");
			}
		}
	}

	File::removeDirectory(aPath);
}

void File::moveDirectory(const string& aSource, const string& aTarget, const string& aPattern) {
	File::ensureDirectory(aTarget);
	File::forEachFile(aSource, aPattern, [&](const FilesystemItem& aInfo) {
		auto sourcePath = aInfo.getPath(aSource);
		auto destPath = aInfo.getPath(aTarget);

		if (aInfo.isDirectory) {
			moveDirectory(sourcePath, destPath);
		} else {
			renameFile(sourcePath, destPath);
		}
	});
}

bool File::deleteFile(const string& aFileName) noexcept {
	try {
		deleteFileThrow(aFileName);
		return true;
	} catch (...) { }

	return false;
}

bool File::deleteFileEx(const string& aFileName, int maxAttempts) noexcept {
	bool success = false;
	for (int i = 0; i < maxAttempts && (success = deleteFile(aFileName)) == false; ++i) {
		Thread::sleep(1000);
	}

	return success;
}

bool File::createFile(const string& aPath, const string& aContent) noexcept {
	try {
		File ff(aPath, File::WRITE, File::CREATE | File::TRUNCATE);
		if (!aContent.empty()) {
			ff.write(aContent);
		}
		return true;
	} catch (...) { 
		return false;
	}
}

string File::read(size_t aLen) {
	string s(aLen, 0);
	size_t x = read(&s[0], aLen);
	if(x != s.size())
		s.resize(x);
	return s;
}

string File::readFromEnd(size_t aLen) {
	auto size = getSize();

	if (size > static_cast<int64_t>(aLen)) {
		setPos(size - aLen);
	}

	return read(aLen);
}

string File::read() {
	setPos(0);
	int64_t sz = getSize();
	if(sz == -1)
		return Util::emptyString;
	return read((size_t)sz);
}

string FilesystemItem::getPath(const string& aBasePath) const noexcept {
	if (isDirectory) {
		return PathUtil::joinDirectory(aBasePath, name);
	}

	return aBasePath + name;
}

StringList File::findFiles(const string& aPath, const string& aNamePattern, int aFindFlags) {
	dcassert(PathUtil::isDirectoryPath(aPath));
	StringList ret;

	{
		forEachFile(aPath, aNamePattern, [&](const FilesystemItem& aInfo) {
			if ((aFindFlags & TYPE_FILE && !aInfo.isDirectory) || (aFindFlags & TYPE_DIRECTORY && aInfo.isDirectory)) {
				ret.push_back(aInfo.getPath(aPath));
			}
		}, !(aFindFlags & FLAG_HIDDEN));
	}

	return ret;
}

void File::forEachFile(const string& aPath, const string& aNamePattern, FileIterF aHandlerF, bool aSkipHidden) {
	for (FileFindIter i(aPath, aNamePattern); i != FileFindIter(); ++i) {
		if (!aSkipHidden || !i->isHidden()) {
			aHandlerF({
				i->getFileName(),
				i->getSize(),
				i->isDirectory(),
			});
		}
	}
}

int64_t File::getDirSize(const string& aPath, bool aRecursive, const string& aNamePattern) noexcept {
	int64_t size = 0;
	File::forEachFile(aPath, aNamePattern, [&](const FilesystemItem& aInfo) {
		if (aInfo.isDirectory && aRecursive) {
			size += getDirSize(aInfo.getPath(aPath), true, aNamePattern);
		} else {
			size += aInfo.size;
		}
	});

	return size;
}

int64_t File::getFreeSpace(const string& aPath) noexcept {
	auto info = getDiskInfo(aPath);
	return info.freeSpace;
}

string File::getMountPath(const string& aPath, const VolumeSet& aVolumes, bool aIgnoreNetworkPaths) noexcept {
	if (aVolumes.find(aPath) != aVolumes.end()) {
		return aPath;
	}

	auto l = aPath.length();
	for (;;) {
		l = aPath.rfind(PATH_SEPARATOR, l - 2);
		if (l == string::npos || l <= 1)
			break;

		if (aVolumes.find(aPath.substr(0, l + 1)) != aVolumes.end()) {
			return aPath.substr(0, l + 1);
		}
	}

#ifdef WIN32
	if (!aIgnoreNetworkPaths) {
		// Not found from volumes... network path? This won't work with mounted dirs
		// Get the first section containing the network host and the first folder/drive (//HTPC/g/)
		if (aPath.length() > 2 && aPath.substr(0, 2) == "\\\\") {
			l = aPath.find('\\', 2);
			if (l != string::npos) {
				//get the drive letter
				l = aPath.find('\\', l + 1);
				if (l != string::npos) {
					return aPath.substr(0, l + 1);
				}
			}
		}
	}
#else
	// Return the root
	return aVolumes.empty() ? Util::emptyString : PATH_SEPARATOR_STR;
#endif
	return Util::emptyString;
}

File::DiskInfo File::getDiskInfo(const string& aTarget, const VolumeSet& aVolumes, bool aIgnoreNetworkPaths) noexcept {
	auto mountPoint = getMountPath(aTarget, aVolumes, aIgnoreNetworkPaths);
	if (!mountPoint.empty()) {
		return File::getDiskInfo(mountPoint);
	}

	return{ -1LL, -1LL };
}

File::VolumeSet File::getVolumes() noexcept {
	VolumeSet volumes;
#ifdef WIN32
	TCHAR   buf[MAX_PATH];
	HANDLE  hVol;
	BOOL    found;
	TCHAR   buf2[MAX_PATH];

	// lookup drive volumes.
	hVol = FindFirstVolume(buf, MAX_PATH);
	if (hVol != INVALID_HANDLE_VALUE) {
		found = true;
		//while we find drive volumes.
		while (found) {
			if (GetDriveType(buf) != DRIVE_CDROM && GetVolumePathNamesForVolumeName(buf, buf2, MAX_PATH, NULL)) {
				volumes.insert(Text::fromT(buf2));
			}
			found = FindNextVolume(hVol, buf, MAX_PATH);
		}
		found = FindVolumeClose(hVol);
	}

	// and a check for mounted Network drives, todo fix a better way for network space
	ULONG drives = _getdrives();
	TCHAR drive[3] = { _T('A'), _T(':'), _T('\0') };

	while (drives != 0) {
		if (drives & 1 && (GetDriveType(drive) != DRIVE_CDROM && GetDriveType(drive) == DRIVE_REMOTE)) {
			string path = PathUtil::ensureTrailingSlash(Text::fromT(drive));
			volumes.insert(path);
		}

		++drive[0];
		drives = (drives >> 1);
	}
#elif HAVE_MNTENT_H
	struct mntent *ent;
	FILE *aFile;

	aFile = setmntent("/proc/mounts", "r");
	if (aFile == NULL) {
		return volumes;
	}

	while ((ent = getmntent(aFile)) != NULL) {
		auto mountPath = PathUtil::validatePath(ent->mnt_dir, true);

		// Workaround for some standard C libraries not unescaping whitespaces and certain other characters in mount points
		// https://github.com/airdcpp-web/airdcpp-webclient/issues/362
		Util::replace("\\040", " ", mountPath);
		Util::replace("\\011", "\t", mountPath);
		Util::replace("\\012", "\n", mountPath);
		Util::replace("\\134", "\\", mountPath);

		volumes.insert(mountPath);
	}
	endmntent(aFile);
#endif
	return volumes;
}

#ifdef _WIN32

FileFindIter::FileFindIter() : handle(INVALID_HANDLE_VALUE) { }

FileFindIter::FileFindIter(const string& aPath, const string& aPattern, bool aDirsOnlyHint /*false*/) : handle(INVALID_HANDLE_VALUE) {
	auto path = PathUtil::formatPath(aPath);

	// An attempt to open a search with a trailing backslash always fails
	if (aPattern.empty() && PathUtil::isDirectoryPath(path)) {
		path.pop_back();
	}

	handle = ::FindFirstFileEx(Text::toT(path + aPattern).c_str(), FindExInfoBasic, &data.fd, aDirsOnlyHint ? FindExSearchLimitToDirectories : FindExSearchNameMatch, NULL, NULL);
	validateCurrent();
}

FileFindIter::~FileFindIter() {
	if(handle != INVALID_HANDLE_VALUE) {
		::FindClose(handle);
	}
}

FileFindIter& FileFindIter::validateCurrent() {
	if (wcscmp((*this)->fd.cFileName, _T(".")) == 0 || wcscmp((*this)->fd.cFileName, _T("..")) == 0) {
		return this->operator++();
	}

	return *this;
}

FileFindIter& FileFindIter::operator++() {
	if(!::FindNextFile(handle, &data.fd)) {
		::FindClose(handle);
		handle = INVALID_HANDLE_VALUE;
		return *this;
	}

	return validateCurrent();
}

bool FileFindIter::operator!=(const FileFindIter& rhs) const { return handle != rhs.handle; }

FileFindIter::DirData::DirData() { }

string FileFindIter::DirData::getFileName() const noexcept {
	return Text::fromT(fd.cFileName);
}

bool FileFindIter::DirData::isDirectory() const noexcept {
	return (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) > 0;
}

bool FileFindIter::DirData::isHidden() const noexcept {
	return ((fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) || (fd.cFileName[0] == L'.') || (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) || (fd.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE));
}

bool FileFindIter::DirData::isLink() const noexcept {
	return (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) > 0 /*|| PathUtil::getFileExt(fd.cFileName) == L".lnk"*/; // Shortcut support (can't be used anywhere yet)
}

int64_t FileFindIter::DirData::getSize() const noexcept {
	return (int64_t)fd.nFileSizeLow | ((int64_t)fd.nFileSizeHigh)<<32;
}

time_t FileFindIter::DirData::getLastWriteTime() const noexcept {
	return File::convertTime(&fd.ftLastWriteTime);
}

FileItem::FileItem(const string& aPath) : ff(aPath) {
	if (ff != FileFindIter()) {
		// ...
	} else {
		throw FileException(SystemUtil::translateError(GetLastError()));
	}
}

bool FileItem::isDirectory() const noexcept {
	return ff->isDirectory();
}

bool FileItem::isHidden() const noexcept {
	return ff->isHidden();
}

bool FileItem::isLink() const noexcept {
	return ff->isLink();
}

int64_t FileItem::getSize() const noexcept {
	return ff->getSize();
}

time_t FileItem::getLastWriteTime() const noexcept {
	return ff->getLastWriteTime();
}

#else // _WIN32

FileFindIter::FileFindIter() {
	dir = NULL;
	data.ent = NULL;
}

FileFindIter::FileFindIter(const string& aPath, const string& aPattern, bool aDirsOnlyHint /*false*/) {
	dir = opendir(aPath.c_str());
	if (!dir)
		return;

	if (aPattern.empty()) {
		throw FileException("Invalid use of FileFindIter (pattern missing)");
	}

	data.base = aPath;
	data.ent = readdir(dir);

	if (aPattern != "*") {
		pattern.reset(new string(aPattern));
	}

	if (!data.ent) {
		closedir(dir);
		dir = NULL;
		return;
	}

	validateCurrent();
}

FileFindIter::~FileFindIter() {
	if (dir) closedir(dir);
}

FileFindIter& FileFindIter::validateCurrent() {
	if (strcmp((*this)->ent->d_name, ".") == 0 || strcmp((*this)->ent->d_name, "..") == 0) {
		return this->operator++();
	}

	if (pattern && fnmatch(pattern->c_str(), data.ent->d_name, 0) != 0) {
		return this->operator++();
	}

	if (!Text::validateUtf8((*this)->ent->d_name)) {
		dcdebug("FileFindIter: UTF-8 validation failed for the item name (%s)\n", Text::sanitizeUtf8((*this)->ent->d_name).c_str());
		return this->operator++();
	}

	return *this;
}

FileFindIter& FileFindIter::operator++() {
	if (!dir)
		return *this;
	data.ent = readdir(dir);
	if (!data.ent) {
		closedir(dir);
		dir = NULL;
		return *this;
	}

	return validateCurrent();
}

bool FileFindIter::operator!=(const FileFindIter& rhs) const {
	// good enough to to say if it's null
	return dir != rhs.dir;
}

FileFindIter::DirData::DirData() : ent(NULL) {}

string FileFindIter::DirData::getFileName() const noexcept {
	if (!ent) return Util::emptyString;
	return ent->d_name;
}

bool FileFindIter::DirData::isDirectory() const noexcept {
	struct stat inode;
	if (!ent) return false;
	if (stat((base + PATH_SEPARATOR + ent->d_name).c_str(), &inode) == -1) return false;
	return S_ISDIR(inode.st_mode);
}

bool FileFindIter::DirData::isHidden() const noexcept {
	if (!ent) return false;
	// Check if the parent directory is hidden for '.'
	if (strcmp(ent->d_name, ".") == 0 && base[0] == '.') return true;
	return ent->d_name[0] == '.' && strlen(ent->d_name) > 1;
}

bool FileFindIter::DirData::isLink() const noexcept {
	struct stat inode;
	if (!ent) return false;
	return File::isLink(base + PATH_SEPARATOR + ent->d_name);
}

int64_t FileFindIter::DirData::getSize() const noexcept {
	if (!ent) return 0;
	return File::getSize(base + PATH_SEPARATOR + ent->d_name);
}

time_t FileFindIter::DirData::getLastWriteTime() const noexcept {
	if (!ent) return 0;
	return File::getLastWriteTime(base + PATH_SEPARATOR + ent->d_name);
}

FileItem::FileItem(const string& aPath) : path(aPath) {
	struct stat inode;
	if (stat(aPath.c_str(), &inode) == -1) {
		throw FileException(SystemUtil::translateError(errno));
	}
}

bool FileItem::isDirectory() const noexcept {
	return File::isDirectory(path);
}

bool FileItem::isHidden() const noexcept {
	return File::isHidden(path);
}

bool FileItem::isLink() const noexcept {
	return File::isLink(path);
}

int64_t FileItem::getSize() const noexcept {
	return File::getSize(path);
}

time_t FileItem::getLastWriteTime() const noexcept {
	return File::getLastWriteTime(path);
}

#endif // _WIN32

#ifdef _WIN32
FILE* dcpp_fopen(const char* filename, const char* mode) {
	return _wfopen(Text::toT(filename).c_str(), Text::toT(mode).c_str());
}
#endif

} // namespace dcpp
