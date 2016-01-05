/*
* Copyright (C) 2011-2015 AirDC++ Project
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

#include <web-server/FileServer.h>
#include <web-server/WebServerManager.h>

#include <api/common/Deserializer.h>

#include <airdcpp/File.h>
#include <airdcpp/Util.h>
#include <airdcpp/ViewFileManager.h>

#include <sstream>

namespace webserver {
	using namespace dcpp;

	FileServer::FileServer() {
	}

	FileServer::~FileServer() {

	}

	const string& FileServer::getResourcePath() const noexcept {
		return resourcePath;
	}

	void FileServer::setResourcePath(const string& aPath) noexcept {
		resourcePath = aPath;
	}

	struct mime { const char* ext; const char* type; };
	struct mime mimes[] = {
		{ "exe", "application/octet-stream" },
		{ "pdf", "application/pdf" },
		{ "zip", "application/zip" },
		{ "gz", "application/x-gzip" },
		{ "js", "application/javascript; charset=utf-8" },
		{ "mp3", "audio/mpeg" },
		{ "wma", "audio/x-ms-wma" },
		{ "wav", "audio/vnd.wave" },
		{ "gif", "image/gif" },
		{ "jpg", "image/jpeg" },
		{ "png", "image/png" },
		{ "tiff", "image/tiff" },
		{ "tif", "image/tiff" },
		{ "ico", "image/vnd.microsoft.icon" },
		{ "css", "text/css" },
		{ "html", "text/html; charset=utf-8" },
		{ "txt", "text/plain" },
		{ "xml", "text/xml" },
		{ "mpg", "video/mpeg" },
		{ "mp4", "video/mp4" },
		{ "wmv", "video/x-ms-wmv" },
		{ "odt", "application/vnd.oasis.opendocument.text" },
		{ "ods", "application/vnd.oasis.opendocument.spreadsheet" },
		{ "odp", "application/vnd.oasis.opendocument.presentation" },
		{ "odg", "application/vnd.oasis.opendocument.graphics" },
		{ "xls", "application/vnd.ms-excel" },
		{ "ppt", "application/vnd.ms-powerpoint" },
		{ "doc", "application/msword" },
		{ "docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document" },
		{ "ttf", "application/x-font-ttf" },
		{ "rar", "application/x-rar-compressed" },
		{ "tar", "application/x-tar" },
		{ "c", "text/plain" },
		{ "cpp", "text/plain" },
		{ "asm", "text/plain" },
		{ "bat", "text/plain" },
		{ "vb", "text/plain" },
		{ "cs", "text/plain" },
		{ "pl", "text/plain" },
		{ "py", "text/plain" },
		{ "class", "text/plain" },
		{ "vbs", "text/plain" },
		{ NULL, NULL }
	};

	string FileServer::parseResourcePath(const string& aResource, const websocketpp::http::parser::request& aRequest, StringPairList& headers_) const noexcept {
		auto request = aResource;

		auto extension = getExtension(request);
		if (!extension.empty()) {
			// Strip the dot
			extension = extension.substr(1);

			// We have compressed versions only for JS files
			if (extension == "js" && aRequest.get_header("Accept-Encoding").find("gzip") != string::npos) {
				request += ".gz";
				headers_.emplace_back("Content-Encoding", "gzip");
			}
		} else if (request.find("/build") != 0 && request != "/favicon.ico") {
			// Forward all requests for non-static files to index
			request = "/index.html";
		}

		// For windows
		Util::replace(request, "/", PATH_SEPARATOR_STR);

		return resourcePath + request;
	}

	string FileServer::parseViewFilePath(const string& aResource) const {
		string protocol, tth, port, path, query, fragment;
		Util::decodeUrl(aResource, protocol, tth, port, path, query, fragment);

		auto auth = Util::decodeQuery(query)["auth"];
		if (auth.empty()) {
			throw std::domain_error("Authorization query missing");
		}

		auto session = WebServerManager::getInstance()->getUserManager().getSession(auth);
		if (!session || !session->getUser()->hasPermission(Access::VIEW_FILES_VIEW)) {
			throw std::domain_error("Not authorized");
		}

		auto file = ViewFileManager::getInstance()->getFile(Deserializer::parseTTH(tth));
		if (!file) {
			throw std::domain_error("No files matching the TTH were found");
		}

		return file->getPath();
	}

	string FileServer::getExtension(const string& aResource) noexcept {
		auto extension = Util::getFileExt(aResource);
		if (!extension.empty()) {
			// Strip the dot
			extension = extension.substr(1);
		}

		return extension;
	}

	websocketpp::http::status_code::value FileServer::handleRequest(const string& aResource, const websocketpp::http::parser::request& aRequest,
		string& output_, StringPairList& headers_) noexcept {

		dcassert(!resourcePath.empty());
		dcdebug("Requesting file %s\n", aResource.c_str());

		// Get the disk path path
		string request;
		if (aResource.length() >= 6 && aResource.compare(0, 6, "/view/") == 0) {
			try {
				request = parseViewFilePath(aResource.substr(6));
			} catch (const std::exception& e) {
				output_ = e.what();
				return websocketpp::http::status_code::bad_request;
			}
		} else {
			request = parseResourcePath(aResource, aRequest, headers_);
		}

		// Read file
		try {
			File f(request, File::READ, File::OPEN);
			output_ = f.read();
		} catch (const FileException& e) {
			output_ = e.getError();
			return websocketpp::http::status_code::not_found;
		}

		// Get the mime type
		auto extension = getExtension(request);
		for (int i = 0; mimes[i].ext != NULL; i++) {
			if (extension == mimes[i].ext) {
				headers_.emplace_back("Content-Type", mimes[i].type);
				break;
			}
		}

		return websocketpp::http::status_code::ok;
	}
}