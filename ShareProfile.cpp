/*
 * Copyright (C) 2011-2012 AirDC++ Project
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

#include "ShareProfile.h"
#include "TimerManager.h"
#include "FilteredFile.h"
#include "Streams.h"
#include "SimpleXML.h"
#include "BZUtils.h"
#include "ClientManager.h"
#include "version.h"

#include <boost/range/algorithm/for_each.hpp>

namespace dcpp {

//atomic_flag FileList::generating = ATOMIC_FLAG_INIT;

FileList::FileList(ProfileToken aProfile) : profile(aProfile), xmlDirty(true), forceXmlRefresh(true), lastXmlUpdate(0), listN(0), isSavedSuccessfully(false) { 
	if (profile == SP_HIDDEN && !Util::fileExists(getFileName()))  {
		FilteredOutputStream<BZFilter, true> emptyXmlFile(new File(getFileName(), File::WRITE, File::TRUNCATE | File::CREATE));
		emptyXmlFile.write(SimpleXML::utf8Header);
		emptyXmlFile.write("<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"/\" Generator=\"DC++ " DCVERSIONSTRING "\">\r\n"); // Hide Share Mod
		emptyXmlFile.write("</FileListing>");
		emptyXmlFile.flush();
	}
}

string FileList::getFileName() {
	return isSavedSuccessfully ? getDefaultFileName() : getNFileName();
}

string FileList::getDefaultFileName() {
	return Util::getPath(Util::PATH_USER_CONFIG) + "files_" + Util::toString(profile) + ".xml.bz2";
}

string FileList::getNFileName() {
	return Util::getPath(Util::PATH_USER_CONFIG) + "files_" + Util::toString(profile) + "_" + Util::toString(listN) + ".xml.bz2";
}

bool FileList::isDirty(bool forced) {
	if (profile == SP_HIDDEN)
		return false;

	cs.mutex.lock();

	/*if(!forced && generating.test_and_set()) {
		return false;
	}*/

	bool dirty = (forced && xmlDirty) || forceXmlRefresh || (xmlDirty && (lastXmlUpdate + 15 * 60 * 1000 < GET_TICK()));
	if (!dirty) {
		cs.mutex.unlock();
		return false;
	}

	return true;
}

void FileList::unsetDirty() {
	xmlDirty = false;
	forceXmlRefresh = false;
	lastXmlUpdate = GET_TICK();
	cs.mutex.unlock();
	//generating.clear();
}

void FileList::saveList() {
	if(bzXmlRef.get()) {
		bzXmlRef.reset();
		File::deleteFile(getFileName());
	}

	try {
		File::renameFile(getNFileName(), getDefaultFileName());
		isSavedSuccessfully = true;
		//newXmlName = Util::getPath(Util::PATH_USER_CONFIG) + "files" + aProfile + ".xml.bz2";
	} catch(const FileException& e) {
		dcdebug("error renaming filelist: ", e.getError());
		isSavedSuccessfully = false;
		// Ignore, this is for caching only...
	}

	bzXmlRef = unique_ptr<File>(new File(getFileName(), File::READ, File::OPEN));
	bzXmlListLen = File::getSize(getFileName());

	//cleanup old filelists we failed to delete before due to uploading them.
	StringList lists = File::findFiles(Util::getPath(Util::PATH_USER_CONFIG), "files_" + Util::toString(profile) + "?*.xml.bz2");
	boost::for_each(lists, File::deleteFile);
}

ShareProfile::ShareProfile(const string& aName, ProfileToken aToken) : token(aToken), plainName(aName), profileList(new FileList(aToken)) { }

ShareProfile::~ShareProfile() {
	delete profileList;
}

string ShareProfile::getDisplayName() {
	string ret = plainName;
	if (token == SP_DEFAULT) {
		ret += " (" + STRING(DEFAULT) + ")";
	}
	return ret;
}

FileList* ShareProfile::generateProfileList() {
	profileList = new FileList(token);
	return profileList;
}

} //dcpp