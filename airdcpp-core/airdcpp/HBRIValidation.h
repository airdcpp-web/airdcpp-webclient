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

#ifndef DCPLUSPLUS_DCPP_HBRI_VALIDATION_H
#define DCPLUSPLUS_DCPP_HBRI_VALIDATION_H

#include <airdcpp/typedefs.h>

#include <airdcpp/Message.h>

#include <thread>

namespace dcpp {

class Socket;
class HBRIValidator {
public:
	struct ConnectInfo {
		ConnectInfo(bool aV6, bool aSecure) : v6(aV6), secure(aSecure) {}

		string ip;
		string port;
		bool v6 = false;
		bool secure = false;
	};

	HBRIValidator(const ConnectInfo& aConnectInfo, const string& aRequest, const LogMessageF& aMessageF);

	void stopAndWait() noexcept;
private:
	// Run the validations, return false in case of a timeout (or when aborted) and throws for other errors
	bool runValidation(const ConnectInfo& aConnectInfo, const string& aRequest);

	class HBRISocket {
	public:
		HBRISocket(bool v6, bool aSecure, bool& aStopping);

		bool connect(const string& aIP, const string& aPort);
		void initSocket(bool aSecure);

		void send(const string& aData);
		bool read(string& data_);
	private:
		unique_ptr<Socket> socket;
		string port;

		const bool v6;
		bool& stopping;
	};

	bool stopValidation = false;
	static void validateHBRIResponse(const string& aResponse);

	unique_ptr<std::jthread> hbriThread;
};

} // namespace dcpp

#endif // !defined(ADC_HUB_H)