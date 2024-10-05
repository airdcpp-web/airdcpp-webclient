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
#include <airdcpp/util/Util.h>

#include <airdcpp/core/types/DirectoryContentInfo.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/util/SystemUtil.h>

#include <locale.h>

namespace dcpp {

string Util::emptyString;
wstring Util::emptyStringW;
tstring Util::emptyStringT;

string Util::addBrackets(const string& s) noexcept {
	return '<' + s + '>';
}

void Util::parseIpPort(const string& aIpPort, string& ip, string& port) noexcept {
	string::size_type i = aIpPort.rfind(':');
	if (i == string::npos) {
		ip = aIpPort;
	} else {
		ip = aIpPort.substr(0, i);
		port = aIpPort.substr(i + 1);
	}
}

string Util::formatBytes(int64_t aBytes) noexcept {
	/*if (aBytes < 0) {
		aBytes = abs(aBytes);
	} */
	char buf[64];
	if (aBytes < 1024) {
		snprintf(buf, sizeof(buf), "%d %s", (int)(aBytes&0xffffffff), CSTRING(B));
	} else if(aBytes < 1048576) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double)aBytes/(1024.0), CSTRING(KiB));
	} else if(aBytes < 1073741824) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double)aBytes/(1048576.0), CSTRING(MiB));
	} else if(aBytes < (int64_t)1099511627776) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double)aBytes/(1073741824.0), CSTRING(GiB));
	} else if(aBytes < (int64_t)1125899906842624) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double)aBytes/(1099511627776.0), CSTRING(TiB));
	} else if(aBytes < (int64_t)1152921504606846976)  {
		snprintf(buf, sizeof(buf), "%.02f %s", (double)aBytes/(1125899906842624.0), CSTRING(PiB));
	} else {
		snprintf(buf, sizeof(buf), "%.02f %s", (double)aBytes/(1152921504606846976.0), CSTRING(EiB));
	}

	return buf;
}

string Util::formatAbbreviated(int aNum) noexcept {
	char buf[64];
	if (aNum < 2000) {
		snprintf(buf, sizeof(buf), "%d", aNum);
	} else if (aNum < 1000000) {
		snprintf(buf, sizeof(buf), "%.01f%s", (double)aNum / 1000.0, "k");
	} else {
		snprintf(buf, sizeof(buf), "%.01f%s", (double)aNum / (1000000.0), "m");
	}

	return buf;
}

#ifdef _WIN32
wstring Util::formatBytesW(int64_t aBytes) noexcept {
	wchar_t buf[64];
	if(aBytes < 1024) {
		snwprintf(buf, sizeof(buf), L"%d %s", (int)(aBytes&0xffffffff), CWSTRING(B));
	} else if(aBytes < 1048576) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double)aBytes/(1024.0), CWSTRING(KiB));
	} else if(aBytes < 1073741824) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double)aBytes/(1048576.0), CWSTRING(MiB));
	} else if(aBytes < (int64_t)1099511627776) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double)aBytes/(1073741824.0), CWSTRING(GiB));
	} else if(aBytes < (int64_t)1125899906842624) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double)aBytes/(1099511627776.0), CWSTRING(TiB));
	} else if(aBytes < (int64_t)1152921504606846976)  {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double)aBytes/(1125899906842624.0), CWSTRING(PiB));
	} else {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double)aBytes/(1152921504606846976.0), CWSTRING(EiB));
	}
	return buf;
}

wstring Util::formatAbbreviatedW(int aNum) noexcept {
	wchar_t buf[64];
	if (aNum < 2000) {
		snwprintf(buf, sizeof(buf), L"%d", aNum);
	} else if (aNum < 1000000) {
		snwprintf(buf, sizeof(buf), L"%.01f%s", (double)aNum / 1000.0, L"k");
	} else {
		snwprintf(buf, sizeof(buf), L"%.01f%s", (double)aNum / (1000000.0), L"m");
	}

	return buf;
}
#endif

int64_t Util::convertSize(int64_t aValue, Util::SizeUnits valueType, Util::SizeUnits to /*B*/) noexcept {
	if (valueType > to) {
		return aValue * static_cast<int64_t>(pow(1024LL, static_cast<int64_t>(valueType - to)));
	} else if (valueType < to) {
		return aValue / static_cast<int64_t>(pow(1024LL, static_cast<int64_t>(to - valueType)));
	}
	return aValue;
}

string Util::formatConnectionSpeed(int64_t aBytes) noexcept {
	aBytes *= 8;
	char buf[64];
	if (aBytes < 1000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000.0), CSTRING(KBITS));
	} else if (aBytes < 1000000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000000.0), CSTRING(MBITS));
	} else if (aBytes < (int64_t) 1000000000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000000000.0), CSTRING(GBITS));
	} else if (aBytes < (int64_t) 1000000000000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000000000000.0), CSTRING(TBITS));
	} else if (aBytes < (int64_t) 1000000000000000000) {
		snprintf(buf, sizeof(buf), "%.02f %s", (double) aBytes / (1000000000000000.0), CSTRING(PBITS));
	}

	return buf;
}

string Util::formatPriority(Priority aPriority) noexcept {
	switch (aPriority) {
	case Priority::PAUSED_FORCE: return STRING(PAUSED_FORCED);
	case Priority::PAUSED: return STRING(PAUSED);
	case Priority::LOWEST: return STRING(LOWEST);
	case Priority::LOW: return STRING(LOW);
	case Priority::NORMAL: return STRING(NORMAL);
	case Priority::HIGH: return STRING(HIGH);
	case Priority::HIGHEST: return STRING(HIGHEST);
	default: return STRING(PAUSED);
	}
}

#ifdef _WIN32
wstring Util::formatConnectionSpeedW(int64_t aBytes) noexcept {
	wchar_t buf[64];
	aBytes *= 8;
	if (aBytes < 1000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000.0), CWSTRING(KBITS));
	} else if (aBytes < 1000000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000000.0), CWSTRING(MBITS));
	} else if (aBytes < (int64_t) 1000000000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000000000.0), CWSTRING(GBITS));
	} else if (aBytes < (int64_t) 1000000000000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000000000000.0), CWSTRING(TBITS));
	} else if (aBytes < (int64_t) 1000000000000000000) {
		snwprintf(buf, sizeof(buf), L"%.02f %s", (double) aBytes / (1000000000000000.0), CWSTRING(PBITS));
	}

	return buf;
}

wstring Util::formatExactSizeW(int64_t aBytes) noexcept {
//#ifdef _WIN32	
	wchar_t buf[64];
	wchar_t number[64];
	NUMBERFMT nf;
	snwprintf(number, sizeof(number), _T("%I64d"), aBytes);
	wchar_t Dummy[16];
	TCHAR sep[2] = _T(",");
    
	/*No need to read these values from the system because they are not
	used to format the exact size*/
	nf.NumDigits = 0;
	nf.LeadingZero = 0;
	nf.NegativeOrder = 0;
	nf.lpDecimalSep = sep;

	GetLocaleInfo( LOCALE_USER_DEFAULT, LOCALE_SGROUPING, Dummy, 16 );
	nf.Grouping = _wtoi(Dummy);
	GetLocaleInfo( LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, Dummy, 16 );
	nf.lpThousandSep = Dummy;

	GetNumberFormat(LOCALE_USER_DEFAULT, 0, number, &nf, buf, 64);
		
	snwprintf(buf, sizeof(buf), _T("%s %s"), buf, CTSTRING(B));
	return buf;
/*#else
		wchar_t buf[64];
		snwprintf(buf, sizeof(buf), L"%'lld", (long long int)aBytes);
		return tstring(buf) + TSTRING(B);
#endif*/
}
#endif

string Util::formatExactSize(int64_t aBytes) noexcept {
#ifdef _WIN32	
	TCHAR tbuf[128];
	TCHAR number[64];
	NUMBERFMT nf;
	_sntprintf(number, 64, _T("%I64d"), aBytes);
	TCHAR Dummy[16];
	TCHAR sep[2] = _T(",");

	/*No need to read these values from the system because they are not
	used to format the exact size*/
	nf.NumDigits = 0;
	nf.LeadingZero = 0;
	nf.NegativeOrder = 0;
	nf.lpDecimalSep = sep;

	GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SGROUPING, Dummy, 16);
	nf.Grouping = Util::toInt(Text::fromT(Dummy));
	GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, Dummy, 16);
	nf.lpThousandSep = Dummy;

	GetNumberFormat(LOCALE_USER_DEFAULT, 0, number, &nf, tbuf, sizeof(tbuf) / sizeof(tbuf[0]));

	char buf[128];
	_snprintf(buf, sizeof(buf), "%s %s", Text::fromT(tbuf).c_str(), CSTRING(B));
	return buf;
#else
	char buf[128];
	snprintf(buf, sizeof(buf), "%'lld %s", (long long int)aBytes, CSTRING(B));
	return string(buf);
#endif
}

typedef const uint8_t* ccp;
static wchar_t utf8ToLC(ccp& str) noexcept {
	wchar_t c = 0;
   if(str[0] & 0x80) { 
      if(str[0] & 0x40) { 
         if(str[0] & 0x20) { 
            if(str[1] == 0 || str[2] == 0 ||
				!((((unsigned char)str[1]) & ~0x3f) == 0x80) ||
					!((((unsigned char)str[2]) & ~0x3f) == 0x80))
				{
					str++;
					return 0;
				}
				c = ((wchar_t)(unsigned char)str[0] & 0xf) << 12 |
					((wchar_t)(unsigned char)str[1] & 0x3f) << 6 |
					((wchar_t)(unsigned char)str[2] & 0x3f);
				str += 3;
			} else {
				if(str[1] == 0 ||
					!((((unsigned char)str[1]) & ~0x3f) == 0x80)) 
				{
					str++;
					return 0;
				}
				c = ((wchar_t)(unsigned char)str[0] & 0x1f) << 6 |
					((wchar_t)(unsigned char)str[1] & 0x3f);
				str += 2;
			}
		} else {
			str++;
			return 0;
		}
	} else {
		c = Text::asciiToLower((char)str[0]);
		str++;
		return c;
	}

	return Text::toLower(c);
}

string Util::toString(const string& sep, const StringList& lst) noexcept {
	string ret;
	for(auto i = lst.begin(), iend = lst.end(); i != iend; ++i) {
		ret += *i;
		if(i + 1 != iend)
			ret += sep;
	}
	return ret;
}

string::size_type Util::findSubString(const string& aString, const string& aSubString, string::size_type start) noexcept {
	if(aString.length() < start)
		return (string::size_type)string::npos;

	if(aString.length() - start < aSubString.length())
		return (string::size_type)string::npos;

	if(aSubString.empty())
		return 0;

	// Hm, should start measure in characters or in bytes? bytes for now...
	const uint8_t* tx = (const uint8_t*)aString.c_str() + start;
	const uint8_t* px = (const uint8_t*)aSubString.c_str();

	const uint8_t* end = tx + aString.length() - start - aSubString.length() + 1;

	wchar_t wp = utf8ToLC(px);

	while(tx < end) {
		const uint8_t* otx = tx;
		if(wp == utf8ToLC(tx)) {
			const uint8_t* px2 = px;
			const uint8_t* tx2 = tx;

			for(;;) {
				if(*px2 == 0)
					return otx - (uint8_t*)aString.c_str();

				if(utf8ToLC(px2) != utf8ToLC(tx2))
					break;
			}
		}
	}
	return (string::size_type)string::npos;
}

wstring::size_type Util::findSubString(const wstring& aString, const wstring& aSubString, wstring::size_type pos) noexcept {
	if(aString.length() < pos)
		return static_cast<wstring::size_type>(wstring::npos);

	if(aString.length() - pos < aSubString.length())
		return static_cast<wstring::size_type>(wstring::npos);

	if(aSubString.empty())
		return 0;

	wstring::size_type j = 0;
	wstring::size_type end = aString.length() - aSubString.length() + 1;

	for(; pos < end; ++pos) {
		if(Text::toLower(aString[pos]) == Text::toLower(aSubString[j])) {
			wstring::size_type tmp = pos+1;
			bool found = true;
			for(++j; j < aSubString.length(); ++j, ++tmp) {
				if(Text::toLower(aString[tmp]) != Text::toLower(aSubString[j])) {
					j = 0;
					found = false;
					break;
				}
			}

			if(found)
				return pos;
		}
	}
	return static_cast<wstring::size_type>(wstring::npos);
}

int Util::stricmp(const char* a, const char* b) noexcept {
	while(*a) {
		wchar_t ca = 0, cb = 0;
		int na = Text::utf8ToWc(a, ca);
		int nb = Text::utf8ToWc(b, cb);
		ca = Text::toLower(ca);
		cb = Text::toLower(cb);
		if(ca != cb) {
			return (int)ca - (int)cb;
		}
		a += abs(na);
		b += abs(nb);
	}
	wchar_t ca = 0, cb = 0;
	Text::utf8ToWc(a, ca);
	Text::utf8ToWc(b, cb);

	return (int)Text::toLower(ca) - (int)Text::toLower(cb);
}

int Util::strnicmp(const char* a, const char* b, size_t n) noexcept {
	const char* end = a + n;
	while(*a && a < end) {
		wchar_t ca = 0, cb = 0;
		int na = Text::utf8ToWc(a, ca);
		int nb = Text::utf8ToWc(b, cb);
		ca = Text::toLower(ca);
		cb = Text::toLower(cb);
		if(ca != cb) {
			return (int)ca - (int)cb;
		}
		a += abs(na);
		b += abs(nb);
	}
	wchar_t ca = 0, cb = 0;
	Text::utf8ToWc(a, ca);
	Text::utf8ToWc(b, cb);
	return (a >= end) ? 0 : ((int)Text::toLower(ca) - (int)Text::toLower(cb));
}

// used to parse the boost::variant params of the formatParams function.
struct GetString : boost::static_visitor<string> {
	string operator()(const string& s) const noexcept { return s; }
	string operator()(const std::function<string ()>& f) const noexcept { return f(); }
};

/**
 * This function takes a string and a set of parameters and transforms them according to
 * a simple formatting rule, similar to strftime. In the message, every parameter should be
 * represented by %[name]. It will then be replaced by the corresponding item in 
 * the params stringmap. After that, the string is passed through strftime with the current
 * date/time and then finally written to the log file. If the parameter is not present at all,
 * it is removed from the string completely...
 */

string Util::formatParams(const string& aMsg, const ParamMap& aParams, FilterF aFilter, time_t aTime) noexcept {
	string result = aMsg;

	string::size_type i, j, k;
	i = 0;
	while (( j = result.find("%[", i)) != string::npos) {
		if( (result.size() < j + 2) || ((k = result.find(']', j + 2)) == string::npos) ) {
			break;
		}

		auto param = aParams.find(result.substr(j + 2, k - j - 2));

		if(param == aParams.end()) {
			result.erase(j, k - j + 1);
			i = j;

		} else {
			auto replacement = boost::apply_visitor(GetString(), param->second);

			// replace all % in params with %% for strftime
			replace("%", "%%", replacement);

			if(aFilter) {
				replacement = aFilter(replacement);
			}

			result.replace(j, k - j + 1, replacement);
			i = j + replacement.size();
		}
	}

	if (aTime > 0) {
		result = formatTime(result, aTime);
	}

	return result;
}

string Util::formatTime(const string &msg, const time_t t) noexcept {
	if (!msg.empty()) {
		string ret = msg;
		tm* loc = localtime(&t);
		if(!loc) {
			return Util::emptyString;
		}
#ifdef _WIN32
	#ifndef _WIN64
		// Work it around :P
		string::size_type i = 0;
		while((i = ret.find("%", i)) != string::npos) {
			if(string("aAbBcdHIjmMpSUwWxXyYzZ%").find(ret[i+1]) == string::npos) {
				ret.replace(i, 1, "%%");
			}
			i += 2;
		}
	#endif
#endif
		size_t bufsize = ret.size() + 256;
		string buf(bufsize, 0);

		errno = 0;

		buf.resize(strftime(&buf[0], bufsize-1, ret.c_str(), loc));
		
		while(buf.empty()) {
			if(errno == EINVAL)
				return Util::emptyString;

			bufsize+=64;
			buf.resize(bufsize);
			buf.resize(strftime(&buf[0], bufsize-1, ret.c_str(), loc));
		}

#ifdef _WIN32
		if(!Text::validateUtf8(buf))
#endif
		{
			buf = Text::toUtf8(buf);
		}
		return buf;
	}
	return Util::emptyString;
}

#ifdef _WIN32
string Util::formatDateTime(time_t t) noexcept {
	if (t == 0)
		return Util::emptyString;

	char buf[64];
	tm _tm;
	auto err = localtime_s(&_tm, &t);
	if (err > 0) {
		dcdebug("Failed to parse date " I64_FMT ": %s\n", t, SystemUtil::translateError(err).c_str());
		return Util::emptyString;
	}

	strftime(buf, 64, SETTING(DATE_FORMAT).c_str(), &_tm);

	return buf;
}
#else
string Util::formatDateTime(time_t t) noexcept {
	if (t == 0)
		return Util::emptyString;

	char buf[64];
	tm _tm;
	if (!localtime_r(&t, &_tm)) {
		dcdebug("Failed to parse date " I64_FMT ": %s\n", static_cast<int64_t>(t), SystemUtil::translateError(errno).c_str());
		return Util::emptyString;
	}

	strftime(buf, 64, SETTING(DATE_FORMAT).c_str(), &_tm);

	return buf;
}
#endif

string Util::formatCurrentTime() noexcept {
	char buf[64];
	time_t _tt;
	time(&_tt);
	tm* _tm = localtime(&_tt);
	if(_tm == NULL) {
		strcpy(buf, "xx:xx:xx");
	} else {
		strftime(buf, 64, "%X", _tm);
	}
	return buf;
}

string Util::getTimeStamp(time_t t) noexcept {
	char buf[255];
	tm* _tm = localtime(&t);
	if (_tm == NULL) {
		strcpy(buf, "xx:xx");
	}
	else {
		strftime(buf, 254, SETTING(TIME_STAMPS_FORMAT).c_str(), _tm);
	}
	return Text::toUtf8(buf);
}

#ifdef _WIN32
wstring Util::formatSecondsW(int64_t aSec, bool supressHours /*false*/) noexcept {
	wchar_t buf[64];
	if (!supressHours)
		snwprintf(buf, sizeof(buf), L"%01lu:%02d:%02d", (unsigned long)(aSec / (60*60)), (int)((aSec / 60) % 60), (int)(aSec % 60));
	else
		snwprintf(buf, sizeof(buf), L"%02d:%02d", (int)(aSec / 60), (int)(aSec % 60));	
	return buf;
}
#endif

string Util::formatSeconds(int64_t aSec, bool supressHours /*false*/) noexcept {
	char buf[64];
	if (!supressHours)
		snprintf(buf, sizeof(buf), "%01lu:%02d:%02d", (unsigned long)(aSec / (60*60)), (int)((aSec / 60) % 60), (int)(aSec % 60));
	else
		snprintf(buf, sizeof(buf), "%02d:%02d", (int)(aSec / 60), (int)(aSec % 60));	
	return buf;
}

string Util::formatDuration(uint64_t aSec, bool aTranslate, bool aPerMinute) noexcept {
	string formatedTime;

	decltype(aSec) n, added = 0;

	auto appendTime = [&] (const string& aTranslatedS, const string& aEnglishS, const string& aTranslatedP, const string& aEnglishP) -> void {
		if (aPerMinute && added == 2) //add max 2 values
			return;

		char buf[128];
		if(n >= 2) {
			snprintf(buf, sizeof(buf), (U64_FMT " " + ((aTranslate ? Text::toLower(aTranslatedP) : aEnglishP) + " ")).c_str(), n);
		} else {
			snprintf(buf, sizeof(buf), (U64_FMT " " + ((aTranslate ? Text::toLower(aTranslatedS) : aEnglishS) + " ")).c_str(), n);
		}
		formatedTime += (string)buf;
		added++;
	};

	n = aSec / (24*3600*365);
	aSec %= (24*3600*365);
	if(n) {
		appendTime(STRING(YEAR), "year", STRING(YEARS), "years");
	}

	n = aSec / (24*3600*30);
	aSec %= (24*3600*30);
	if(n) {
		appendTime(STRING(MONTH), "month", STRING(MONTHS), "months");
	}

	n = aSec / (24*3600*7);
	aSec %= (24*3600*7);
	if(n) {
		appendTime(STRING(WEEK), "week", STRING(WEEKS), "weeks");
	}

	n = aSec / (24*3600);
	aSec %= (24*3600);
	if(n) {
		appendTime(STRING(DAY), "day", STRING(DAYS), "days");
	}

	n = aSec / (3600);
	aSec %= (3600);
	if(n) {
		appendTime(STRING(HOUR), "hour", STRING(HOURS), "hours");
	}

	n = aSec / (60);
	aSec %= (60);
	if(n || aPerMinute) {
		appendTime(STRING(MINUTE), "min", STRING(MINUTES_LOWER), "min");
	}

	n = aSec;
	if(++added <= 3 && !aPerMinute) {
		appendTime(STRING(SECOND), "sec", STRING(SECONDS_LOWER), "sec");
	}

	return (!formatedTime.empty() ? formatedTime.erase(formatedTime.size()-1) : formatedTime);
}


string Util::truncate(const string& aStr, int aMaxLength) noexcept {
	return aStr.size() > static_cast<size_t>(aMaxLength) ? (aStr.substr(0, aMaxLength) + "...") : aStr;
}

string Util::formatDirectoryContent(const DirectoryContentInfo& aContentInfo) noexcept {
	if (!aContentInfo.isInitialized()) {
		return Util::emptyString;
	}

	string name;

	bool hasFiles = aContentInfo.files > 0;
	bool hasFolders = aContentInfo.directories > 0;

	if (hasFolders) {
		if (aContentInfo.directories == 1) {
			name += Util::toString(aContentInfo.directories) + " " + Text::toLower(STRING(FOLDER));
		} else {
			name += STRING_F(X_FOLDERS, Util::formatAbbreviated(aContentInfo.directories));
		}
	}

	if (hasFiles || !hasFolders) { // We must return something even if the directory is empty
		if (hasFolders)
			name += ", ";

		if (aContentInfo.files == 1) {
			name += Util::toString(aContentInfo.files) + " " + Text::toLower(STRING(FILE));
		} else {
			name += STRING_F(X_FILES, Util::formatAbbreviated(aContentInfo.files));
		}
	}

	return name;
}

string Util::formatFileType(const string& aPath) noexcept {
	auto type = PathUtil::getFileExt(aPath);
	if (type.size() > 0 && type[0] == '.') {
		type.erase(0, 1);
	}

	return type;
}

#define MIN_REMOTE_FILE_ITEM_DATE 946684800 // 1/1/2000

time_t Util::parseRemoteFileItemDate(const string& aString) noexcept {
	auto date = static_cast<time_t>(toInt64(aString)); // handle negative values too

	// Avoid using really old dates as those are most likely invalid and 
	// would confuse the client/user (e.g. with grouped search results)
	return date <= MIN_REMOTE_FILE_ITEM_DATE ? 0 : date;
}

/* natural sorting */
int Util::DefaultSort(const wchar_t *a, const wchar_t *b) noexcept {
	int v1, v2;
	while(*a != 0 && *b != 0) {
		v1 = 0; v2 = 0;
		auto t1 = iswdigit(*a);
		auto t2 = iswdigit(*b);
		if(t1 != t2) return (t1) ? -1 : 1;

		if(!t1) {
			if(Text::toLower(*a) != Text::toLower(*b))
				return ((int)Text::toLower(*a)) - ((int)Text::toLower(*b));
			a++; b++;
		} else {
			while(iswdigit(*a)) {
			    v1 *= 10;
			    v1 += *a - '0';
			    a++;
			}

	        while(iswdigit(*b)) {
		        v2 *= 10;
		        v2 += *b - '0';
		        b++;
		    }

			if(v1 != v2)
				return (v1 < v2) ? -1 : 1;
		}			
	}

	return (int)Text::toLower(*a) - (int)Text::toLower(*b);
}

int Util::DefaultSort(const char* a, const char* b) noexcept {
	int v1, v2;
	while (*a != 0 && *b != 0) {
		v1 = 0; v2 = 0;

		wchar_t ca = 0, cb = 0;
		int na = abs(Text::utf8ToWc(a, ca));
		int nb = abs(Text::utf8ToWc(b, cb));

		auto t1 = iswdigit(ca);
		auto t2 = iswdigit(cb);
		if (t1 != t2) return (t1) ? -1 : 1;

		if (!t1) {
			auto caLower = Text::toLower(ca);
			auto cbLower = Text::toLower(cb);
			if (caLower != cbLower)
				return ((int)caLower) - ((int)cbLower);

			a += na; b += nb;
		} else {
			while (iswdigit(ca)) {
				v1 *= 10;
				v1 += *a - '0';

				a += na;
				na = Text::utf8ToWc(a, ca);
			}

			while (iswdigit(cb)) {
				v2 *= 10;
				v2 += *b - '0';

				b += nb;
				nb = Text::utf8ToWc(b, cb);
			}

			if (v1 != v2)
				return (v1 < v2) ? -1 : 1;
		}
	}

	return Util::stricmp(a, b);
}

void Util::replace(string& aString, const string& findStr, const string& replaceStr) noexcept {
   string::size_type offset = 0;
   while((offset = aString.find(findStr, offset)) != string::npos) {
      aString.replace(offset, findStr.length(), replaceStr);
      offset += replaceStr.length();
   }
}

tstring Util::replaceT(const tstring& aString, const tstring& fStr, const tstring& rStr) noexcept {
	tstring tmp = aString;
	tstring::size_type pos = 0;
	while( (pos = tmp.find(fStr, pos)) != tstring::npos ) {
		tmp.replace(pos, fStr.length(), rStr);
		pos += rStr.length() - fStr.length();
	}

	return tmp;
}
	
//password protect

static const string base64_chars = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";


static inline bool is_base64(unsigned char c) noexcept {
    return (isalnum(c) || (c == '+') || (c == '/'));
}
string Util::base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) noexcept {
	string ret;
	int i = 0;
	int j = 0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];

	while(in_len--) {
		char_array_3[i++] = *(bytes_to_encode++);
		if(i == 3) {
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;
			for(i = 0; (i <4) ; i++)
				ret += base64_chars[char_array_4[i]];

			i = 0;
		}
	}

	if(i) {
		for(j = i; j < 3; j++)
			char_array_3[j] = '\0';

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;

		for (j = 0; (j < i + 1); j++)
			ret += base64_chars[char_array_4[j]];

		while((i++ < 3))
			ret += '=';

    }

    return ret;
}

string Util::base64_decode(string const& encoded_string) noexcept {
	int in_len = encoded_string.size();
	int i = 0;
	int j = 0;
	int in_ = 0;
	unsigned char char_array_4[4], char_array_3[3];
	string ret;

	while(in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
		char_array_4[i++] = encoded_string[in_]; in_++;
		if(i ==4) {
			for (i = 0; i <4; i++)
				char_array_4[i] = static_cast<unsigned char>(base64_chars.find(char_array_4[i]));

			char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

			for(i = 0; (i < 3); i++)
				ret += char_array_3[i];

			i = 0;
		}
	}

	if(i) {
		for(j = i; j <4; j++)
			char_array_4[j] = 0;

		for(j = 0; j <4; j++)
			char_array_4[j] = static_cast<unsigned char>(base64_chars.find(char_array_4[j]));

		char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
		char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
		char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

		for (j = 0; (j < i - 1); j++)
			ret += char_array_3[j];
	}

	return ret;
}

bool Util::isChatCommand(const string& aText) noexcept {
	return !aText.empty() && aText.front() == '/';
}

} // namespace dcpp