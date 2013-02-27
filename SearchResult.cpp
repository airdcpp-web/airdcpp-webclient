/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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
#include "SearchResult.h"

#include "UploadManager.h"
#include "Text.h"
#include "User.h"
#include "ClientManager.h"
#include "Client.h"

namespace dcpp {

SearchResult::SearchResult(const string& aPath) : file(aPath), size(0), date(0), slots(0), type(TYPE_DIRECTORY) { }

SearchResult::SearchResult(const HintedUser& aUser, Types aType, uint8_t aSlots, uint8_t aFreeSlots, 
	int64_t aSize, const string& aFilePath, const string& ip, TTHValue aTTH, const string& aToken, time_t aDate, const string& aConnection) :

	file(aFilePath), user(aUser),
	size(aSize), type(aType), slots(aSlots), freeSlots(aFreeSlots), IP(ip),
	tth(aTTH), token(aToken), date(aDate), connection(aConnection) { }

SearchResult::SearchResult(Types aType, int64_t aSize, const string& aFile, const TTHValue& aTTH, time_t aDate) :
	file(aFile), user(HintedUser(ClientManager::getInstance()->getMe(), Util::emptyString)), size(aSize), type(aType), slots(UploadManager::getInstance()->getSlots()), 
	freeSlots(UploadManager::getInstance()->getFreeSlots()),  
	tth(aTTH), date(aDate) { }

string SearchResult::toSR(const Client& c) const {
	// File:		"$SR %s %s%c%s %d/%d%c%s (%s)|"
	// Directory:	"$SR %s %s %d/%d%c%s (%s)|"
	string tmp;
	tmp.reserve(128);
	tmp.append("$SR ", 4);
	tmp.append(Text::fromUtf8(c.getMyNick(), c.getEncoding()));
	tmp.append(1, ' ');
	string acpFile = Text::fromUtf8(file, c.getEncoding());
	if(type == TYPE_FILE) {
		tmp.append(acpFile);
		tmp.append(1, '\x05');
		tmp.append(Util::toString(size));
	} else {
		tmp.append(acpFile, 0, acpFile.length() - 1);
	}
	tmp.append(1, ' ');
	tmp.append(Util::toString(freeSlots));
	tmp.append(1, '/');
	tmp.append(Util::toString(slots));
	tmp.append(1, '\x05');
	tmp.append("TTH:" + getTTH().toBase32());
	tmp.append(" (", 2);
	tmp.append(c.getIpPort());
	tmp.append(")|", 2);
	return tmp;
}

AdcCommand SearchResult::toRES(char type) const {
	AdcCommand cmd(AdcCommand::CMD_RES, type);
	cmd.addParam("SI", Util::toString(size));
	cmd.addParam("SL", Util::toString(freeSlots));
	cmd.addParam("FN", Util::toAdcFile(file));
	if (!SettingsManager::lanMode)
		cmd.addParam("TR", getTTH().toBase32());
	cmd.addParam("DM", Util::toString(date));
	return cmd;
}

string SearchResult::getFileName() const { 
	if(getType() == TYPE_FILE) 
		return Util::getFileName(getFile()); 

	if(getFile().size() < 2)
		return getFile();

	string::size_type i = getFile().rfind('\\', getFile().length() - 2);
	if(i == string::npos)
		return getFile();

	return getFile().substr(i + 1);
}

string SearchResult::getSlotString() const { 
	return Util::toString(getFreeSlots()) + '/' + Util::toString(getSlots()); 
}

int64_t SearchResult::getConnectionInt() const {
	return isNMDC() ? Util::toInt64(connection)*1024*1024/8 : Util::toInt64(connection);
}

string SearchResult::getConnectionStr() const {
	return isNMDC() ? connection : Util::formatBytes(connection) + "/s";
}

int64_t SearchResult::getSpeedPerSlot() const {
	return slots > 0 ? getConnectionInt() / slots : 0;
}

bool SearchResult::SpeedSortOrder::operator()(const SearchResultPtr& lhs, const SearchResultPtr& rhs) const {
	//prefer the one that has free slots
	if (lhs->getFreeSlots() > 0 && rhs->getFreeSlots() == 0)
		return true;

	if (lhs->getFreeSlots() == 0 && rhs->getFreeSlots() > 0)
		return false;


	//both have free slots? compare the per slot speed
	if (lhs->getFreeSlots() && rhs->getFreeSlots())
		return lhs->getFreeSlots() * lhs->getSpeedPerSlot() > rhs->getFreeSlots() * rhs->getSpeedPerSlot();

	//no free slots, choose a random user with the fastest connection available
	return lhs->getConnectionInt() > lhs->getConnectionInt();
}

void SearchResult::pickResults(SearchResultList& aResults, int pickedNum) {
	if (aResults.size() <= pickedNum) {
		//we can pick all matches
	} else {
		//pick the best matches
		sort(aResults.begin(), aResults.end(), SearchResult::SpeedSortOrder());
		aResults.erase(aResults.begin()+pickedNum, aResults.end());
	}
}

}
