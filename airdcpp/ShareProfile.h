/*
 * Copyright (C) 2012-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SHAREPROFILE_H_
#define DCPLUSPLUS_DCPP_SHAREPROFILE_H_

#include <string>
#include "forward.h"

#include "File.h"
#include "GetSet.h"
#include "HashValue.h"
#include "TigerHash.h"
#include "Util.h"

namespace dcpp {

using std::string;


/*
A Class that holds info on a profile specific file list
..*/
class FileList {
	public:
		FileList(ProfileToken aProfile);

		GETSET(TTHValue, xmlRoot, XmlRoot);
		GETSET(TTHValue, bzXmlRoot, BzXmlRoot);
		GETSET(ProfileToken, profile, Profile);

		IGETSET(int64_t, xmlListLen, XmlListLen, 0);
		IGETSET(int64_t, bzXmlListLen, BzXmlListLen, 0);
		IGETSET(uint64_t, lastXmlUpdate, LastXmlUpdate, 0);
		IGETSET(bool, xmlDirty, XmlDirty, true);
		IGETSET(bool, forceXmlRefresh, ForceXmlRefresh, true); /// bypass the 15-minutes guard

		unique_ptr<File> bzXmlRef;
		string getFileName() const noexcept;

		bool allowGenerateNew(bool aForce = false) noexcept;
		void generationFinished(bool aFailed) noexcept;
		void saveList();
		CriticalSection cs;
		int getCurrentNumber() const noexcept { return listN; }
	private:
		int listN = 0;
};

class ShareProfileInfo;
typedef std::shared_ptr<ShareProfileInfo> ShareProfileInfoPtr;

class ShareProfileInfo : public FastAlloc<ShareProfileInfo> {
public:
	enum State {
		STATE_NORMAL,
		STATE_ADDED,
		STATE_REMOVED,
		STATE_RENAMED
	};

	ShareProfileInfo(const string& aName, ProfileToken aToken = Util::randInt(100), State aState = STATE_NORMAL);
	~ShareProfileInfo() {}

	string name;
	const ProfileToken token;
	bool isDefault = false;
	State state;

	typedef vector<ShareProfileInfoPtr> List;
	string getDisplayName() const;
};

inline bool operator==(const ShareProfileInfoPtr& ptr, ProfileToken aToken) { return ptr->token == aToken; }

class ShareProfile {
public:
	static bool hasCommonProfiles(const ProfileTokenSet& a, const ProfileTokenSet& b) noexcept;
	static StringList getProfileNames(const ProfileTokenSet& aTokens, const ShareProfileList& aProfiles) noexcept;

	struct Hash {
		size_t operator()(const ShareProfilePtr& x) const { return x->getToken(); }
	};

	GETSET(ProfileToken, token, Token);
	GETSET(string, plainName, PlainName);
	IGETSET(bool, profileInfoDirty, ProfileInfoDirty, true);

	// For caching the last information (these should only be accessed from ShareManager, use ShareManager::getProfileInfo for up-to-date information)
	IGETSET(int64_t, shareSize, ShareSize, 0);
	IGETSET(size_t, sharedFiles, SharedFiles, 0);

	ShareProfile(const string& aName = Util::emptyString, ProfileToken aToken = Util::randInt(100));
	~ShareProfile();

	FileList* getProfileList() noexcept;
	bool isDefault() const noexcept;
	bool isHidden() const noexcept;
	string getDisplayName() const noexcept;

	typedef unordered_set<ShareProfilePtr, Hash> Set;
	typedef vector<ShareProfilePtr> List;

	struct NotHidden {
		bool operator()(const ShareProfilePtr& aProfile) const {
			return !aProfile->isHidden();
		}
	};
private:
	FileList fileList;
};

inline bool operator==(const ShareProfilePtr& ptr, ProfileToken aToken) noexcept { return ptr->getToken() == aToken; }

}

#endif /* DCPLUSPLUS_DCPP_SHAREPROFILE_H_ */
