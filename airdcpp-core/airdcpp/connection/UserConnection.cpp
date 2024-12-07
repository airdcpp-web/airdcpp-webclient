/* 
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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
#include <airdcpp/connection/UserConnection.h>

#include <airdcpp/hub/ClientManager.h>
#include <airdcpp/core/localization/ResourceManager.h>

#include <airdcpp/protocol/AdcCommand.h>
#include <airdcpp/transfer/Transfer.h>
#include <airdcpp/protocol/ProtocolCommandManager.h>
#include <airdcpp/favorites/FavoriteManager.h>
#include <airdcpp/message/Message.h>
#include <airdcpp/util/text/StringTokenizer.h>


#include <airdcpp/transfer/download/Download.h>

namespace dcpp {

IncrementingIdCounter<UserConnectionToken> UserConnection::idCounter;

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
const string UserConnection::FEATURE_ADC_CPMI = "CPMI";

const string UserConnection::FILE_NOT_AVAILABLE = "File Not Available";

const string UserConnection::UPLOAD = "Upload";
const string UserConnection::DOWNLOAD = "Download";

void UserConnection::on(BufferedSocketListener::Line, const string& aLine) noexcept {

	COMMAND_DEBUG(aLine, ProtocolCommandManager::TYPE_CLIENT, ProtocolCommandManager::INCOMING, getRemoteIp());
	
	if(aLine.length() < 2) {
		fire(UserConnectionListener::ProtocolError(), this, STRING(MALFORMED_DATA));
		return;
	}

	if(aLine[0] == 'C' && !isSet(FLAG_NMDC)) {
		if(!Text::validateUtf8(aLine)) {
			fire(UserConnectionListener::ProtocolError(), this, STRING(UTF_VALIDATION_ERROR));
			return;
		}
		dispatch(aLine, [this](const AdcCommand& aCmd) {
			ProtocolCommandManager::getInstance()->fire(ProtocolCommandManagerListener::IncomingTCPCommand (), aCmd, getRemoteIp(), getHintedUser());
		});
		return;
	} else if(aLine[0] == '$') {
		onNmdcLine(aLine);
		setFlag(FLAG_NMDC);
	} else {
		// We shouldn't be here?
		fire(UserConnectionListener::ProtocolError(), this, STRING(MALFORMED_DATA));
		return;
	}
}

void UserConnection::onNmdcLine(const string & aLine) noexcept {
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
		x = param.find(' ');
		if(x != string::npos) {
			fire(UserConnectionListener::Direction(), this, param.substr(0, x), param.substr(x+1));
		}
	} else if(cmd == "Error") {
		if (Util::stricmp(param.c_str(), FILE_NOT_AVAILABLE) == 0 ||
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
    	dispatch(aLine, true, nullptr);
	} else if (cmd == "ListLen") {
		if(!param.empty()) {
			fire(UserConnectionListener::ListLength(), this, param);
		}
	} else {
		fire(UserConnectionListener::ProtocolError(), this, STRING(MALFORMED_DATA));
	}
}

int64_t UserConnection::getChunkSize() const noexcept {
	int64_t min_seg_size = (SETTING(MIN_SEGMENT_SIZE)*1024);
	if(chunkSize < min_seg_size) {
		return min_seg_size;
	}else{
		return chunkSize; 
	}
}

void UserConnection::setThreadPriority(Thread::Priority aPriority) {
	socket->setThreadPriority(aPriority);
}

bool UserConnection::isMCN() const noexcept {
	return supports.includes(FEATURE_ADC_MCN1);
}

void UserConnection::setUseLimiter(bool aEnabled) noexcept {
	if (socket) {
		socket->setUseLimiter(aEnabled);
	}
}

void UserConnection::setState(States aNewState) noexcept {
	if (aNewState == state) {
		return;
	}

	state = aNewState;
	callAsync([this] {
		fire(UserConnectionListener::State(), this);
	});
}

void UserConnection::setUser(const UserPtr& aUser) noexcept {
	user = aUser;

	if (aUser && socket) {
		socket->callAsync([this] {
			fire(UserConnectionListener::UserSet(), this);
		});
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
		sendHooked(cmd);
	}
}

void UserConnection::initSocket() {
	dcassert(!socket);
	socket = BufferedSocket::getSocket(0);
	socket->setUseLimiter(true);
	socket->addListener(this);
}

void UserConnection::connect(const AddressInfo& aServer, const SocketConnectOptions& aOptions, const string& localPort, const UserPtr& aUser /*nullptr*/) {
	initSocket();

	//string expKP;
	if (aUser) {
		// @see UserConnection::accept, additionally opt to treat connections in both directions identically to avoid unforseen issues
		//expKP = ClientManager::getInstance()->getField(aUser->getCID(), hubUrl, "KP");
		setUser(aUser);
	}

	socket->connect(aServer, aOptions, localPort, /*SETTING(ALLOW_UNTRUSTED_CLIENTS)*/ true, true);
}

void UserConnection::accept(const Socket& aServer, bool aSecure, const BufferedSocket::SocketAcceptFloodF& aFloodCheckF) {
	initSocket();

	/*
	Technically only one side needs to verify KeyPrint, 
	also since we most likely requested to be connected to (and we have insufficient info otherwise) deal with TLS options check post handshake
	-> SSLSocket::verifyKeyprint does full certificate verification after INF
	*/
	socket->accept(aServer, aSecure, true, aFloodCheckF);
}

void UserConnection::inf(bool withToken, int mcnSlots) { 
	AdcCommand c(AdcCommand::CMD_INF);
	c.addParam("ID", ClientManager::getInstance()->getMyCID().toBase32());
	if(mcnSlots > 0)
		c.addParam("CO", Util::toString(mcnSlots));
	if(withToken) {
		c.addParam("TO", getConnectToken());
	}
	if (isSet(FLAG_PM)) {
		c.addParam("PM", "1");
	}
	sendHooked(c);
}

void UserConnection::get(const string& aType, const string& aName, const int64_t aStart, const int64_t aBytes) {
	sendHooked(
		AdcCommand(AdcCommand::CMD_GET)
			.addParam(aType)
			.addParam(aName)
			.addParam(Util::toString(aStart))
			.addParam(Util::toString(aBytes))
	); 
}

void UserConnection::snd(const string& aType, const string& aName, const int64_t aStart, const int64_t aBytes) {
	sendHooked(
		AdcCommand(AdcCommand::CMD_SND)
			.addParam(aType)
			.addParam(aName)
			.addParam(Util::toString(aStart))
			.addParam(Util::toString(aBytes))
	); 
}

bool UserConnection::sendHooked(const AdcCommand& c, CallerPtr aOwner, string& error_) {
	AdcCommand::ParamMap params;
	auto isNmdc = isSet(FLAG_NMDC);
	if (!isNmdc) {
		{
			try {
				auto results = ClientManager::getInstance()->outgoingTcpCommandHook.runHooksDataThrow(aOwner, c, *this);
				params = ActionHook<AdcCommand::ParamMap>::normalizeMap(results);
			} catch (const HookRejectException& e) {
				error_ = ActionHookRejection::formatError(e.getRejection());
				return false;
			}
		}

		ProtocolCommandManager::getInstance()->fire(ProtocolCommandManagerListener::OutgoingTCPCommand(), c, *this);
	}

	if (!params.empty()) {
		send(AdcCommand(c).addParams(params).toString(0, isNmdc));
	} else {
		send(c.toString(0, isNmdc));
	}
	return true;
}

bool UserConnection::sendPrivateMessageHooked(const OutgoingChatMessage& aMessage, string& error_) {
	auto error = ClientManager::getInstance()->outgoingPrivateMessageHook.runHooksError(aMessage.owner, aMessage, getHintedUser(), true);
	if (error) {
		error_ = ActionHookRejection::formatError(error);
		return false;
	}

	if (Util::isChatCommand(aMessage.text)) {
		return false;
	}

	AdcCommand c(AdcCommand::CMD_MSG);
	c.addParam(aMessage.text);
	if (aMessage.thirdPerson) {
		c.addParam("ME", "1");
	}

	if (!sendHooked(c, aMessage.owner, error_)) {
		return false;
	}

	// simulate an echo message.
	callAsync([=, this]{ handlePM(c, true); });
	return true;
}

void UserConnection::handle(AdcCommand::MSG t, const AdcCommand& c) {
	handlePM(c, false);

	fire(t, this, c);
}

void UserConnection::handle(AdcCommand::PMI t, const AdcCommand& c) {
	fire(t, this, c);
}


void UserConnection::handlePM(const AdcCommand& c, bool aEcho) noexcept {
	const string& message = c.getParam(0);

	auto cm = ClientManager::getInstance();
	auto peer = cm->findOnlineUser(user->getCID(), getHubUrl());
	//try to use the same hub so nicks match to a hub, not the perfect solution for CCPM, nicks keep changing when hubs go offline.
	if(peer && peer->getHubUrl() != hubUrl) 
		setHubUrl(peer->getHubUrl());
	auto me = cm->findOnlineUser(cm->getMyCID(), getHubUrl());

	if (!me || !peer){ //ChatMessage cant be formatted without the OnlineUser!
		disconnect(true);
		return;
	}

	if (aEcho) {
		std::swap(peer, me);
	}

	string tmp;

	auto msg = std::make_shared<ChatMessage>(message, peer, me, peer);
	msg->setThirdPerson(c.hasFlag("ME", 1));
	if (c.getParam("TS", 1, tmp)) {
		msg->setTime(Util::toTimeT(tmp));
	}

	if (!ClientManager::processChatMessage(msg, me->getIdentity(), ClientManager::getInstance()->incomingPrivateMessageHook)) {
		disconnect(true);
		return;
	}

	fire(UserConnectionListener::PrivateMessage(), this, msg);
}

void UserConnection::sup(const StringList& features) {
	AdcCommand c(AdcCommand::CMD_SUP);
	for(const auto& f: features)
		c.addParam(f);
	sendHooked(c);
}

void UserConnection::sendError(const std::string& msg /*FILE_NOT_AVAILABLE*/, AdcCommand::Error aError /*AdcCommand::ERROR_FILE_NOT_AVAILABLE*/) {
	if (isSet(FLAG_NMDC)) {
		send("$Error " + msg + "|");
	} else {
		sendHooked(AdcCommand(AdcCommand::SEV_RECOVERABLE, aError, msg));
	}
}

void UserConnection::sendSupports(const StringList& feat) {
	string x;
	for(const auto& f: feat)
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

void UserConnection::on(BufferedSocketListener::Connected) noexcept {
	lastActivity = GET_TICK();
    fire(UserConnectionListener::Connected(), this); 
}

void UserConnection::on(BufferedSocketListener::Data, uint8_t* data, size_t len) noexcept {
	lastActivity = GET_TICK(); 
	fire(UserConnectionListener::Data(), this, data, len); 
}

void UserConnection::on(BufferedSocketListener::BytesSent, size_t bytes, size_t actual) noexcept {
	lastActivity = GET_TICK();
	fire(UserConnectionListener::BytesSent(), this, bytes, actual); 
}

void UserConnection::on(BufferedSocketListener::ModeChange) noexcept {
	lastActivity = GET_TICK(); 
	fire(UserConnectionListener::ModeChange(), this); 
}

void UserConnection::on(BufferedSocketListener::TransmitDone) noexcept {
	fire(UserConnectionListener::TransmitDone(), this);
}

void UserConnection::on(BufferedSocketListener::Failed, const string& aLine) noexcept {
	//setState(STATE_UNCONNECTED);  // let the listeners to see the old state
	fire(UserConnectionListener::Failed(), this, aLine);

	delete this;	
}

// # ms we should aim for per segment
static const int64_t SEGMENT_TIME = 120*1000;
static const int64_t MIN_CHUNK_SIZE = 64*1024;

void UserConnection::updateChunkSize(int64_t leafSize, int64_t lastChunk, uint64_t ticks) noexcept {
	
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
	COMMAND_DEBUG(aString, ProtocolCommandManager::TYPE_CLIENT, ProtocolCommandManager::OUTGOING, getRemoteIp());
	socket->write(aString);
}

UserConnection::UserConnection() noexcept : encoding(SETTING(NMDC_ENCODING)), download(nullptr), token(idCounter.next()) {
}

UserConnection::~UserConnection() {
	BufferedSocket::putSocket(socket);
	dcdebug("User connection %s was deleted\n", getConnectToken().c_str());
}

} // namespace dcpp
