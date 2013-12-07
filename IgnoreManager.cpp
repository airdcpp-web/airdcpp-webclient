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

	if (aXml.findChild("IgnoreItems")) {
		aXml.stepIn();
		while (aXml.findChild("IgnoreItem")) {
			ignoreItems.push_back(IgnoreItem(aXml.getChildAttrib("Nick"), aXml.getChildAttrib("Text"), 
				(StringMatch::Method)aXml.getIntChildAttrib("NickMethod"), (StringMatch::Method)aXml.getIntChildAttrib("TextMethod"), 
				aXml.getBoolChildAttrib("MC"), aXml.getBoolChildAttrib("PM")));
		}
		aXml.stepOut();
	}
}

void IgnoreManager::save(SimpleXML& aXml) {
	aXml.addTag("IgnoreItems");
	aXml.stepIn();

	for (const auto& i : ignoreItems) {
		aXml.addTag("IgnoreItem");
		aXml.addChildAttrib("Nick", i.getNickPattern());
		aXml.addChildAttrib("NickMethod", i.getNickMethod());
		aXml.addChildAttrib("Text", i.getTextPattern());
		aXml.addChildAttrib("TextMethod", i.getTextMethod());
		aXml.addChildAttrib("MC", i.matchMainchat);
		aXml.addChildAttrib("PM", i.matchPM);
	}
	aXml.stepOut();
}

void IgnoreManager::storeIgnore(const string& aNick) {
	auto i = find_if(ignoreItems.begin(), ignoreItems.end(), [aNick](const IgnoreItem& s) { return compare(s.getNickPattern(), aNick) == 0; });
	if (i == ignoreItems.end())
		ignoreItems.push_back(IgnoreItem(aNick, "", StringMatch::EXACT, StringMatch::EXACT, true, true));
}

void IgnoreManager::removeIgnore(const string& aNick) {
	auto i = find_if(ignoreItems.begin(), ignoreItems.end(), [aNick](const IgnoreItem& s) { return compare(s.getNickPattern(), aNick) == 0; });
	if (i != ignoreItems.end())
		ignoreItems.erase(i);
}
/*
bool IgnoreManager::addIgnore(const string& aNick, const string& aText, StringMatch::Method aNickMethod, StringMatch::Method aTextMethod, bool aMainChat, bool aPM) {
	auto i = find_if(ignoreItems.begin(), ignoreItems.end(), [aNick, aText](const IgnoreItem& s) { 
		return ((s.getNickPattern().empty() || compare(s.getNickPattern(), aNick) == 0) && (s.getTextPattern().empty() || compare(s.getTextPattern(), aText) == 0));
	});
	if (i == ignoreItems.end()){
		ignoreItems.push_back(IgnoreItem(aNick, aText, aNickMethod, aTextMethod, aMainChat, aPM));
		return true;
	}
	return false;
}

void IgnoreManager::removeIgnore(int pos) {
	ignoreItems.erase(ignoreItems.begin() + pos);
}
*/
bool IgnoreManager::isIgnored(const string& aNick, const string& aText, IgnoreItem::Context aContext) {
	for (auto& i : ignoreItems) {
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
