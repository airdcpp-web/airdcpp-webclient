/* 
 * Copyright (C) 2003 Opera, opera@home.se
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

#ifndef _WEBSHORTCUTS_H
#define _WEBSHORTCUTS_H

#include "Singleton.h"
#include "SettingsManager.h"

namespace dcpp {

class WebShortcut {
public:
	typedef WebShortcut* Ptr;
	typedef std::vector<Ptr> List;

	WebShortcut(const string& _name, const string& _key, const string& _url, bool _clean = false) :
	name(_name), key(_key), url(_url), clean(_clean) { }
	WebShortcut() {}

	string name;
	string key;
	string url;
	bool clean;
};

class WebShortcuts : public Singleton<WebShortcuts>, private SettingsManagerListener {
public:
	WebShortcuts();
	~WebShortcuts();

	WebShortcut* getShortcutByKey(const string& key);
	static WebShortcut* getShortcutByName(WebShortcut::List& _list, const string& name);
	static WebShortcut* getShortcutByKey(WebShortcut::List& _list, const string& key);

	WebShortcut::List copyList();
	void replaceList(WebShortcut::List& new_list);

	WebShortcut::List list;
private:
	void clear();

	void load(SimpleXML& xml);
	void save(SimpleXML& xml);

	virtual void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept;
	virtual void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept;
};
}
#endif // _WEBSHORTCUTS_H
