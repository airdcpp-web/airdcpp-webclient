/* 
 * Copyright (C) 2001-2010 Jacek Sieka, arnetheduck on gmail point com
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


#define APPNAME "AirDC++"
#define VERSIONSTRING "2.21 Beta 1"

#define VERSIONFLOAT "2.208"

#define DCVERSIONSTRING "0.782"

#ifdef _WIN64
# define CONFIGURATION_TYPE "x86-64"
#define INSTALLER "AirDC_Installer64.exe"
#else
# define CONFIGURATION_TYPE "x86-32"
#define INSTALLER "AirDC_Installer.exe"
#endif

#define BETADATE

#ifdef BETADATE
#define VERSION_URL "http://version.airdcpp.net/beta_airdcversion.php"
#else
#define VERSION_URL "http://version.airdcpp.net/airdcversion.php"
#endif



#ifndef BETADATE
# define COMPLETEVERSIONSTRING _T(APPNAME) _T(" ") _T(VERSIONSTRING)  _T(" ") _T(CONFIGURATION_TYPE)
#else
# define COMPLETEVERSIONSTRING Text::toT(APPNAME " " VERSIONSTRING  " " CONFIGURATION_TYPE " - Beta Compiled : " + WinUtil::getCompileDate()).c_str()
#endif
/* Update the .rc file as well... */

/**
 * @file
 * $Id: version.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
