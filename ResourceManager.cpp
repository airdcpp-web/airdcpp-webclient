/* 
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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
#include "ResourceManager.h"

#include "SimpleXML.h"
#include "File.h"
#include "Text.h"

namespace dcpp {
	
wstring ResourceManager::wstrings[ResourceManager::LAST];

void ResourceManager::loadLanguage(const string& aFile) {
	try {
		File f(aFile, File::READ, File::OPEN, false);
		SimpleXML xml;
		xml.fromXML(f.read());

		unordered_map<string, int> h;
		
		for(int i = 0; i < LAST; ++i) {
			h[names[i]] = i;
		}

		if(xml.findChild("Language")) {
			rtl = xml.getBoolChildAttrib("RightToLeft");

			xml.stepIn();
			if(xml.findChild("Strings")) {
				xml.stepIn();

				while(xml.findChild("String")) {
					unordered_map<string, int>::const_iterator j = h.find(xml.getChildAttrib("Name"));

					if(j != h.end()) {
						strings[j->second] = xml.getChildData();
					}
				}
				createWide();
			}
		}
	} catch(const Exception&) {
		// ...
	}
}

void ResourceManager::createWide() {
	for(int i = 0; i < LAST; ++i) {
		wstrings[i].clear();
		Text::utf8ToWide(strings[i], wstrings[i]);
	}
}

} // namespace dcpp

/**
 * @file
 * $Id: ResourceManager.cpp 568 2011-07-24 18:28:43Z bigmuscle $
 */
