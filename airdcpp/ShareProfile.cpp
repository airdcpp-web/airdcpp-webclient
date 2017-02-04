/*
 * Copyright (C) 2012-2017 AirDC++ Project
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

#include "BZUtils.h"
#include "FilteredFile.h"
#include "ShareProfile.h"
#include "TimerManager.h"
#include "Streams.h"

namespace dcpp {

FileList::FileList(ProfileToken aProfile) : profile(aProfile) { }

string FileList::getFileName() const noexcept {
	return Util::getPath(Util::PATH_USER_CONFIG) + "files_" + Util::toString(profile) + "_" + Util::toString(listN) + ".xml.bz2";
}

bool FileList::allowGenerateNew(bool aForced) noexcept {
	bool dirty = (aForced && xmlDirty) || forceXmlRefresh || (xmlDirty && (lastXmlUpdate + 15 * 60 * 1000 < GET_TICK()));
	if (!dirty) {
		return false;
	}

	listN++;
	return true;
}

void FileList::generationFinished(bool aFailed) noexcept {
	xmlDirty = false;
	forceXmlRefresh = false;
	lastXmlUpdate = GET_TICK();
	if (aFailed)
		listN--;
}

void FileList::saveList() {
	bzXmlRef.reset(new File(getFileName(), File::READ, File::OPEN, File::BUFFER_SEQUENTIAL, false));
	bzXmlListLen = File::getSize(getFileName());

	//cleanup old filelists we failed to delete before due to uploading them.
	StringList list = File::findFiles(Util::getPath(Util::PATH_USER_CONFIG), "files_" + Util::toString(profile) + "?*.xml.bz2");
	for (auto& f : list) {
		if (f != getFileName())
			File::deleteFile(f);
	}
}

ShareProfileInfo::ShareProfileInfo(const string& aName, ProfileToken aToken /*rand*/, State aState /*STATE_NORMAL*/) : name(aName), token(aToken), state(aState) {}

string ShareProfileInfo::getDisplayName() const {
	string ret = name;
	if (isDefault) {
		ret += " (" + STRING(DEFAULT) + ")";
	}
	return ret;
}

ShareProfile::ShareProfile(const string& aName, ProfileToken aToken) : token(aToken), plainName(aName), fileList(aToken) {}

ShareProfile::~ShareProfile() {

}

bool ShareProfile::hasCommonProfiles(const ProfileTokenSet& a, const ProfileTokenSet& b) noexcept {
	for (auto profileToken : a) {
		if (b.find(profileToken) != b.end()) {
			return true;
		}
	}

	return false;
}

StringList ShareProfile::getProfileNames(const ProfileTokenSet& aTokens, const ShareProfileList& aProfiles) noexcept {
	StringList ret;
	for (auto profileToken : aTokens) {
		auto p = find(aProfiles.begin(), aProfiles.end(), profileToken);
		if (p != aProfiles.end()) {
			ret.push_back((*p)->getPlainName());
		}
	}

	return ret;
}

string ShareProfile::getDisplayName() const noexcept {
	string ret = plainName;
	if (token == SETTING(DEFAULT_SP)) {
		ret += " (" + STRING(DEFAULT) + ")";
	}
	return ret;
}

FileList* ShareProfile::getProfileList() noexcept {
	return &fileList;
}

bool ShareProfile::isDefault() const noexcept {
	return token == SETTING(DEFAULT_SP);
}

bool ShareProfile::isHidden() const noexcept {
	return token == SP_HIDDEN;
}

} //dcpp