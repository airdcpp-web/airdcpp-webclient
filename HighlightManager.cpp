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

#include "HighlightManager.h"

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
			try{
				ColorSettings cs;
				cs.setContext(aXml.getIntChildAttrib("Context"));
				cs.setMatch( Text::toT( aXml.getChildAttrib("Match") ) );
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
				cs.setTab( aXml.getBoolChildAttrib("Tab") );
				cs.setPlaySound( aXml.getBoolChildAttrib("PlaySound") );
				//cs.setLog( aXml.getBoolChildAttrib("LastLog") );
				cs.setFlashWindow( aXml.getBoolChildAttrib("FlashWindow") );
				cs.setMatchType( aXml.getIntChildAttrib("MatchType") );
				cs.setHasFgColor(aXml.getBoolChildAttrib("HasFgColor"));
				cs.setHasBgColor(aXml.getBoolChildAttrib("HasBgColor"));
				cs.setBgColor(aXml.getIntChildAttrib("BgColor"));
				cs.setFgColor(aXml.getIntChildAttrib("FgColor"));
				cs.setSoundFile(aXml.getChildAttrib("SoundFile"));
				cs.setMatchColumn(aXml.getIntChildAttrib("MatchColumn"));

				cs.setRegexp();
				colorSettings.push_back(cs);
			}catch(...) { }
		}
		aXml.stepOut();
	} else {
		aXml.resetCurrentChild();
	}
}

void HighlightManager::save(SimpleXML& aXml){
	aXml.addTag("Highlights");
	aXml.stepIn();

	for(const auto& hl: colorSettings) {
		aXml.addTag("Highlight");
		aXml.addChildAttrib("Context", hl.getContext());
		aXml.addChildAttrib("Match", Text::fromT(hl.getMatch()));
		aXml.addChildAttrib("Bold", hl.getBold());
		aXml.addChildAttrib("Italic", hl.getItalic());
		aXml.addChildAttrib("Underline", hl.getUnderline());
		aXml.addChildAttrib("Strikeout", hl.getStrikeout());
		aXml.addChildAttrib("CaseSensitive", hl.getCaseSensitive());
		aXml.addChildAttrib("WholeLine", hl.getWholeLine());
		aXml.addChildAttrib("WholeWord", hl.getWholeWord());
		aXml.addChildAttrib("Popup", hl.getPopup());
		aXml.addChildAttrib("Tab", hl.getTab());
		aXml.addChildAttrib("PlaySound", hl.getPlaySound());
		//aXml.addChildAttrib("LastLog", hl.getLog());
		aXml.addChildAttrib("FlashWindow", hl.getFlashWindow() );
		aXml.addChildAttrib("MatchType", hl.getMatchType());
		aXml.addChildAttrib("HasFgColor", hl.getHasFgColor());
		aXml.addChildAttrib("HasBgColor", hl.getHasBgColor());
		aXml.addChildAttrib("FgColor", Util::toString(hl.getFgColor()));
		aXml.addChildAttrib("BgColor", Util::toString(hl.getBgColor()));
		aXml.addChildAttrib("SoundFile", hl.getSoundFile());
		aXml.addChildAttrib("MatchColumn", hl.getMatchColumn());
	}//end for

	aXml.stepOut();
}

void HighlightManager::on(SettingsManagerListener::Load, SimpleXML& xml) noexcept {
	load(xml);
}

void HighlightManager::on(SettingsManagerListener::Save, SimpleXML& xml) noexcept {
	save(xml);
}
}
