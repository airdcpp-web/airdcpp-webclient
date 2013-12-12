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
#include "Speaker.h"
#include "SettingsManager.h"
#include "SimpleXML.h"
#include "User.h"
#include "StringMatch.h"

namespace dcpp {

class IgnoreManagerListener {
public:
	virtual ~IgnoreManagerListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> IgnoreAdded;
	typedef X<1> IgnoreRemoved;

	virtual void on(IgnoreAdded, const UserPtr&) noexcept{}
	virtual void on(IgnoreRemoved, const UserPtr&) noexcept{}
};

class ChatFilterItem {
public:
	ChatFilterItem(const string& aNickMatch, const string& aTextMatch, StringMatch::Method aNickMethod,
		StringMatch::Method aTextMethod, bool aMainchat, bool aPM, bool aEnabled = true) : matchPM(aPM), matchMainchat(aMainchat), enabled(aEnabled)
	{
		nickMatcher.setMethod(aNickMethod);
		nickMatcher.pattern = aNickMatch;
		nickMatcher.prepare();

		textMatcher.setMethod(aTextMethod);
		textMatcher.pattern = aTextMatch;
		textMatcher.prepare();
	}
	~ChatFilterItem() {}

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
		if (!getEnabled())
			return false;

		if ((aContext == PM && !matchPM) || (aContext == MC && !matchMainchat))
			return false;

		if (!nickMatcher.pattern.empty() && nickMatcher.match(aNick)){
			//nick matched, match the text in case we just want to ignore some messages of the user
			return (textMatcher.pattern.empty() || textMatcher.match(aText));
		}
		//General text match ignore type, no nick pattern just match the text
		if (nickMatcher.pattern.empty() && !textMatcher.pattern.empty() && textMatcher.match(aText))
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

	GETSET(bool, enabled, Enabled)

	bool matchPM;
	bool matchMainchat;

private:
	StringMatch nickMatcher;
	StringMatch textMatcher;
};

class IgnoreManager : public Singleton<IgnoreManager>, public Speaker<IgnoreManagerListener>, private SettingsManagerListener
{
public:
	IgnoreManager() : dirty(false) { SettingsManager::getInstance()->addListener(this); }
	~IgnoreManager() { SettingsManager::getInstance()->removeListener(this); }

	// store & remove ignores through/from hubframe
	void storeIgnore(const UserPtr& aUser);
	void removeIgnore(const UserPtr& aUser);
	bool isIgnored(const UserPtr& aUser);

	// chat filter
	bool isChatFiltered(const string& aNick, const string& aText, ChatFilterItem::Context aContext = ChatFilterItem::ALL);
	vector<ChatFilterItem>& getIgnoreList() { return ChatFilterItems; }
	void replaceList(vector<ChatFilterItem>& newList) {
		ChatFilterItems = newList;
	}

private:
	friend class Singleton<IgnoreManager>;

	typedef unordered_set<UserPtr, User::Hash> IgnoredUsersList;
	IgnoredUsersList ignoredUsers;

	// save & load
	void load(SimpleXML& aXml);
	void save(SimpleXML& aXml);

	void saveUsers();
	void loadUsers();

	bool dirty;

	// SettingsManagerListener
	virtual void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept;
	virtual void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept;

	// contains the ignored nicks and patterns 
	vector<ChatFilterItem> ChatFilterItems;
};

}

#endif // IGNOREMANAGER_H