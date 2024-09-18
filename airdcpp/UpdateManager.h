/*
 * Copyright (C) 2012-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_UPDATE_MANAGER_H
#define DCPLUSPLUS_DCPP_UPDATE_MANAGER_H

#include "Message.h"
#include "Singleton.h"
#include "Speaker.h"
#include "TimerManagerListener.h"
#include "UpdateManagerListener.h"

#include "version.h"

namespace dcpp {

struct HttpDownload;
class UpdateDownloader;

class UpdateManager : public Singleton<UpdateManager>, public Speaker<UpdateManagerListener>, private TimerManagerListener
{

public:
	UpdateManager();
	~UpdateManager() final;

	struct {
		string geoip;
		string language;
		string ipcheck4;
		string ipcheck6;
	} links;

	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;
	static bool verifyVersionData(const string& data, const ByteVector& singature);

	enum {
		CONN_VERSION,
		CONN_GEO,
		CONN_LANGUAGE_FILE,
		CONN_LANGUAGE_CHECK,
		CONN_SIGNATURE,
		CONN_IP4,
		CONN_IP6,

		CONN_LAST
	};

	unique_ptr<HttpDownload> conns[CONN_LAST];

	void checkVersion(bool aManualCheck);
	void checkLanguage();

	void checkIP(bool aManualCheck, bool v6);

	void checkGeoUpdate();

	void init();

	void checkAdditionalUpdates(bool aManualCheck);
	string getVersionUrl() const;

	UpdateDownloader& getUpdater() const noexcept {
		return *updater.get();
	}
private:
	unique_ptr<UpdateDownloader> updater;

	void failVersionDownload(const string& aError, bool aManualCheck);

	static const char* versionUrl[VERSION_LAST];

	uint64_t lastIPUpdate;
	static uint8_t publicKey[270];

	ByteVector versionSig;

	void updateGeo();

	void completeSignatureDownload(bool aManualCheck);
	void completeLanguageCheck();
	void completeGeoDownload();
	void completeVersionDownload(bool aManualCheck);
	void completeLanguageDownload();
	void completeIPCheck(bool aManualCheck, bool v6);

	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept override;

	static string parseIP(const string& aText, bool v6);
};

} // namespace dcpp

#endif // UPDATE_MANAGER_H