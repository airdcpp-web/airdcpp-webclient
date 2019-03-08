/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_DESERIALIZER_H
#define DCPLUSPLUS_DCPP_DESERIALIZER_H

#include <airdcpp/typedefs.h>
#include <airdcpp/MerkleTree.h>
#include <airdcpp/Message.h>
#include <airdcpp/Priority.h>

namespace webserver {
	typedef std::function<api_return(const string& aTarget, Priority aPriority)> DownloadHandler;

	class Deserializer {
	public:
		static CID parseCID(const string& aCID);

		// Get user with the provided CID
		// Throws if the user is not found
		static UserPtr getUser(const string& aCID, bool aAllowMe);
		static UserPtr getUser(const CID& aCID, bool aAllowMe);

		static TTHValue parseTTH(const string& aTTH);

		//static UserPtr deserializeUser(const json& aJson, bool aAllowMe = false, const string& aFieldName = "user", bool aOptional = false);
		static UserPtr deserializeUser(const json& aJson, bool aAllowMe, bool aOptional = false);
		static HintedUser deserializeHintedUser(const json& aJson, bool aAllowMe = false, const string& aFieldName = "user");
		static OnlineUserPtr deserializeOnlineUser(const json& aJson, bool aAllowMe = false, const string& aFieldName = "user");
		static TTHValue deserializeTTH(const json& aJson);
		static Priority deserializePriority(const json& aJson, bool allowDefault);

		static void deserializeDownloadParams(const json& aJson, const SessionPtr& aSession, string& targetDirectory_, string& targetName_, Priority& priority_);

		// Returns all connected hubs if the list is not found from the JSON
		static StringList deserializeHubUrls(const json& aJson);

		static pair<string, bool> deserializeChatMessage(const json& aJson);
		static pair<string, LogMessage::Severity> deserializeStatusMessage(const json& aJson);

		// Returns the default profile in case no profile was specified
		static ProfileToken deserializeShareProfile(const json& aJson);

		static OptionalProfileToken deserializeOptionalShareProfile(const json& aJson);
	private:
		static LogMessage::Severity parseSeverity(const string& aText);
	};
}

#endif