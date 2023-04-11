/*
 * Copyright (C) 2001-2023 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_MAPPER_NATPMP_H
#define DCPLUSPLUS_DCPP_MAPPER_NATPMP_H

#include "Mapper.h"

namespace dcpp {

class Mapper_NATPMP : public Mapper
{
public:
	Mapper_NATPMP(const string& localIp, bool v6);

	static const string name;

private:
	bool init();
	void uninit();

	bool add(const string& port, const Protocol protocol, const string& description);
	bool remove(const string& port, const Protocol protocol);
	bool supportsProtocol(bool aV6) const;

	uint32_t renewal() const { return lifetime / 2; }

	string getDeviceName();
	string getExternalIP();

	const string& getName() const { return name; }

	string gateway;
	uint32_t lifetime; // in minutes
};

} // dcpp namespace

#endif
