/*
 * Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_ADC_COMMAND_H
#define DCPLUSPLUS_DCPP_ADC_COMMAND_H

#include "typedefs.h"

#include "Exception.h"

namespace dcpp {

class CID;

class AdcCommand {
public:
	template<uint32_t T>
	struct Type {
		enum { CMD = T };
	};

	enum Error {
		SUCCESS = 0,
		ERROR_GENERIC = 0,
		ERROR_HUB_GENERIC = 10,
		ERROR_HUB_FULL = 11,
		ERROR_HUB_DISABLED = 12,
		ERROR_LOGIN_GENERIC = 20,
		ERROR_NICK_INVALID = 21,
		ERROR_NICK_TAKEN = 22,
		ERROR_BAD_PASSWORD = 23,
		ERROR_CID_TAKEN = 24,
		ERROR_COMMAND_ACCESS = 25,
		ERROR_REGGED_ONLY = 26,
		ERROR_INVALID_PID = 27,
		ERROR_BANNED_GENERIC = 30,
		ERROR_PERM_BANNED = 31,
		ERROR_TEMP_BANNED = 32,
		ERROR_PROTOCOL_GENERIC = 40,
		ERROR_PROTOCOL_UNSUPPORTED = 41,
		ERROR_CONNECT_FAILED = 42,
		ERROR_INF_MISSING = 43,
		ERROR_BAD_STATE = 44,
		ERROR_FEATURE_MISSING = 45,
		ERROR_BAD_IP = 46,
		ERROR_NO_HUB_HASH = 47,
		ERROR_TRANSFER_GENERIC = 50,
		ERROR_FILE_NOT_AVAILABLE = 51,
		ERROR_FILE_PART_NOT_AVAILABLE = 52,
		ERROR_SLOTS_FULL = 53,
		ERROR_NO_CLIENT_HASH = 54,
		ERROR_HBRI_TIMEOUT = 55,
		ERROR_FILE_ACCESS_DENIED = 60,
		ERROR_UNKNOWN_USER = 61,
		ERROR_TLS_REQUIRED = 62
	};

	enum Severity {
		SEV_SUCCESS = 0,
		SEV_RECOVERABLE = 1,
		SEV_FATAL = 2
	};

	static const char TYPE_BROADCAST = 'B';
	static const char TYPE_CLIENT = 'C';
	static const char TYPE_DIRECT = 'D';
	static const char TYPE_ECHO = 'E';
	static const char TYPE_FEATURE = 'F';
	static const char TYPE_INFO = 'I';
	static const char TYPE_HUB = 'H';
	static const char TYPE_UDP = 'U';

#define C(n, a, b, c) static const uint32_t CMD_##n = (((uint32_t)a) | (((uint32_t)b)<<8) | (((uint32_t)c)<<16)); typedef Type<CMD_##n> n
	// Base commands
	C(SUP, 'S','U','P');
	C(STA, 'S','T','A');
	C(INF, 'I','N','F');
	C(MSG, 'M','S','G');
	C(SCH, 'S','C','H');
	C(RES, 'R','E','S');
	C(CTM, 'C','T','M');
	C(RCM, 'R','C','M');
	C(GPA, 'G','P','A');
	C(PAS, 'P','A','S');
	C(QUI, 'Q','U','I');
	C(GET, 'G','E','T');
	C(GFI, 'G','F','I');
	C(SND, 'S','N','D');
	C(SID, 'S','I','D');
	// Extensions
	C(CMD, 'C','M','D');
	C(NAT, 'N','A','T');
	C(RNT, 'R','N','T');
	C(PSR, 'P','S','R');
	C(ZON, 'Z','O','N');
	C(ZOF, 'Z','O','F');
	C(PBD, 'P','B','D');
	C(UBD, 'U','B','D');
	C(UBN, 'U','B','N');
	C(TCP, 'T','C','P');
	C(PMI, 'P', 'M', 'I');
#undef C

	static const uint32_t HUB_SID = 0xffffffff;		// No client will have this sid

	static uint32_t toFourCC(const char* x) noexcept { return *reinterpret_cast<const uint32_t*>(x); }
	static std::string fromFourCC(uint32_t x) noexcept { return std::string(reinterpret_cast<const char*>(&x), sizeof(x)); }

	explicit AdcCommand(uint32_t aCmd, char aType = TYPE_CLIENT) noexcept;
	explicit AdcCommand(uint32_t aCmd, const uint32_t aTarget, char aType) noexcept;
	explicit AdcCommand(Severity sev, Error err, const string& desc, char aType = TYPE_CLIENT) noexcept;

	// Throws ParseException on errors
	explicit AdcCommand(const string& aLine, bool nmdc = false);

	// Throws ParseException on errors
	void parse(const string& aLine, bool nmdc = false);

	uint32_t getCommand() const noexcept { return cmdInt; }
	char getType() const noexcept { return type; }
	void setType(char t) noexcept { type = t; }
	string getFourCC() const noexcept { string tmp(4, 0); tmp[0] = type; tmp[1] = cmd[0]; tmp[2] = cmd[1]; tmp[3] = cmd[2]; return tmp; }

	const string& getFeatures() const noexcept { return features; }
	AdcCommand& setFeatures(const string& feat) noexcept { features = feat; return *this; }

	StringList& getParameters() noexcept { return parameters; }
	const StringList& getParameters() const noexcept { return parameters; }

	string toString() const noexcept;
	string toString(const CID& aCID) const noexcept;
	string toString(uint32_t sid, bool nmdc = false) const noexcept;

	AdcCommand& addParam(const string& name, const string& value) noexcept {
		parameters.push_back(name);
		parameters.back() += value;
		return *this;
	}
	AdcCommand& addParam(const string& str) noexcept {
		parameters.push_back(str);
		return *this;
	}
	const string& getParam(size_t n) const noexcept;
	/** Return a named parameter where the name is a two-letter code */
	bool getParam(const char* name, size_t start, string& ret) const noexcept;
	bool getParam(const char* name, size_t start, StringList& ret) const noexcept;
	bool hasFlag(const char* name, size_t start) const noexcept;
	static uint16_t toCode(const char* x) noexcept { return *((uint16_t*)x); }

	bool operator==(uint32_t aCmd) const noexcept { return cmdInt == aCmd; }

	static string escape(const string& str, bool old) noexcept;
	uint32_t getTo() const noexcept { return to; }
	AdcCommand& setTo(const uint32_t sid) noexcept { to = sid; return *this; }
	uint32_t getFrom() const noexcept { return from; }
	void setFrom(const uint32_t sid) noexcept { from = sid; }

	static uint32_t toSID(const string& aSID) noexcept { return *reinterpret_cast<const uint32_t*>(aSID.data()); }
	static string fromSID(const uint32_t aSID) noexcept { return string(reinterpret_cast<const char*>(&aSID), sizeof(aSID)); }
private:
	string getHeaderString(const CID& cid) const noexcept;
	string getHeaderString() const noexcept;
	string getHeaderString(uint32_t sid, bool nmdc) const noexcept;
	string getParamString(bool nmdc) const noexcept;
	StringList parameters;
	string features;
	union {
		char cmdChar[4];
		uint8_t cmd[4];
		uint32_t cmdInt;
	};
	uint32_t from = 0;
	uint32_t to = 0;
	char type;

};

template<class T>
class CommandHandler {
public:
	inline void dispatch(const string& aLine) noexcept {
		dispatch(aLine, false);
	}

	template<typename... ArgT>
	void dispatch(const string& aLine, bool aNmdc, ArgT&&... args) noexcept {
		try {
			AdcCommand c(aLine, aNmdc);

#define C(n) case AdcCommand::CMD_##n: ((T*)this)->handle(AdcCommand::n(), c, std::forward<ArgT>(args)...); break;
			switch(c.getCommand()) {
				C(SUP);
				C(STA);
				C(INF);
				C(MSG);
				C(SCH);
				C(RES);
				C(CTM);
				C(RCM);
				C(GPA);
				C(PAS);
				C(QUI);
				C(GET);
				C(GFI);
				C(SND);
				C(SID);
				C(CMD);
				C(NAT);
				C(RNT);
				C(PSR);
				C(PBD);
				C(ZON);
				C(ZOF);
				C(TCP);
				C(PMI);
				C(UBN);
				C(UBD);
			default: 
				dcdebug("Unknown ADC command: %.50s\n", aLine.c_str());
				break;
	#undef C
	
			}
		} catch(const ParseException&) {
			dcdebug("Invalid ADC command: %.50s\n", aLine.c_str());
			return;
		}
	}
};

} // namespace dcpp

#endif // !defined(ADC_COMMAND_H)