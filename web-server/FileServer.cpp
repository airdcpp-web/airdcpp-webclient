/*
* Copyright (C) 2011-2016 AirDC++ Project
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

		{ "flac", "audio/x-flac" },
		{ "m4a", "audio/mp4" },
		{ "mid", "audio/midi" },
		{ "mp3", "audio/mpeg" },
		{ "ogg", "audio/ogg" },
		{ "wma", "audio/x-ms-wma" },
		{ "wav", "audio/vnd.wave" },

		{ "bmp", "image/bmp" },
		{ "gif", "image/gif" },
		{ "ico", "image/x-icon" },
		{ "jpg", "image/jpeg" },
		{ "jpeg", "image/jpeg" },
		{ "png", "image/png" },
		{ "psd", "image/vnd.adobe.photoshop" },
		{ "tga", "image/tga" },
		{ "tiff", "image/tiff" },
		{ "tif", "image/tiff" },
		{ "ico", "image/vnd.microsoft.icon" },
		{ "webp", "image/webp" },

		{ "3gp", "video/3gpp" },
		{ "avi", "video/avi" },
		{ "asf", "video/x-ms-asf" },
		{ "asx", "video/x-ms-asf" },
		{ "flv", "video/x-flv" },
		{ "mkv", "video/x-matroska" },
		{ "mov", "video/quicktime" },
		{ "mpg", "video/mpeg" },
		{ "mpeg", "video/mpeg" },
		{ "mp4", "video/mp4" },
		{ "qt", "video/quicktime" },
		{ "webm", "video/webm" },
		{ "wmv", "video/x-ms-wmv" },
		{ "vob", "video/x-ms-vob" },

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
		{ "swf", "application/x-shockwave-flash" },

		{ "c", "text/plain" },
		{ "cpp", "text/plain" },
		{ "asm", "text/plain" },
		{ "bat", "text/plain" },
		{ "vb", "text/plain" },
		{ "cs", "text/plain" },
		{ "nfo", "text/x-nfo" },
		{ "pl", "text/plain" },
		{ "py", "text/plain" },
		{ "class", "text/plain" },
		{ "vbs", "text/plain" },
		{ "css", "text/css" },
		{ "html", "text/html; charset=utf-8" },
		{ "txt", "text/plain" },
		{ "xml", "text/xml" },
		{ NULL, NULL }
	};

	const char* FileServer::getMimeType(const string& aFileName) noexcept {
		auto extension = getExtension(aFileName);
		for (int i = 0; mimes[i].ext != NULL; i++) {
			if (extension == mimes[i].ext) {
				return mimes[i].type;
			}
		}

		return nullptr;
	}

	string FileServer::getExtension(const string& aResource) noexcept {
		auto extension = Util::getFileExt(aResource);
		if (!extension.empty()) {
			// Strip the dot
			extension = extension.substr(1);
		}

		return extension;
	}

	string FileServer::parseResourcePath(const string& aResource, const websocketpp::http::parser::request& aRequest, StringPairList& headers_) const {
		// Serve files only from the resource directory
		if (aResource.empty() || aResource.find("..") != std::string::npos) {
			throw std::domain_error("Invalid request");
		}

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

	string FileServer::formatPartialRange(int64_t aStartPos, int64_t aEndPos, int64_t aFileSize) noexcept {
		dcassert(aEndPos < aFileSize);
		return "bytes " + Util::toString(aStartPos) + "-" + Util::toString(aEndPos) + "/" + Util::toString(aFileSize);
	}

	// Support partial requests will enhance media file playback
	// This will only support simple range values (unsupported range types will be ignored)
	bool FileServer::parsePartialRange(const string& aHeaderData, int64_t& start_, int64_t& end_) noexcept {
		if (aHeaderData.find("bytes=") != 0) {
			return false;
		}

		dcdebug("Partial HTTP request: %s)\n", aHeaderData.c_str());

		auto tokenizer = StringTokenizer<string>(aHeaderData.substr(6), '-', true);
		if (tokenizer.getTokens().size() != 2) {
			dcdebug("Partial HTTP request: unsupported range\n");
			return false;
		}

		auto parsedStart = Util::toInt64(tokenizer.getTokens().at(0));

		// Not "parsedStart >= end_" because Safari seems to request one byte past the end (shouldn't be an issue when reading the file)
		if (parsedStart > end_ || parsedStart < 0) {
			dcdebug("Partial HTTP request: start position not accepted (" I64_FMT ")\n", parsedStart);
			return false;
		}

		const auto& endToken = tokenizer.getTokens().at(1);
		if (endToken.empty()) {
			end_ = end_ - start_;
		} else {
			auto parsedEnd = Util::toInt64(endToken);
			if (parsedEnd > end_ || parsedEnd <= parsedStart) {
				dcdebug("Partial HTTP request: end position not accepted (parsed start: " I64_FMT ", parsed end: " I64_FMT ", file size: " I64_FMT ")\n", parsedStart, parsedEnd, end_);
				return false;
			}

			end_ = parsedEnd;
		}

		// Both values were passed successfully
		start_ = parsedStart;
		return true;
	}

	websocketpp::http::status_code::value FileServer::handleRequest(const string& aResource, const websocketpp::http::parser::request& aRequest,
		string& output_, StringPairList& headers_) noexcept {

		dcdebug("Requesting file %s\n", aResource.c_str());

		// Get the disk path
		string request;
		try {
			if (aResource.length() >= 6 && aResource.compare(0, 6, "/view/") == 0) {
				request = parseViewFilePath(aResource.substr(6));
			} else {
				request = parseResourcePath(aResource, aRequest, headers_);
			}
		} catch (const std::exception& e) {
			output_ = e.what();
			return websocketpp::http::status_code::bad_request;
		}

		auto fileSize = File::getSize(request);
		int64_t startPos = 0, endPos = fileSize - 1;

		auto partialContent = parsePartialRange(aRequest.get_header("Range"), startPos, endPos);

		// Read file
		try {
			File f(request, File::READ, File::OPEN);
			f.setPos(startPos);
			output_ = f.read(endPos + 1);
		} catch (const FileException& e) {
			output_ = e.getError();
			return websocketpp::http::status_code::not_found;
		} catch (const std::bad_alloc&) {
			output_ = "Not enough memory on the server to serve this request";
			return websocketpp::http::status_code::internal_server_error;
		}

		// Get the mime type
		auto type = getMimeType(request);
		if (type) {
			headers_.emplace_back("Content-Type", type);
		}

		if (partialContent) {
			headers_.emplace_back("Content-Range", formatPartialRange(startPos, endPos, fileSize));
			headers_.emplace_back("Accept-Ranges", "bytes");
			return websocketpp::http::status_code::partial_content;
		}

		return partialContent ? websocketpp::http::status_code::partial_content : websocketpp::http::status_code::ok;
	}
}