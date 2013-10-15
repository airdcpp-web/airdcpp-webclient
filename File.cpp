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

#include "stdinc.h"
#include "File.h"
#include "Thread.h"

#ifdef _WIN32
#include "w.h"
#else
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <fnmatch.h>
#include <utime.h>
#endif

namespace dcpp {

#ifdef _WIN32
File::File(const string& aFileName, int access, int mode, BufferMode aBufferMode, bool isAbsolute /*true*/, bool isDirectory /*false*/) {
	dcassert(access == WRITE || access == READ || access == (READ | WRITE));

	int m = 0;
	if (mode & OPEN) {
		if (mode & CREATE) {
			m = (mode & TRUNCATE) ? CREATE_ALWAYS : OPEN_ALWAYS;
		} else {
			m = (mode & TRUNCATE) ? TRUNCATE_EXISTING : OPEN_EXISTING;
		}
	} else {
		if (mode & CREATE) {
			m = (mode & TRUNCATE) ? CREATE_ALWAYS : CREATE_NEW;
		} else {
			dcassert(0);
		}
	}

	DWORD shared = FILE_SHARE_READ | (mode & SHARED_WRITE ? (FILE_SHARE_WRITE) : 0);
	if (mode & SHARED_DELETE)
		shared |= FILE_SHARE_DELETE;

	DWORD dwFlags = aBufferMode;
	string path = aFileName;
	if (isAbsolute)
		path = Util::FormatPath(aFileName);

	if (isDirectory)
		dwFlags |= FILE_FLAG_BACKUP_SEMANTICS;

	h = ::CreateFile(Text::toT(path).c_str(), access, shared, NULL, m, dwFlags, NULL);
	if(h == INVALID_HANDLE_VALUE) {
		throw FileException(Util::translateError(GetLastError()));
	}
}

uint64_t File::getLastModified() const noexcept {
	FILETIME f = {0};
	::GetFileTime(h, NULL, NULL, &f);
	return convertTime(&f);
}

uint64_t File::convertTime(FILETIME* f) {
	SYSTEMTIME s = { 1970, 1, 0, 1, 0, 0, 0, 0 };
	FILETIME f2 = {0};
	if(::SystemTimeToFileTime(&s, &f2)) {
		ULARGE_INTEGER a,b;
		a.LowPart =f->dwLowDateTime;
		a.HighPart=f->dwHighDateTime;
		b.LowPart =f2.dwLowDateTime;
		b.HighPart=f2.dwHighDateTime;
		return (a.QuadPart - b.QuadPart) / (10000000LL); // 100ns > s
	}
	return 0;
}

FILETIME File::convertTime(uint64_t f) {
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
		throw(FileException(Util::translateError(GetLastError())));
	}
	len = x;
	return x;
}

size_t File::write(const void* buf, size_t len) {
	DWORD x;
	if(!::WriteFile(h, buf, (DWORD)len, &x, NULL)) {
		throw FileException(Util::translateError(GetLastError()));
	}
	dcassert(x == len);
	return x;
}
void File::setEOF() {
	dcassert(isOpen());
	if(!SetEndOfFile(h)) {
		throw FileException(Util::translateError(GetLastError()));
	}
}

size_t File::flush() {
	if(isOpen() && !FlushFileBuffers(h))
		throw FileException(Util::translateError(GetLastError()));
	return 0;
}

void File::renameFile(const string& source, const string& target) {
	if(!::MoveFileEx(Text::toT(Util::FormatPath(source)).c_str(), Text::toT(Util::FormatPath(target)).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
		throw FileException(Util::translateError(GetLastError()));
	}
}

void File::copyFile(const string& src, const string& target) {
	if(!::CopyFile(Text::toT(Util::FormatPath(src)).c_str(), Text::toT(Util::FormatPath(target)).c_str(), FALSE)) {
		throw FileException(Util::translateError(GetLastError()));
	}
}

uint64_t File::getLastModified(const string& aPath) noexcept {
	if (aPath.empty())
		return 0;

	FileFindIter ff = FileFindIter(aPath.back() == PATH_SEPARATOR ? aPath.substr(0, aPath.size() - 1) : aPath);
	if (ff != FileFindIter()) {
		return ff->getLastWriteTime();
	}

	return 0;
}

bool File::isHidden(const string& aPath) noexcept {
	if (aPath.empty())
		return 0;

	FileFindIter ff = FileFindIter(aPath.back() == PATH_SEPARATOR ? aPath.substr(0, aPath.size() - 1) : aPath);
	if (ff != FileFindIter()) {
		return ff->isHidden();
	}

	return false;
}

bool File::deleteFile(const string& aFileName) noexcept {
	return ::DeleteFile(Text::toT(Util::FormatPath(aFileName)).c_str()) > 0 ? true : false;
}

TimeKeeper::TimeKeeper(const string& aPath) : initialized(false), File(aPath, File::RW, File::OPEN | File::SHARED_WRITE, File::BUFFER_NONE, true, true) {
	if (::GetFileTime(h, NULL, NULL, &time) > 0)
		initialized = true;
}

TimeKeeper::~TimeKeeper() {
	if (!initialized)
		return;

	::SetFileTime(h, NULL, NULL, &time);
}

void File::removeDirectory(const string& aPath) noexcept {
	::RemoveDirectory(Text::toT(Util::FormatPath(aPath)).c_str());
}

int64_t File::getSize(const string& aFileName) noexcept {
	WIN32_FIND_DATA fd;
	HANDLE hFind;

	hFind = FindFirstFile(Text::toT(Util::FormatPath(aFileName)).c_str(), &fd);

	if (hFind == INVALID_HANDLE_VALUE) {
		return -1;
	} else {
		FindClose(hFind);
		return ((int64_t)fd.nFileSizeHigh << 32 | (int64_t)fd.nFileSizeLow);
	}
}

void File::ensureDirectory(const string& aFile) noexcept {
	// Skip the first dir...
	tstring file;
	Text::toT(aFile, file);
	tstring::size_type start = file.find_first_of(_T("\\/"));
	if(start == string::npos)
		return;
	start++;
	while( (start = file.find_first_of(_T("\\/"), start)) != string::npos) {
		::CreateDirectory((Util::FormatPathT(file.substr(0, start+1))).c_str(), NULL);
		start++;
	}
}

bool File::createDirectory(const string& aFile) {
	// Skip the first dir...
	int result = 0;
	tstring file;
	Text::toT(aFile, file);
	wstring::size_type start = file.find_first_of(L"\\/");
	if(start == string::npos)
		return false;
	start++;
	while( (start = file.find_first_of(L"\\/", start)) != string::npos) {
		result = CreateDirectory(file.substr(0, start+1).c_str(), NULL);
		start++;
	}
	if(result == 0) {
		result = GetLastError();
		if(result == ERROR_ALREADY_EXISTS || result == ERROR_SUCCESS)
			return false;
		else if(result == ERROR_PATH_NOT_FOUND) //we can't recover from this gracefully.
			throw FileException(Util::translateError(result));
	}

	return true;
}

bool File::isAbsolute(const string& path) noexcept {
	return path.size() > 2 && (path[1] == ':' || path[0] == '/' || path[0] == '\\');
}

string File::getMountPath(const string& aPath) noexcept {
	unique_ptr<TCHAR> buf(new TCHAR[aPath.length()]);
	GetVolumePathName(Text::toT(aPath).c_str(), buf.get(), aPath.length());
	return Text::fromT(buf.get());
}

int64_t File::getFreeSpace(const string& aPath) noexcept {
	int64_t freeSpace = 0, tmp = 0;
	auto ret = GetDiskFreeSpaceEx(Text::toT(aPath).c_str(), NULL, (PULARGE_INTEGER)&tmp, (PULARGE_INTEGER)&freeSpace);
	return ret > 0 ? freeSpace : -1;
}

int64_t File::getBlockSize(const string& aFileName) noexcept {
	DWORD sectorBytes, clusterSectors, tmp2, tmp3;
	auto ret = GetDiskFreeSpace(Text::toT(aFileName).c_str(), &clusterSectors, &sectorBytes, &tmp2, &tmp3);
	return ret > 0 ? static_cast<int64_t>(sectorBytes)*static_cast<int64_t>(clusterSectors) : 4096;
}

#else // !_WIN32

File::File(const string& aFileName, int access, int mode, BufferMode /*aBufferMode*/, bool /*isAbsolute*/, bool /*isDirectory*/) {
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

	string filename = Text::fromUtf8(aFileName);
	
	struct stat s;
	if(lstat(filename.c_str(), &s) != -1) {
		if(!S_ISREG(s.st_mode) && !S_ISLNK(s.st_mode))
			throw FileException("Invalid file type");
	}

	h = open(filename.c_str(), m, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if(h == -1)
		throw FileException(Util::translateError(errno));
}

uint64_t File::getLastModified() const noexcept {
	struct stat s;
	if (::fstat(h, &s) == -1)
		return 0;

	return (uint32_t)s.st_mtime;
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
		throw FileException(Util::translateError(errno));
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
				throw FileException(Util::translateError(errno));
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
		ftruncate(h,(off_t)len);
		return 1;
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
		throw FileException(Util::translateError(errno));
}

void File::setSize(int64_t newSize) {
	int64_t pos = getPos();
	setPos(newSize);
	setEOF();
	setPos(pos);
}

size_t File::flush() {
	if(isOpen() && fsync(h) == -1)
		throw FileException(Util::translateError(errno));
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
	int ret = ::rename(Text::fromUtf8(source).c_str(), Text::fromUtf8(target).c_str());
	if(ret != 0 && errno == EXDEV) {
		copyFile(source, target);
		deleteFile(source);
	} else if(ret != 0)
		throw FileException(source + Util::translateError(errno));
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

bool File::deleteFile(const string& aFileName) noexcept {
	return ::unlink(Text::fromUtf8(aFileName).c_str()) == 0;
}

int64_t File::getSize(const string& aFileName) noexcept {
	struct stat s;
	if(stat(Text::fromUtf8(aFileName).c_str(), &s) == -1)
		return -1;

	return s.st_size;
}

void File::ensureDirectory(const string& aFile) noexcept {
	string file = Text::fromUtf8(aFile);
	string::size_type start = 0;
	while( (start = file.find_first_of('/', start)) != string::npos) {
		mkdir(file.substr(0, start+1).c_str(), S_IRWXU | S_IRWXG | S_IRWXO);
		start++;
	}
}

bool File::isAbsolute(const string& path) noexcept {
	return path.size() > 1 && path[0] == '/';
}

int64_t File::getFreeSpace(const string& aFileName) noexcept {
	struct statvfs sfs;
	if (statvfs(Text::fromUtf8(aFileName).c_str(), &sfs) == -1) {
		return -1;
	}

	int64_t ret = (int64_t)sfs.f_bsize * (int64_t)sfs.f_bfree;
	return ret;
}

int64_t File::getBlockSize(const string& aFileName) noexcept {
	struct stat statbuf;
	if (stat(Text::fromUtf8(aFileName).c_str(), &statbuf) == -1) {
		return 4096;
	}

	return statbuf.st_size;
}

string File::getMountPath(const string& aPath) noexcept {
	struct stat statbuf;
	if (stat(Text::fromUtf8(aPath).c_str(), &statbuf) == -1) {
		return Util::emptyString;
	}

	return Util::toString((uint32_t)statbuf.st_dev);
}

uint64_t File::getLastModified(const string& aPath) noexcept {
	struct stat statbuf;
	if (stat(Text::fromUtf8(aPath).c_str(), &statbuf) == -1) {
		return 0;
	}

	return statbuf.st_mtime;
}

void File::removeDirectory(const string& aPath) noexcept {
	rmdir(Text::fromUtf8(aPath).c_str());
}

bool File::isHidden(const string& aPath) noexcept {
	return aPath.find("/.") != string::npos;
}

TimeKeeper::TimeKeeper(const string& aPath) : path(Text::fromUtf8(aPath)), time(File::getLastModified(path)) {
}

TimeKeeper::~TimeKeeper() {
	if (time == 0)
		return;

	struct utimbuf ubuf;
	ubuf.modtime = time;
	::time(&ubuf.actime); 
	utime(path.c_str(), &ubuf);
}

#endif // !_WIN32

TimeKeeper* TimeKeeper::createKeeper(const string& aPath) noexcept{
	TimeKeeper* ret = nullptr;
	try {
		ret = new TimeKeeper(aPath);
		return ret;
	} catch (FileException & /*e*/) {
		delete ret;
		return nullptr;
	}
}

bool File::deleteFileEx(const string& aFileName, int maxAttempts, bool keepFolderDate /*false*/) noexcept {
	unique_ptr<TimeKeeper> keeper;
	if (keepFolderDate) {
		keeper.reset(TimeKeeper::createKeeper(Util::getFilePath(aFileName)));
	}

	bool success = false;
	for (int i = 0; i < maxAttempts && (success = deleteFile(aFileName)) == false; ++i)
		Thread::sleep(1000);

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

string File::read(size_t len) {
	string s(len, 0);
	size_t x = read(&s[0], len);
	if(x != s.size())
		s.resize(x);
	return s;
}

string File::read() {
	setPos(0);
	int64_t sz = getSize();
	if(sz == -1)
		return Util::emptyString;
	return read((uint32_t)sz);
}

StringList File::findFiles(const string& aPath, const string& pattern, int flags) {
	StringList ret;
	forEachFile(aPath, pattern, [&](const string& aFileName, bool isDir, int64_t /*size*/) { 
		if ((flags & TYPE_FILE && !isDir) || (flags & TYPE_DIRECTORY && isDir))
			ret.push_back(aPath + aFileName); 
	}, !(flags & FLAG_HIDDEN));
	return ret;
}

void File::forEachFile(const string& aPath, const string& pattern, std::function<void (const string & /*name*/, bool /*isDir*/, int64_t /*size*/)> aF, bool skipHidden) {
	for (FileFindIter i(aPath, pattern); i != FileFindIter(); ++i) {
		if ((!skipHidden || !i->isHidden())) {
			auto name = i->getFileName();
			if (name.compare(".") != 0 && (name.length() < 2 || name.compare("..") != 0)) {
				bool isDir = i->isDirectory();
				aF(name + (isDir ? PATH_SEPARATOR_STR : Util::emptyString), isDir, i->getSize());
			}
		}
	}
}

static void getDirSizeInternal(const string& aPath, int64_t& size_, bool recursive, const string& pattern) {
	File::forEachFile(aPath, pattern, [&](const string& aFileName, bool isDir, int64_t aSize) { 
		if (isDir && recursive) {
			getDirSizeInternal(aPath + aFileName, size_, true, pattern);
		} else {
			size_ += aSize;
		}
	});
}

int64_t File::getDirSize(const string& aPath, bool recursive, const string& pattern) noexcept {
	int64_t ret = 0;
	getDirSizeInternal(aPath, ret, recursive, pattern);
	return ret;
}

#ifdef _WIN32

FileFindIter::FileFindIter() : handle(INVALID_HANDLE_VALUE) { }

FileFindIter::FileFindIter(const string& aPath, const string& aPattern, bool dirsOnly /*false*/) : handle(INVALID_HANDLE_VALUE) {
	if (Util::getOsMajor() >= 6 && Util::getOsMinor() >= 1) {
		handle = ::FindFirstFileEx(Text::toT(Util::FormatPath(aPath) + aPattern).c_str(), FindExInfoBasic, &data, dirsOnly ? FindExSearchLimitToDirectories : FindExSearchNameMatch, NULL, NULL);
	} else {
		handle = ::FindFirstFile(Text::toT(Util::FormatPath(aPath) + aPattern).c_str(), &data);
	}
}

FileFindIter::~FileFindIter() {
	if(handle != INVALID_HANDLE_VALUE) {
		::FindClose(handle);
	}
}

FileFindIter& FileFindIter::operator++() {
	if(!::FindNextFile(handle, &data)) {
		::FindClose(handle);
		handle = INVALID_HANDLE_VALUE;
	}
	return *this;
}

bool FileFindIter::operator!=(const FileFindIter& rhs) const { return handle != rhs.handle; }

FileFindIter::DirData::DirData() { }

string FileFindIter::DirData::getFileName() {
	return Text::fromT(cFileName);
}

bool FileFindIter::DirData::isDirectory() {
	return (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) > 0;
}

bool FileFindIter::DirData::isHidden() {
	return ((dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) || (cFileName[0] == L'.') || (dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) || (dwFileAttributes & FILE_ATTRIBUTE_OFFLINE));
}

bool FileFindIter::DirData::isLink() {
	return (dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) > 0;
}

int64_t FileFindIter::DirData::getSize() {
	return (int64_t)nFileSizeLow | ((int64_t)nFileSizeHigh)<<32;
}

uint64_t FileFindIter::DirData::getLastWriteTime() {
	return File::convertTime(&ftLastWriteTime);
}

#else // _WIN32

FileFindIter::FileFindIter() {
	dir = NULL;
	data.ent = NULL;
}

FileFindIter::FileFindIter(const string& aPath, const string& aPattern, bool dirsOnly /*false*/) {
	string filename = Text::fromUtf8(aPath);
	dir = opendir(filename.c_str());
	if (!dir)
		return;

	data.base = filename;
	data.ent = readdir(dir);
	if (!aPattern.empty() && aPattern != "*") {
		pattern.reset(new string(aPattern));
	}

	if (!data.ent) {
		closedir(dir);
		dir = NULL;
		return;
	} else if (!matchPattern()) {
		operator++();
	}
}

FileFindIter::~FileFindIter() {
	if (dir) closedir(dir);
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

	if (matchPattern())
		return *this;

	// continue to the next one...
	return operator++();
}

bool FileFindIter::matchPattern() const {
	return !pattern || fnmatch(pattern->c_str(), data.ent->d_name, 0) == 0;
}

bool FileFindIter::operator!=(const FileFindIter& rhs) const {
	// good enough to to say if it's null
	return dir != rhs.dir;
}

FileFindIter::DirData::DirData() : ent(NULL) {}

string FileFindIter::DirData::getFileName() {
	if (!ent) return Util::emptyString;
	return Text::toUtf8(ent->d_name);
}

bool FileFindIter::DirData::isDirectory() {
	struct stat inode;
	if (!ent) return false;
	if (stat((base + PATH_SEPARATOR + ent->d_name).c_str(), &inode) == -1) return false;
	return S_ISDIR(inode.st_mode);
}

bool FileFindIter::DirData::isHidden() {
	if (!ent) return false;
	// Check if the parent directory is hidden for '.'
	if (strcmp(ent->d_name, ".") == 0 && base[0] == '.') return true;
	return ent->d_name[0] == '.' && strlen(ent->d_name) > 1;
}

bool FileFindIter::DirData::isLink() {
	struct stat inode;
	if (!ent) return false;
	if (lstat((base + PATH_SEPARATOR + ent->d_name).c_str(), &inode) == -1) return false;
	return S_ISLNK(inode.st_mode);
}

int64_t FileFindIter::DirData::getSize() {
	struct stat inode;
	if (!ent) return false;
	if (stat((base + PATH_SEPARATOR + ent->d_name).c_str(), &inode) == -1) return 0;
	return inode.st_size;
}

uint64_t FileFindIter::DirData::getLastWriteTime() {
	struct stat inode;
	if (!ent) return false;
	if (stat((base + PATH_SEPARATOR + ent->d_name).c_str(), &inode) == -1) return 0;
	return inode.st_mtime;
}

#endif // _WIN32

#ifdef _WIN32
FILE* dcpp_fopen(const char* filename, const char* mode) {
	return _wfopen(Text::toT(filename).c_str(), Text::toT(mode).c_str());
}
#endif

} // namespace dcpp
