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
#include <airdcpp/core/version.h>

#include <airdcpp/hub/HBRIValidation.h>

#include <airdcpp/hub/activity/ActivityManager.h>
#include <airdcpp/protocol/AdcCommand.h>
#include <airdcpp/core/classes/Exception.h>
#include <airdcpp/protocol/ProtocolCommandManager.h>
#include <airdcpp/core/classes/ScopedFunctor.h>
#include <airdcpp/settings/SettingsManager.h>
#include <airdcpp/connection/socket/SSLSocket.h>
#include <airdcpp/core/timer/TimerManager.h>

namespace dcpp {

HBRIValidator::HBRISocket::HBRISocket(bool aV6, bool aSecure, bool& aStopping) : v6(aV6), stopping(aStopping) {
	initSocket(aSecure);
}

void HBRIValidator::HBRISocket::initSocket(bool aSecure) {
	socket = aSecure ?
		make_unique<SSLSocket>(CryptoManager::SSL_CLIENT, SETTING(ALLOW_UNTRUSTED_HUBS), Util::emptyString) :
		make_unique<Socket>(Socket::TYPE_TCP);

	if (v6) {
		socket->setLocalIp6(SETTING(BIND_ADDRESS6));
		socket->setV4only(false);
	} else {
		socket->setLocalIp4(SETTING(BIND_ADDRESS));
		socket->setV4only(true);
	}
}

bool HBRIValidator::HBRISocket::connect(const string& aIP, const string& aPort) {
	port = aPort;

	socket->connect(AddressInfo(aIP, v6 ? AddressInfo::TYPE_V6 : AddressInfo::TYPE_V4), aPort);

	auto endTime = GET_TICK() + 10000;
	bool connSucceeded = false;
	while (!(connSucceeded = socket->waitConnected(100)) && endTime >= GET_TICK()) {
		if (stopping) return false;
	}

	return connSucceeded;
}

void HBRIValidator::HBRISocket::send(const string& aData) {
	COMMAND_DEBUG(aData, ProtocolCommandManager::TYPE_HUB, ProtocolCommandManager::OUTGOING, socket->getIp() + ":" + port);
	socket->write(aData);
}

bool HBRIValidator::HBRISocket::read(string& data_) {
	boost::scoped_array<char> buf(new char[8192]);
	auto endTime = GET_TICK() + 10000;
	while (endTime >= GET_TICK()) {
		int read = socket->read(&buf[0], 8192);
		if (read <= 0) {
			if (stopping) {
				return false;
			}

			Thread::sleep(50);
			continue;
		}

		// We got our reply
		data_ = string(&buf[0], read);

		COMMAND_DEBUG(data_, ProtocolCommandManager::TYPE_HUB, ProtocolCommandManager::INCOMING, socket->getIp() + ":" + port);
		return true;
	}

	return false;
}

void HBRIValidator::validateHBRIResponse(const string& aResponse) {
	AdcCommand responseCmd(aResponse);
	if (responseCmd.getParameters().size() < 2) {
		throw Exception(STRING(INVALID_HUB_RESPONSE));
	}

	int severity = Util::toInt(responseCmd.getParam(0).substr(0, 1));
	if (responseCmd.getParam(0).size() != 3) {
		throw Exception(STRING(INVALID_HUB_RESPONSE));
	}

	if (severity != AdcCommand::SUCCESS) {
		throw Exception(responseCmd.getParam(1));
	}
}

bool HBRIValidator::runValidation(const ConnectInfo& aConnectInfo, const string& aRequest) {
	// Connect socket
	auto hbriSocket = HBRISocket(aConnectInfo.v6, aConnectInfo.secure, stopValidation);
	if (!hbriSocket.connect(aConnectInfo.ip, aConnectInfo.port)) {
		return false;
	}

	// Send our request
	hbriSocket.send(aRequest);

	// Read response
	string responseStr;
	if (!hbriSocket.read(responseStr)) {
		return false;
	}

	validateHBRIResponse(responseStr);
	return true;
}

void HBRIValidator::stopAndWait() noexcept {
	if (hbriThread->joinable()) {
		dcdebug("HBRI: aborting validation\n");
		stopValidation = true;

		hbriThread->join();
	}
}

HBRIValidator::HBRIValidator(const ConnectInfo& aConnectInfo, const string& aRequest, const LogMessageF& aMessageF) {
	hbriThread = make_unique<jthread>([this, aConnectInfo, aRequest, aMessageF] {
		dcdebug(
			"HBRI: starting validation (IP: %s, port: %s, v6: %s, secure: %s)\n", 
			aConnectInfo.ip.c_str(), aConnectInfo.port.c_str(), aConnectInfo.v6 ? "true" : "false", aConnectInfo.secure ? "true" : "false"
		);

		try {
			if (!runValidation(aConnectInfo, aRequest)) {
				if (!stopValidation) {
					throw Exception(STRING(CONNECTION_TIMEOUT));
				}

				dcdebug("HBRI: validation aborted\n");
				return;
			}

			dcdebug("HBRI: validation completed\n");
			aMessageF(STRING(VALIDATION_SUCCEEDED), LogMessage::SEV_INFO);
		} catch (const Exception& e) {
			dcdebug("HBRI: validation failed (%s)\n", e.getError().c_str());
			aMessageF(STRING_F(HBRI_VALIDATION_FAILED, e.getError() % (aConnectInfo.v6 ? "IPv6" : "IPv4")), LogMessage::SEV_ERROR);
		}
	});
}

} // namespace dcpp