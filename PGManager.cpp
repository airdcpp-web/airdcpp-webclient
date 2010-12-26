/*
 * PGManager.cpp
 *
 * Remarks:
 *	svn230: Initial revision with simple .p2p liat loading and handling
 *	svn231: Added support for binary formats (libp2p) as well as replaced
 *			my loading functions with the ones from libp2p (modified)
 *
 * libp2p info for the parts taken from it:
 *	Copyright (C) 2004-2005 Cory Nelson
 *
 *	This software is provided 'as-is', without any express or implied
 *	warranty.  In no event will the authors be held liable for any damages
 *	arising from the use of this software.
 *
 *	Permission is granted to anyone to use this software for any purpose,
 *	including commercial applications, and to alter it and redistribute it
 *	freely, subject to the following restrictions:
 *
 *	1. The origin of this software must not be misrepresented; you must not
 *	   claim that you wrote the original software. If you use this software
 *	   in a product, an acknowledgment in the product documentation would be
 *	   appreciated but is not required.
 *	2. Altered source versions must be plainly marked as such, and must not be
 *	   misrepresented as being the original software.
 *	3. This notice may not be removed or altered from any source distribution.
 */

#include "stdinc.h"
#include "DCPlusPlus.h"

#include "PGManager.h"
#include "File.h"
#include "StringTokenizer.h"
#include "ClientManager.h"
#include "LogManager.h"

namespace dcpp {
inline static unsigned int make_ip(unsigned int a, unsigned int b, unsigned int c, unsigned int d) {
	return ((a << 24) | (b << 16) | (c << 8) | d);
}

void PGManager::updateBlockList(bool clean /*= true*/) {
	if(BOOLSETTING(PG_ENABLE)){
	Lock l(cs);
	if(clean) clearEntries();

	try {
		load_list(SETTING(PG_FILE));
		optimize(true);
	} catch(const FileException& e) {
		if(SETTING(PG_LOG)) {
			log("Loading Error: " + e.getError());
		}
	}
}
}
void PGManager::load_list(const string& file, int type /*= FILE_AUTO*/) {
	ifstream stream(file.c_str(), ifstream::binary);
	if(!stream.is_open()) throw FileException("unable to open file");
	if(type == FILE_AUTO) type = get_file_type(stream);

	switch(type) {
		case FILE_P2P:
			load_p2p(stream);
			break;
		case FILE_P2B:
			load_p2b(stream);
			break;
		default:
			throw FileException("format not supported");
	}
}

int PGManager::get_file_type(istream &stream) {
	char buf[6];
	stream.read(buf, 6);
		
	stream.putback(buf[5]);
	stream.putback(buf[4]);
	stream.putback(buf[3]);
	stream.putback(buf[2]);
	stream.putback(buf[1]);
	stream.putback(buf[0]);

	if(memcmp(buf, "\xFF\xFF\xFF\xFFP2B", 6) == 0) 
		return FILE_P2B;
	else 
		return FILE_P2P;
}

void PGManager::load_p2b(istream &stream) {
	char buf[7];
	unsigned char version;
	if(!stream.read(buf, sizeof(buf)) || memcmp(buf, "\xFF\xFF\xFF\xFFP2B", 7) || !stream.read((char*)&version, sizeof(version))) 
		throw FileException("invalid p2b stream");

	if(version == 1 || version == 2) {
		unsigned int start, end;
			
		string name;
		while(getline(stream, name, '\0')) {
			if(!stream.read((char*)&start, sizeof(start)) || !stream.read((char*)&end, sizeof(end)))
				throw FileException("invalid p2b stream");

			while(name[0] == ' ')
				name.erase(0, 1);
			while(name[name.length() - 1] == ' ')
				name.erase(name.length() - 1, 1);

			range *r = new range();

			if(version == 1) { 
				// P2B v1 is expected to be ISO-8859-1 encoded.
				r->name = Text::acpToWide(name);
			} else if(version == 2) {
				r->name = Text::toT(name); 
			}

			start = ntohl(start);
			end = ntohl(end);

			r->start = min(start, end);
			r->end = max(start, end);

			ranges.push_back(r);
		}
	} else if(version == 3) {
		unsigned int namecount;
		if(!stream.read((char*)&namecount, sizeof(namecount))) 
			throw FileException("invalid p2b stream: name count expected");
		namecount = ntohl(namecount);

		TStringList names;
		names.reserve(namecount);

		for(unsigned int i = 0; i < namecount; i++) {
			string name;
			if(!getline(stream, name, '\0')) 
				throw FileException("invalid p2b stream: name expected");

			names.push_back(Text::toT(name));
		}

		unsigned int rangecount;
		if(!stream.read((char*)&rangecount, sizeof(rangecount))) 
			throw FileException("invalid p2b stream: range count expected");

		rangecount = ntohl(rangecount);

		unsigned int name, start, end;

		for(unsigned int i = 0; i < rangecount; i++) {
			if(!stream.read((char*)&name, sizeof(name)) || !stream.read((char*)&start, sizeof(start)) || !stream.read((char*)&end, sizeof(end)))
				throw FileException("invalid p2b stream: range expected");

			name = ntohl(name);
			start = ntohl(start);
			end = ntohl(end);

			range *r = new range(names.at(name), min(start, end), max(start, end));

			while(r->name[0] == ' ')
				r->name.erase(0, 1);
			while(r->name[r->name.length() - 1] == ' ')
				r->name.erase(r->name.length() - 1, 1);

			ranges.push_back(r);
		}
	} else throw FileException("unknown p2b version");
}

void PGManager::load_p2p(istream &stream) {
	string line;
	while(getline(stream, line)) {
		string::size_type i = line.rfind(':');
		if(i == string::npos)
				continue;

		string name(line.c_str(), i);
		line.erase(0, i+1);
			
		unsigned int sa, sb, sc, sd;
		unsigned int ea, eb, ec, ed;

		if(_stscanf(Text::toT(line).c_str(), _T("%u.%u.%u.%u-%u.%u.%u.%u"),
			&sa, &sb, &sc, &sd, &ea, &eb, &ec, &ed) != 8 ||
			sa > 255 || sb > 255 || sc > 255 || sd > 255 ||
			ea > 255 || eb > 255 || ec > 255 || ed > 255) continue;

		while(name[0] == ' ')
			name.erase(0, 1);
		while(name[name.length() - 1] == ' ')
			name.erase(name.length() - 1, 1);

		range *r = new range();

		// P2P format is expected to be ISO-8859-1 encoded.
		r->name = Text::acpToWide(name);

		unsigned int start = make_ip(sa, sb, sc, sd);
		unsigned int end = make_ip(ea, eb, ec, ed);

		r->start = min(start, end);
		r->end = max(start, end);

		ranges.push_back(r);
	}
}

tstring PGManager::getIPBlock(const tstring& aIP) {
	Lock l(cs);
	if(aIP.empty() || ranges.size() == 0)
		return Util::emptyStringT;

	unsigned int a, b, c, d, iIP;
	if(_stscanf(aIP.c_str(), _T("%u.%u.%u.%u"), &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255) 
		return Util::emptyStringT;

	iIP = make_ip(a, b, c, d);

	if(iIP == 0)
		return Util::emptyStringT;

	// Does this work for all occasions? 
	range::iter find;
	if((find = range_search(ranges.begin(), ranges.end(), ip(iIP))) != ranges.end()) {
		return (*find)->name;
   }

	return Util::emptyStringT;
}

PGManager::range::iter PGManager::range_search(range::iter itr1, range::iter itr2, const ip &addr) {
	range::iter find_itr = lower_bound(itr1, itr2, addr, ipSearcher());
	if(find_itr != itr2 && !(addr < (*find_itr)->start))
		return find_itr;
	else
		return itr2;
}

tstring PGManager::getTotalIPRangesTStr() const {
#ifdef _WIN32
	wchar_t buf[256];
	wchar_t number[256];
	NUMBERFMT nf;
	snwprintf(number, sizeof(number), _T("%d"), ranges.size());
	wchar_t Dummy[16];
	TCHAR sep[2] = _T(",");
    
	nf.NumDigits = 0;
	nf.LeadingZero = 0;
	nf.NegativeOrder = 0;
	nf.lpDecimalSep = sep;

	GetLocaleInfo( LOCALE_USER_DEFAULT, LOCALE_SGROUPING, Dummy, 16);
	nf.Grouping = _wtoi(Dummy);
	GetLocaleInfo( LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, Dummy, 16);
	nf.lpThousandSep = Dummy;

	GetNumberFormat(LOCALE_USER_DEFAULT, 0, number, &nf, buf, 256);
	return buf;
#else
	wchar_t buf[64];
	snwprintf(buf, sizeof(buf), L"%'lld", (long long int)ranges.size());
	return tstring(buf);
#endif
}

tstring PGManager::getStatusTStr() const {
	if(!(FindWindow(NULL, _T("PeerGuardian")) || FindWindow(NULL, _T("PeerGuardian 2")) || FindWindow(NULL, _T("ProtoWall")))) {
		if(BOOLSETTING(PG_ENABLE)) {
			if(ranges.size() != 0 && (BOOLSETTING(PG_UP) || BOOLSETTING(PG_DOWN) || BOOLSETTING(PG_SEARCH))) {
				TCHAR buf[512];
				_stprintf(buf, CTSTRING(PG_RUNNING), getTotalIPRangesStr().c_str());
				return buf;
			} else {
				return TSTRING(PG_RUNNING_IDLE);
			}
		} else {
			return TSTRING(PG_NOT_RUNNING);
		}
	} else {
		return TSTRING(PG_ALT_SOFT);
	}
}

void PGManager::log(const UserConnectionPtr& aSource, const string& aCompany, int aType) {

	
	StringMap params;
	params["userNI"] = ClientManager::getInstance()->getNicks(replyTo->getCID())[0];
	params["company"] = aCompany;
	params["type"] = (aType == INCONN) ? STRING(INCOMING) : STRING(OUTGOING);
	params["userI4"] = aSource->getRemoteIp();
	StringList hubNames = ClientManager::getInstance()->getHubNames(aSource->getUser()->getCID());
	if(hubNames.empty())
		hubNames.push_back(STRING(OFFLINE));
	params["hubNI"] = Util::toString(hubNames);
	StringList hubs = ClientManager::getInstance()->getHubs(aSource->getUser()->getCID());
	if(hubs.empty())
		hubs.push_back(STRING(OFFLINE));
	params["hubURL"] = Util::toString(hubs);

	LOG(LogManager::PGUARDIAN, params);
}

void PGManager::log(const string& msg) throw() {
	Lock l(cs);
	try {
		string area = Util::validateFileName(SETTING(LOG_DIRECTORY) + SETTING(PG_LOG_FILE));
		File::ensureDirectory(area);
		File f(area, File::WRITE, File::OPEN | File::CREATE);
		f.setEndPos(0);
		if(f.getPos() == 0) {
			f.write("\xef\xbb\xbf");
		}
		f.write('[' + Util::formatTime("%Y-%m-%d %H:%M", GET_TIME()) + "] " + msg + "\r\n");
	} catch (const FileException&) {
		// ...
	}
}
}