///////////////////////////////////////////////////////////////////////////////
//
//	Handles saving and loading of ignorelists
//
///////////////////////////////////////////////////////////////////////////////
#include "stdinc.h"
#include "IgnoreManager.h"
#include "ClientManager.h"

#include "Util.h"
#include "Wildcards.h"

namespace dcpp {

#define CONFIG_DIR Util::PATH_USER_CONFIG
#define CONFIG_NAME "IgnoredUsers.xml"

void IgnoreManager::load(SimpleXML& aXml) {

	if (aXml.findChild("ChatFilterItems")) {
		aXml.stepIn();
		while (aXml.findChild("ChatFilterItem")) {
			ChatFilterItems.push_back(ChatFilterItem(aXml.getChildAttrib("Nick"), aXml.getChildAttrib("Text"), 
				(StringMatch::Method)aXml.getIntChildAttrib("NickMethod"), (StringMatch::Method)aXml.getIntChildAttrib("TextMethod"), 
				aXml.getBoolChildAttrib("MC"), aXml.getBoolChildAttrib("PM"), aXml.getBoolChildAttrib("Enabled")));
		}
		aXml.stepOut();
	}
	loadUsers();
}

void IgnoreManager::save(SimpleXML& aXml) {
	aXml.addTag("ChatFilterItems");
	aXml.stepIn();

	for (const auto& i : ChatFilterItems) {
		aXml.addTag("ChatFilterItem");
		aXml.addChildAttrib("Nick", i.getNickPattern());
		aXml.addChildAttrib("NickMethod", i.getNickMethod());
		aXml.addChildAttrib("Text", i.getTextPattern());
		aXml.addChildAttrib("TextMethod", i.getTextMethod());
		aXml.addChildAttrib("MC", i.matchMainchat);
		aXml.addChildAttrib("PM", i.matchPM);
		aXml.addChildAttrib("Enabled", i.getEnabled());
	}
	aXml.stepOut();
	
	if (dirty)
		saveUsers();
}

void IgnoreManager::saveUsers() {
	try {
		SimpleXML xml;

		xml.addTag("Ignored");
		xml.stepIn();

		xml.addTag("Users");
		xml.stepIn();

		//TODO: cache this information?
		for (const auto& u : ignoredUsers) {
			xml.addTag("User");
			xml.addChildAttrib("CID", u->getCID().toBase32());
			auto ou = ClientManager::getInstance()->findOnlineUser(u->getCID(), "");
			if (ou) {
				xml.addChildAttrib("Nick", ou->getIdentity().getNick());
				xml.addChildAttrib("Hub", ou->getHubUrl());
				xml.addChildAttrib("LastSeen", GET_TIME());
			} else {
				auto ofu = ClientManager::getInstance()->getOfflineUser(u->getCID());
				xml.addChildAttrib("Nick", ofu ? ofu->getNick() : "");
				xml.addChildAttrib("Hub", ofu ? ofu->getUrl() : "");
				xml.addChildAttrib("LastSeen", ofu ? ofu->getLastSeen() : GET_TIME());
			}
		}

		xml.stepOut();
		xml.stepOut();

		xml.saveSettingFile(CONFIG_DIR, CONFIG_NAME);
	}
	catch (const Exception& e) {
		dcdebug("ignored save: %s\n", e.getError().c_str());
	}
}

void IgnoreManager::loadUsers() {
	try {
		SimpleXML xml;
		xml.loadSettingFile(CONFIG_DIR, CONFIG_NAME);
		ClientManager* cm = ClientManager::getInstance();
		if (xml.findChild("Ignored")) {
			xml.stepIn();
			xml.resetCurrentChild();
			if (xml.findChild("Users")) {
				xml.stepIn();
				while (xml.findChild("User")) {
					UserPtr user = cm->getUser(CID(xml.getChildAttrib("CID")));
					{
						WLock(cm->getCS());
						cm->addOfflineUser(user, xml.getChildAttrib("Nick"), xml.getChildAttrib("Hub"), (uint32_t)xml.getIntChildAttrib("LastSeen"));
					}
					ignoredUsers.emplace(user);
					user->setFlag(User::IGNORED);
				}
				xml.stepOut();
			}
			xml.stepOut();
		}
	}
	catch (const Exception& e) {
		dcdebug("ignored load: %s\n", e.getError().c_str());
	}
}

void IgnoreManager::storeIgnore(const UserPtr& aUser) {
	ignoredUsers.emplace(aUser);
	aUser->setFlag(User::IGNORED);
	dirty = true;
	fire(IgnoreManagerListener::IgnoreAdded(), aUser);
}

void IgnoreManager::removeIgnore(const UserPtr& aUser) {
	auto i = ignoredUsers.find(aUser);
	if (i != ignoredUsers.end())
		ignoredUsers.erase(i);
	aUser->unsetFlag(User::IGNORED);
	dirty = true;
	fire(IgnoreManagerListener::IgnoreRemoved(), aUser);
}

bool IgnoreManager::isIgnored(const UserPtr& aUser) {
	auto i = ignoredUsers.find(aUser);
	return (i != ignoredUsers.end());
}

bool IgnoreManager::isChatFiltered(const string& aNick, const string& aText, ChatFilterItem::Context aContext) {
	for (auto& i : ChatFilterItems) {
		if (i.match(aNick, aText, aContext))
			return true;
	}
	return false;
}

// SettingsManagerListener
void IgnoreManager::on(SettingsManagerListener::Load, SimpleXML& aXml) noexcept {
	load(aXml);
}

void IgnoreManager::on(SettingsManagerListener::Save, SimpleXML& aXml) noexcept {
	save(aXml);
}

} //dcpp
