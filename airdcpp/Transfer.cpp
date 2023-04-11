/*
 * Copyright (C) 2001-2023 Jacek Sieka, arnetheduck on gmail point com
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
#include "Transfer.h"

#include "UserConnection.h"
#include "ResourceManager.h"
#include "ClientManager.h"
#include "Upload.h"

namespace dcpp {

const string Transfer::names[] = {
	"file", "file", "list", "tthl"
};

const string Transfer::USER_LIST_NAME = "files.xml";
const string Transfer::USER_LIST_NAME_BZ = "files.xml.bz2";

Transfer::Transfer(UserConnection& conn, const string& path_, const TTHValue& tth_) : segment(0, -1),
	path(path_), tth(tth_), userConnection(conn) { }

void Transfer::tick() noexcept {
	WLock l(cs);
	
	uint64_t t = GET_TICK();
	
	if(samples.size() >= 1) {
		int64_t tdiff = samples.back().first - samples.front().first;
		if((tdiff / 1000) > MIN_SECS) {
			while(samples.size() >= MIN_SAMPLES) {
				samples.pop_front();
			}
		}
		
	}
	
	if(samples.size() > 1) {
		if(samples.back().second == pos) {
			// Position hasn't changed, just update the time
			samples.back().first = t;
			return;
		}
	}

	samples.emplace_back(t, pos);
}

int64_t Transfer::getAverageSpeed() const noexcept {
	RLock l(cs);
	if(samples.size() < 2) {
		return 0;
	}
	uint64_t ticks = samples.back().first - samples.front().first;
	int64_t bytes = samples.back().second - samples.front().second;

	return ticks > 0 ? static_cast<int64_t>((static_cast<double>(bytes) / ticks) * 1000.0) : 0;
}

int64_t Transfer::getSecondsLeft(bool wholeFile) const noexcept {
	auto avg = getAverageSpeed();
	int64_t bytesLeft =  (wholeFile ? ((Upload*)this)->getFileSize() : getSegmentSize()) - getPos();
	return (avg > 0) ? bytesLeft / avg : 0;
}

void Transfer::getParams(const UserConnection& aSource, ParamMap& params) const noexcept {
	params["userCID"] = [&] { return  aSource.getUser()->getCID().toBase32(); };
	params["userNI"] = [&] { return ClientManager::getInstance()->getFormatedNicks(aSource.getHintedUser());  };
	params["userI4"] = [&] { return aSource.getRemoteIp(); };

	params["hub"] = [&] { return ClientManager::getInstance()->getFormatedHubNames(aSource.getHintedUser()); };
	params["hubNI"] = [&] { return ClientManager::getInstance()->getFormatedHubNames(aSource.getHintedUser()); };

	params["hubURL"] = [&] { 
		StringList hubs = ClientManager::getInstance()->getHubUrls(aSource.getUser()->getCID());
		if(hubs.empty())
			hubs.push_back(STRING(OFFLINE));
		return Util::listToString(hubs); 
	};

	params["fileSI"] = [&] { return Util::toString(getSegmentSize()); };
	params["fileSIshort"] = [&] { return Util::formatBytes(getSegmentSize()); };
	params["fileSIchunk"] = [&] { return Util::toString(getPos()); };
	params["fileSIchunkshort"] = [&] { return Util::formatBytes(getPos()); };
	params["fileSIactual"] = [&] { return Util::toString(getActual()); };
	params["fileSIactualshort"] = [&] { return Util::formatBytes(getActual()); };
	params["speed"] = [&] { return Util::formatBytes(static_cast<int64_t>(getAverageSpeed())) + "/s"; };
	params["time"] = [&] { return Util::formatSeconds((GET_TICK() - getStart()) / 1000); };
	params["fileTR"] = [&] { return getTTH().toBase32(); };
}

UserPtr Transfer::getUser() noexcept {
	return getUserConnection().getUser();
}

const UserPtr Transfer::getUser() const noexcept {
	return getUserConnection().getUser();
}

HintedUser Transfer::getHintedUser() const noexcept {
	return getUserConnection().getHintedUser();
}

const string& Transfer::getToken() const noexcept {
	return getUserConnection().getToken(); 
}

bool Transfer::isFilelist() const noexcept {
	return type == TYPE_FULL_LIST || type == TYPE_PARTIAL_LIST;
}

void Transfer::appendFlags(OrderedStringSet& flags_) const noexcept {
	if (getUserConnection().isSet(UserConnection::FLAG_MCN1)) {
		flags_.insert("M");
	}

	if (getUserConnection().isSecure()) {
		if (getUserConnection().isSet(UserConnection::FLAG_TRUSTED)) {
			flags_.insert("S");
		} else {
			flags_.insert("U");
		}
	}
}

void Transfer::resetPos() noexcept {
	pos = 0; 
	actual = 0;
	samples.clear();
};

void Transfer::addPos(int64_t aBytes, int64_t aActual) noexcept {
	pos += aBytes; 
	actual+= aActual; 
}

} // namespace dcpp
