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
#include <airdcpp/HintedUser.h>
#include <airdcpp/MerkleTree.h>
#include <airdcpp/Message.h>
#include <airdcpp/Priority.h>

#include <web-server/JsonUtil.h>


namespace webserver {
	typedef std::function<api_return(const string& aTarget, Priority aPriority)> DownloadHandler;

	class Deserializer {
	public:
		struct OfflineHintedUser : public HintedUser {
			OfflineHintedUser(const UserPtr& aUser, const string& aHubUrl, const string& aNicks) : HintedUser(aUser, aHubUrl), nicks(aNicks) { }

			string nicks;
		};

		static CID parseCID(const string& aCID);

		// Get user with the provided CID
		// Throws if the user is not found
		static UserPtr getUser(const string& aCID, bool aAllowMe);
		static UserPtr getUser(const CID& aCID, bool aAllowMe);

		// Get or create a user
		static UserPtr getOfflineUser(const string& aCID, const string& aNicks, const string& aHubUrl, bool aAllowMe);

		static TTHValue parseTTH(const string& aTTH);
		static HintedUser parseHintedUser(const json& aJson, const string& aFieldName, bool aAllowMe = false);

		static UserPtr parseOfflineUser(const json& aJson, const string& aFieldName, bool aAllowMe = false, const string& aHubUrl = "");
		static OfflineHintedUser parseOfflineHintedUser(const json& aJson, const string& aFieldName, bool aAllowMe = false);

		static UserPtr deserializeUser(const json& aJson, bool aAllowMe, bool aOptional = false);
		static HintedUser deserializeHintedUser(const json& aJson, bool aAllowMe = false, bool aOptional = false, const string& aFieldName = "user");
		static OnlineUserPtr deserializeOnlineUser(const json& aJson, bool aAllowMe = false, const string& aFieldName = "user");
		static TTHValue deserializeTTH(const json& aJson);
		static Priority deserializePriority(const json& aJson, bool allowDefault);

		static void deserializeDownloadParams(const json& aJson, const SessionPtr& aSession, string& targetDirectory_, string& targetName_, Priority& priority_);

		// Returns all connected hubs if the list is not found from the JSON
		static StringList deserializeHubUrls(const json& aJson);
		static ClientPtr deserializeClient(const json& aJson, bool aOptional = false);

		static pair<string, bool> deserializeChatMessage(const json& aJson);
		static pair<string, LogMessage::Severity> deserializeStatusMessage(const json& aJson);

		// Returns the default profile in case no profile was specified
		static ProfileToken deserializeShareProfile(const json& aJson);

		static OptionalProfileToken deserializeOptionalShareProfile(const json& aJson);

		template <typename ItemT>
		using ArrayDeserializerFunc = std::function<ItemT(const json& aJson, const string& aFieldName)>;

		template <typename ItemT>
		static vector<ItemT> deserializeList(const string& aFieldName, const json& aList, const ArrayDeserializerFunc<ItemT>& aF, bool aAllowEmpty) {
			const auto arrayJson = JsonUtil::getArrayField(aFieldName, aList, aAllowEmpty);

			vector<ItemT> ret;
			for (const auto& item: arrayJson) {
				// ret.push_back(aF ? aF(item) : JsonUtil::parseValue<ItemT>(aFieldName, item, false));
				ret.push_back(aF(item, aFieldName));
			}

			return ret;
		}

		static TTHValue tthArrayValueParser(const json& aJson, const string& aFieldName);
		static CID cidArrayValueParser(const json& aJson, const string& aFieldName);
		static HintedUser hintedUserArrayValueParser(const json& aJson, const string& aFieldName);

		template<typename IdT>
		static IdT defaultArrayValueParser(const json& aJson, const string& aFieldName) {
			return JsonUtil::parseValue<IdT>(aFieldName, aJson, false);
		}
	private:
		static LogMessage::Severity parseSeverity(const string& aText);
	};
}

#endif