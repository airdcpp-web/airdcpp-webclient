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

#include "stdinc.h"
#include "ResourceManager.h"
#include "LogManager.h"

#include "SimpleXML.h"
#include "File.h"
#include "Text.h"

namespace dcpp {
	
wstring ResourceManager::wstrings[ResourceManager::LAST];

void ResourceManager::loadLanguage(const string& aFile) {
	try {
		File f(aFile, File::READ, File::OPEN, File::BUFFER_SEQUENTIAL, false);
		SimpleXML xml;
		xml.fromXML(f.read());

		unordered_map<string, int> h;
		
		for(int i = 0; i < LAST; ++i) {
			h[names[i]] = i;
		}

		string childName = "String";
		string attribName = "Name";
		if (xml.findChild("Language")) {
			rtl = xml.getBoolChildAttrib("RightToLeft");

			xml.stepIn();
			if (xml.findChild("Strings")) {
				xml.stepIn();
			}
		} else {
			xml.resetCurrentChild();
			if (xml.findChild("resources")) {
				xml.stepIn();
				childName = "string";
				attribName = "name";
			} else {
				throw Exception("Invalid format");
			}
		}

		while (xml.findChild(childName)) {
			const auto j = h.find(xml.getChildAttrib(attribName));
			if(j != h.end()) {
				strings[j->second] = xml.getChildData();
			}
		}
		createWide();
	} catch(const Exception& e) {
		LogManager::getInstance()->message("Failed to load the language file " + aFile + ": " + e.getError(), LogManager::LOG_ERROR);
	}
}

void ResourceManager::createWide() {
	for(int i = 0; i < LAST; ++i) {
		wstrings[i] = Text::utf8ToWide(strings[i]);
	}
}

} // namespace dcpp