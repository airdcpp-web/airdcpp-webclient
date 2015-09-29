/*
	Copyright (C) 2004-2005 Cory Nelson

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any damages
	arising from the use of this software.

	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:

	1. The origin of this software must not be misrepresented; you must not
		claim that you wrote the original software. If you use this software
		in a product, an acknowledgment in the product documentation would be
		appreciated but is not required.
	2. Altered source versions must be plainly marked as such, and must not be
		misrepresented as being the original software.
	3. This notice may not be removed or altered from any source distribution.
	
	SVN Info :
		$Author: crise $
		$Date: 2013-01-06 17:59:44 +0200 (pe, 06 tammi 2013) $
		$Revision: 1215 $
*/

/*
	- Updated for DC++ base code @ 2008, by Crise
	- Added basic zip creation support (minizip) @ 2010, by Crise
*/

#include "stdinc.h"
#include "ZipFile.h"

#include "TimerManager.h"
#include "Util.h"

#include <minizip/zip.h>
#include <sys/stat.h>

namespace dcpp {

using std::make_pair;

string ZipFileException::TranslateError(int e) {
	switch(e) {
		case UNZ_END_OF_LIST_OF_FILE:		return "end of file list reached";
		case UNZ_EOF:						return "end of file reached";
		case UNZ_PARAMERROR:				return "invalid parameter given";
		case UNZ_BADZIPFILE:				return "bad zip file";
		case UNZ_INTERNALERROR:				return "internal error";
		case UNZ_CRCERROR:					return "crc error, file is corrupt";
		case UNZ_ERRNO:						return strerror(errno);
		default:							return "unknown error (" + Util::translateError(e) + ")";
	}
}

ZipFile::ZipFile(const string &file) : fp(NULL) {
	this->Open(file);
}

ZipFile::~ZipFile() {
	this->Close();
}

void ZipFile::Open(const string &file) {
	this->Close();
	this->fp = unzOpen(file.c_str());
	if(this->fp == NULL) throw ZipFileException("unzOpen");
}

void ZipFile::Close() {
	if(this->IsOpen()) {
		int ret = unzClose(this->fp);
		if(ret != UNZ_OK) throw ZipFileException("unzClose", ret);
		this->fp=NULL;
	}
}

bool ZipFile::IsOpen() const {
	return (this->fp != NULL);
}

bool ZipFile::GoToFirstFile() {
	return (unzGoToFirstFile(this->fp) == UNZ_OK);
}

bool ZipFile::GoToNextFile() {
	return (unzGoToNextFile(this->fp) == UNZ_OK);
}

void ZipFile::OpenCurrentFile() {
	int ret = unzOpenCurrentFile(this->fp);
	if(ret != UNZ_OK) throw ZipFileException("unzOpenCurrentFile", ret);
}

void ZipFile::CloseCurrentFile() {
	int ret = unzCloseCurrentFile(this->fp);
	if(ret != UNZ_OK) throw ZipFileException("unzCloseCurrentFile", ret);
}

string ZipFile::GetCurrentFileName() {
	char buf[1024];
	unz_file_info info;

	int ret = unzGetCurrentFileInfo(this->fp, &info, buf, sizeof(buf), NULL, 0, NULL, 0);
	if(ret != UNZ_OK) throw ZipFileException("unzGetCurrentFileInfo", ret);

	return buf;
}

const ZipFile::FileInfo ZipFile::GetCurrentFileInfo() {
	char buf[1024];
	unz_file_info info;

	int ret = unzGetCurrentFileInfo(this->fp, &info, buf, sizeof(buf), NULL, 0, NULL, 0);
	if(ret != UNZ_OK) throw ZipFileException("unzGetCurrentFileInfo", ret);

	struct tm t;
	t.tm_year = info.tmu_date.tm_year - 1900;
	t.tm_isdst = -1;
	t.tm_mon = info.tmu_date.tm_mon;
	t.tm_mday = info.tmu_date.tm_mday;
	t.tm_hour = info.tmu_date.tm_hour;
	t.tm_min = info.tmu_date.tm_min;
	t.tm_sec = info.tmu_date.tm_sec;

	return FileInfo(buf, mktime(&t), info.uncompressed_size);
}

pair<uint8_t*,size_t> ZipFile::ReadCurrentFile() {
	unz_file_info info;
	uLong ret = unzGetCurrentFileInfo(this->fp, &info, NULL, 0, NULL, 0, NULL, 0);
	if(ret != UNZ_OK) throw ZipFileException("unzGetCurrentFileInfo", ret);

	uint8_t* buf = new uint8_t[info.uncompressed_size];

	ret = unzReadCurrentFile(fp, buf, info.uncompressed_size);
	if(ret != info.uncompressed_size) throw ZipFileException("unzReadCurrentFile", ret);

	return { buf, info.uncompressed_size };
}

void ZipFile::ReadCurrentFile(const string &path) {
	try {
		string nameInZip = this->GetCurrentFileName();
		if(nameInZip[nameInZip.size()-1] != '/' &&  nameInZip[nameInZip.size()-1] != '\\') {
			pair<uint8_t*,size_t> file = this->ReadCurrentFile();

			const string& fullPath = (path[path.size()-1] == PATH_SEPARATOR) ? path + nameInZip : path;
			File::ensureDirectory(fullPath);

			File f(fullPath, File::WRITE, File::OPEN | File::CREATE | File::TRUNCATE);
			f.setEndPos(0);
			f.write(file.first, file.second);
			f.close();

			delete[] file.first;
		}
	} catch (const Exception& e) {
		throw ZipFileException(e.getError());
	}
}

void ZipFile::ReadFiles(ZipFile::FileMap& files) {
	try {
		if(this->GoToFirstFile()) {
			do {
				this->OpenCurrentFile();
				const FileInfo& fi = this->GetCurrentFileInfo();
				if(fi.name[fi.name.size()-1] != '/' && fi.name[fi.name.size()-1] != '\\') {
					pair<uint8_t*,size_t> file = this->ReadCurrentFile();
					files[fi.name] = make_pair(fi, FileContentType(file.first));
				}
				this->CloseCurrentFile();
			} while(this->GoToNextFile());
		}
	} catch (const Exception& e) {
		throw ZipFileException(e.getError());
	}
}

// Zip file creation
void ZipFile::CreateZipFile(const string& dstPath, const StringPairList& files) {
	zipFile zFile;
	int err = ZIP_OK;

	zFile = zipOpen(dstPath.c_str(), APPEND_STATUS_CREATE);
	if(!zFile)
		throw ZipFileException("zipOpen");

	for(const auto& i: files) {
		auto& path = i.first;
		const char* nameInZip = i.second.c_str();

		while(nameInZip[0] == '/' || nameInZip[0] == '\\')
			nameInZip++;

		zip_fileinfo zi;
		memzero(&zi, sizeof(zip_fileinfo));

		if(i.second[i.second.size()-1] == '/') {
			// Add directory entry (missing folder attributes)
			err = zipOpenNewFileInZip(zFile, nameInZip, &zi,
				NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_BEST_COMPRESSION);

			if(err != ZIP_OK)
				throw ZipFileException("zipOpenNewFileInZip", err);

			err = zipCloseFileInZip(zFile);
			if(err != ZIP_OK)
				throw ZipFileException("zipCloseFileInZip", err);
		} else {
			const size_t buf_size = 16384;
			char* buf = new char[buf_size];

			try {
				File f(path, File::READ, File::OPEN | File::SHARED_WRITE);

				if(f.getSize() != -1) {
					time_t time = f.getLastModified();
					tm* filedate = localtime(&time);

					zi.tmz_date.tm_sec = filedate->tm_sec;
					zi.tmz_date.tm_min = filedate->tm_min;
					zi.tmz_date.tm_hour = filedate->tm_hour;
					zi.tmz_date.tm_mday = filedate->tm_mday;
					zi.tmz_date.tm_mon = filedate->tm_mon;
					zi.tmz_date.tm_year = filedate->tm_year;

					err = zipOpenNewFileInZip(zFile, nameInZip, &zi,
						NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_BEST_COMPRESSION);

					if(err != ZIP_OK)
						throw ZipFileException("zipOpenNewFileInZip", err);

					size_t read = buf_size;
					while((read = f.read(buf, read)) > 0) {
						err = zipWriteInFileInZip(zFile, buf, read);
						if(err < ZIP_OK)
							throw ZipFileException("zipWriteInFileInZip", err);
						read = buf_size;
					}

					err = zipCloseFileInZip(zFile);
					if(err != ZIP_OK)
						throw ZipFileException("zipCloseFileInZip", err);
				}

				f.close();
			} catch(const FileException& e) {
				throw ZipFileException(e.getError());
			}

			delete[] buf;
		}
	}

	err = zipClose(zFile, NULL);
	if(err != ZIP_OK)
		throw ZipFileException("zipClose", err);
}

// dstPath: path inside zip file (use forward slashes, not backslashes)
void ZipFile::CreateZipFileList(StringPairList& files, const string& srcPath, const string& dstPath, const string& aPattern, bool keepEmpty) noexcept {
	FileFindIter end;
	for(FileFindIter i(srcPath, "*"); i != end; ++i) {
		string name = i->getFileName();
		if(name == "." || name == "..")
			continue;

		if(i->isHidden() || i->isLink() || name.empty())
			continue;

		if(!aPattern.empty()) {
			try {
				boost::regex reg(aPattern);
				if(!boost::regex_search(name.begin(), name.end(), reg))
					continue;
			} catch(...) { /* ... */ }
		}

		if(i->isDirectory()) {
			string newSrcPath = srcPath + name + PATH_SEPARATOR;
			string newDstPath = dstPath + name + '/';

			StringPairList subFiles;
			ZipFile::CreateZipFileList(subFiles, newSrcPath, newDstPath); //don't pass the pattern to sub directories

			if(keepEmpty || !subFiles.empty()) {
				files.emplace_back(newSrcPath, newDstPath);
				files.insert(files.end(), subFiles.begin(), subFiles.end());
			}
		} else {
			files.emplace_back(srcPath + name, dstPath + name);
		}
	}
}

} // namespace dcpp
