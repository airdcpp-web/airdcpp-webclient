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

namespace dcpp {

class IgnoreManager: public Singleton<IgnoreManager>, private SettingsManagerListener
{
public:
	IgnoreManager() { SettingsManager::getInstance()->addListener(this); }
	~IgnoreManager() { SettingsManager::getInstance()->removeListener(this); }

	// store & remove ignores through/from hubframe
	void storeIgnore(const string& aNick);
	void removeIgnore(const string& aNick);

	// check if user is ignored
	bool isIgnored(const string& aNick);

	// get and put ignorelist (for MiscPage)
	StringSet getIgnoredUsers() const;
	void putIgnoredUsers(StringSet& ignoreList);

private:
	// save & load
	void load(SimpleXML& aXml);
	void save(SimpleXML& aXml);

	// SettingsManagerListener
	virtual void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept;
	virtual void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept;

	// contains the ignored nicks and patterns 
	StringSet ignoredUsers;
};

}

#endif // IGNOREMANAGER_H