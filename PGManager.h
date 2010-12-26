/*
 * PGManager.h
 *
 * Remarks:
 *	svn230: Initial revision with simple .p2p list loading and handling
 *	svn231: Added support for binary formats (libp2p) as well as replaced
 *			my loading functions with the ones from libp2p (modified)
 *	svn279: Changed from vector to list, for speed, removed bad memory leaks
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

#ifndef PGMANAGER_H
#define PGMANAGER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Singleton.h"
#include "SettingsManager.h"
#include "ResourceManager.h"
#include "UserConnection.h"
#include <fstream>

namespace dcpp {
class PGManager: public Singleton<PGManager>
{
public:
	PGManager() { }
	~PGManager() throw() { clearEntries(); }

	enum {
		FILE_AUTO,
		FILE_P2P,
		FILE_P2B,
		INCONN,
		OUTCONN
	};

	struct ip {
		union {
			unsigned int iIP;
			unsigned char bIP[4];
		};

		ip() : iIP(0) { };
		ip(unsigned int iIP) : iIP(iIP) { };
		bool operator < (const ip &ip) const { return this->iIP < ip.iIP; }
		bool operator > (const ip &ip) const { return this->iIP > ip.iIP; }
		bool operator <= (const ip &ip) const { return this->iIP <= ip.iIP; }
		bool operator >= (const ip &ip) const { return this->iIP >= ip.iIP; }
		bool operator == (const ip &ip) const { return this->iIP == ip.iIP; }
		bool operator != (const ip &ip) const { return this->iIP != ip.iIP; }

		ip operator + (unsigned int i) const { return this->iIP + i; }
		ip operator - (unsigned int i) const { return this->iIP - i; }
	};

	struct range {
		typedef range* ptr;
		typedef list<ptr> list;
		typedef list::const_iterator iter;

		ip start, end;
		tstring name;

		range() { };
		range(const tstring &name) : name(name) { };
		range(const tstring &name, const ip &start, const ip &end) : name(name), start(start), end(end) { };

		bool operator < (const range &range) const {
			return (this->start < range.start) | (this->start == range.start && this->end < range.end);
		}
		bool operator > (const range &range) const {
			return (this->start > range.start) | (this->start == range.start && this->end > range.end);
		}
		bool operator == (const range &range) const {
			return (this->start >= range.start && this->end <= range.end);
		}
	};

	tstring getIPBlock(const tstring& aIP);
	tstring getTotalIPRangesTStr() const;
	tstring getStatusTStr() const;
	void updateBlockList(bool clean = true);
	void log(const UserConnectionPtr& aSource, const string& aCompany, int aType);
	void log(const string& msg);

	bool notAbused() {
		if((getIPBlockBool("1.1.1.1") && getIPBlockBool("255.255.255.255")) && (getIPBlock("1.1.1.1") == getIPBlock("255.255.255.255"))) {
			SettingsManager::getInstance()->set(SettingsManager::PG_ENABLE, false);
			return false;
		} else
			return true;
	}

	string getIPStr(const ip &aIP) const { 
		return Util::toString(aIP.bIP[3]) + "." + Util::toString(aIP.bIP[2]) + "." + Util::toString(aIP.bIP[1]) + "." + Util::toString(aIP.bIP[0]); 
	};
	UserPtr replyTo;
	tstring getIPTStr(const ip &aIP) const { return Text::toT(getIPStr(aIP)); };
	string getIPBlock(const string& aIP) { return Text::fromT(getIPBlock(Text::toT(aIP))); };
	string getTotalIPRangesStr() const { return Text::fromT(getTotalIPRangesTStr()); };
	string getStatusStr() const { return Text::fromT(getStatusTStr()); };
	bool getIPBlockBool(const string& aIP) { return getIPBlock(Text::toT(aIP)).empty() ? false : true; };
	void load() { updateBlockList(false); };
	size_t getTotalIPRanges() { return ranges.size(); };
	const range::list* getRanges() const { return &ranges; };

private:
	range::list ranges;
	mutable CriticalSection cs;

	void load_list(const string& file, int type = FILE_AUTO);
	void load_p2b(istream &stream);
	void load_p2p(istream &stream);
	static int get_file_type(istream &stream);
	static range::iter range_search(range::iter itr1, range::iter itr2, const ip &addr);

	void clearEntries() {
		for(range::iter i = ranges.begin(); i != ranges.end(); ++i) {
			delete *i;
		}
		ranges.clear();
	}

	/*
	 * ipSort: used with plain sort()
	 * ipSearcher: used with lower_bound(), see range_search.
	 */
	struct ipSort {
		bool operator() (range *left, const range *right) const {
			return (left->start < right->start) | (left->start == right->start && left->end < right->end);
		}
	};

	struct ipSearcher {
		bool operator() (range *rng, const ip &addr) const {
			return (rng->end < addr);
		}
	};

	/*
	 * Everything below is used for optimizing lists
	 * simply call optimize()
	 */
	class merge_pred {
	private:
		bool aggressive;

		inline static bool is_adjacent(const range *left, const range *right) {
			return (left->end == (right->start - 1));
		}

		inline static bool is_semiadjacent(const range *left, const range *right) {
			return (left->end.bIP[0] == 255 && left->end == (right->start - 2));
		}

		inline static bool is_between(const ip &low, const ip &value, const ip &high) {
			return (low <= value && value <= high);
		}

	public:
		merge_pred(bool a) : aggressive(a) { };

		bool operator() (range *left, const range *right) const {
			if(is_between(left->start, right->start, left->end) || ((is_adjacent(left, right) || is_semiadjacent(left, right)) && (this->aggressive || left->name == right->name))) {
				left->start = min(left->start, right->start);
				left->end = max(left->end, right->end);
				left->name += _T("; ") + right->name;
				delete right;
				return true;
			} else return false;
		}
	};

	void optimize(bool aggressive = false) {
		ranges.sort(ipSort());
		ranges.unique(merge_pred(aggressive));
	}

};
}

#endif // PGMANAGER_H