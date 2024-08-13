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
#include "PathUtil.h"

#include "File.h"
#include "Exception.h"
#include "Text.h"
#include "Thread.h"
#include "Util.h"


namespace dcpp {

static const char badChars[] = { 
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
	17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
	31, 
#ifdef _WIN32
	'<', '>', '"', '|', '?', '*', '/',
#endif
	0
};

/**
 * Replaces all strange characters in a file with '_'
 * @todo Check for invalid names such as nul and aux...
 */
string PathUtil::cleanPathChars(string tmp, bool isFileName) noexcept {
	string::size_type i = 0;

	// First, eliminate forbidden chars
	while( (i = tmp.find_first_of(badChars, i)) != string::npos) {
		tmp[i] = '_';
		i++;
	}

#ifdef _WIN32
	// Then, eliminate all ':' that are not the second letter ("c:\...")
	i = 0;
	while( (i = tmp.find(':', i)) != string::npos) {
		if (i == 1 && !isFileName) {
			i++;
			continue;
		}
		tmp[i] = '_';	
		i++;
	}
#endif

	// Remove the .\ that doesn't serve any purpose
	i = 0;
	while( (i = tmp.find("\\.\\", i)) != string::npos) {
		tmp.erase(i+1, 2);
	}
	i = 0;
	while( (i = tmp.find("/./", i)) != string::npos) {
		tmp.erase(i+1, 2);
	}

	// Remove any double \\ that are not at the beginning of the path...
	i = isFileName ? 0 : 1;
	while( (i = tmp.find("\\\\", i)) != string::npos) {
		tmp.erase(i+1, 1);
	}
	i = isFileName ? 0 : 1;
	while( (i = tmp.find("//", i)) != string::npos) {
		tmp.erase(i+1, 1);
	}

	// And last, but not least, the infamous ..\! ...
	i = 0;
	while( ((i = tmp.find("\\..\\", i)) != string::npos) ) {
		tmp[i + 1] = '_';
		tmp[i + 2] = '_';
		tmp[i + 3] = '_';
		i += 2;
	}
	i = 0;
	while( ((i = tmp.find("/../", i)) != string::npos) ) {
		tmp[i + 1] = '_';
		tmp[i + 2] = '_';
		tmp[i + 3] = '_';
		i += 2;
	}

	// Dots at the end of path names aren't popular
	i = 0;
	while( ((i = tmp.find(".\\", i)) != string::npos) ) {
		if(i != 0)
			tmp[i] = '_';
		i += 1;
	}
	i = 0;
	while( ((i = tmp.find("./", i)) != string::npos) ) {
		if(i != 0)
			tmp[i] = '_';
		i += 1;
	}

	if (isFileName) {
		tmp = cleanPathSeparators(tmp);
	}


	return tmp;
}

string PathUtil::cleanPathSeparators(const string& str) noexcept {
	string ret(str);
	string::size_type i = 0;
	while ((i = ret.find_first_of("/\\", i)) != string::npos) {
		ret[i] = '_';
	}
	return ret;
}


bool PathUtil::checkExtension(const string& tmp) noexcept {
	for(size_t i = 0, n = tmp.size(); i < n; ++i) {
		if (tmp[i] < 0 || tmp[i] == 32 || tmp[i] == ':') {
			return false;
		}
	}
	if(tmp.find_first_of(badChars, 0) != string::npos) {
		return false;
	}
	return true;
}

bool PathUtil::isAdcDirectoryPath(const string& aPath) noexcept {
	return !aPath.empty() && aPath.front() == ADC_ROOT && aPath.back() == ADC_SEPARATOR;
}

bool PathUtil::isAdcRoot(const string& aPath) noexcept {
	return aPath.size() == 1 && aPath.front() == ADC_ROOT;
}

bool PathUtil::fileExists(const string &aFile) noexcept {
	if(aFile.empty())
		return false;

#ifdef _WIN32
	DWORD attr = GetFileAttributes(Text::toT(PathUtil::formatPath(aFile)).c_str());
	return (attr != 0xFFFFFFFF);
#else
	return File::getSize(aFile) != -1;
#endif
}

string PathUtil::toAdcFile(const string& file) noexcept {
	if(file == "files.xml.bz2" || file == "files.xml")
		return file;

	string ret;
	ret.reserve(file.length() + 1);
	ret += ADC_ROOT;
	ret += file;
	for(string::size_type i = 0; i < ret.length(); ++i) {
		if(ret[i] == NMDC_SEPARATOR) {
			ret[i] = ADC_SEPARATOR;
		}
	}
	return ret;
}
string PathUtil::toNmdcFile(const string& file) noexcept {
	if(file.empty())
		return Util::emptyString;

	string ret(file.substr(1));
	for(string::size_type i = 0; i < ret.length(); ++i) {
		if(ret[i] == ADC_SEPARATOR) {
			ret[i] = NMDC_SEPARATOR;
		}
	}
	return ret;
}

string PathUtil::getFilePath(const string& path, const char separator) noexcept {
	string::size_type i = path.rfind(separator);
	return (i != string::npos) ? path.substr(0, i + 1) : path;
}

string PathUtil::getFileName(const string& path, const char separator) noexcept {
	string::size_type i = path.rfind(separator);
	return (i != string::npos) ? path.substr(i + 1) : path;
}

string PathUtil::getFileExt(const string& path) noexcept {
	string::size_type i = path.rfind('.');
	return (i != string::npos) ? path.substr(i) : Util::emptyString;
}

string PathUtil::getLastDir(const string& path, const char separator) noexcept {
	string::size_type i = path.rfind(separator);
	if(i == string::npos)
		return path;

	string::size_type j = path.rfind(separator, i - 1);
	if (j == string::npos)
		return path.substr(0, i);

	return path.substr(j+1, i-j-1);
}

string PathUtil::getParentDir(const string& path, const char separator /*PATH_SEPARATOR*/, bool allowEmpty /*false*/) noexcept {
	string::size_type i = path.rfind(separator);
	if(i == string::npos)
		return allowEmpty ? Util::emptyString : path;

	string::size_type j = path.rfind(separator, i - 1);
	if (j != string::npos) 
		return path.substr(0, j+1);
	
	return allowEmpty ? Util::emptyString : path;
}

string PathUtil::joinDirectory(const string& aPath, const string& aDirectoryName, const char separator) noexcept {
	return aPath + aDirectoryName + separator;
}

string PathUtil::ensureTrailingSlash(const string& aPath, const char aSeparator) noexcept {
	if (!aPath.empty() && !isDirectoryPath(aPath, aSeparator)) {
		return aPath + aSeparator;
	}

	return aPath;
}

string PathUtil::validatePath(const string& aPath, bool aRequireEndSeparator) noexcept {
	auto path = cleanPathChars(aPath, false);
	if (aRequireEndSeparator) {
		path = PathUtil::ensureTrailingSlash(path);
	}

	return path;
}

wstring PathUtil::getFilePath(const wstring& path) noexcept {
	wstring::size_type i = path.rfind(PATH_SEPARATOR);
	return (i != wstring::npos) ? path.substr(0, i + 1) : path;
}

wstring PathUtil::getFileName(const wstring& path) noexcept {
	wstring::size_type i = path.rfind(PATH_SEPARATOR);
	return (i != wstring::npos) ? path.substr(i + 1) : path;
}

wstring PathUtil::getFileExt(const wstring& path) noexcept {
	wstring::size_type i = path.rfind('.');
	return (i != wstring::npos) ? path.substr(i) : Util::emptyStringW;
}

wstring PathUtil::getLastDir(const wstring& path) noexcept {
	wstring::size_type i = path.rfind(PATH_SEPARATOR);
	if(i == wstring::npos)
		return Util::emptyStringW;

	wstring::size_type j = path.rfind(PATH_SEPARATOR, i-1);
	if (j == wstring::npos)
		return i == path.length()-1 ? path.substr(0, i) : path;

	return path.substr(j+1, i-j-1);
}

int PathUtil::pathSort(const string& a, const string& b) noexcept {
	auto comp = compare(PathUtil::getFilePath(a), PathUtil::getFilePath(b));
	if (comp == 0) {
		return compare(a, b);
	}

	return comp;
}

/* returns true if aDir is a subdir of aParent */
bool PathUtil::isSub(const string& aTestSub, const string& aParent, const char aSeparator) noexcept {
	if (aTestSub.length() <= aParent.length())
		return false;

	if (Util::strnicmp(aTestSub, aParent, aParent.length()) != 0)
		return false;

	// either the parent must end with a separator or it must follow in the subdirectory
	return aParent.empty() || aParent.back() == aSeparator || aTestSub[aParent.length()] == aSeparator;
}

/* returns true if aSub is a subdir of aDir OR both are the same dir */
bool PathUtil::isParentOrExact(const string& aTestParent, const string& aSub, const char aSeparator) noexcept {
	if (aSub.length() < aTestParent.length())
		return false;

	if (Util::strnicmp(aSub, aTestParent, aTestParent.length()) != 0)
		return false;

	// either the parent must end with a separator or it must follow in the subdirectory
	return aTestParent.empty() || aTestParent.length() == aSub.length() || aTestParent.back() == aSeparator || aSub[aTestParent.length()] == aSeparator;
}

bool PathUtil::isParentOrExactLower(const string& aParentLower, const string& aSubLower, const char aSeparator) noexcept {
	if (aSubLower.length() < aParentLower.length())
		return false;

	if (strncmp(aSubLower.c_str(), aParentLower.c_str(), aParentLower.length()) != 0) {
		return false;
	}

	// either the parent must end with a separator or it must follow in the subdirectory
	return aParentLower.empty() || aParentLower.length() == aSubLower.length() || aParentLower.back() == aSeparator || aSubLower[aParentLower.length()] == aSeparator;
}


string PathUtil::subtractCommonParents(const string& aToCompare, const StringList& aToSubtract) noexcept {
	StringList converted;
	for (const auto& p : aToSubtract) {
		if (p.length() > aToCompare.length()) {
			converted.push_back(p.substr(aToCompare.length()));
		}
	}

	return Util::listToString(converted);
}

string PathUtil::subtractCommonDirs(const string& aToCompare, const string& aToSubtract, char aSeparator) noexcept {
	auto res = compareFromEnd(aToCompare, aToSubtract, aSeparator);
	if (res == string::npos) {
		return aToSubtract;
	}

	return aToSubtract.substr(0, res);
}

string PathUtil::getLastCommonDirectoryPathFromSub(const string& aMainPath, const string& aSubPath, char aSubSeparator, size_t aMainBaseLength) noexcept {
	auto pos = compareFromEnd(aMainPath, aSubPath, aSubSeparator);

	// Get the next directory
	if (pos < aSubPath.length()) {
		auto pos2 = aSubPath.find(aSubSeparator, pos + 1);
		if (pos2 != string::npos) {
			pos = pos2 + 1;
		}
	}

	auto mainSubSectionLength = aMainPath.length() - aMainBaseLength;
	return aSubPath.substr(0, max(pos, aSubPath.length() > mainSubSectionLength ? aSubPath.length() - mainSubSectionLength : 0));
}

size_t PathUtil::compareFromEnd(const string& aMainPath, const string& aSubPath, char aSubSeparator) noexcept {
	if (aSubPath.length() > 1) {
		string::size_type i = aSubPath.length() - 2;
		string::size_type j;
		for (;;) {
			j = aSubPath.find_last_of(aSubSeparator, i);
			if (j == string::npos) {
				j = 0; // compare from beginning
			} else {
				j++;
			}

			if (static_cast<int>(aMainPath.length() - (aSubPath.length() - j)) < 0)
				break; // out of scope for aMainPath

			if (Util::stricmp(aSubPath.substr(j, i - j + 1), aMainPath.substr(aMainPath.length() - (aSubPath.length() - j), i - j + 1)) != 0)
				break;

			if (j <= 1) {
				// Fully matched
				return 0;
			}

			i = j - 2;
		}

		return i + 2;
	}

	return aSubPath.length();
}

string PathUtil::getAdcMatchPath(const string& aRemoteFile, const string& aLocalFile, const string& aLocalBundlePath, bool aNmdc) noexcept {
	if (aNmdc) {
		// For simplicity, only perform the path comparison for ADC results
		if (Text::toLower(aRemoteFile).find(Text::toLower(getLastDir(aLocalBundlePath))) != string::npos) {
			return aLocalBundlePath;
		}

		return ADC_ROOT_STR;
	}

	// Get last matching directory for matching recursive filelist from the user
	auto remoteFileDir = getAdcFilePath(aRemoteFile);
	auto localBundleFileDir = getFilePath(aLocalFile);
	return getLastCommonAdcDirectoryPathFromSub(localBundleFileDir, remoteFileDir, aLocalBundlePath.length());
}


bool PathUtil::removeDirectoryIfEmptyRecursive(const string& aPath, int aMaxAttempts, int aAttempts) {
	/* recursive check for empty dirs */
	for(FileFindIter i(aPath, "*"); i != FileFindIter(); ++i) {
		try {
			if(i->isDirectory()) {
				string dir = aPath + i->getFileName() + PATH_SEPARATOR;
				if (!removeDirectoryIfEmptyRecursive(dir, aMaxAttempts, 0))
					return false;
			} else if (getFileExt(i->getFileName()) == ".dctmp") {
				if (aAttempts == aMaxAttempts) {
					return false;
				}

				Thread::sleep(500);
				return removeDirectoryIfEmptyRecursive(aPath, aMaxAttempts, aAttempts + 1);
			} else {
				return false;
			}
		} catch(const FileException&) { } 
	}

	File::removeDirectory(aPath);
	return true;
}

bool PathUtil::removeDirectoryIfEmpty(const string& aPath, int aMaxAttempts) {
	return removeDirectoryIfEmptyRecursive(aPath, aMaxAttempts, 0);
}


#if defined _WIN32 && defined _DEBUG
void PathUtil::test() noexcept {
	dcassert(isParentOrExactLocal(R"(C:\Projects\)", R"(C:\Projects\)"));
	dcassert(isParentOrExactLocal(R"(C:\Projects\)", R"(C:\Projects\test)"));
	dcassert(isParentOrExactLocal(R"(C:\Projects)", R"(C:\Projects\test)"));
	dcassert(isParentOrExactLocal(R"(C:\Projects\)", R"(C:\Projects\test)"));
	dcassert(!isParentOrExactLocal(R"(C:\Projects)", R"(C:\Projectstest)"));
	dcassert(!isParentOrExactLocal(R"(C:\Projectstest)", R"(C:\Projects)"));
	dcassert(!isParentOrExactLocal(R"(C:\Projects\test)", ""));
	dcassert(isParentOrExactLocal("", R"(C:\Projects\test)"));

	dcassert(!isSubLocal(R"(C:\Projects\)", R"(C:\Projects\)"));
	dcassert(isSubLocal(R"(C:\Projects\test)", R"(C:\Projects\)"));
	dcassert(isSubLocal(R"(C:\Projects\test)", R"(C:\Projects)"));
	dcassert(!isSubLocal(R"(C:\Projectstest)", R"(C:\Projects)"));
	dcassert(!isSubLocal(R"(C:\Projects)", R"(C:\Projectstest)"));
	dcassert(isSubLocal(R"(C:\Projects\test)", ""));
	dcassert(!isSubLocal("", R"(C:\Projects\test)"));

	dcassert(compareFromEndAdc(R"(Downloads\1\)", R"(/Downloads/1/)") == 0);
	dcassert(compareFromEndAdc(R"(Downloads\1\)", R"(/Download/1/)") == 10);

	dcassert(compareFromEndAdc(R"(E:\Downloads\Projects\CD1\)", R"(/CD1/)") == 0);
	dcassert(compareFromEndAdc(R"(E:\Downloads\1\)", R"(/1/)") == 0);
	dcassert(compareFromEndAdc(R"(/Downloads/Projects/CD1/)", R"(/cd1/)") == 0);
	dcassert(compareFromEndAdc(R"(/Downloads/1/)", R"(/1/)") == 0);


	// MATCH PATHS (NMDC)
	dcassert(getAdcMatchPath(R"(/SHARE/Random/CommonSub/File1.zip)", R"(E:\Downloads\Bundle\CommonSub\File1.zip)", R"(E:\Downloads\Bundle\)", true) == ADC_ROOT_STR);
	dcassert(getAdcMatchPath(R"(/SHARE/Bundle/Bundle/CommonSub/File1.zip)", R"(E:\Downloads\Bundle\CommonSub\File1.zip)", R"(E:\Downloads\Bundle\)", true) == R"(E:\Downloads\Bundle\)");

	// MATCH PATHS (ADC)

	// Different remote bundle path
	dcassert(getAdcMatchPath(R"(/SHARE/Bundle/RandomRemoteDir/File1.zip)", R"(E:\Downloads\Bundle\RandomLocalDir\File1.zip)", R"(E:\Downloads\Bundle\)", false) == R"(/SHARE/Bundle/RandomRemoteDir/)");
	dcassert(getAdcMatchPath(R"(/SHARE/RandomRemoteBundle/File1.zip)", R"(E:\Downloads\Bundle\File1.zip)", R"(E:\Downloads\Bundle\)", false) == R"(/SHARE/RandomRemoteBundle/)");

	// Common directory name for file parent
	dcassert(getAdcMatchPath(R"(/SHARE/Bundle/RandomRemoteDir/CommonSub/File1.zip)", R"(E:\Downloads\Bundle\RandomLocalDir\CommonSub\File1.zip)", R"(E:\Downloads\Bundle\)", false) == R"(/SHARE/Bundle/RandomRemoteDir/CommonSub/)");

	// Subpath is shorter than subdir in main
	dcassert(getAdcMatchPath(R"(/CommonSub/File1.zip)", R"(E:\Downloads\Bundle\RandomLocalDir\CommonSub\File1.zip)", R"(E:\Downloads\Bundle\)", false) == R"(/CommonSub/)");

	// Exact match
	dcassert(getAdcMatchPath(R"(/CommonParent/Bundle/Common/File1.zip)", R"(E:\CommonParent\Bundle\Common\File1.zip)", R"(E:\CommonParent\Bundle\)", false) == R"(/CommonParent/Bundle/)");

	// Short parent
	dcassert(getAdcMatchPath(R"(/1/File1.zip)", R"(E:\Bundle\File1.zip)", R"(E:\Bundle\)", false) == R"(/1/)");

	// Invalid path 1 (the result won't matter, just don't crash here)
	dcassert(getAdcMatchPath(R"(File1.zip)", R"(E:\Bundle\File1.zip)", R"(E:\Bundle\)", false) == R"(File1.zip)");

	// Invalid path 2 (the result won't matter, just don't crash here)
	dcassert(getAdcMatchPath(R"(/File1.zip)", R"(E:\Bundle\File1.zip)", R"(E:\Bundle\)", false) == R"(/)");
}

#endif

} // namespace dcpp