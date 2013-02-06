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

#ifndef DCPLUSPLUS_DCPP_MAPPER_H
#define DCPLUSPLUS_DCPP_MAPPER_H

#include <string>
#include <set>
#include <utility>

#include <boost/noncopyable.hpp>

namespace dcpp {

using std::string;

/** abstract class to represent an implementation usable by MappingManager. */
class Mapper : boost::noncopyable
{
public:
	Mapper(const string& localIp, bool v6);
	virtual ~Mapper() { }

	enum Protocol {
		PROTOCOL_TCP,
		PROTOCOL_UDP,
		PROTOCOL_LAST
	};
	static const char* protocols[PROTOCOL_LAST];

	/** begin the initialization phase.
	@return true if the initialization passed; false otherwise. */
	virtual bool init() = 0;
	/** end the initialization phase. called regardless of the return value of init(). */
	virtual void uninit() = 0;

	bool open(const string& port, const Protocol protocol, const string& description);
	bool close();
	bool hasRules() const;

	/** interval after which ports should be re-mapped, in minutes. 0 = no renewal. */
	virtual uint32_t renewal() const = 0;

	virtual string getDeviceName() = 0;
	virtual string getExternalIP() = 0;

	/* by contract, implementations of this class should define a public user-friendly name in:
	static const string name; */

	/** user-friendly name for this implementation. */
	virtual const string& getName() const = 0;
	virtual bool supportsProtocol(bool v6) const = 0;
protected:
	string localIp;
	const bool v6;
private:
	/** add a port mapping rule. */
	virtual bool add(const string& port, const Protocol protocol, const string& description) = 0;
	/** remove a port mapping rule. */
	virtual bool remove(const string& port, const Protocol protocol) = 0;

	std::set<std::pair<string, Protocol>> rules;
};

} // namespace dcpp

#endif
