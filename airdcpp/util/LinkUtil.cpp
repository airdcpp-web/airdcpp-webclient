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

#include <airdcpp/util/LinkUtil.h>
#include <airdcpp/util/Util.h>

#include <boost/algorithm/string/trim.hpp>

namespace dcpp {

boost::regex LinkUtil::urlReg = boost::regex(LinkUtil::getUrlReg());

const string LinkUtil::getUrlReg() noexcept {
	// Keep the protocol section lower case only to avoid false positives (e.g. client tags being formatted as links)
	return R"(((?:(?:[a-z][a-z0-9+.-]*):/{1,3}|(?:[a-z][a-z0-9+.-]*):|www\d{0,3}[.]|magnet:\?[^\s=]+=|[A-Za-z0-9.\-]+[.][A-Za-z]{2,4}/)(?:[^\s()<>]+|\(([^\s()<>]+|(\([^\s()<>]+\)))*\))+(?:\(([^\s()<>]+|(\([^\s()<>]+\)))*\)|[^\s`()\[\]{};:'\".,<>?«»“”‘’]))|(?:[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}))";
}

bool LinkUtil::isAdcHub(const string& aHubUrl) noexcept {
	if(Util::strnicmp("adc://", aHubUrl.c_str(), 6) == 0) {
		return true;
	} else if(Util::strnicmp("adcs://", aHubUrl.c_str(), 7) == 0) {
		return true;
	}
	return false;
}

bool LinkUtil::isSecure(const string& aHubUrl) noexcept {
	return Util::strnicmp("adcs://", aHubUrl.c_str(), 7) == 0 || Util::strnicmp("nmdcs://", aHubUrl.c_str(), 8) == 0;
}

bool LinkUtil::isHubLink(const string& aHubUrl) noexcept {
	return isAdcHub(aHubUrl) || Util::strnicmp("dchub://", aHubUrl.c_str(), 8) == 0;
}

void LinkUtil::sanitizeUrl(string& url) noexcept {
	boost::algorithm::trim_if(url, boost::is_space() || boost::is_any_of("<>\""));
}

string LinkUtil::parseLink(const string& aLink) noexcept {

	// hasScheme: ^[A-Za-z][A-Za-z0-9+.-]*:  OR protocol-relative: ^//
	const boost::regex reHasScheme(R"(^[A-Za-z][A-Za-z0-9+\-.]*:)", boost::regex::perl);
	const boost::regex reProtoRelative(R"(^//)", boost::regex::perl);
	// isEmail: very simple and safe variation
	const boost::regex reEmail(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)", boost::regex::perl);
	// isUnsafeScheme: javascript: | data: | vbscript:
	const boost::regex reUnsafe(R"(^(?:javascript|data|vbscript):)", boost::regex::perl | boost::regex::icase);

	if (!aLink.empty()) {
		const bool hasScheme = boost::regex_search(aLink, reHasScheme) || boost::regex_search(aLink, reProtoRelative);

		if (hasScheme) {
			// Block unsafe schemes
			if (boost::regex_search(aLink, reUnsafe)) {
				return Util::emptyString; // ignore click
			}
			// Expand protocol-relative for ShellExecute compatibility
			if (boost::regex_search(aLink, reProtoRelative)) {
				return "http:" + aLink;
			}
		}
		else {
			// Email without scheme -> mailto:
			if (boost::regex_match(aLink, reEmail)) {
				return "mailto:" + aLink;
			} else {
				// Fallback to HTTPS for bare hosts/domains
				return "http://" + aLink;
			}
		}
	}

	return aLink;
}


/**
 * Decodes a URL the best it can...
 * Default ports:
 * http:// -> port 80
 * dchub:// -> port 411
 */
void LinkUtil::decodeUrl(const string& url, string& protocol, string& host, string& port, string& path, string& query, string& fragment) noexcept {
	auto fragmentEnd = url.size();
	auto fragmentStart = url.rfind('#');

	size_t queryEnd;
	if(fragmentStart == string::npos) {
		queryEnd = fragmentStart = fragmentEnd;
	} else {
		dcdebug("f");
		queryEnd = fragmentStart;
		fragmentStart++;
	}

	auto queryStart = url.rfind('?', queryEnd);
	size_t fileEnd;

	if(queryStart == string::npos) {
		fileEnd = queryStart = queryEnd;
	} else {
		dcdebug("q");
		fileEnd = queryStart;
		queryStart++;
	}

	auto protoStart = 0;
	auto protoEnd = url.find("://", protoStart);

	auto authorityStart = protoEnd == string::npos ? protoStart : protoEnd + 3;
	auto authorityEnd = url.find_first_of("/#?", authorityStart);

	size_t fileStart;
	if(authorityEnd == string::npos) {
		authorityEnd = fileStart = fileEnd;
	} else {
		dcdebug("a");
		fileStart = authorityEnd;
	}

	protocol = (protoEnd == string::npos ? Util::emptyString : url.substr(protoStart, protoEnd - protoStart));

	if(authorityEnd > authorityStart) {
		dcdebug("x");
		size_t portStart = string::npos;
		if(url[authorityStart] == '[') {
			// IPv6?
			auto hostEnd = url.find(']');
			if(hostEnd == string::npos) {
				return;
			}

			host = url.substr(authorityStart + 1, hostEnd - authorityStart - 1);
			if(hostEnd + 1 < url.size() && url[hostEnd + 1] == ':') {
				portStart = hostEnd + 2;
			}
		} else {
			size_t hostEnd;
			portStart = url.find(':', authorityStart);
			if(portStart != string::npos && portStart > authorityEnd) {
				portStart = string::npos;
			}

			if(portStart == string::npos) {
				hostEnd = authorityEnd;
			} else {
				hostEnd = portStart;
				portStart++;
			}

			dcdebug("h");
			host = url.substr(authorityStart, hostEnd - authorityStart);
		}

		if(portStart == string::npos) {
			if(protocol == "http") {
				port = "80";
			} else if(protocol == "https") {
				port = "443";
			} else if(protocol == "dchub"  || protocol.empty()) {
				port = "411";
			}
		} else {
			dcdebug("p");
			port = url.substr(portStart, authorityEnd - portStart);
		}
	}

	dcdebug("\n");
	path = url.substr(fileStart, fileEnd - fileStart);
	query = url.substr(queryStart, queryEnd - queryStart);
	fragment = url.substr(fragmentStart, fragmentEnd - fragmentStart);
}

/*void LinkUtil::parseIpPort(const string& aIpPort, string& ip, string& port) noexcept {
	string::size_type i = aIpPort.rfind(':');
	if (i == string::npos) {
		ip = aIpPort;
	} else {
		ip = aIpPort.substr(0, i);
		port = aIpPort.substr(i + 1);
	}
}*/

map<string, string> LinkUtil::decodeQuery(const string& query) noexcept {
	map<string, string> ret;
	size_t start = 0;
	while(start < query.size()) {
		auto eq = query.find('=', start);
		if(eq == string::npos) {
			break;
		}

		auto param = eq + 1;
		auto end = query.find('&', param);

		if(end == string::npos) {
			end = query.size();
		}

		if(eq > start && end > param) {
			ret[query.substr(start, eq-start)] = query.substr(param, end - param);
		}

		start = end + 1;
	}

	return ret;
}

string LinkUtil::encodeURI(const string& aString, bool aReverse) noexcept {
	// reference: rfc2396
	string tmp = aString;
	if (aReverse) {
		string::size_type idx;
		for(idx = 0; idx < tmp.length(); ++idx) {
			if(tmp.length() > idx + 2 && tmp[idx] == '%' && isxdigit(tmp[idx+1]) && isxdigit(tmp[idx+2])) {
				tmp[idx] = Util::fromHexEscape(tmp.substr(idx+1,2));
				tmp.erase(idx+1, 2);
			} else { // reference: rfc1630, magnet-uri draft
				if(tmp[idx] == '+')
					tmp[idx] = ' ';
			}
		}
	} else {
		const string disallowed = ";/?:@&=+$," // reserved
			                      "<>#%\" "    // delimiters
		                          "{}|\\^[]`"; // unwise
		string::size_type idx, loc;
		for(idx = 0; idx < tmp.length(); ++idx) {
			if(tmp[idx] == ' ') {
				tmp[idx] = '+';
			} else {
				if(tmp[idx] <= 0x1F || tmp[idx] >= 0x7f || (loc = disallowed.find_first_of(tmp[idx])) != string::npos) {
					tmp.replace(idx, 1, Util::toHexEscape(tmp[idx]));
					idx+=2;
				}
			}
		}
	}

	return tmp;
}


}
