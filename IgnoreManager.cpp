///////////////////////////////////////////////////////////////////////////////
//
//	Handles saving and loading of ignorelists
//
///////////////////////////////////////////////////////////////////////////////
#include "stdinc.h"
#include "IgnoreManager.h"

#include "Util.h"
#include "Wildcards.h"

namespace dcpp {

void IgnoreManager::load(SimpleXML& aXml) {
	if(aXml.findChild("IgnoreList")) {
		aXml.stepIn();
		while(aXml.findChild("User")) {	
			ignoredUsers.insert(aXml.getChildAttrib("Nick"));
		}
		aXml.stepOut();
	}
}

void IgnoreManager::save(SimpleXML& aXml) {
	aXml.addTag("IgnoreList");
	aXml.stepIn();

	for(const auto& def: ignoredUsers) {
		aXml.addTag("User");
		aXml.addChildAttrib("Nick", def);
	}
	aXml.stepOut();
}


void IgnoreManager::storeIgnore(const string& aNick) {
	ignoredUsers.insert(aNick);
}

void IgnoreManager::removeIgnore(const string& aNick) {
	ignoredUsers.erase(aNick);
}

StringSet IgnoreManager::getIgnoredUsers() const {
	return ignoredUsers;
	/*for(auto& def: ignoredUsers) {
		lst.push_back(Text::toT(def));
	}*/
}

void IgnoreManager::putIgnoredUsers(StringSet& newList) { 
	ignoredUsers = newList;
	/*ignoredUsers.clear();
	for(auto& def: newList)
		ignoredUsers.insert(Text::fromT(def));*/
}


bool IgnoreManager::isIgnored(const string& aNick) {
	if (ignoredUsers.find(aNick) != ignoredUsers.end())
		return true;

	if(SETTING(IGNORE_USE_REGEXP_OR_WC)) {
		for(auto& def: ignoredUsers) {
			if(Util::strnicmp(def, "$Re:", 4) == 0 && def.length() > 4) {
				string str1 = def.substr(4);
				string str2 = aNick;
				try {
					boost::regex reg(str1);
					if(boost::regex_search(str2.begin(), str2.end(), reg)){
						return true;
					};
				} catch(...) { }
			} else if(Wildcard::patternMatch(Text::toLower(aNick), Text::toLower(def), false)) {
				return true;
			}
		}
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
