/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_RESOURCE_MANAGER_H
#define DCPLUSPLUS_DCPP_RESOURCE_MANAGER_H

#include "Singleton.h"
#include "format.h"

namespace dcpp {

#define STRING(x) ResourceManager::getInstance()->getString(ResourceManager::x)
#define CSTRING(x) ResourceManager::getInstance()->getString(ResourceManager::x).c_str()
#define WSTRING(x) ResourceManager::getInstance()->getStringW(ResourceManager::x)
#define CWSTRING(x) ResourceManager::getInstance()->getStringW(ResourceManager::x).c_str()

#define STRING_I(x) ResourceManager::getInstance()->getString(x)
#define CSTRING_I(x) ResourceManager::getInstance()->getString(x).c_str()
#define WSTRING_I(x) ResourceManager::getInstance()->getStringW(x)
#define CWSTRING_I(x) ResourceManager::getInstance()->getStringW(x).c_str()

#define STRING_F(x, args) (dcpp_fmt(ResourceManager::getString(ResourceManager::x)) % args).str()
#define CSTRING_F(x, args) (dcpp_fmt(ResourceManager::getString(ResourceManager::x)) % args).str().c_str()
#define WSTRING_F(x, args) (dcpp_fmt(ResourceManager::getStringW(ResourceManager::x)) % args).str()
#define CWSTRING_F(x, args) (dcpp_fmt(ResourceManager::getStringW(ResourceManager::x)) % args).str().c_str()

#ifdef UNICODE
#define TSTRING WSTRING
#define TSTRING_I WSTRING_I
#define TSTRING_F WSTRING_F
#define CTSTRING CWSTRING
#define CTSTRING_I CWSTRING_I
#define CTSTRING_F CWSTRING_F
#else
#define TSTRING STRING
#define TSTRING_I STRING_I
#define TSTRING_F WSTRING_F
#define CTSTRING CSTRING
#define CTSTRING_I CSTRING_I
#define CTSTRING_F CSTRING_F
#endif

class ResourceManager : public Singleton<ResourceManager> {
public:
	
#include "StringDefs.h"

	void loadLanguage(const string& aFile);
	static const string& getString(Strings x) { dcassert(x >= 0 && x < LAST); return strings[x]; }
	static const wstring& getStringW(Strings x) { dcassert(x >= 0 && x < LAST); return wstrings[x]; }
	bool isRTL() { return rtl; }

private:
	friend class Singleton<ResourceManager>;

	ResourceManager() : rtl(false) {
		createWide();
	}

	~ResourceManager() { }
	
	static string strings[LAST];
	static wstring wstrings[LAST];
	static string names[LAST];

	bool rtl;

	void createWide();
};

} // namespace dcpp

#endif // !defined(RESOURCE_MANAGER_H)