/*
 * Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_GEOIP_H
#define DCPLUSPLUS_DCPP_GEOIP_H

#include <string>
#include <vector>

struct MMDB_s;

namespace dcpp {

using std::string;
using std::vector;

class GeoIP : boost::noncopyable {
public:
	explicit GeoIP(string&& path);
	~GeoIP();

	string getCountry(const string& ip) const;
	void update();
	//void rebuild();

private:
	bool decompress() const;
	void open();
	void close();

	//mutable CriticalSection cs;
	::MMDB_s* geo;

	const string path;
	const string language;
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_GEOIP_H)
