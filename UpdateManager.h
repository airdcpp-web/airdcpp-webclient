/*
 * Copyright (C) 2006-2011 Crise, crise<at>mail.berlios.de
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

#ifndef UPDATE_MANAGER_H
#define UPDATE_MANAGER_H

#include "Speaker.h"
#include "Singleton.h"
#include "version.h"
#include "UpdateManagerListener.h"
#include "HttpDownload.h"

#define UPDATE_TEMP_DIR Util::getTempPath() + INST_NAME PATH_SEPARATOR_STR

namespace dcpp {

using std::unique_ptr;

class UpdateManager : public Singleton<UpdateManager>, public Speaker<UpdateManagerListener>
{

public:
	UpdateManager() : updating(false) { }
	~UpdateManager() { }

	struct {
		string homepage;
		string downloads;
		string geoip6;
		string geoip4;
		string guides;
		string customize;
		string discuss;
		string language;
		string ipcheck;
	} links;

	//void checkUpdates(const string& aUrl, bool bManual = false);
	void downloadUpdate(const string& aUrl, const string& aExeName);

	bool isUpdating() const { return updating; }

	static void signVersionFile(const string& file, const string& key, bool makeHeader = false);
	static bool verifyVersionData(const string& data, const ByteVector& singature);

	static bool applyUpdate(const string& sourcePath, const string& installPath);
	static void cleanTempFiles(const string& tmpPath = UPDATE_TEMP_DIR);

	enum {
		CONN_VERSION,
		CONN_GEO_V6,
		CONN_GEO_V4,
		CONN_LANGUAGE_FILE,
		CONN_LANGUAGE_CHECK,
		CONN_CLIENT,
		CONN_SIGNATURE,
		CONN_IP,

		CONN_LAST
	};

	unique_ptr<HttpDownload> conns[CONN_LAST];

	void checkVersion(bool aManual);
	void checkLanguage();

	void checkIP(bool manual);

	void checkGeoUpdate();
	void checkGeoUpdate(bool v6);
	//void updateGeo();
	void updateGeo(bool v6);

	void completeLanguageCheck();
	void completeGeoDownload(bool v6);
	void completeVersionDownload();
	void completeLanguageDownload();
	void completeUpdateDownload();
	void completeIPCheck(bool manual);

	void init();
private:
	static uint8_t publicKey[];

	string exename;
	string versionUrl;
	string updateTTH;

	bool updating;

	ByteVector versionSig;

	void completeSignatureDownload();
	//void versionCheck(const HttpConnection*, const string& versionInfo, uint8_t stFlags);

	//void updateIP(const HttpConnection*, const string& ipData, uint8_t stFlags);
};

} // namespace dcpp

#endif // UPDATE_MANAGER_H