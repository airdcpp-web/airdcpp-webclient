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

#include "stdinc.h"
#include "DCPlusPlus.h"

#include "SettingsManager.h"
#include "StringTokenizer.h"
#include "SimpleXML.h"
#include "WebShortcuts.h"
#include "Pointer.h"

namespace dcpp {

WebShortcuts::WebShortcuts() {
	SettingsManager::getInstance()->addListener(this);
	//add our default ones, should we pick translations here?
	list.push_back(new WebShortcut(STRING(SEARCH_GOOGLE_FULL), "google", "http://www.google.com/search?q=", false));
	list.push_back(new WebShortcut(STRING(SEARCH_GOOGLE_TITLE), "googletitle", "http://www.google.com/search?q=", true));
	list.push_back(new WebShortcut(STRING(SEARCH_IMDB), "imdb", "http://www.imdb.com/find?q=", true));
	list.push_back(new WebShortcut(STRING(SEARCH_TVCOM), "tvcom", "http://www.tv.com/search?q=", true));
	list.push_back(new WebShortcut(STRING(SEARCH_METACRITIC), "metacritic", "http://www.metacritic.com/search/all/%s/results", true));
}

WebShortcuts::~WebShortcuts() {
	SettingsManager::getInstance()->removeListener(this);
	clear();
}

void WebShortcuts::load(SimpleXML& xml) {
	xml.resetCurrentChild();

	if(xml.findChild("WebShortcuts")){
		xml.stepIn();

		clear();

		while(xml.findChild("WebShortcut")){
			auto tmp = new WebShortcut();

			tmp->name  = xml.getChildAttrib("Name");
			tmp->key   = xml.getChildAttrib("Key");
			tmp->url   = xml.getChildAttrib("URL");

			tmp->clean = xml.getBoolChildAttrib("Clean");

			list.push_back(tmp);
		}
		
		xml.stepOut();
	}
}

void WebShortcuts::save(SimpleXML& xml) {
	xml.addTag("WebShortcuts");
	xml.stepIn();
	for(auto i: list) {
		xml.addTag("WebShortcut");
		xml.addChildAttrib("Name", i->name);
		xml.addChildAttrib("Key", i->key);
		xml.addChildAttrib("URL", i->url);
		xml.addChildAttrib("Clean", i->clean);
	}
	xml.stepOut();
}

WebShortcut* WebShortcuts::getShortcutByKey(const string& key) {
	return getShortcutByKey(list, key);
}

WebShortcut* WebShortcuts::getShortcutByName(WebShortcut::List& _list, const string& name) {
	for (auto i : _list)
		if (i->name == name )
			return i;
	return nullptr;
}
WebShortcut* WebShortcuts::getShortcutByKey(WebShortcut::List& _list, const string& key) {
	for (auto i : _list)
		if (i->key == key )
			return i;
	return nullptr;
}

WebShortcut::List WebShortcuts::copyList() {
	WebShortcut::List lst;
	for (auto i: list)
		lst.push_back(new WebShortcut(i->name, i->key, i->url, i->clean ));
	return lst;
}
void WebShortcuts::replaceList(WebShortcut::List& new_list) {
	clear();
	for (auto i: new_list)
		list.push_back(new WebShortcut(i->name, i->key, i->url, i->clean ));
}

void WebShortcuts::clear() {
	for_each(list.begin(), list.end(), DeleteFunction());
	list.clear();
}

void WebShortcuts::on(SettingsManagerListener::Save, SimpleXML& xml) noexcept {
	save(xml);
}
void WebShortcuts::on(SettingsManagerListener::Load, SimpleXML& xml) noexcept {
	load(xml);
}
}