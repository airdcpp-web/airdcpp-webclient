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

#ifndef DCPLUSPLUS_DCPP_DOWNLOAD_H_
#define DCPLUSPLUS_DCPP_DOWNLOAD_H_

#include <string>
#include <memory>

#include "forward.h"

#include "Flags.h"
#include "GetSet.h"
#include "MerkleTree.h"
#include "TimerManager.h"
#include "Transfer.h"

namespace dcpp {

using std::string;
using std::unique_ptr;

/**
 * Comes as an argument in the DownloadManagerListener functions.
 * Use it to retrieve information about the ongoing transfer.
 */
class Download : public Transfer, public Flags {
public:

	enum {
		FLAG_ZDOWNLOAD			= 0x01,
		FLAG_CHUNKED			= 0x02,
		FLAG_TTH_CHECK			= 0x04,
		FLAG_SLOWUSER			= 0x08,
		FLAG_XML_BZ_LIST		= 0x10,
		FLAG_PARTIAL			= 0x40,
		FLAG_OVERLAP			= 0x80,
		FLAG_VIEW				= 0x100,
		FLAG_RECURSIVE			= 0x200,
		FLAG_QUEUE				= 0x400,
		FLAG_TTHLIST            = 0x800,
		FLAG_TTHLIST_BUNDLE		= 0x1000,
		FLAG_HIGHEST_PRIO		= 0x2000
	};

	bool operator==(const Download* d) const;

	Download(UserConnection& conn, QueueItem& qi) noexcept;

	void getParams(const UserConnection& aSource, ParamMap& params) const noexcept;

	~Download();

	/** @return Target filename without path. */
	string getTargetFileName() const noexcept;

	/** Open the target output for writing */
	void open(int64_t bytes, bool z, bool hasDownloadedBytes);

	/** Release the target output */
	void close();

	/** @internal */
	TigerTree& getTigerTree() { return tt; }
	const string& getPFS() const { return pfs; }

	/** @internal */
	AdcCommand getCommand(bool zlib, const string& mySID) const noexcept;
	const unique_ptr<OutputStream>& getOutput() const { return output; }

	GETSET(string, tempTarget, TempTarget);
	GETSET(string, listDirectoryPath, ListDirectoryPath);

	IGETSET(uint64_t, lastTick, LastTick, GET_TICK());
	IGETSET(bool, treeValid, TreeValid, false);
	IGETSET(BundlePtr, bundle, Bundle, nullptr);

	string getBundleStringToken() const noexcept;

	void appendFlags(OrderedStringSet& flags_) const noexcept;
private:
	Download(const Download&);
	Download& operator=(const Download&) = delete;

	const string& getDownloadTarget() const noexcept;

	unique_ptr<OutputStream> output;
	TigerTree tt;
	string pfs;
};

} // namespace dcpp

#endif /*DOWNLOAD_H_*/
