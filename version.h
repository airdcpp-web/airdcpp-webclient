/* 
 * Copyright (C) 2001-2008 Jacek Sieka, arnetheduck on gmail point com
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
#define VERSIONSTRING "2.09 Beta 2"
#define BETADATE "Beta Compiled : 31.01.2011"
#define VERSIONFLOAT 2.091

#define DCVERSIONSTRING "0.781"

#ifdef _WIN64
# define CONFIGURATION_TYPE "x86-64"
#define INSTALLER "AirDC_Installer64.exe"
#else
# define CONFIGURATION_TYPE "x86-32"
#define INSTALLER "AirDC_Installer.exe"
#endif

#ifdef BETADATE
#define VERSION_URL "http://airdc.sourceforge.net/xmls/beta_airdcversion.php"
#else
#define VERSION_URL "http://airdc.sourceforge.net/xmls/airdcversion.php"
#endif

/* Update the .rc file as well... */

/**
 * @file
 * $Id: version.h 421 2008-09-03 17:20:45Z BigMuscle $
 */
