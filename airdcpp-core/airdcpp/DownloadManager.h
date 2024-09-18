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

#ifndef DCPLUSPLUS_DCPP_DOWNLOAD_MANAGER_H
#define DCPLUSPLUS_DCPP_DOWNLOAD_MANAGER_H

#include "forward.h"

#include "DownloadManagerListener.h"
#include "UserConnectionListener.h"
#include "TimerManagerListener.h"
#include "Singleton.h"
#include "Speaker.h"

#include "CriticalSection.h"
#include "GetSet.h"
#include "MerkleTree.h"
#include "QueueItemBase.h"
#include "Util.h"

namespace dcpp {

/**
 * Singleton. Use its listener interface to update the download list
 * in the user interface.
 */
class DownloadManager : public Speaker<DownloadManagerListener>, 
	private UserConnectionListener, private TimerManagerListener, 
	public Singleton<DownloadManager>
{
public:

	/** @internal */
	void addConnection(UserConnection* conn);
	bool checkIdle(const UserPtr& aUser, bool aSmallSlot);
	bool checkIdle(const string& aToken);

	/** @internal */
	void abortDownload(const string& aTarget, const UserPtr& aUser = nullptr);
	void disconnectBundle(const BundlePtr& aBundle, const UserPtr& aUser = nullptr);

	/** @return Running average download speed in Bytes/s */
	int64_t getRunningAverage() const;

	size_t getTotalDownloadConnectionCount() const noexcept;
	size_t getFileDownloadConnectionCount() const noexcept;
	size_t getBundleDownloadConnectionCount(const BundlePtr& aBundle) const noexcept;

	// This will ignore bundles with no downloads and 
	// bundles using highest priority
	QueueTokenSet getRunningBundles(bool aIgnoreHighestPrio = true) const noexcept;
	size_t getRunningBundleCount() const noexcept;

	SharedMutex& getCS() noexcept { return cs; }
	const DownloadList& getDownloads() const noexcept {
		return downloads;
	}

	IGETSET(int64_t, lastUpSpeed, LastUpSpeed, 0);
	IGETSET(int64_t, lastDownSpeed, LastDownSpeed, 0);
private:
	
	mutable SharedMutex cs;
	DownloadList downloads;

	// The list of bundles being download. Note that all of them may not be running
	// as the bundle is removed from here only after the connection has been 
	// switched to use another bundle (or no other downloads were found)
	UserConnectionList idlers;

	void disconnect(UserConnectionPtr aConn, bool aGraceless = false) const noexcept;
	void removeConnection(UserConnectionPtr aConn);
	void removeDownload(Download* aDown);
	void fileNotAvailable(UserConnection* aSource, bool aNoAccess, const string& aMessage = Util::emptyString);
	void noSlots(UserConnection* aSource, const string& param = Util::emptyString);

	void failDownload(UserConnection* aSource, const string& reason, bool rotateQueue);

	friend class Singleton<DownloadManager>;

	DownloadManager();
	~DownloadManager() final;

	void checkDownloads(UserConnection* aConn);
	bool disconnectSlowSpeed(Download* aDownload, uint64_t aTick) const noexcept;
	void startData(UserConnection* aSource, int64_t start, int64_t newSize, bool z);

	void revive(UserConnection* uc);
	void endData(UserConnection* aSource);

	void onFailed(UserConnection* aSource, const string& aError);

	// UserConnectionListener
	void on(UserConnectionListener::Data, UserConnection*, const uint8_t*, size_t) noexcept override;
	void on(UserConnectionListener::Failed, UserConnection* aSource, const string& aError) noexcept override { onFailed(aSource, aError); }
	void on(UserConnectionListener::ProtocolError, UserConnection* aSource, const string& aError) noexcept override { onFailed(aSource, aError); }
	void on(UserConnectionListener::MaxedOut, UserConnection*, const string& param) noexcept override;
	void on(UserConnectionListener::FileNotAvailable, UserConnection*) noexcept override;
		
	void on(AdcCommand::SND, UserConnection*, const AdcCommand&) noexcept override;
	void on(AdcCommand::STA, UserConnection*, const AdcCommand&) noexcept override;

	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t aTick) noexcept override;

	// Statistics
	uint64_t lastUpdate = 0;
	int64_t lastUpBytes = 0;
	int64_t lastDownBytes = 0;

	string updateConnectionHubUrl(UserConnection* aSource, const string& aNewHubUrl) const noexcept;
};

} // namespace dcpp

#endif // !defined(DOWNLOAD_MANAGER_H)