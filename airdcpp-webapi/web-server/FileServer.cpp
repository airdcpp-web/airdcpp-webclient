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

#include "stdinc.h"

#include <web-server/FileServer.h>
#include <web-server/HttpRequest.h>
#include <web-server/HttpUtil.h>
#include <web-server/WebServerManager.h>
#include <web-server/WebUserManager.h>

#include <api/common/Deserializer.h>

#include <airdcpp/util/DupeUtil.h>
#include <airdcpp/core/classes/Exception.h>
#include <airdcpp/core/io/File.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/core/thread/Thread.h>
#include <airdcpp/util/Util.h>

#include <airdcpp/util/LinkUtil.h>
#include <airdcpp/connection/http/HttpDownload.h>
#include <airdcpp/core/classes/ScopedFunctor.h>
#include <airdcpp/util/ValueGenerator.h>
#include <airdcpp/viewed_files/ViewFileManager.h>

#include <sstream>

namespace webserver {
	using namespace dcpp;

	FileServer::FileServer() {
	}

	FileServer::~FileServer() {
		RLock l(cs);
		for (const auto& [_, path] : tempFiles) {
			File::deleteFile(path);
		}
	}

	const string& FileServer::getResourcePath() const noexcept {
		return resourcePath;
	}

	void FileServer::setResourcePath(const string& aPath) noexcept {
		resourcePath = PathUtil::validateDirectoryPath(aPath);
	}

	string FileServer::getExtension(const string& aResource) noexcept {
		auto extension = PathUtil::getFileExt(aResource);
		if (!extension.empty()) {
			// Strip the dot
			extension = extension.substr(1);
		}

		return extension;
	}

	string FileServer::parseResourcePath(const string& aResource, const websocketpp::http::parser::request& aRequest, StringPairList& headers_) const {
		// Serve files only from the resource directory
		if (aResource.empty() || aResource.find("..") != std::string::npos) {
			throw RequestException(http_status::bad_request, "Invalid resource path");
		}

		auto request = aResource;

		auto extension = getExtension(request);
		if (!extension.empty()) {
			dcassert(extension[0] != '.');

			// We have compressed versions only for JS files
			if (extension == "js" && aRequest.get_header("Accept-Encoding").find("gzip") != string::npos) {
				request += ".gz";
				headers_.emplace_back("Content-Encoding", "gzip");
			}

			if (extension != "html" && aResource != "/sw.js") {
				// File versioning is done with hashes in filenames (except for the index file and service worker)
				HttpUtil::addCacheControlHeader(headers_, 365);
			}
		} else {
			// Forward all requests for non-static files to index
			// (but try to report API requests or other downloads with an invalid path)

			if (aRequest.get_header("Accept").find("text/html") == string::npos) {
				if (aRequest.get_header("Content-Type") == "application/json") {
					throw RequestException(http_status::not_acceptable, "File server won't serve JSON files. Did you mean \"/api" + aResource + "\" instead?");
				}

				throw RequestException(http_status::not_found, "Invalid file path (hint: use \"Accept: text/html\" if you want index.html)");
			}

			request = "index.html";

			// The main chunk name may change and it's stored in the HTML file
			HttpUtil::addCacheControlHeader(headers_, 0);
		}

		// Avoid double separators because of assertions
		if (!request.empty() && request.front() == '/') {
			request = request.substr(1);
		}

		// For windows
		Util::replace(request, "/", PATH_SEPARATOR_STR);

		return resourcePath + request;
	}

	string FileServer::getPath(const TTHValue& aTTH) const {
		auto file = ViewFileManager::getInstance()->getFile(aTTH);
		if (file) {
			return file->getPath();
		}
		
		auto dupe = DupeUtil::checkFileDupe(aTTH);
		if (DupeUtil::allowOpenFileDupe(dupe)) {
			auto paths = DupeUtil::getFileDupePaths(dupe, aTTH);
			if (!paths.empty()) {
				return paths.front();
			}
		}

		throw RequestException(http_status::not_found, "No viewable file matching the TTH " + aTTH.toBase32() + " was found");
	}

	string FileServer::parseViewFilePath(const string& aResource, StringPairList& headers_, const SessionPtr& aSession) const {
		string protocolTmp, tthStr, portTmp, pathTmp, query, fragmentTmp;
		LinkUtil::decodeUrl(aResource, protocolTmp, tthStr, portTmp, pathTmp, query, fragmentTmp);

		auto session = aSession;
		if (!session) {
			auto auth = LinkUtil::decodeQuery(query)["auth_token"];
			if (!auth.empty()) {
				session = WebServerManager::getInstance()->getUserManager().getSession(auth);
			}

			if (!session || !session->getUser()->hasPermission(Access::VIEW_FILES_VIEW)) {
				throw RequestException(http_status::unauthorized, "Not authorized");
			}
		}

		auto tth = Deserializer::parseTTH(tthStr);
		auto path = getPath(tth);

		HttpUtil::addCacheControlHeader(headers_, 1); // One day (files are identified by their TTH so the content won't change)

		return path;
	}

	http_status FileServer::handlePostRequest(const websocketpp::http::parser::request& aRequest,
		std::string& output_, StringPairList& headers_, const SessionPtr& aSession) noexcept {

		const auto& requestPath = aRequest.get_uri();
		if (requestPath == "/temp") {
			if (!aSession || !aSession->getUser()->hasPermission(Access::FILESYSTEM_EDIT)) {
				output_ = "Not authorized";
				return http_status::unauthorized;
			}

			const auto fileName = Util::toString(ValueGenerator::rand());
			const auto filePath = AppUtil::getPath(AppUtil::PATH_TEMP) + fileName;

			try {
				File file(filePath, File::WRITE, File::TRUNCATE | File::CREATE, File::BUFFER_SEQUENTIAL);
				file.write(aRequest.get_body());
			} catch (const FileException& e) {
				output_ = "Failed to write the file: " + e.getError();
				return http_status::internal_server_error;
			}

			{
				WLock l(cs);
				tempFiles.try_emplace(fileName, filePath);
			}

			headers_.emplace_back("Location", fileName);
			return http_status::created;
		}

		output_ = "Requested resource was not found";
		return http_status::not_found;
	}

	string FileServer::getTempFilePath(const string& fileId) const noexcept {
		RLock l(cs);
		auto i = tempFiles.find(fileId);
		return i != tempFiles.end() ? i->second : Util::emptyString;
	}

	http_status FileServer::handleRequest(const HttpRequest& aRequest,
		string& output_, StringPairList& headers_, const FileDeferredHandler& aDeferF) {

		const auto& httpRequest = aRequest.httpRequest;
		if (httpRequest.get_method() == "GET") {
			return handleGetRequest(httpRequest, output_, headers_, aRequest.session, aDeferF);
		} else if (httpRequest.get_method() == "POST") {
			return handlePostRequest(httpRequest, output_, headers_, aRequest.session);
		}

		output_ = "Requested resource was not found";
		return http_status::not_found;
	}

	http_status FileServer::handleGetRequest(const websocketpp::http::parser::request& aRequest,
		string& output_, StringPairList& headers_, const SessionPtr& aSession, const FileDeferredHandler& aDeferF) {

		const auto& requestUrl = aRequest.get_uri();
		dcdebug("Requesting file %s\n", requestUrl.c_str());

		// Proxy request?
		if (requestUrl.starts_with("/proxy")) {
			if (!aSession) {
				output_ = "Not authorized";
				return http_status::unauthorized;
			}

			return handleProxyDownload(requestUrl, output_, aDeferF);
		}

		// File request
		string filePath;
		auto isViewFile = requestUrl.length() >= 6 && requestUrl.compare(0, 6, "/view/") == 0;
		try {
			if (isViewFile) {
				filePath = parseViewFilePath(requestUrl.substr(6), headers_, aSession);
			} else if (requestUrl.length() >= 6 && requestUrl.compare(0, 6, "/proxy") == 0) {
				if (!aSession) {
					throw RequestException(http_status::unauthorized, "Not authorized");
				}

				return handleProxyDownload(requestUrl, output_, aDeferF);
			} else {
				filePath = parseResourcePath(requestUrl, aRequest, headers_);
			}
		} catch (const RequestException& e) {
			output_ = e.what();
			return e.getCode();
		}

		auto fileSize = File::getSize(filePath);
		int64_t startPos = 0, endPos = fileSize - 1;

		auto partialContent = HttpUtil::parsePartialRange(aRequest.get_header("Range"), startPos, endPos);

		// Read file
		try {
			File f(filePath, File::READ, File::OPEN);
			f.setPos(startPos);
			output_ = f.read(static_cast<size_t>(endPos) + 1);
		} catch (const FileException& e) {
			dcdebug("Failed to serve the file %s: %s\n", filePath.c_str(), e.getError().c_str());

			// Don't show the local file path for public resources
			auto responsePath = isViewFile ? filePath : requestUrl;
			output_ = e.getError() + " (" + responsePath + ")";
			return http_status::not_found;
		} catch (const std::bad_alloc&) {
			output_ = "Not enough memory on the server to serve this request";
			return http_status::internal_server_error;
		}

		{
			const auto ext = PathUtil::getFileExt(filePath);
			if (ext == ".nfo") {
				string encoding;

				// Platform-independent encoding conversion function could be added if there is more use for it
#ifdef _WIN32
				encoding = "CP.437";
#else
				encoding = "cp437";
#endif
				output_ = Text::toUtf8(output_, encoding);
			}
		}

		{
			// Get the mime type (but get it from the original request with gzipped content)
			auto usingEncoding = ranges::find(headers_ | views::keys, "Content-Encoding").base() != headers_.end();
			auto type = HttpUtil::getMimeType(usingEncoding ? requestUrl : filePath);
			if (type) {
				headers_.emplace_back("Content-Type", type);
			}
		}

		if (partialContent) {
			headers_.emplace_back("Content-Range", HttpUtil::formatPartialRange(startPos, endPos, fileSize));
			headers_.emplace_back("Accept-Ranges", "bytes");
			return http_status::partial_content;
		}

		return http_status::ok;
	}

	http_status FileServer::handleProxyDownload(const string& aRequestUrl, string& output_, const FileDeferredHandler& aDeferF) noexcept {
		string protocol, host, port, path, query, fragment;
		LinkUtil::decodeUrl(aRequestUrl, protocol, host, port, path, query, fragment);

		// Parse query
		auto proxyUrlEscaped = LinkUtil::decodeQuery(query)["url"];
		if (proxyUrlEscaped.empty()) {
			output_ = "Proxy URL missing";
			return http_status::bad_request;
		}

		// Decode URL
		string proxyUrl;
		if (!HttpUtil::unescapeUrl(proxyUrlEscaped, proxyUrl)) {
			output_ = "Invalid URL " + proxyUrlEscaped;
			return http_status::bad_request;
		}

		auto downloadId = proxyDownloadCounter++;
		auto download = std::make_shared<HttpDownload>(
			proxyUrl,
			[this, downloadId, completionHandler = aDeferF()]() {
				onProxyDownloadCompleted(downloadId, completionHandler);
			}
		);

		{
			WLock l(cs);
			proxyDownloads.try_emplace(downloadId, download);
		}

		return http_status::accepted;
	}

	static StringSet forwardedProxyHeaders = { "content-type", "content-encoding", "etag", "expires", "last-modified", "date", "vary" };

	void FileServer::onProxyDownloadCompleted(int64_t aDownloadId, const HTTPFileCompletionF& aCompletionF) noexcept {
		ScopedFunctor([&] {
			WLock l(cs);
			proxyDownloads.erase(aDownloadId);
		});

		shared_ptr<HttpDownload> d = nullptr;

		{
			RLock l(cs);
			auto i = proxyDownloads.find(aDownloadId);
			if (i != proxyDownloads.end()) {
				d = i->second;
			}
		}

		dcassert(d);
		if (d) {
			if (d->buf.empty()) {
				int statusCode;
				string statusText;
				if (HttpUtil::parseStatus(d->status, statusCode, statusText)) {
					aCompletionF(static_cast<http_status>(statusCode), statusText, StringPairList());
				} else {
					aCompletionF(http_status::not_acceptable, d->status, StringPairList());
				}
			} else {
				StringPairList headers;
				for (const auto& [name, value] : d->headers) {
					if (forwardedProxyHeaders.contains(Text::toLower(name))) {
						headers.push_back({ name, value });
					}
				}

				HttpUtil::addCacheControlHeader(headers, 0);
				aCompletionF(http_status::ok, d->buf, headers);
			}
		}
	}

	void FileServer::stop() noexcept {
		for (;;) {
			bool hasDownloads;

			{
				RLock l(cs);
				hasDownloads = !proxyDownloads.empty();
			}

			if (hasDownloads) {
				Thread::sleep(50);
			} else {
				break;
			}
		}
	}
}