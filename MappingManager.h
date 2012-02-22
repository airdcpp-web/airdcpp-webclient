/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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

#include <memory>
#include <functional>
#include <vector>

#include "forward.h"
#include "typedefs.h"
#include "Mapper.h"
#include "TimerManager.h"
#include "atomic.h"

namespace dcpp {

using std::function;
using std::make_pair;
using std::unique_ptr;
using std::vector;

class MappingManager :
	public Singleton<MappingManager>,
	private Thread,
	private TimerManagerListener
{
public:
	/** add an implementation derived from the base Mapper class, passed as template parameter.
	the first added mapper will be tried first, unless the "MAPPER" setting is not empty. */
	template<typename T> void addMapper() {
#ifndef _MSC_VER
		mappers.emplace_back(T::name, [](string&& localIp) {
			return new T(std::forward<string>(localIp));
		});
#else
		// the rvalue ref deal is too smart for MSVC; resort to a string copy...
		mappers.push_back(make_pair(T::name, [](string localIp) {
			return new T(std::move(localIp));
		}));
#endif
	}
	StringList getMappers() const;

	bool open();
	void close();
	/** whether a working port mapping implementation is currently in use. */
	bool getOpened() const;
	/** get information about the currently working implementation, if there is one; or a status
	string stating otherwise. */
	string getStatus() const;

private:
	friend class Singleton<MappingManager>;

#ifndef _MSC_VER
	vector<pair<string, function<Mapper* (string&&)>>> mappers;
#else
	vector<pair<string, function<Mapper* (const string&)>>> mappers;
#endif

	atomic_flag busy;
	unique_ptr<Mapper> working; /// currently working implementation.
	uint64_t renewal; /// when the next renewal should happen, if requested by the mapper.

	MappingManager();
	virtual ~MappingManager() { }

	int run();

	void close(Mapper& mapper);
	void log(const string& message);
	string deviceString(Mapper& mapper) const;
	void renewLater(Mapper& mapper);

	void on(TimerManagerListener::Minute, uint64_t tick) noexcept;
};

} // namespace dcpp

#endif
