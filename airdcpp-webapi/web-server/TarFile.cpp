/*
* Copyright (C) 2011-2018 AirDC++ Project
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

#include <web-server/stdinc.h>
#include <web-server/TarFile.h>

#include <airdcpp/Exception.h>
#include <airdcpp/File.h>
#include <airdcpp/Text.h>

#ifdef WIN32
#include <boost/algorithm/string/replace.hpp>
#endif

namespace webserver {
	TarFile::TarFile(const string& aPath) {
		auto res = mtar_open(&tar, Text::fromUtf8(aPath).c_str(), "r");
		if (res != MTAR_ESUCCESS) {
			throw Exception(mtar_strerror(res));
		}
	}

	void TarFile::extract(const string& aDestPath) {
		mtar_header_t h;
		while ((mtar_read_header(&tar, &h)) != MTAR_ENULLRECORD) {
			if (h.type != MTAR_TDIR && strcmp(h.name, "pax_global_header") != 0) {
				boost::scoped_array<char> buf(new char[h.size + 1]);
				auto res = mtar_read_data(&tar, &buf[0], h.size);
				if (res != MTAR_ESUCCESS) {
					throw Exception(mtar_strerror(res));
				}

				string destFile = aDestPath + h.name;

#ifdef WIN32
				// Wrong path separators would hit assertions...
				boost::replace_all(destFile, "/", PATH_SEPARATOR_STR);
#endif

				File::ensureDirectory(destFile);
				{
					File f(destFile, File::WRITE, File::OPEN | File::CREATE | File::TRUNCATE, File::BUFFER_SEQUENTIAL);
					f.write(&buf[0], h.size);
				}
			}

			mtar_next(&tar);
		}
	}

	TarFile::~TarFile() {
		mtar_close(&tar);
	}
}