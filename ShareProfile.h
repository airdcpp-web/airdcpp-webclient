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

#ifndef DCPLUSPLUS_DCPP_SHAREPROFILE_H_
#define DCPLUSPLUS_DCPP_SHAREPROFILE_H_

#include <string>

#include "forward.h"
#include "Pointer.h"
#include "GetSet.h"
#include "Util.h"
#include "HashValue.h"
#include "TigerHash.h"
#include "File.h"

namespace dcpp {

using std::string;


/*
A Class that holds info on Hub spesific Filelist,
a Full FileList that contains all like it did before is constructed with sharemanager instance, and then updated like before,
this means that we should allways have FileListALL, other lists are just extra.
Now this would be really simple if just used recursive Locks in sharemanager, to protect everything at once.
BUT i dont want freezes and lockups so lets make it a bit more complex :) 
..*/
class FileList {
	public:
		FileList(ProfileToken aProfile);

		GETSET(int64_t, xmlListLen, XmlListLen);
		GETSET(TTHValue, xmlRoot, XmlRoot);
		GETSET(int64_t, bzXmlListLen, BzXmlListLen);
		GETSET(TTHValue, bzXmlRoot, BzXmlRoot);
		GETSET(uint64_t, lastXmlUpdate, LastXmlUpdate);
		GETSET(ProfileToken, profile, Profile);
		GETSET(bool, xmlDirty, XmlDirty);
		GETSET(bool, forceXmlRefresh, ForceXmlRefresh); /// bypass the 15-minutes guard

		//static atomic_flag generating;

		unique_ptr<File> bzXmlRef;
		string getFileName();

		bool generateNew(bool force=false);
		void unsetDirty(bool failed);
		void saveList();
	private:
		CriticalSection cs;
		int listN;
		bool isSavedSuccessfully;
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
	bool isDefault;
	State state;

	typedef vector<ShareProfileInfoPtr> List;
	string getDisplayName() const;

	/*class PathCompare {
	public:
		PathCompare(const string& compareTo) : a(compareTo) { }
		bool operator()(const ShareDirInfoPtr& p) { return stricmp(p->path.c_str(), a.c_str()) == 0; }
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
	GETSET(bool, profileInfoDirty, ProfileInfoDirty);
	GETSET(int64_t, shareSize, ShareSize);
	GETSET(size_t, sharedFiles, SharedFiles);

	GETSET(FileList*, profileList, ProfileList);

	string getDisplayName() const;
	ShareProfile(const string& aName, ProfileToken aToken = Util::randInt(100));
	~ShareProfile();

	FileList* generateProfileList();
	typedef unordered_set<ShareProfilePtr, Hash> Set;
	typedef vector<ShareProfilePtr> List;
};

inline bool operator==(const ShareProfilePtr& ptr, ProfileToken aToken) { return ptr->getToken() == aToken; }

}

#endif /* DCPLUSPLUS_DCPP_SHAREPROFILE_H_ */
