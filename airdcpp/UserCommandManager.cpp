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
#include "UserCommandManager.h"

#include "ClientManager.h"
#include "FavoriteManager.h"
#include "ResourceManager.h"
#include "SimpleXML.h"
#include "UserCommand.h"

namespace dcpp {

using ranges::find_if;

UserCommandManager::UserCommandManager() {
	FavoriteManager::getInstance()->addListener(this);
}

UserCommandManager::~UserCommandManager() {
	FavoriteManager::getInstance()->removeListener(this);
}

UserCommand UserCommandManager::addUserCommand(int type, int ctx, Flags::MaskType flags, const string& name, const string& command, const string& to, const string& hub) noexcept {
	
	// The following management is to protect users against malicious hubs or clients.
	// Hubs (or clients) can send an arbitrary amount of user commands, which means that there is a possibility that
	// the client will need to manage thousands and thousands of user commands.
	// This can naturally cause problems with memory etc, so the client may even crash at some point.
	// The following management tries to remedy this problem by doing two things;
	// a) Replaces previous user commands (if they have the same name etc)
	// b) Restricts the amount of user commands that pertain to a particlar hub
	// Note that this management only cares about externally created user commands, 
	// which means that the user themselves can create however large user commands.
	if (flags == UserCommand::FLAG_NOSAVE)
	{
		const int maximumUCs = 2000; // Completely arbitrary
		int externalCommands = 0; // Used to count the number of external commands
		RLock l(cs);
		for (auto& uc : userCommands) {
			if ((uc.isSet(UserCommand::FLAG_NOSAVE)) &&	// Only care about external commands...
				(uc.getHub() == hub))	// ... and those pertaining to this particular hub.
			{
				++externalCommands;

				// If the UC is generally identical otherwise, change the command
				if ((uc.getName() == name) &&
					(uc.getCtx() == ctx) &&
					(uc.getType() == type) &&
					(uc.isSet(flags)) &&
					(uc.getTo() == to))
				{
					uc.setCommand(command);
					return uc;
				}
			}

		}

		// Validate if there's too many user commands
		if (maximumUCs <= externalCommands)
		{
			return userCommands.back();
		}
	}
	
	// No dupes, add it...
	auto cmd = UserCommand(lastId++, type, ctx, flags, name, command, to, hub);

	{
		WLock l(cs);
		userCommands.emplace_back(cmd);
	}

	if(!cmd.isSet(UserCommand::FLAG_NOSAVE)) 
		setDirty();

	return cmd;
}

bool UserCommandManager::getUserCommand(int cid, UserCommand& uc) noexcept {
	WLock l(cs);
	for(auto& u: userCommands) {
		if(u.getId() == cid) {
			uc = u;
			return true;
		}
	}
	return false;
}

bool UserCommandManager::moveUserCommand(int cid, int pos) noexcept {
	dcassert(pos == -1 || pos == 1);
	WLock l(cs);
	for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
		if(i->getId() == cid) {
			swap(*i, *(i + pos));
			return true;
		}
	}
	return false;
}

void UserCommandManager::updateUserCommand(const UserCommand& uc) noexcept {
	bool nosave = true;
	{
		WLock l(cs);
		for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
			if(i->getId() == uc.getId()) {
				*i = uc;
				nosave = uc.isSet(UserCommand::FLAG_NOSAVE);
				break;
			}
		}
	}

	if(!nosave)
		setDirty();
}

int UserCommandManager::findUserCommand(const string& aName, const string& aUrl) noexcept {
	RLock l(cs);
	for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
		if(i->getName() == aName && i->getHub() == aUrl) {
			return i->getId();
		}
	}
	return -1;
}

void UserCommandManager::removeUserCommand(int cid) noexcept {
	bool nosave = true;
	{
		WLock l(cs);
		for(auto i = userCommands.begin(); i != userCommands.end(); ++i) {
			if(i->getId() == cid) {
				nosave = i->isSet(UserCommand::FLAG_NOSAVE);
				userCommands.erase(i);
				break;
			}
		}
	}

	if(!nosave)
		setDirty();
}
void UserCommandManager::removeUserCommand(const string& srv) noexcept {
	WLock l(cs);
	userCommands.erase(std::remove_if(userCommands.begin(), userCommands.end(), [&](const UserCommand& uc) {
		return uc.getHub() == srv && uc.isSet(UserCommand::FLAG_NOSAVE);
	}), userCommands.end());

}

void UserCommandManager::removeHubUserCommands(int ctx, const string& hub) noexcept {
	WLock l(cs);
	for(auto i = userCommands.begin(); i != userCommands.end(); ) {
		if(i->getHub() == hub && i->isSet(UserCommand::FLAG_NOSAVE) && i->getCtx() & ctx) {
			i = userCommands.erase(i);
		} else {
			++i;
		}
	}
}

void UserCommandManager::saveUserCommands(SimpleXML& aXml) const noexcept {
	aXml.addTag("UserCommands");
	aXml.stepIn();

	{
		RLock l(cs);
		for (const auto& i : userCommands) {
			if (!i.isSet(UserCommand::FLAG_NOSAVE)) {
				aXml.addTag("UserCommand");
				aXml.addChildAttrib("Type", i.getType());
				aXml.addChildAttrib("Context", i.getCtx());
				aXml.addChildAttrib("Name", i.getName());
				aXml.addChildAttrib("Command", i.getCommand());
				aXml.addChildAttrib("To", i.getTo());
				aXml.addChildAttrib("Hub", i.getHub());
			}
		}
	}

	aXml.stepOut();
}

void UserCommandManager::on(FavoriteManagerListener::Load, SimpleXML& xml) noexcept {
	// Add NMDC standard op commands
	static const char kickstr[] =
		"$To: %[userNI] From: %[myNI] $<%[myNI]> You are being kicked because: %[kickline:Reason]|<%[myNI]> is kicking %[userNI] because: %[kickline:Reason]|$Kick %[userNI]|";
	addUserCommand(UserCommand::TYPE_RAW_ONCE, UserCommand::CONTEXT_USER | UserCommand::CONTEXT_SEARCH, UserCommand::FLAG_NOSAVE,
		STRING(KICK_USER), kickstr, "", "op");
	static const char kickfilestr[] =
		"$To: %[userNI] From: %[myNI] $<%[myNI]> You are being kicked because: %[kickline:Reason] %[fileFN]|<%[myNI]> is kicking %[userNI] because: %[kickline:Reason] %[fileFN]|$Kick %[userNI]|";
	addUserCommand(UserCommand::TYPE_RAW_ONCE, UserCommand::CONTEXT_SEARCH, UserCommand::FLAG_NOSAVE,
		STRING(KICK_USER_FILE), kickfilestr, "", "op");
	static const char redirstr[] =
		"$OpForceMove $Who:%[userNI]$Where:%[line:Target Server]$Msg:%[line:Message]|";
	addUserCommand(UserCommand::TYPE_RAW_ONCE, UserCommand::CONTEXT_USER | UserCommand::CONTEXT_SEARCH, UserCommand::FLAG_NOSAVE,
		STRING(REDIRECT_USER), redirstr, "", "op");

	loadUserCommands(xml);
}

void UserCommandManager::setDirty() noexcept {
	FavoriteManager::getInstance()->setDirty();
}

void UserCommandManager::on(FavoriteManagerListener::Save, SimpleXML& xml) noexcept {
	saveUserCommands(xml);
}

void UserCommandManager::loadUserCommands(SimpleXML& aXml) {
	if (aXml.findChild("UserCommands")) {
		aXml.stepIn();
		while (aXml.findChild("UserCommand")) {
			addUserCommand(aXml.getIntChildAttrib("Type"), aXml.getIntChildAttrib("Context"), 0, aXml.getChildAttrib("Name"),
				aXml.getChildAttrib("Command"), aXml.getChildAttrib("To"), aXml.getChildAttrib("Hub"));
		}
		aXml.stepOut();
	}

	aXml.resetCurrentChild();
}

UserCommand::List UserCommandManager::getUserCommands(int ctx, const StringList& hubs, bool& op) noexcept {
	vector<bool> isOp(hubs.size());

	for(size_t i = 0; i < hubs.size(); ++i) {
		if(ClientManager::getInstance()->isOp(ClientManager::getInstance()->getMe(), hubs[i])) {
			isOp[i] = true;
			op = true; // ugly hack
		}
	}

	RLock l(cs);
	UserCommand::List lst;
	for(const auto& uc: userCommands) {
		if(!(uc.getCtx() & ctx)) {
			continue;
		}

		for(size_t j = 0; j < hubs.size(); ++j) {
			const string& hub = hubs[j];
			bool hubAdc = hub.compare(0, 6, "adc://") == 0 || hub.compare(0, 7, "adcs://") == 0;
			bool commandAdc = uc.getHub().compare(0, 6, "adc://") == 0 || uc.getHub().compare(0, 7, "adcs://") == 0;
			if(hubAdc && commandAdc) {
				if((uc.getHub() == "adc://" || uc.getHub() == "adcs://") ||
					((uc.getHub() == "adc://op" || uc.getHub() == "adcs://op") && isOp[j]) ||
					(uc.getHub() == hub) )
				{
					lst.push_back(uc);
					break;
				}
			} else if((!hubAdc && !commandAdc) || uc.isChat()) {
				if((uc.getHub().length() == 0) || 
					(uc.getHub() == "op" && isOp[j]) ||
					(uc.getHub() == hub) )
				{
					lst.push_back(uc);
					break;
				}
			}
		}
	}
	return lst;
}

} // namespace dcpp
