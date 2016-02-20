/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/QueueItemBase.h>
#include <airdcpp/MerkleTree.h>
#include <airdcpp/TargetUtil.h>

namespace webserver {
	typedef std::function<api_return(const string& aTarget, TargetUtil::TargetType aTargetType, QueueItemBase::Priority aPriority)> DownloadHandler;

	class Deserializer {
	public:
		static CID parseCID(const string& aCID);
		static TTHValue parseTTH(const string& aTTH);

		static UserPtr deserializeUser(const json& aJson, bool aAllowMe = false, const string& aFieldName = "user");
		static HintedUser deserializeHintedUser(const json& aJson, bool aAllowMe = false, const string& aFieldName = "user");
		static TTHValue deserializeTTH(const json& aJson);
		static QueueItemBase::Priority deserializePriority(const json& aJson, bool allowDefault);

		static void deserializeDownloadParams(const json& aJson, string& targetDirectory_, string& targetName_, TargetUtil::TargetType& targetType_, QueueItemBase::Priority& priority_);

		// Returns all connected hubs if the list is not found from the JSON
		static StringList deserializeHubUrls(const json& aJson);
	private:
		static UserPtr parseUser(const json& aJson, bool aAllowMe = false);
	};
}

#endif