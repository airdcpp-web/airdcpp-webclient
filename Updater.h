/*
 * Copyright (C) 2012-2013 AirDC++ Project
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

#ifndef UPDATER_H
#define UPDATER_H

namespace dcpp {

using std::unique_ptr;

class Updater {

#ifdef _WIN32
public:
	static bool applyUpdate(const string& sourcePath, const string& installPath, string& error);
	static bool extractFiles(const string& curSourcePath, const string& curExtractPath, string& error);
	static void signVersionFile(const string& file, const string& key, bool makeHeader = false);
	static void createUpdate();

#endif
};

}

#endif // UPDATE_MANAGER_H