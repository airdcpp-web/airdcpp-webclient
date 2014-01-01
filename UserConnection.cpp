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
#include "UserConnection.h"

#include "ClientManager.h"
#include "ResourceManager.h"

#include "StringTokenizer.h"
#include "AdcCommand.h"
#include "Transfer.h"
#include "DebugManager.h"
#include "FavoriteManager.h"

#include "Download.h"

namespace dcpp {

const string UserConnection::FEATURE_MINISLOTS = "MiniSlots";
const string UserConnection::FEATURE_XML_BZLIST = "XmlBZList";
const string UserConnection::FEATURE_ADCGET = "ADCGet";
const string UserConnection::FEATURE_ZLIB_GET = "ZLIG";
const string UserConnection::FEATURE_TTHL = "TTHL";
const string UserConnection::FEATURE_TTHF = "TTHF";
const string UserConnection::FEATURE_ADC_BAS0 = "BAS0";
const string UserConnection::FEATURE_ADC_BASE = "BASE";
const string UserConnection::FEATURE_ADC_BZIP = "BZIP";
const string UserConnection::FEATURE_ADC_TIGR = "TIGR";
const string UserConnection::FEATURE_ADC_MCN1 = "MCN1";
const string UserConnection::FEATURE_ADC_UBN1 = "UBN1";

const string UserConnection::FILE_NOT_AVAILABLE = "File Not Available";

const string UserConnection::UPLOAD = "Upload";
const string UserConnection::DOWNLOAD = "Download";

const string UserConnection::FEATURE_AIRDC = "AIRDC";

void UserConnection::on(BufferedSocketListener::Line, const string& aLine) throw () {

	COMMAND_DEBUG(aLine, DebugManager::TYPE_CLIENT, DebugManager::INCOMING, getRemoteIp());
	
	if(aLine.length() < 2) {
		fire(UserConnectionListener::ProtocolError(), this, "Invalid data"); // TODO: translate
		return;
	}

	if(aLine[0] == 'C' && !isSet(FLAG_NMDC)) {
		if(!Text::validateUtf8(aLine)) {
			fire(UserConnectionListener::ProtocolError(), this, "Non-UTF-8 data in an ADC connection");  // TODO: translate
			return;
		}
		dispatch(aLine);
		return;
	} else if(aLine[0] == '$') {
		setFlag(FLAG_NMDC);
	} else {
		// We shouldn't be here?
		fire(UserConnectionListener::ProtocolError(), this, "Invalid data");  // TODO: translate
		return;
	}

	string cmd;
	string param;

	string::size_type x;
                
	if( (x = aLine.find(' ')) == string::npos) {
		cmd = aLine.substr(1);
	} else {
		cmd = aLine.substr(1, x - 1);
		param = aLine.substr(x+1);
    }
    
	if(cmd == "MyNick") {
		if(!param.empty())
			fire(UserConnectionListener::MyNick(), this, param);
	} else if(cmd == "Direction") {
		x = param.find(" ");
		if(x != string::npos) {
			fire(UserConnectionListener::Direction(), this, param.substr(0, x), param.substr(x+1));
		}
	} else if(cmd == "Error") {
		if(Util::stricmp(param.c_str(), FILE_NOT_AVAILABLE) == 0 ||
			param.rfind(/*path/file*/" no more exists") != string::npos) { 
    		fire(UserConnectionListener::FileNotAvailable(), this);
    	} else {
			fire(UserConnectionListener::ProtocolError(), this, param);
	    }
	} else if(cmd == "GetListLen") {
    	fire(UserConnectionListener::GetListLength(), this);
	} else if(cmd == "Get") {
		x = param.find('$');
		if(x != string::npos) {
			fire(UserConnectionListener::Get(), this, Text::toUtf8(param.substr(0, x), encoding), Util::toInt64(param.substr(x+1)) - (int64_t)1);
	    }
	} else if(cmd == "Key") {
		if(!param.empty())
			fire(UserConnectionListener::Key(), this, param);
	} else if(cmd == "Lock") {
		if(!param.empty()) {
			x = param.find(" Pk=");
			if(x != string::npos) {
				fire(UserConnectionListener::CLock(), this, param.substr(0, x));
			} else {
				// Workaround for faulty linux clients...
				x = param.find(' ');
				if(x != string::npos) {
					fire(UserConnectionListener::CLock(), this, param.substr(0, x));
	    		} else {
					fire(UserConnectionListener::CLock(), this, param);
    			}
	        }
       	}
	} else if(cmd == "Send") {
    	fire(UserConnectionListener::Send(), this);
	} else if(cmd == "MaxedOut") {
		fire(UserConnectionListener::MaxedOut(), this, param);
	} else if(cmd == "Supports") {
		if(!param.empty()) {
			fire(UserConnectionListener::Supports(), this, StringTokenizer<string>(param, ' ').getTokens());
	    }
	} else if(cmd.compare(0, 3, "ADC") == 0) {
    	dispatch(aLine, true);
	} else if (cmd == "ListLen") {
		if(!param.empty()) {
			fire(UserConnectionListener::ListLength(), this, param);
		}
	} else {

		
		fire(UserConnectionListener::ProtocolError(), this, "Invalid data"); // TODO: translate
	}
}

void UserConnection::connect(const string& aServer, const string& aPort, const string& localPort, BufferedSocket::NatRoles natRole) {
	dcassert(!socket);

	socket = BufferedSocket::getSocket(0);
	socket->addListener(this);
	socket->connect(aServer, aPort, localPort, natRole, secure, SETTING(ALLOW_UNTRUSTED_CLIENTS), true);
}

int64_t UserConnection::getChunkSize() const {
	int64_t min_seg_size = (SETTING(MIN_SEGMENT_SIZE)*1024);
	if(chunkSize < min_seg_size) {
		return min_seg_size;
	}else{
		return chunkSize; 
	}
}

void UserConnection::setUser(const UserPtr& aUser) {
	user = aUser;
	if (aUser && socket) {
		socket->setUseLimiter(true);
		if (aUser->isSet(User::FAVORITE)) {
			auto u = FavoriteManager::getInstance()->getFavoriteUser(aUser);
			if (u) {
				socket->setUseLimiter(!u->isSet(FavoriteUser::FLAG_SUPERUSER));
			}
		}
	}
}

void UserConnection::maxedOut(size_t qPos /*0*/) {
	bool sendPos = qPos > 0;

	if(isSet(FLAG_NMDC)) {
		send("$MaxedOut" + (sendPos ? (" " + Util::toString(qPos)) : Util::emptyString) + "|");
	} else {
		AdcCommand cmd(AdcCommand::SEV_RECOVERABLE, AdcCommand::ERROR_SLOTS_FULL, "Slots full");
		if(sendPos) {
			cmd.addParam("QP", Util::toString(qPos));
		}
		send(cmd);
	}
}

void UserConnection::accept(const Socket& aServer) {
	dcassert(!socket);
	socket = BufferedSocket::getSocket(0);
	socket->addListener(this);
	socket->accept(aServer, secure, SETTING(ALLOW_UNTRUSTED_CLIENTS));
}

void UserConnection::inf(bool withToken, int mcnSlots) { 
	AdcCommand c(AdcCommand::CMD_INF);
	c.addParam("ID", ClientManager::getInstance()->getMyCID().toBase32());
	if(mcnSlots > 0)
		c.addParam("CO", Util::toString(mcnSlots));
	if(withToken) {
		c.addParam("TO", getToken());
	}
	send(c);
}

void UserConnection::sup(const StringList& features) {
	AdcCommand c(AdcCommand::CMD_SUP);
	for(auto& f: features)
		c.addParam(f);
	send(c);
}

void UserConnection::sendError(const std::string& msg /*FILE_NOT_AVAILABLE*/, AdcCommand::Error aError /*AdcCommand::ERROR_FILE_NOT_AVAILABLE*/) {
	if (isSet(FLAG_NMDC)) {
		send("$Error " + msg + "|");
	} else {
		send(AdcCommand(AdcCommand::SEV_RECOVERABLE, aError, msg));
	}
}

void UserConnection::supports(const StringList& feat) {
	string x;
	for(auto f: feat)
		x += f + ' ';

	send("$Supports " + x + '|');
}

void UserConnection::handle(AdcCommand::STA t, const AdcCommand& c) {
	if(c.getParameters().size() >= 2) {
		const string& code = c.getParam(0);
		if(!code.empty() && code[0] - '0' == AdcCommand::SEV_FATAL) {
			fire(UserConnectionListener::ProtocolError(), this, c.getParam(1));
			return;
		}
	}

	fire(t, this, c);
}

void UserConnection::on(Connected) noexcept {
	lastActivity = GET_TICK();
    fire(UserConnectionListener::Connected(), this); 
}

void UserConnection::on(Data, uint8_t* data, size_t len) noexcept { 
	lastActivity = GET_TICK(); 
	fire(UserConnectionListener::Data(), this, data, len); 
}

void UserConnection::on(BytesSent, size_t bytes, size_t actual) noexcept { 
	lastActivity = GET_TICK();
	fire(UserConnectionListener::BytesSent(), this, bytes, actual); 
}

void UserConnection::on(ModeChange) noexcept { 
	lastActivity = GET_TICK(); 
	fire(UserConnectionListener::ModeChange(), this); 
}

void UserConnection::on(TransmitDone) noexcept {
	fire(UserConnectionListener::TransmitDone(), this);
}

void UserConnection::on(Failed, const string& aLine) noexcept {
	//setState(STATE_UNCONNECTED);  // let the listeners to see the old state
	fire(UserConnectionListener::Failed(), this, aLine);

	delete this;	
}

// # ms we should aim for per segment
static const int64_t SEGMENT_TIME = 120*1000;
static const int64_t MIN_CHUNK_SIZE = 64*1024;

void UserConnection::updateChunkSize(int64_t leafSize, int64_t lastChunk, uint64_t ticks) {
	
	if(chunkSize == 0) {
		chunkSize = std::max((int64_t)64*1024, std::min(lastChunk, (int64_t)1024*1024));
		return;
	}
	
	if(ticks <= 10) {
		// Can't rely on such fast transfers - double
		chunkSize *= 2;
		return;
	}
	
	double lastSpeed = (1000. * lastChunk) / ticks;

	int64_t targetSize = chunkSize;

	// How long current chunk size would take with the last speed...
	double msecs = 1000 * targetSize / lastSpeed;
	
	if(msecs < SEGMENT_TIME / 4) {
		targetSize *= 2;
	} else if(msecs < SEGMENT_TIME / 1.25) {
		targetSize += leafSize;
	} else if(msecs < SEGMENT_TIME * 1.25) {
		// We're close to our target size - don't change it
	} else if(msecs < SEGMENT_TIME * 4) {
		targetSize = std::max(MIN_CHUNK_SIZE, targetSize - leafSize);
	} else {
		targetSize = std::max(MIN_CHUNK_SIZE, targetSize / 2);
	}
	
	chunkSize = targetSize;
}

void UserConnection::send(const string& aString) {
	lastActivity = GET_TICK();
	COMMAND_DEBUG(aString, DebugManager::TYPE_CLIENT, DebugManager::OUTGOING, getRemoteIp());
	socket->write(aString);
}

UserConnection::UserConnection(bool secure_) noexcept : encoding(SETTING(NMDC_ENCODING)), state(STATE_UNCONNECTED),
	lastActivity(0), speed(0), chunkSize(0), secure(secure_), socket(0), slotType(NOSLOT), lastBundle(Util::emptyString), download(nullptr) {
}
} // namespace dcpp