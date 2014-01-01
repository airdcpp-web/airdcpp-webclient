/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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
#include "LogManager.h"

namespace dcpp {

using std::function;
using std::unique_ptr;
using std::vector;

class MappingManager :
	private Thread,
	private TimerManagerListener
{
public:
	/** add an implementation derived from the base Mapper class, passed as template parameter.
	the first added mapper will be tried first, unless the "MAPPER" setting is not empty. */
	template<typename T> void addMapper() {
		mappers.emplace_back(T::name, [](const string& localIp, bool v6) {
			return new T(localIp, v6);
		});
	}
	StringList getMappers() const;

	bool open();
	void close();
	/** whether a working port mapping implementation is currently in use. */
	bool getOpened() const;
	/** get information about the currently working implementation, if there is one; or a status
	string stating otherwise. */
	string getStatus() const;

	MappingManager(bool v6);
	virtual ~MappingManager() { }
private:
	//friend class Singleton<MappingManager>;

	vector<pair<string, function<Mapper* (const string&, bool)>>> mappers;

	atomic_flag busy;
	unique_ptr<Mapper> working; /// currently working implementation.
	uint64_t renewal = 0; /// when the next renewal should happen, if requested by the mapper.

	int run();

	void close(Mapper& mapper);
	void log(const string& message, LogManager::Severity sev);
	string deviceString(Mapper& mapper) const;
	void renewLater(Mapper& mapper);

	bool v6;
	void on(TimerManagerListener::Minute, uint64_t tick) noexcept;
};

} // namespace dcpp

#endif
