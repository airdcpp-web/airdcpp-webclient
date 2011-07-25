/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_MAPPING_MANAGER_H
#define DCPLUSPLUS_DCPP_MAPPING_MANAGER_H

#include <atomic>

#include "forward.h"
#include "typedefs.h"
#include "Mapper.h"

#include "Singleton.h"
#include "Thread.h"
#include "TimerManager.h"

namespace dcpp {

class MappingManager :
	public Singleton<MappingManager>,
	private Thread,
	private TimerManagerListener
{
public:
	/** add an implementation derived from the base Mapper class, passed as template parameter.
	the first added mapper will be tried first, unless the "MAPPER" setting is not empty. */
	template<typename T> void addMapper() { mappers.push_back(make_pair(T::name, [] { return new T(); })); }
	StringList getMappers() const;

	bool open();
	void close();
	bool getOpened() const;

private:
	friend class Singleton<MappingManager>;

	vector<pair<string, function<Mapper* ()>>> mappers;

	atomic_flag busy;
	unique_ptr<Mapper> working; /// currently working implementation.
	uint64_t renewal; /// when the next renewal should happen, if requested by the mapper.

	MappingManager() : busy(false), renewal(0) { }
	virtual ~MappingManager() { join(); }

	int run();

	void close(Mapper& mapper);
	void log(const string& message);
	string deviceString(Mapper& mapper) const;

	void on(TimerManagerListener::Minute, uint64_t tick) noexcept;
};

} // namespace dcpp

#endif
