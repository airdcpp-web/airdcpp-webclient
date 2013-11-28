/*
 * Copyright (C) 2012-2013 AirDC++ Project
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
#include "UpdateManagerListener.h"

#include "HttpDownload.h"
#include "SimpleXML.h"
#include "TimerManager.h"

#define UPDATE_TEMP_DIR Util::getTempPath() + "Updater" + PATH_SEPARATOR_STR

namespace dcpp {

using std::unique_ptr;

class UpdateManager : public Singleton<UpdateManager>, public Speaker<UpdateManagerListener>, private TimerManagerListener
{

public:
	UpdateManager();
	~UpdateManager();

	struct {
		string homepage;
		string downloads;
		string geoip6;
		string geoip4;
		string guides;
		string customize;
		string discuss;
		string language;
		string ipcheck4;
		string ipcheck6;
	} links;

	void downloadUpdate(const string& aUrl, int newBuildID, bool manualCheck);

	static bool verifyVersionData(const string& data, const ByteVector& singature);

	static bool checkPendingUpdates(const string& aDstDir, string& updater_, bool updated);

	static bool applyUpdate(const string& sourcePath, const string& installPath);
	static void cleanTempFiles(const string& tmpPath);

	enum {
		CONN_VERSION,
		CONN_GEO_V6,
		CONN_GEO_V4,
		CONN_LANGUAGE_FILE,
		CONN_LANGUAGE_CHECK,
		CONN_CLIENT,
		CONN_SIGNATURE,
		CONN_IP4,
		CONN_IP6,

		CONN_LAST
	};

	enum {
		UPDATE_UNDEFINED,
		UPDATE_AUTO,
		UPDATE_PROMPT
	};

	unique_ptr<HttpDownload> conns[CONN_LAST];

	void checkVersion(bool aManual);
	void checkLanguage();

	void checkIP(bool manual, bool v6);

	void checkGeoUpdate();
	//void updateGeo();

	void init(const string& aExeName);
	int getInstalledUpdate() { return installedUpdate; }
	bool isUpdating();

	static bool getVersionInfo(SimpleXML& xml, string& versionString, int& remoteBuild);
	void checkAdditionalUpdates(bool manualCheck);
	string getVersionUrl() const;
private:
	static const char* versionUrl[VERSION_LAST];

	uint64_t lastIPUpdate;
	static uint8_t publicKey[270];

	string exename;
	string updateTTH;
	string sessionToken;

	int installedUpdate;

	ByteVector versionSig;

	void updateGeo(bool v6);
	void checkGeoUpdate(bool v6);

	void completeSignatureDownload(bool manual);
	void completeLanguageCheck();
	void completeGeoDownload(bool v6);
	void completeVersionDownload(bool manualCheck);
	void completeLanguageDownload();
	void completeUpdateDownload(int buildID, bool manualCheck);
	void completeIPCheck(bool manualCheck, bool v6);

	void failUpdateDownload(const string& aError, bool manualCheck);

	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept;
};

} // namespace dcpp

#endif // UPDATE_MANAGER_H