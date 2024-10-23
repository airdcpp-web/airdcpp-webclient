/*
 * Copyright (C) 2011-2024 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_UPLOAD_BUNDLE_SENDER_H_
#define DCPLUSPLUS_DCPP_UPLOAD_BUNDLE_SENDER_H_

#include <airdcpp/core/thread/CriticalSection.h>
#include <airdcpp/message/Message.h>
#include <airdcpp/user/User.h>

#include <airdcpp/transfer/download/DownloadManagerListener.h>
#include <airdcpp/queue/QueueManagerListener.h>



namespace dcpp {

class UploadBundleInfoSender: public DownloadManagerListener, public QueueManagerListener {
public:
	static const string FEATURE_ADC_UBN1;

	void dbgMsg(const string& aMsg, LogMessage::Severity aSeverity) const noexcept;

	class UBNBundle {
	public:
		using SendUpdateF = std::function<void (AdcCommand &, const UserPtr &)>;
		using DebugMsgF = std::function<void (const string &, LogMessage::Severity)>;

		static string formatSpeed(int64_t aSpeed) noexcept;

		void onDownloadTick() noexcept;

		bool addRunningUser(const UserConnection* aSource) noexcept;
		bool removeRunningUser(const UserConnection* aSource, bool sendRemove) noexcept;
		void setUserMode(bool aSetSingleUser) noexcept;

		void sendSizeUpdate() const noexcept;

		using Ptr = shared_ptr<UBNBundle>;

		UBNBundle(const BundlePtr& aBundle, SendUpdateF&& aSendUpdate, DebugMsgF&& aDebugMsgF) : 
			sendUpdate(std::move(aSendUpdate)), debugMsg(std::move(aDebugMsgF)), bundle(aBundle) {}

		AdcCommand getAddCommand(const string& aConnectionToken, bool aNewBundle) const noexcept;
		AdcCommand getRemoveCommand(const string& aConnectionToken) const noexcept;
		AdcCommand getBundleFinishedCommand() const noexcept;
		AdcCommand getUserModeCommand() const noexcept;
		AdcCommand getBundleSizeUpdateCommand() const noexcept;
		AdcCommand getTickCommand(const string& aPercent, const string& aSpeed) const noexcept;

		const BundlePtr& getBundle() const noexcept {
			return bundle;
		}
	private:
		SendUpdateF sendUpdate;
		DebugMsgF debugMsg;

		bool singleUser = true;
		int64_t lastSpeed = 0; // the speed sent on last time to UBN sources
		int64_t lastDownloaded = 0; // the progress percent sent on last time to UBN sources

		void getTickParams(string& percentStr_, string& speedStr_) noexcept;

		BundlePtr bundle;

		unordered_map<UserPtr, StringSet, User::Hash> uploadReports;
	};

	UploadBundleInfoSender() noexcept;
	~UploadBundleInfoSender() noexcept final;
private:
	mutable SharedMutex cs;

	void on(QueueManagerListener::BundleSize, const BundlePtr& aBundle) noexcept override;

	void on(DownloadManagerListener::Starting, const Download*) noexcept override;
	void on(DownloadManagerListener::Failed, const Download*, const string&) noexcept override;
	void on(DownloadManagerListener::BundleTick, const BundleList& aBundles, uint64_t aTick) noexcept override;
	void on(DownloadManagerListener::Remove, const UserConnection* aConn) noexcept override;
	void on(DownloadManagerListener::Idle, const UserConnection* aConn, const string& aError) noexcept override;


	unordered_map<QueueToken, UBNBundle::Ptr> bundleTokenMap;
	unordered_map<string, UBNBundle::Ptr> connectionTokenMap;

	UBNBundle::Ptr findInfoByBundleToken(QueueToken aBundleToken) const noexcept;
	UBNBundle::Ptr findInfoByConnectionToken(const string& aDownloadToken) const noexcept;

	void removeRunningUser(const UserConnection* aSource, bool aSendRemove) noexcept;

	void addRunningUserUnsafe(const UBNBundle::Ptr& aBundle, const UserConnection* aSource) noexcept;
	void removeRunningUserUnsafe(const UBNBundle::Ptr& aBundle, const UserConnection* aSource, bool sendRemove) noexcept;

	void sendUpdate(AdcCommand& aCmd, const UserPtr& aUser) noexcept;
};

}

#endif /* DCPLUSPLUS_DCPP_UBN_MANAGER_H_ */
