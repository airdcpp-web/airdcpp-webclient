/*
* Copyright (C) 2011-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_WEBSERVER_FILESERVER_H
#define DCPLUSPLUS_WEBSERVER_FILESERVER_H

#include "forward.h"

#include <airdcpp/core/header/typedefs.h>
#include <airdcpp/core/thread/CriticalSection.h>


namespace webserver {
	struct HttpRequest;
	class FileServer {
	public:
		FileServer();
		~FileServer();

		void setResourcePath(const string& aPath) noexcept;

		// Get location of the file server root directory (Web UI files)
		const string& getResourcePath() const noexcept;

		http_status handleRequest(const HttpRequest& aRequest, 
			std::string& output_, StringPairList& headers_, const FileDeferredHandler& aDeferF);

		string getTempFilePath(const string& fileId) const noexcept;
		void stop() noexcept;

		FileServer(FileServer&) = delete;
		FileServer& operator=(FileServer&) = delete;
	private:
		http_status handleGetRequest(const websocketpp::http::parser::request& aRequest,
			std::string& output_, StringPairList& headers_, const SessionPtr& aSession, const FileDeferredHandler& aDeferF);

		http_status handleProxyDownload(const string& aUrl, string& output_, const FileDeferredHandler& aDeferF) noexcept;
		void onProxyDownloadCompleted(int64_t aDownloadId, const HTTPFileCompletionF& aCompletionF) noexcept;

		http_status handlePostRequest(const websocketpp::http::parser::request& aRequest,
			std::string& output_, StringPairList& headers_, const SessionPtr& aSession) noexcept;

		string resourcePath;

		string parseResourcePath(const string& aResource, const websocketpp::http::parser::request& aRequest, StringPairList& headers_) const;
		string parseViewFilePath(const string& aResource, StringPairList& headers_, const SessionPtr& aSession) const;
		string getPath(const TTHValue& aTTH) const;

		static string getExtension(const string& aResource) noexcept;

		mutable SharedMutex cs;
		StringMap tempFiles;

		int64_t proxyDownloadCounter = 0;
		map<int64_t, std::shared_ptr<HttpDownload>> proxyDownloads;
	};
}

#endif