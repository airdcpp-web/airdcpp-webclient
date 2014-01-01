/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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
#include "SimpleXML.h"
#include "HubSettings.h"

namespace dcpp {

const string HubSettings::stringNames[StringCount] = {
	"Nick", "UserDescription", "Email", "UserIp", "UserIp6", "AwayMessage", "Encoding" // not "Description" or "NmdcEncoding" for compat with prev fav hub lists
};
const string HubSettings::boolNames[BoolCount] = {
	"ShowJoins", "FavShowJoins", "LogMainChat", "ShowChatNotify", "AcceptFailovers"
};
const string HubSettings::intNames[IntCount] = {
	"MinSearchInterval", "IncomingConnections", "IncomingConnections6"
};

namespace {
inline bool defined(const string& s) { return !s.empty(); }
inline bool defined(tribool b) { return !indeterminate(b); }
inline bool defined(int b) { return b > numeric_limits<int>::min(); }
}

int HubSettings::getMinInt() { 
	return numeric_limits<int>::min(); 
}

HubSettings::HubSettings() {
	// tribools default to false; init them to an indeterminate value.
	for(auto& setting: bools) {
		setting = indeterminate;
	}

	for(auto& setting: ints) {
		setting = getMinInt();
	}
}

const string& HubSettings::get(HubStrSetting setting) const {
	return strings[setting - HubStrFirst];
}

const tribool& HubSettings::get(HubBoolSetting setting) const {
	return bools[setting - HubBoolFirst];
}

const int& HubSettings::get(HubIntSetting setting) const {
	return ints[setting - HubIntFirst];
}

string& HubSettings::get(HubStrSetting setting) {
	return strings[setting - HubStrFirst];
}

tribool& HubSettings::get(HubBoolSetting setting) {
	return bools[setting - HubBoolFirst];
}

int& HubSettings::get(HubIntSetting setting) {
	return ints[setting - HubIntFirst];
}

void HubSettings::merge(const HubSettings& sub) {
	for(uint8_t i = 0; i < StringCount; ++i) {
		if(defined(sub.strings[i])) {
			strings[i] = sub.strings[i];
		}
	}
	for(uint8_t i = 0; i < BoolCount; ++i) {
		if(defined(sub.bools[i])) {
			bools[i] = sub.bools[i];
		}
	}
	for(uint8_t i = 0; i < IntCount; ++i) {
		if(defined(sub.ints[i])) {
			ints[i] = sub.ints[i];
		}
	}
}

void HubSettings::load(SimpleXML& xml) {
	for(uint8_t i = 0; i < StringCount; ++i) {
		strings[i] = xml.getChildAttrib(stringNames[i]);
	}
	for(uint8_t i = 0; i < BoolCount; ++i) {
		bools[i] = to3boolXml(xml.getIntChildAttrib(boolNames[i]));
	}
	for(uint8_t i = 0; i < IntCount; ++i) {
		auto tmp = xml.getChildAttrib(intNames[i]);
		if (!tmp.empty())
			ints[i] = Util::toInt(tmp);
	}
}

void HubSettings::save(SimpleXML& xml) const {
	for(uint8_t i = 0; i < StringCount; ++i) {
		if(defined(strings[i])) {
			xml.addChildAttrib(stringNames[i], strings[i]);
		}
	}
	for(uint8_t i = 0; i < BoolCount; ++i) {
		if(defined(bools[i])) {
			xml.addChildAttrib(boolNames[i], toIntXml(bools[i]));
		}
	}
	for(uint8_t i = 0; i < IntCount; ++i) {
		if(defined(ints[i])) {
			xml.addChildAttrib(intNames[i], ints[i]);
		}
	}
}

} // namespace dcpp
