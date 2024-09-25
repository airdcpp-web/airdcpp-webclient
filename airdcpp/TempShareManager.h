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

#ifndef DCPLUSPLUS_DCPP_TEMPSHARE_MANAGER_H
#define DCPLUSPLUS_DCPP_TEMPSHARE_MANAGER_H

#include <airdcpp/typedefs.h>

#include <airdcpp/CriticalSection.h>
#include <airdcpp/UploadFileProvider.h>
#include <airdcpp/Singleton.h>
#include <airdcpp/Speaker.h>
#include <airdcpp/TempShareItem.h>
#include <airdcpp/TempShareManagerListener.h>

namespace dcpp {

class TempShareManager : public Speaker<TempShareManagerListener>, public Singleton<TempShareManager>, public UploadFileProvider {
public:
	TempShareManager();
	~TempShareManager() override;

	optional<TempShareInfo> addTempShare(const TTHValue& aTTH, const string& aName, const string& aFilePath, int64_t aSize, ProfileToken aProfile, const UserPtr& aUser) noexcept;
	bool removeTempShare(TempShareToken aId) noexcept;

	using TempShareMap = unordered_multimap<TTHValue, TempShareInfo>;

	TempShareInfoList getTempShares() const noexcept;
	TempShareInfoList getTempShares(const TTHValue& aTTH) const noexcept;

	optional<TempShareToken> isTempShared(const UserPtr& aUser, const TTHValue& aTTH) const noexcept;

	bool toRealWithSize(const UploadFileQuery& aQuery, string& path_, int64_t& size_, bool& noAccess_) const noexcept override;
	void getRealPaths(const TTHValue& root, StringList& paths_) const noexcept override;
	void getBloom(ProfileToken aToken, HashBloom& bloom_) const noexcept override;
	void getBloomFileCount(ProfileToken aToken, size_t& fileCount_) const noexcept override;
	void search(SearchResultList& results, const TTHValue& aTTH, const ShareSearch& aSearchInfo) const noexcept override;
	const string& getProviderName() const noexcept override {
		return providerName;
	}

	const string providerName = "temp_share";
private:
	mutable SharedMutex cs;

	TempShareMap tempShares;

	// Add temp share item
	// The boolean will false for files that are temp shared already
	pair<TempShareInfo, bool> addTempShareImpl(const TTHValue& aTTH, const string& aName, const string& aFilePath, int64_t aSize, const UserPtr& aUser) noexcept;
	optional<TempShareInfo> removeTempShareImpl(TempShareToken aId) noexcept;
};


} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_TEMPSHARE_MANAGER_H)
