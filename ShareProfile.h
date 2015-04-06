/*
 * Copyright (C) 2012-2015 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_SHAREPROFILE_H_
#define DCPLUSPLUS_DCPP_SHAREPROFILE_H_

#include <string>
#include "forward.h"

#include "File.h"
#include "GetSet.h"
#include "HashValue.h"
#include "Pointer.h"
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
		string getFileName();

		bool allowGenerateNew(bool force=false);
		void generationFinished(bool failed);
		void saveList();
		CriticalSection cs;
		int getCurrentNumber() const { return listN; }
	private:
		int listN = 0;
};

class ShareProfileInfo;
typedef boost::intrusive_ptr<ShareProfileInfo> ShareProfileInfoPtr;

class ShareProfileInfo : public FastAlloc<ShareProfileInfo>, public intrusive_ptr_base<ShareProfileInfo> {
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
	ProfileToken token;
	bool isDefault = false;
	State state;

	typedef vector<ShareProfileInfoPtr> List;
	string getDisplayName() const;

	/*class PathCompare {
	public:
		PathCompare(const string& compareTo) : a(compareTo) { }
		bool operator()(const ShareDirInfoPtr& p) { return Util::stricmp(p->path.c_str(), a.c_str()) == 0; }
	private:
		PathCompare& operator=(const PathCompare&) ;
		const string& a;
	};*/
};

inline bool operator==(const ShareProfileInfoPtr& ptr, ProfileToken aToken) { return ptr->token == aToken; }

class ShareProfile : public intrusive_ptr_base<ShareProfile> {
public:
	struct Hash {
		size_t operator()(const ShareProfilePtr& x) const { return x->getToken(); }
	};

	GETSET(ProfileToken, token, Token);
	GETSET(string, plainName, PlainName);
	IGETSET(bool, profileInfoDirty, ProfileInfoDirty, true);
	IGETSET(int64_t, shareSize, ShareSize, 0);
	IGETSET(size_t, sharedFiles, SharedFiles, 0);

	string getDisplayName() const;
	ShareProfile(const string& aName, ProfileToken aToken = Util::randInt(100));
	~ShareProfile();

	FileList* getProfileList();
	typedef unordered_set<ShareProfilePtr, Hash> Set;
	typedef vector<ShareProfilePtr> List;
private:
	FileList fileList;
};

inline bool operator==(const ShareProfilePtr& ptr, ProfileToken aToken) { return ptr->getToken() == aToken; }

}

#endif /* DCPLUSPLUS_DCPP_SHAREPROFILE_H_ */
