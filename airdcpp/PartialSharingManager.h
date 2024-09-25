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

#ifndef DCPLUSPLUS_DCPP_PARTIAL_SHARING_MANAGER_H
#define DCPLUSPLUS_DCPP_PARTIAL_SHARING_MANAGER_H

#include <airdcpp/Singleton.h>

#include <airdcpp/PartialBundleSharingManager.h>
#include <airdcpp/PartialFileSharingManager.h>
#include <airdcpp/UploadFileProvider.h>
#include <airdcpp/UploadManagerListener.h>
#include <airdcpp/UploadSlot.h>


namespace dcpp {

struct ParsedUpload;
class UserConnection;
class PartialSharingManager : public Singleton<PartialSharingManager>, public UploadFileProvider, public UploadManagerListener
{
public:
	PartialBundleSharingManager bundles;
	PartialFileSharingManager files;

	PartialSharingManager();

	bool toRealWithSize(const UploadFileQuery& aQuery, string& path_, int64_t& size_, bool& noAccess_) const noexcept override;
	void getRealPaths(const TTHValue& root, StringList& paths_) const noexcept override;
	void getBloom(ProfileToken aToken, HashBloom& bloom_) const noexcept override;
	void getBloomFileCount(ProfileToken aToken, size_t& fileCount_) const noexcept override;

	const string& getProviderName() const noexcept override {
		return providerName;
	}

	const string providerName = "partial_sharing";
private:
	uint8_t extraPartial = 0;
	ActionHookResult<OptionalUploadSlot> onSlotType(const UserConnection& aUserConnection, const ParsedUpload&, const ActionHookResultGetter<OptionalUploadSlot>& aResultGetter) const noexcept;

	void on(UploadManagerListener::Created, Upload*, const UploadSlot& aNewSlot) noexcept override;
	void on(UploadManagerListener::Failed, const Upload*, const string&) noexcept override;

	QueueItemList getBloomFiles() const noexcept;
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_PARTIAL_SHARING_MANAGER_H)