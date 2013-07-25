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

#ifndef DCPLUSPLUS_DCPP_SEARCHRESULT_H
#define DCPLUSPLUS_DCPP_SEARCHRESULT_H

#include "AdcCommand.h"
#include "GetSet.h"
#include "forward.h"
#include "FastAlloc.h"
#include "HintedUser.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "Util.h"

#include <boost/noncopyable.hpp>

namespace dcpp {

class SearchResult : public FastAlloc<SearchResult>, public intrusive_ptr_base<SearchResult> {
public:	
	enum Types {
		TYPE_FILE,
		TYPE_DIRECTORY
	};

	//outgoing result (direct)
	SearchResult(const string& name);

	//outgoing result (normal)
	SearchResult(Types aType, int64_t aSize, const string& name, const TTHValue& aTTH, time_t aDate, int fileCount=0, int dirCount=0);

	//incoming results
	SearchResult(const HintedUser& aUser, Types aType, uint8_t aSlots, uint8_t aFreeSlots, 
		int64_t aSize, const string& aFilePath, const string& ip, TTHValue aTTH, const string& aToken, time_t aDate, const string& connection, int fileCount, int dirCount);

	string getFileName() const;
	string toSR(const Client& client) const;
	AdcCommand toRES(char type) const;

	HintedUser& getUser() { return user; }
	string getSlotString() const;

	string getFilePath() const;
	const string& getPath() const { return path; }
	int64_t getSize() const { return size; }
	Types getType() const { return type; }
	size_t getSlots() const { return slots; }
	size_t getFreeSlots() const { return freeSlots; }
	int getFileCount() const { return files; }
	const TTHValue& getTTH() const { return tth; }
	
	string getConnectionStr() const;
	int64_t getConnectionInt() const;
	int64_t getSpeedPerSlot() const;

	const string& getIP() const { return IP; }
	const string& getToken() const { return token; }
	time_t getDate() const { return date; }
	const CID& getCID() const { return user.user->getCID(); }
	bool isNMDC() const { return user.user->isNMDC(); }

	static void pickResults(SearchResultList& aResults, int pickedNum);
	struct SpeedSortOrder {
		bool operator()(const SearchResultPtr& left, const SearchResultPtr& right) const;
	};
private:
	friend class SearchManager;

	SearchResult();

	TTHValue tth;
	
	string path;
	string IP;
	string token;
	
	int64_t size;
	
	size_t slots;
	size_t freeSlots;

	int folders;
	int files;
	
	HintedUser user;
	Types type;

	time_t date;
	string connection;
};

}

#endif
