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

#ifndef DCPLUSPLUS_DCPP_EXTENSIONMANAGER_H
#define DCPLUSPLUS_DCPP_EXTENSIONMANAGER_H

#include "stdinc.h"

#include <airdcpp/CriticalSection.h>
#include <airdcpp/Singleton.h>
#include <airdcpp/Speaker.h>
#include <airdcpp/Util.h>

#include <web-server/ExtensionManagerListener.h>
#include <web-server/WebServerManagerListener.h>

namespace dcpp {
	struct HttpDownload;
}

namespace webserver {
	class ExtensionManager: public Speaker<ExtensionManagerListener>, private WebServerManagerListener {
	public:
		ExtensionManager(WebServerManager* aWsm);
		~ExtensionManager();

		void load() noexcept;

		// Download extension from the given URL and install it
		// SHA1 checksum is optional
		// Returns false if the extension is being downloaded already
		bool downloadExtension(const string& aInstallId, const string& aUrl, const string& aSha1) noexcept;

		// Install extensions from the given tarball
		void installLocalExtension(const string& aInstallId, const string& aPath) noexcept;

		// Register non-local extension
		ExtensionPtr registerRemoteExtension(const SessionPtr& aSession, const json& aPackageJson);

		// Remove extension from disk
		// Throws FileException on disk errors and Exception on other errors
		void removeExtension(const ExtensionPtr& aExtension);

		ExtensionPtr getExtension(const string& aName) const noexcept;
		ExtensionList getExtensions() const noexcept;

		typedef map<string, string> EngineMap;

		// Get the engine start command for extension
		// Throws on errors
		string getStartCommand(const StringList& aEngines) const;

		EngineMap getEngines() const noexcept;

		// Parses the engine command param (command1;command2;...) and tests each token for an existing application
		static string selectEngineCommand(const string& aEngineCommands) noexcept;
	private:
		void onExtensionFailed(const Extension* aExtension, uint32_t aExitCode) noexcept;
		bool startExtensionImpl(const ExtensionPtr& aExtension) noexcept;

		EngineMap engines;

		void onExtensionDownloadCompleted(const string& aInstallId, const string& aUrl, const string& aSha1) noexcept;
		void failInstallation(const string& aInstallId, const string& aMessage, const string& aException) noexcept;

		typedef map<string, shared_ptr<HttpDownload>> HttpDownloadMap;
		HttpDownloadMap httpDownloads;

		mutable SharedMutex cs;

		// Load extension from the supplied path and store in the extension list
		// Returns the extension pointer in case it was parsed succesfully
		ExtensionPtr loadLocalExtension(const string& aPath) noexcept;

		ExtensionList extensions;

		WebServerManager* wsm;

		void on(WebServerManagerListener::Started) noexcept override;
		void on(WebServerManagerListener::Stopping) noexcept override;
		void on(WebServerManagerListener::Stopped) noexcept override;
		void on(WebServerManagerListener::SocketDisconnected, const WebSocketPtr& aSocket) noexcept override;
	};
}

#endif