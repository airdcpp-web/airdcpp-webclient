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

#ifndef DCPLUSPLUS_DCPP_SEGMENT_H_
#define DCPLUSPLUS_DCPP_SEGMENT_H_

namespace dcpp {

// minimum file size to be PFS : 20M
#define PARTIAL_SHARE_MIN_SIZE 20971520

class Segment {
public:
	Segment() { }
	Segment(int64_t start_, int64_t size_, bool overlapped_ = false) : start(start_), size(size_), overlapped(overlapped_) { }
	
	int64_t getStart() const noexcept { return start; }
	int64_t getSize() const noexcept { return size; }
	int64_t getEnd() const noexcept { return getStart() + getSize(); }
	
	void setSize(int64_t size_) noexcept { size = size_; }
	
	bool overlaps(const Segment& rhs) const noexcept {
		int64_t end = getEnd();
		int64_t rend = rhs.getEnd();
		return getStart() < rend && rhs.getStart() < end;
	}
	
	void trim(const Segment& rhs) noexcept {
		if(!overlaps(rhs)) {
			return;
		}
		
		if(rhs.getStart() < start) {
			int64_t rend = rhs.getEnd();
			if(rend > getEnd()) {
				start = size = 0;
			} else {
				size -= rend - start;
				start = rend;
			}
			return;
		}

		size = rhs.getStart() - start;
	}

	bool inSet(const set<Segment>& segmentSet) const noexcept {
		return find_if(segmentSet.begin(), segmentSet.end(), [&](const Segment& s) {
			return s.contains(*this);
		}) != segmentSet.end();
	}
	
	bool contains(const Segment& rhs) const noexcept {
		return getStart() <= rhs.getStart() && getEnd() >= rhs.getEnd();
	}

	bool operator==(const Segment& rhs) const noexcept {
		return getStart() == rhs.getStart() && getSize() == rhs.getSize();
	}
	bool operator<(const Segment& rhs) const noexcept {
		return (getStart() < rhs.getStart()) || (getStart() == rhs.getStart() && getSize() < rhs.getSize());
	}
private:	
	int64_t start = 0;
	int64_t size = -1;

	IGETSET(bool, overlapped, Overlapped, false);
};

} // namespace dcpp

#endif /*SEGMENT_H_*/
