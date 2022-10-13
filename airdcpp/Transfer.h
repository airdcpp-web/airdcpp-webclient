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

#ifndef DCPLUSPLUS_DCPP_TRANSFER_H_
#define DCPLUSPLUS_DCPP_TRANSFER_H_

#include "forward.h"

#include "CriticalSection.h"
#include "GetSet.h"
#include "MerkleTree.h"
#include "Segment.h"

namespace dcpp {

class Transfer : private boost::noncopyable {
public:
	enum Type {
		TYPE_FILE,
		TYPE_FULL_LIST,
		TYPE_PARTIAL_LIST,
		TYPE_TREE,
		TYPE_LAST
	};
	
	static const string names[TYPE_LAST];

	static const string USER_LIST_NAME;
	static const string USER_LIST_NAME_BZ;

	Transfer(UserConnection& conn, const string& path, const TTHValue& tth);
	virtual ~Transfer() { };

	int64_t getPos() const noexcept { return pos; }

	int64_t getStartPos() const noexcept { return getSegment().getStart(); }
	
	void resetPos() noexcept;
	void addPos(int64_t aBytes, int64_t aActual) noexcept;

	enum { MIN_SAMPLES = 15, MIN_SECS = 15 };
	
	/** Record a sample for average calculation */
	void tick() noexcept;

	int64_t getActual() const noexcept { return actual; }

	int64_t getSegmentSize() const noexcept { return getSegment().getSize(); }
	void setSegmentSize(int64_t size) noexcept { segment.setSize(size); }

	bool getOverlapped() const noexcept { return getSegment().getOverlapped(); }
	void setOverlapped(bool overlap) noexcept { segment.setOverlapped(overlap); }

	int64_t getAverageSpeed() const noexcept;

	int64_t getSecondsLeft(bool wholeFile = false) const noexcept;

	void getParams(const UserConnection& aSource, ParamMap& params) const noexcept;

	UserPtr getUser() noexcept;
	const UserPtr getUser() const noexcept;
	HintedUser getHintedUser() const noexcept;
	
	//const string& getPath() const { return path; }
	const TTHValue& getTTH() const noexcept { return tth; }

	UserConnection& getUserConnection() noexcept { return userConnection; }
	const UserConnection& getUserConnection() const noexcept { return userConnection; }
	const string& getToken() const noexcept;

	GETSET(string, path, Path);
	GETSET(Segment, segment, Segment);
	IGETSET(Type, type, Type, TYPE_FILE);
	IGETSET(uint64_t, start, Start, 0);

	virtual void appendFlags(OrderedStringSet& flags_) const noexcept;

	bool isFilelist() const noexcept;
private:
	typedef std::pair<uint64_t, int64_t> Sample;
	typedef deque<Sample> SampleList;
	
	SampleList samples;
	mutable SharedMutex cs;
	
	/** The file being transferred */
	//string path;
	/** TTH of the file being transferred */
	TTHValue tth;
	/** Bytes transferred over socket */
	int64_t actual = 0;
	/** Bytes transferred to/from file */
	int64_t pos = 0;

	UserConnection& userConnection;
};

} // namespace dcpp

#endif /*TRANSFER_H_*/
