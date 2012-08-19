/* 
* Copyright (C) 2003-2005 Pär Björklund, per.bjorklund@gmail.com
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

#include "highlightmanager.h"

namespace dcpp {
HighlightManager::HighlightManager(void)
{
	SettingsManager::getInstance()->addListener(this);
}

HighlightManager::~HighlightManager(void)
{
	SettingsManager::getInstance()->removeListener(this);
}

void HighlightManager::load(SimpleXML& aXml){
	aXml.resetCurrentChild();

	if(aXml.findChild("Highlights")) {
		aXml.stepIn();
		while(aXml.findChild("Highlight")) {
			ColorSettings cs;
			cs.setContext(aXml.getIntChildAttrib("Context"));
			cs.setMatch( Text::utf8ToWide( aXml.getChildAttrib("Match") ) );
			cs.setBold(	aXml.getBoolChildAttrib("Bold") );
			cs.setItalic( aXml.getBoolChildAttrib("Italic") );
			cs.setUnderline( aXml.getBoolChildAttrib("Underline") );
			cs.setStrikeout( aXml.getBoolChildAttrib("Strikeout") );
			//Convert old setting to correct context
			if(aXml.getBoolChildAttrib("IncludeNickList") == true)
				cs.setContext(CONTEXT_NICKLIST);
			cs.setCaseSensitive( aXml.getBoolChildAttrib("CaseSensitive") );
			cs.setWholeLine( aXml.getBoolChildAttrib("WholeLine") );
			cs.setWholeWord( aXml.getBoolChildAttrib("WholeWord") );
			cs.setPopup( aXml.getBoolChildAttrib("Popup") );
			//cs.setTab( aXml.getBoolChildAttrib("Tab") );
			cs.setPlaySound( aXml.getBoolChildAttrib("PlaySound") );
			//cs.setLog( aXml.getBoolChildAttrib("LastLog") );
			cs.setFlashWindow( aXml.getBoolChildAttrib("FlashWindow") );
			cs.setMatchType( aXml.getIntChildAttrib("MatchType") );
			cs.setHasFgColor( aXml.getBoolChildAttrib("HasFgColor") );
			cs.setHasBgColor( aXml.getBoolChildAttrib("HasBgColor") );
			cs.setBgColor( (int)aXml.getLongLongChildAttrib("BgColor") );
			cs.setFgColor( (int)aXml.getLongLongChildAttrib("FgColor") );
			cs.setSoundFile( Text::utf8ToWide( aXml.getChildAttrib("SoundFile") ) );

			colorSettings.push_back(cs);
		}
		aXml.stepOut();
	} else {
		aXml.resetCurrentChild();
	}
	//convert the old setting to highlights
	if(!SETTING(HIGHLIGHT_LIST).empty()) {
		ColorSettings cs;
		cs.setContext(CONTEXT_FILELIST);
		cs.setMatch(Text::toT(SETTING(HIGHLIGHT_LIST)));
		cs.setFgColor( SETTING(LIST_HL_COLOR) );
		cs.setBgColor( SETTING(LIST_HL_BG_COLOR) );
		SettingsManager::getInstance()->set(SettingsManager::HIGHLIGHT_LIST, "");
		SettingsManager::getInstance()->set(SettingsManager::USE_HIGHLIGHT, true);
		colorSettings.push_back(cs);
	}

}

void HighlightManager::save(SimpleXML& aXml){
	aXml.addTag("Highlights");
	aXml.stepIn();

	ColorIter iter = colorSettings.begin();
	for(;iter != colorSettings.end(); ++iter) {
		aXml.addTag("Highlight");
		aXml.addChildAttrib("Context", (*iter).getContext());
		aXml.addChildAttrib("Match", Text::wideToUtf8((*iter).getMatch()));
		aXml.addChildAttrib("Bold", (*iter).getBold());
		aXml.addChildAttrib("Italic", (*iter).getItalic());
		aXml.addChildAttrib("Underline", (*iter).getUnderline());
		aXml.addChildAttrib("Strikeout", (*iter).getStrikeout());
		aXml.addChildAttrib("CaseSensitive", (*iter).getCaseSensitive());
		aXml.addChildAttrib("WholeLine", (*iter).getWholeLine());
		aXml.addChildAttrib("WholeWord", (*iter).getWholeWord());
		aXml.addChildAttrib("Popup", (*iter).getPopup());
		//aXml.addChildAttrib("Tab", (*iter).getTab());
		aXml.addChildAttrib("PlaySound", (*iter).getPlaySound());
		//aXml.addChildAttrib("LastLog", (*iter).getLog());
		aXml.addChildAttrib("FlashWindow", (*iter).getFlashWindow() );
		aXml.addChildAttrib("MatchType", (*iter).getMatchType());
		aXml.addChildAttrib("HasFgColor", (*iter).getHasFgColor());
		aXml.addChildAttrib("HasBgColor", (*iter).getHasBgColor());
		aXml.addChildAttrib("FgColor", Util::toString((*iter).getFgColor()));
		aXml.addChildAttrib("BgColor", Util::toString((*iter).getBgColor()));
		aXml.addChildAttrib("SoundFile", Text::wideToUtf8((*iter).getSoundFile()));
	}//end for

	aXml.stepOut();
}

void HighlightManager::on(SettingsManagerListener::Load, SimpleXML& xml){
	load(xml);
}

void HighlightManager::on(SettingsManagerListener::Save, SimpleXML& xml){
	save(xml);
}
}
