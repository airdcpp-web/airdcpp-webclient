/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#ifndef DCPLUSPLUS_DCPP_MAPPER_WINUPNP_H
#define DCPLUSPLUS_DCPP_MAPPER_WINUPNP_H

#include <airdcpp/Mapper.h>

struct IUPnPNAT;
struct IStaticPortMappingCollection;

namespace dcpp {

/// @todo this class is far from complete (should register callbacks, etc)
class Mapper_WinUPnP : public Mapper
{
public:
	Mapper_WinUPnP(const string& localIp, bool v6);

	static const string name;

private:
	bool init() override;
	void uninit() override;

	bool add(const string& port, const Protocol protocol, const string& description) override;
	bool remove(const string& port, const Protocol protocol) override;
	bool supportsProtocol(bool aV6) const override;

	uint32_t renewal() const override { return 0; }

	string getDeviceName() override;
	string getExternalIP() override;

	const string& getName() const override { return name; }

	// this one can become invalid so we can't cache it
	IStaticPortMappingCollection* getStaticPortMappingCollection();
#ifdef WIN32
	IUPnPNAT* pUN = 0;

	// need to save these to get the external IP...
	long lastPort = 0;
	Protocol lastProtocol = PROTOCOL_LAST;
#endif
};

} // dcpp namespace

#endif
