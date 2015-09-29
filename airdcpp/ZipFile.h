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

#ifndef ZIPFILE_H
#define ZIPFILE_H

#include "compiler.h"

#include <map>
#include <memory>
#include <utility>

#include <boost/noncopyable.hpp>
#ifdef STD_MAP_STD_UNIQUE_PTR_BUG
#include <boost/shared_array.hpp>
#endif

#include "Exception.h"
#include "File.h"

#ifndef STRICTZIPUNZIP
# define STRICTZIPUNZIP
#endif

#include <zlib.h>
#include <minizip/unzip.h>

namespace dcpp {

using std::unique_ptr;
using std::pair;
using std::map;

class ZipFileException : public Exception 
{
public:
	ZipFileException(const string &func) : Exception(func) { }
	ZipFileException(const string &func, int msg) : Exception(func + ": " + TranslateError(msg)) { }
	ZipFileException(const string &func, const string &msg) : Exception(func + ": " + msg) { }
	virtual ~ZipFileException() noexcept { }

private:
	static string TranslateError(int e);
};

class ZipFile : private boost::noncopyable 
{
public:
	struct FileInfo {
		FileInfo() : name(Util::emptyString), time((time_t)-1), size(-1) { }
		FileInfo(string zfn, time_t zft, int64_t zfs) : name(zfn), time(zft), size(zfs) { }
		~FileInfo() { }

		string name;
		time_t time;
		int64_t size;
	};

#ifndef STD_MAP_STD_UNIQUE_PTR_BUG
	typedef unique_ptr<uint8_t[]> FileContentType;
#else
	typedef boost::shared_array<uint8_t> FileContentType;
#endif
	typedef map<string, pair<FileInfo, FileContentType > > FileMap;

	ZipFile() : fp(NULL) { }
	ZipFile(const string &file);
	~ZipFile();

	void Open(const string &file);
	void Close();

	bool IsOpen() const;

	bool GoToFirstFile();
	bool GoToNextFile();

	void OpenCurrentFile();
	void CloseCurrentFile();

	string GetCurrentFileName();
	const FileInfo GetCurrentFileInfo();

	pair<uint8_t*,size_t> ReadCurrentFile();
	void ReadCurrentFile(const string &path);
	void ReadFiles(FileMap& files);

	// Zip file creation
	static void CreateZipFile(const string& dstPath, const StringPairList& files);
	static void CreateZipFileList(StringPairList& files, const string& srcPath, const string& dstPath, const string& aPattern = Util::emptyString, bool keepEmpty = true) noexcept;

	static void CreateZipFile(const string& dstPath, const string& srcDir, const string& aPattern = Util::emptyString, bool keepEmpty = true) {
		StringPairList files;
		ZipFile::CreateZipFileList(files, srcDir, Util::emptyString, aPattern, keepEmpty);

		ZipFile::CreateZipFile(dstPath, files);
	}

private:
	unzFile fp;
};

} // namespace dcpp

#endif // ZIPFILE_H
