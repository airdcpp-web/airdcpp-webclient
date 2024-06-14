/* 
* Copyright (C) 2003-2005 Pär Björklund, per.bjorklund@gmail.com
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
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

#ifndef HIGHLIGHTMANAGER_H
#define HIGHLIGHTMANAGER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <airdcpp/SettingsManager.h>
#include <airdcpp/SimpleXML.h>
#include <airdcpp/Singleton.h>

#include "ColorSettings.h"


namespace dcpp {
typedef vector<ColorSettings> ColorList;
typedef ColorList::iterator ColorIter;

class HighlightManager : public Singleton<HighlightManager>, private SettingsManagerListener
{
public:
	HighlightManager(void);
	~HighlightManager(void);

	ColorList*	getList() {
		return &colorSettings;
	}

	enum: uint8_t {
		CONTEXT_CHAT,
		CONTEXT_NICKLIST,
		CONTEXT_FILELIST,
		CONTEXT_SEARCH,
	};


	void replaceList(ColorList& settings) {
		colorSettings.clear();
		colorSettings = settings;
	}
	void clearList() {
		colorSettings.clear();
	}

	bool emptyList() {
		return colorSettings.empty();
	}
	
	void load(SimpleXML& aXml);
	void save(SimpleXML& aXml);

private:
	//store all highlights
	ColorList colorSettings;

	virtual void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept;
	virtual void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept;
};
}
#endif
