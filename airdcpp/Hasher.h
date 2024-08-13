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

#ifndef DCPLUSPLUS_DCPP_HASHER_H
#define DCPLUSPLUS_DCPP_HASHER_H

#include "typedefs.h"

#include "CriticalSection.h"
#include "HasherManager.h"
#include "PathUtil.h"
#include "Semaphore.h"
#include "SortedVector.h"
#include "Thread.h"
#include "Util.h"

namespace dcpp {
	typedef int64_t devid;
	class DirSFVReader;
	class HasherStats;
	class Hasher : public Thread {
	public:
		/** We don't keep leaves for blocks smaller than this... */
		static const int64_t MIN_BLOCK_SIZE;

		Hasher(bool isPaused, int aHasherID, HasherManager* aManager);

		bool hashFile(const string& filePath, const string& filePathLower, int64_t size, devid aDeviceId) noexcept;

		/// @return whether hashing was already paused
		bool pause() noexcept;
		void resume();
		bool isPaused() const noexcept;
		bool isRunning() const noexcept;

		void clear() noexcept;
		void stop() noexcept;

		void stopHashing(const string& baseDir) noexcept;
		int run();
		void getStats(string& curFile_, int64_t& bytesLeft_, size_t& filesLeft_, int64_t& speed_, size_t& filesAdded_, int64_t& bytesAdded_) const noexcept;
		void shutdown();

		bool hasFile(const string& aPath) const noexcept;
		bool hasDevice(int64_t aDeviceId) const noexcept;
		bool hasDevices() const noexcept;
		int64_t getTimeLeft() const noexcept;

		int64_t getBytesLeft() const noexcept { return totalBytesLeft; }

		const int hasherID;
		static SharedMutex hcs;
	private:
		HasherManager* manager;

		void clearStats() noexcept;

		class WorkItem {
		public:
			WorkItem(const string& aFilePathLower, const string& aFilePath, int64_t aSize, devid aDeviceId) noexcept
				: filePath(aFilePath), fileSize(aSize), deviceId(aDeviceId), filePathLower(aFilePathLower) { 

				dcassert(aDeviceId >= 0);
			}

			WorkItem() {}

			WorkItem(WorkItem&& rhs) = default;
			WorkItem& operator=(WorkItem&&) = default;
			WorkItem(const WorkItem&) = delete;
			WorkItem& operator=(const WorkItem&) = delete;

			string filePath;
			int64_t fileSize = 0;
			devid deviceId = -1;
			string filePathLower;

			struct NameLower {
				const string& operator()(const WorkItem& a) const { return a.filePathLower; }
			};
		};

		void processQueue() noexcept;
		optional<HashedFile> hashFile(const WorkItem& aItem, HasherStats& stats_, const DirSFVReader& aSFV) noexcept;

		SortedVector<WorkItem, std::deque, string, PathUtil::PathSortOrderInt, WorkItem::NameLower> w;

		Semaphore s;
		void removeDevice(devid aDevice) noexcept;

		bool isShutdown = false;
		bool stopping = false;
		bool running = false;
		bool paused;

		string currentFile;
		atomic<int64_t> totalBytesLeft = 0;
		atomic<int64_t> totalBytesAdded = 0;
		atomic<int64_t> lastSpeed = 0;
		atomic<int64_t> totalFilesAdded = 0;

		void instantPause();

		map<devid, int> devices;

		void logHasher(const string& aMessage, LogMessage::Severity aSeverity, bool aLock) const noexcept;

		void logHashedDirectory(const string& aPath, const string& aLastFilePath, const HasherStats& aStats) const noexcept;
		void logHashedFile(const string& aPath, int64_t aSpeed) const noexcept;
		void logFailedFile(const string& aPath, const string& aError) const noexcept;
	};

} // namespace dcpp

#endif // !defined(HASHER_H)