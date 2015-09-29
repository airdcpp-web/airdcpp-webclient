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

#include "ColorSettings.h"

namespace dcpp {

ColorSettings::ColorSettings() : bTimestamps(false), bUsers(false), bMyNick(false), bUsingRegexp(false),
	bWholeWord(false),
	bWholeLine(false), bIncludeNickList(false), bCaseSensitive(false), bPopup(false), bTab(false),
	bPlaySound(false), bBold(false), bUnderline(false), bItalic(false), bStrikeout(false),
	/*bLastLog(false),*/ bFlashWindow(false), iMatchType(1), iBgColor(0), iFgColor(0), bHasBgColor(false),
	bHasFgColor(false), bContext(0), bMatchColumn(0) {
}

#ifdef _WIN32

void ColorSettings::setMatch(const tstring& match) {
	if (match.compare(_T("$ts$")) == 0) {
		bTimestamps = true;
	} else if (match.compare(_T("$users$")) == 0) {
		bUsers = true;
	} else if (match.find(_T("$mynick$")) != tstring::npos) {
		bMyNick = true;
	} else if (match.find(_T("$Re:")) == 0) {
		bUsingRegexp = true;
	}
	strMatch = match;
}
void ColorSettings::setRegexp() {
	if (bUsingRegexp)
		regexp.assign(strMatch.substr(4), getCaseSensitive() ? boost::match_default : boost::regex_constants::icase);
}

#else

void ColorSettings::setMatch(const tstring& match) {

}
void ColorSettings::setRegexp() {

}

#endif

}