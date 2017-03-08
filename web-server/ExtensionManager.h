/*
* Copyright (C) 2011-2017 AirDC++ Project
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

#include <web-server/stdinc.h>

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
		bool downloadExtension(const string& aUrl, const string& aSha1) noexcept;

		// Install extensions from the given tarball
		void installExtension(const string& aPath) noexcept;

		// Remove extension from disk
		// Throws FileException on disk errors and Exception on other errors
		void removeExtension(const ExtensionPtr& aExtension);

		ExtensionPtr getExtension(const string& aName) const noexcept;
		ExtensionList getExtensions() const noexcept;

		bool startExtension(const ExtensionPtr& aExtension) noexcept;
		bool stopExtension(const ExtensionPtr& aExtension) noexcept;
	private:
		// Remove extension from the list
		bool removeList(const ExtensionPtr& aExtension);

		void onExtensionDownloadCompleted(const string& aUrl, const string& aSha1) noexcept;
		void failInstallation(const string& aMessage, const string& aException) noexcept;

		typedef map<string, shared_ptr<HttpDownload>> HttpDownloadMap;
		HttpDownloadMap httpDownloads;

		mutable SharedMutex cs;

		// Load extension from the supplied path and store in the extension list
		// Returns the extension pointer in case it was parsed succesfully
		ExtensionPtr loadExtension(const string& aPath) noexcept;

		ExtensionList extensions;

		WebServerManager* wsm;

		void on(WebServerManagerListener::Started) noexcept override;
		void on(WebServerManagerListener::Stopping) noexcept override;
	};
}

#endif