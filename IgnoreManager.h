///////////////////////////////////////////////////////////////////////////////
//
//	Handles saving and loading of ignorelists
//
///////////////////////////////////////////////////////////////////////////////

#ifndef DCPP_IGNOREMANAGER_H
#define DCPP_IGNOREMANAGER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Singleton.h"
#include "SettingsManager.h"
#include "SimpleXML.h"
#include "StringMatch.h"

namespace dcpp {

class IgnoreItem {
public:
	IgnoreItem(const string& aNickMatch, const string& aTextMatch, StringMatch::Method aNickMethod,
		StringMatch::Method aTextMethod, bool aMainchat, bool aPM) : matchPM(aPM), matchMainchat(aMainchat)
	{
		nickMatcher.setMethod(aNickMethod);
		nickMatcher.pattern = aNickMatch;
		nickMatcher.prepare();

		textMatcher.setMethod(aTextMethod);
		textMatcher.pattern = aTextMatch;
		textMatcher.prepare();
	}
	~IgnoreItem() {}

	enum Context {
		PM, // Private chat
		MC, // Main chat
		ALL // Both
	};

	const string& getNickPattern() const { return nickMatcher.pattern; }
	const string&  getTextPattern() const { return textMatcher.pattern; }
	StringMatch::Method getNickMethod() const { return nickMatcher.getMethod(); }
	StringMatch::Method getTextMethod() const { return textMatcher.getMethod(); }
	
	bool match(const string& aNick, const string& aText, Context aContext) {
		if (aContext == PM && !matchPM || aContext == MC && !matchMainchat)
			return false;

		if (!nickMatcher.pattern.empty() && nickMatcher.match(aNick)){
			//nick matched, match the text in case we just want to ignore some messages of the user
			return (textMatcher.pattern.empty() || textMatcher.match(aText));
		}
		//General text match ignore type, no nick pattern just match the text
		if (!textMatcher.pattern.empty() && textMatcher.match(aText))
			return true;

		return false;
	}
	void updateItem(const string& aNickMatch, const string& aTextMatch, StringMatch::Method aNickMethod, StringMatch::Method aTextMethod) {
		nickMatcher.setMethod(aNickMethod);
		nickMatcher.pattern = aNickMatch;
		nickMatcher.prepare();

		textMatcher.setMethod(aTextMethod);
		textMatcher.pattern = aTextMatch;
		textMatcher.prepare();
	}

	bool matchPM;
	bool matchMainchat;

private:
	StringMatch nickMatcher;
	StringMatch textMatcher;
};

class IgnoreManager: public Singleton<IgnoreManager>, private SettingsManagerListener
{
public:
	IgnoreManager() { SettingsManager::getInstance()->addListener(this); }
	~IgnoreManager() { SettingsManager::getInstance()->removeListener(this); }

	// store & remove ignores through/from hubframe
	void storeIgnore(const string& aNick);
	void removeIgnore(const string& aNick);

	// check if user is ignored
	bool isIgnored(const string& aNick, const string& aText, IgnoreItem::Context aContext = IgnoreItem::ALL);
	vector<IgnoreItem>& getIgnoreList() { return ignoreItems; }
	void replaceList(vector<IgnoreItem>& newList) {
		ignoreItems = newList;
	}
	/*
	bool addIgnore(const string& aNick, const string& aText, StringMatch::Method aNickMethod = StringMatch::EXACT, 
		StringMatch::Method aTextMethod = StringMatch::PARTIAL, bool aMainChat = true, bool aPM = true);
	void removeIgnore(int pos);
	void clearIgnores() { ignoreItems.clear(); }
	IgnoreItem getIgnore(int pos) { return ignoreItems[pos]; }
	void updateIgnore(IgnoreItem& item, int pos) { ignoreItems[pos] = item; }
	*/
private:
	// save & load
	void load(SimpleXML& aXml);
	void save(SimpleXML& aXml);

	// SettingsManagerListener
	virtual void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept;
	virtual void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept;

	// contains the ignored nicks and patterns 
	vector<IgnoreItem> ignoreItems;
};

}

#endif // IGNOREMANAGER_H