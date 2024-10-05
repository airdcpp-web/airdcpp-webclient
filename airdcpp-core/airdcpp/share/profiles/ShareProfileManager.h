/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SHARE_PROFILE_MANAGER_H
#define DCPLUSPLUS_DCPP_SHARE_PROFILE_MANAGER_H

#include <airdcpp/share/profiles/ShareProfileManagerListener.h>

#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/message/Message.h>
#include <airdcpp/core/Speaker.h>
#include <airdcpp/share/profiles/ShareProfile.h>

namespace dcpp {

class ShareProfileManager : public Speaker<ShareProfileManagerListener> {
public:
	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;

	void shutdown(const ProgressFunction& progressF) noexcept;

	// Returns TTH value for a file list (not very useful but the ADC specs...)
	// virtualFile = name requested by the other user (Transfer::USER_LIST_NAME_BZ or Transfer::USER_LIST_NAME)
	// Throws ShareException
	TTHValue getListTTH(const string& aVirtualPath, ProfileToken aProfile) const;

	void addProfiles(const ShareProfileInfo::List& aProfiles) noexcept;
	void removeProfiles(const ShareProfileInfo::List& aProfiles) noexcept;
	void renameProfiles(const ShareProfileInfo::List& aProfiles) noexcept;

	void addProfile(const ShareProfilePtr& aProfile) noexcept;
	void updateProfile(const ShareProfilePtr& aProfile) noexcept;
	bool removeProfile(ProfileToken aToken) noexcept;

	// If allowFallback is true, the default profile will be returned if the requested one is not found
	ShareProfilePtr getShareProfile(ProfileToken aProfile, bool allowFallback = false) const noexcept;

	ShareProfileList getProfiles() const noexcept;
	ShareProfileInfo::List getProfileInfos() const noexcept;

	// Get a profile token by its display name
	OptionalProfileToken getProfileByName(const string& aName) const noexcept;

	void setDefaultProfile(ProfileToken aNewDefault) noexcept;

	// aIsMajor will regenerate the file list on next time when someone requests it
	void setProfilesDirty(const ProfileTokenSet& aProfiles, bool aIsMajor) noexcept;

	void ensureDefaultProfiles() noexcept;

	void removeCachedFilelists() noexcept;

	ShareProfilePtr loadProfile(SimpleXML& aXml, bool aIsDefault);
	void saveProfile(const ShareProfilePtr& aProfile, SimpleXML& aXml) const;

	using ProfileCallback = std::function<void (const ShareProfilePtr &)>;
	explicit ShareProfileManager(ProfileCallback&& aOnRemoveProfile);
	~ShareProfileManager();
private:
	ShareProfilePtr getShareProfileUnsafe(ProfileToken aProfile) const noexcept;

	mutable SharedMutex cs;

	// Throws ShareException
	FileList* getProfileFileListUnsafe(ProfileToken aProfile) const;

	ShareProfileList shareProfiles;

	const ProfileCallback onRemoveProfile;
}; //sharemanager end

} // namespace dcpp

#endif // !defined(SHARE_MANAGER_H)
