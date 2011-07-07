/*
 * Copyright (C) 2001-2010 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_DOWNLOAD_MANAGER_H
#define DCPLUSPLUS_DCPP_DOWNLOAD_MANAGER_H

#include "forward.h"

#include "DownloadManagerListener.h"
#include "UserConnectionListener.h"
#include "QueueItem.h"
#include "TimerManager.h"
#include "Singleton.h"
#include "MerkleTree.h"
#include "Speaker.h"
#include "File.h"

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
	void addConnection(UserConnectionPtr conn);
	bool checkIdle(const UserPtr& user, bool partialList);

	/** @internal */
	void abortDownload(const string& aTarget);

	/** @return Running average download speed in Bytes/s */
	int64_t getRunningAverage();

	/** @return Number of downloads. */ 
	size_t getDownloadCount() {
		Lock l(cs);
		return downloads.size();
	}

	bool startDownload(QueueItem::Priority prio);
		
	int64_t getAverageSpeed(const string & path){
		Lock l(cs);
		size_t pos = path.rfind("\\");
		string tmp = path.substr(0, pos);
		
		return averageSpeedMap.find(tmp)->second;
	}

	uint64_t getAveragePos(const string & path) {
		Lock l(cs);
		size_t pos = path.rfind("\\");
		string tmp = path.substr(0, pos);

		return averagePosMap.find(tmp)->second;
	}
	


private:
	
	CriticalSection cs;
	DownloadList downloads;
	UserConnectionList idlers;

	void removeConnection(UserConnectionPtr aConn);
	void removeDownload(Download* aDown);
	void fileNotAvailable(UserConnection* aSource);
	void noSlots(UserConnection* aSource, string param = Util::emptyString);
	
	int64_t getResumePos(const string& file, const TigerTree& tt, int64_t startPos);

	void failDownload(UserConnection* aSource, const string& reason);

	friend class Singleton<DownloadManager>;

	DownloadManager();
	~DownloadManager() throw();

	void checkDownloads(UserConnection* aConn);
	void startData(UserConnection* aSource, int64_t start, int64_t newSize, bool z);
	void endData(UserConnection* aSource);

	void onFailed(UserConnection* aSource, const string& aError);

	// UserConnectionListener
	void on(Data, UserConnection*, const uint8_t*, size_t) throw();
	void on(Failed, UserConnection* aSource, const string& aError) throw() { onFailed(aSource, aError); }
	void on(ProtocolError, UserConnection* aSource, const string& aError) throw() { onFailed(aSource, aError); }
	void on(MaxedOut, UserConnection*, string param = Util::emptyString) throw();
	void on(FileNotAvailable, UserConnection*) throw();
	void on(Updated, UserConnection*) throw();
		
	void on(AdcCommand::SND, UserConnection*, const AdcCommand&) throw();
	void on(AdcCommand::STA, UserConnection*, const AdcCommand&) throw();

	// TimerManagerListener
	void on(TimerManagerListener::Second, uint64_t aTick) throw();

	typedef map< string, int64_t > StringIntMap;
	typedef StringIntMap::iterator StringIntIter;
	typedef pair< string, int64_t > StringIntPair;

	StringIntMap averageSpeedMap;
	StringIntMap averagePosMap;

};

} // namespace dcpp

#endif // !defined(DOWNLOAD_MANAGER_H)

/**
 * @file
 * $Id: DownloadManager.h 500 2010-06-25 22:08:18Z bigmuscle $
 */
