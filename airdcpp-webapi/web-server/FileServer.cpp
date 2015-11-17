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

#include <airdcpp/File.h>
#include <airdcpp/Util.h>

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

	websocketpp::http::status_code::value FileServer::handleRequest(const string& aRequestPath, const SessionPtr& aSession, 
		const string& aRequestBody, string& output_, string& contentType) noexcept {

		if (resourcePath.empty()) {
			output_ = "No resource path set";
			return websocketpp::http::status_code::not_found;
		}

		dcdebug("Requesting file %s\n", aRequestPath.c_str());

		// Forward all requests for non-static files to index
		std::string request = aRequestPath;
		if (request.find("/build") != 0 && request != "/favicon.ico") {
			request = "/index.html";
		}

		auto extension = Util::getFileExt(request);
		if (!extension.empty()) {
			// Strip the dot
			extension = extension.substr(1);
		}

		// For windows
		Util::replace(request, "/", PATH_SEPARATOR_STR);

		try {
			File f(resourcePath + request, File::READ, File::OPEN);
			output_ = f.read();
		} catch (const FileException& e) {
			output_ = e.getError();
			return websocketpp::http::status_code::not_found;
		}

		// Get the mime type
		for (int i = 0; mimes[i].ext != NULL; i++) {
			if (extension == mimes[i].ext) {
				contentType = mimes[i].type;
				break;
			}
		}

		return websocketpp::http::status_code::ok;
	}
}