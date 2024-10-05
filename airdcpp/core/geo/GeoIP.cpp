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

#include "stdinc.h"
#include <airdcpp/core/geo/GeoIP.h>

#include <airdcpp/core/classes/Exception.h>
#include <airdcpp/core/io/File.h>
#include <airdcpp/core/localization/Localization.h>
#include <airdcpp/core/header/format.h>
#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/util/Util.h>
#include <airdcpp/core/io/compress/ZUtils.h>

#include <maxminddb.h>

namespace dcpp {

// Application locales mapped to supported GeoIP languages
const map<string, string> localeGeoMappings = {
	{ "de-DE", "de" },
	{ "en-US", "en" },
	{ "es-ES", "es" },
	{ "fr-FR", "fr" },
	//{ "ja" },
	{ "pt-BR", "pt-BR" },
	{ "ru-RU", "ru" } ,
	//{ "zh-CN" },
};

static string parseLanguage() noexcept {
	auto i = localeGeoMappings.find(Localization::getCurLanguageLocale());
	return i != localeGeoMappings.end() ? i->second : "en";
}

GeoIP::GeoIP(string&& aPath) : geo(nullptr), path(std::move(aPath)), language(parseLanguage()) {
	if (File::getSize(path) > 0 || decompress()) {
		open();
	}
}

GeoIP::~GeoIP() {
	close();
}

namespace {

static string parseData(MMDB_lookup_result_s res, ...) {
	MMDB_entry_data_s entry_data;
	va_list keys;
	va_start(keys, res);

	auto status = MMDB_vget_value(&res.entry, &entry_data, keys);
	va_end(keys);

	if (status != MMDB_SUCCESS)
		return Util::emptyString;

	if (!entry_data.has_data)
		return Util::emptyString;

	if (entry_data.type != MMDB_DATA_TYPE_UTF8_STRING) {
		dcdebug("Invalid data UTF8 GeoIP2 data %d:\n", entry_data.type);
		return Util::emptyString;
	}

	return string(entry_data.utf8_string, entry_data.data_size);
}

} // unnamed namespace

string GeoIP::getCountry(const string& ip) const {
	if (geo) {
		int gai_error, mmdb_error;
		MMDB_lookup_result_s res = MMDB_lookup_string(
			geo, 
			ip.c_str(), 
			&gai_error, 
			&mmdb_error
		);

		if (res.found_entry && mmdb_error == MMDB_SUCCESS && gai_error == 0) {
			const string& setting = SETTING(COUNTRY_FORMAT);

			ParamMap params;
			params["2code"] = [&] { return parseData(res, "country", "iso_code", NULL); };
			params["continent"] = [&] { return parseData(res, "continent", "code", NULL); };
			params["engname"] = [&] { return parseData(res, "country", "names", "en", NULL); };
			params["name"] = [&] { return parseData(res, "country", "names", language.c_str(), NULL); };
			params["officialname"] = [&] { return parseData(res, "country", "names", language.c_str(), NULL); };

			return Util::formatParams(setting, params);
		} else {
			if (gai_error != 0) {
#ifdef WIN32
				dcdebug("Error from getaddrinfo for %s - %ls\n",
#else
				dcdebug("Error from getaddrinfo for %s - %s\n",
#endif
					ip.c_str(), gai_strerror(gai_error));
			}

			if (mmdb_error != MMDB_SUCCESS) {
				dcdebug("Got an error from libmaxminddb (MMDB_lookup_string): %s\n",
					MMDB_strerror(mmdb_error));          	
			}
		}
	}

	return Util::emptyString;
}

void GeoIP::update() {
	close();

	if(decompress()) {
		open();
	}
}

bool GeoIP::decompress() const {
	if(File::getSize(path + ".gz") <= 0) {
		return false;
	}

	try { GZ::decompress(path + ".gz", path); }
	catch(const Exception&) { return false; }

	return true;
}

void GeoIP::open() {
	geo = (MMDB_s*)malloc(sizeof(MMDB_s));

	auto res = MMDB_open(Text::fromUtf8(path).c_str(), MMDB_MODE_MMAP, geo);
	if (res != MMDB_SUCCESS) {
		dcdebug("Failed to open MMDB database %s\n", MMDB_strerror(res));
		if (geo) {
			free(geo);
			geo = nullptr;
		}
	}
}

void GeoIP::close() {
	MMDB_close(geo);
	free(geo);
	geo = nullptr;
}

} // namespace dcpp
