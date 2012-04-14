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
	list.push_back(new WebShortcut(CTSTRING(SEARCH_GOOGLE_FULL), _T("google"), _T("http://www.google.com/search?q="), false));
	list.push_back(new WebShortcut(CTSTRING(SEARCH_GOOGLE_TITLE), _T("googletitle"), _T("http://www.google.com/search?q="), true));
	list.push_back(new WebShortcut(CTSTRING(SEARCH_IMDB), _T("imdb"), _T("http://www.imdb.com/find?q="), true));
	list.push_back(new WebShortcut(CTSTRING(SEARCH_TVCOM), _T("tvcom"), _T("http://www.tv.com/search?q="), true));
	list.push_back(new WebShortcut(CTSTRING(SEARCH_METACRITIC), _T("metacritic"), _T("http://www.metacritic.com/search/all/"), true));
}

WebShortcuts::~WebShortcuts() {
	SettingsManager::getInstance()->removeListener(this);
	for_each(list.begin(),list.end(), DeleteFunction());
}

void WebShortcuts::load(SimpleXML& xml) {
	xml.resetCurrentChild();

	if(xml.findChild("WebShortcuts")){
		xml.stepIn();
		WebShortcut* tmp = NULL;

		clear();

		while(xml.findChild("WebShortcut")){
			tmp = new WebShortcut();

			tmp->name  = Text::toT(xml.getChildAttrib("Name"));
			tmp->key   = Text::toT(xml.getChildAttrib("Key"));
			tmp->url   = Text::toT(xml.getChildAttrib("URL"));

			tmp->clean = xml.getBoolChildAttrib("Clean");

			list.push_back(tmp);
		}
		
		xml.stepOut();
	}
}

void WebShortcuts::save(SimpleXML& xml) {
	xml.addTag("WebShortcuts");
	xml.stepIn();
	for(WebShortcut::Iter i = list.begin(); i != list.end(); ++i){
		xml.addTag("WebShortcut");

		xml.addChildAttrib("Name", Text::fromT((*i)->name));
		xml.addChildAttrib("Key",	Text::fromT((*i)->key));
		xml.addChildAttrib("URL",	Text::fromT((*i)->url));

		xml.addChildAttrib("Clean", (*i)->clean);
	}
	xml.stepOut();
}

WebShortcut* WebShortcuts::getShortcutByName(const tstring& name) {
	for (WebShortcut::Iter i = list.begin(); i != list.end(); ++i)
		if ( (*i)->name == name )
			return *i;
	return NULL;
}
WebShortcut* WebShortcuts::getShortcutByKey(const tstring& key) {
	for (WebShortcut::Iter i = list.begin(); i != list.end(); ++i)
		if ( (*i)->key == key )
			return *i;
	return NULL;
}

WebShortcut* WebShortcuts::getShortcutByName(WebShortcut::List& _list, const tstring& name) {
	for (WebShortcut::Iter i = _list.begin(); i != _list.end(); ++i)
		if ( (*i)->name == name )
			return *i;
	return NULL;
}
WebShortcut* WebShortcuts::getShortcutByKey(WebShortcut::List& _list, const tstring& key) {
	for (WebShortcut::Iter i = _list.begin(); i != _list.end(); ++i)
		if ( (*i)->key == key )
			return *i;
	return NULL;
}

WebShortcut::List WebShortcuts::copyList() {
	WebShortcut::List lst;
	for (WebShortcut::Iter i = list.begin(); i != list.end(); ++i)
		lst.push_back(new WebShortcut( (*i)->name, (*i)->key, (*i)->url, (*i)->clean ));
	return lst;
}
void WebShortcuts::replaceList(WebShortcut::List& new_list) {
	clear();
	for (WebShortcut::Iter i = new_list.begin(); i != new_list.end(); ++i)
		list.push_back(new WebShortcut( (*i)->name, (*i)->key, (*i)->url, (*i)->clean ));
}

void WebShortcuts::clear() {
	for (WebShortcut::Iter i = list.begin(); i != list.end(); ++i)
		delete *i;
	list.clear();
}

void WebShortcuts::on(SettingsManagerListener::Save, SimpleXML& xml) {
	save(xml);
}
void WebShortcuts::on(SettingsManagerListener::Load, SimpleXML& xml) {
	load(xml);
}
}