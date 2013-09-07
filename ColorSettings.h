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

#ifndef COLORSETTINGS_H
#define COLORSETTINGS_H

#include "typedefs.h"
#include "GetSet.h"

namespace dcpp {
class ColorSettings
{
public:
	ColorSettings();
	~ColorSettings(){};

	GETSET(bool, bWholeWord, WholeWord);
	GETSET(bool, bWholeLine, WholeLine);
	GETSET(bool, bIncludeNickList, IncludeNickList);
	GETSET(bool, bCaseSensitive, CaseSensitive);
	GETSET(bool, bPopup, Popup);
	GETSET(bool, bTab, Tab);
	GETSET(bool, bPlaySound, PlaySound);
	GETSET(bool, bBold, Bold);
	GETSET(bool, bUnderline, Underline);
	GETSET(bool, bItalic, Italic);
	GETSET(bool, bStrikeout, Strikeout);
	//GETSET(bool, bLastLog, Log);
	GETSET(bool, bFlashWindow, FlashWindow);
	GETSET(int,  iMatchType, MatchType);
	GETSET(int,  iBgColor, BgColor);
	GETSET(int,  iFgColor, FgColor);
	GETSET(bool, bHasBgColor, HasBgColor);
	GETSET(bool, bHasFgColor, HasFgColor);
	GETSET(string, strSoundFile, SoundFile);
	GETSET(int, bContext, Context);
	GETSET(int, bMatchColumn, MatchColumn);
    	
	void setMatch(const tstring& match);
	void setRegexp();

#ifdef _WIN32
	boost::wregex regexp;
#endif
	bool getUsers() { return bUsers; }
	bool getTimestamps() { return bTimestamps; }
	bool getMyNick() { return bMyNick; }
	bool usingRegexp() { return bUsingRegexp; }

	const tstring& getMatch() const { return strMatch; }
	
private:
	//string to match against
	tstring strMatch;

	bool bTimestamps;
	bool bUsers;
	bool bMyNick;
	bool bUsingRegexp;

};
}
#endif