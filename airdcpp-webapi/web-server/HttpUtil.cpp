/*
* Copyright (C) 2011-2022 AirDC++ Project
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

#include "stdinc.h"

#include <web-server/HttpUtil.h>

#include <airdcpp/StringTokenizer.h>
#include <airdcpp/Util.h>

#include "boost/algorithm/string/replace.hpp"


namespace webserver {
	using namespace dcpp;

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

	const char* HttpUtil::getMimeType(const string& aFileName) noexcept {
		auto extension = getExtension(aFileName);
		for (int i = 0; mimes[i].ext != NULL; i++) {
			if (extension == mimes[i].ext) {
				return mimes[i].type;
			}
		}

		return nullptr;
	}

	string HttpUtil::getExtension(const string& aResource) noexcept {
		auto extension = Util::getFileExt(aResource);
		if (!extension.empty()) {
			// Strip the dot
			extension = extension.substr(1);
		}

		return extension;
	}

	void HttpUtil::addCacheControlHeader(StringPairList& headers_, int aDaysValid) noexcept {
		headers_.emplace_back("Cache-Control", aDaysValid == 0 ? "no-store" : "max-age=" + Util::toString(aDaysValid * 24 * 60 * 60));
	}

	string HttpUtil::formatPartialRange(int64_t aStartPos, int64_t aEndPos, int64_t aFileSize) noexcept {
		dcassert(aEndPos < aFileSize);
		return "bytes " + Util::toString(aStartPos) + "-" + Util::toString(aEndPos) + "/" + Util::toString(aFileSize);
	}

	// Support partial requests will enhance media file playback
	// This will only support simple range values (unsupported range types will be ignored)
	bool HttpUtil::parsePartialRange(const string& aHeaderData, int64_t& start_, int64_t& end_) noexcept {
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

	bool HttpUtil::unespaceUrl(const std::string& in, std::string& out) noexcept {
		out.clear();
		out.reserve(in.size());
		for (std::size_t i = 0; i < in.size(); ++i) {
			if (in[i] == '%') {
				if (i + 3 <= in.size()) {
					int value = 0;
					std::istringstream is(in.substr(i + 1, 2));
					if (is >> std::hex >> value) {
						out += static_cast<char>(value);
						i += 2;
					} else {
						return false;
					}
				} else {
					return false;
				}
			} else if (in[i] == '+') {
				out += ' ';
			} else {
				out += in[i];
			}
		}

		return true;
	}

	bool HttpUtil::isStatusOk(int aCode) noexcept {
		return aCode >= 200 && aCode <= 299;
	}

	bool HttpUtil::parseStatus(const string& aResponse, int& code_, string& text_) noexcept {
		if (aResponse.length() < 6 || aResponse.compare(0, 6, "HTTP/1") != 0) {
			return false;
		}

		auto start = aResponse.find(' ');
		if (start == string::npos) {
			return false;
		}

		auto end = aResponse.find(' ', start + 1);
		if (end == string::npos) {
			return false;
		}

		code_ = Util::toInt(aResponse.substr(start + 1, end));
		text_ = aResponse.substr(end + 1);
		return true;
	}

	string HttpUtil::parseAuthToken(const websocketpp::http::parser::request& aRequest) noexcept {
		// Support custom header name as reverse proxy with basic auth would replace the regular Authorization header
		// https://github.com/airdcpp-web/airdcpp-webclient/issues/330
		auto ret = aRequest.get_header("X-Authorization");
		if (!ret.empty()) {
			return ret;
		}

		return aRequest.get_header("Authorization");
	}
}