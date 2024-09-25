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

#ifndef DCPLUSPLUS_DCPP_HASH_MANAGER_H
#define DCPLUSPLUS_DCPP_HASH_MANAGER_H

#include <functional>
#include <airdcpp/typedefs.h>

#include <airdcpp/DbHandler.h>
#include <airdcpp/HasherManager.h>
#include <airdcpp/HasherStats.h>
#include <airdcpp/HashManagerListener.h>
#include <airdcpp/MerkleTree.h>
#include <airdcpp/Message.h>
#include <airdcpp/Singleton.h>
#include <airdcpp/Speaker.h>
#include <airdcpp/Thread.h>

namespace dcpp {

class Hasher;
class HashStore;
class HasherStats;
class HashedFile;

class HashManager : public Singleton<HashManager>, public Speaker<HashManagerListener>, public HasherManager {

public:
	HashManager();
	~HashManager() override;

	/**
	 * Check if the TTH tree associated with the filename is current.
	 */
	bool checkTTH(const string& aFileLower, const string& aFileName, HashedFile& fi_);

	void stopHashing(const string& aBaseDir) noexcept;
	void setPriority(Thread::Priority p) noexcept;


	// @return HashedFileInfo
	// Throws HashException
	void getFileInfo(const string& aFileLower, const string& aFileName, HashedFile& aFileInfo);

	bool getTree(const TTHValue& root, TigerTree& tt) noexcept;

	/** Return block size of the tree associated with root, or 0 if no such tree is in the store */
	size_t getBlockSize(const TTHValue& aRoot) noexcept;

	static int64_t getMinBlockSize() noexcept;

	// Throws HashException
	void renameFileThrow(const string& aOldPath, const string& aNewPath);

	struct HashStats {
		string curFile;
		int64_t bytesLeft = 0;
		size_t filesLeft = 0;
		int64_t speed = 0;
		size_t filesAdded = 0;
		int64_t bytesAdded = 0;
		int hashersRunning = 0;
		bool isPaused = true;

		bool operator==(const HashStats& rhs) const noexcept {
			return 
				curFile == rhs.curFile && 
				bytesLeft == rhs.bytesLeft &&
				filesLeft == rhs.filesLeft &&
				speed == rhs.speed &&
				filesAdded == rhs.filesLeft &&
				bytesAdded == rhs.bytesAdded &&
				hashersRunning == rhs.hashersRunning &&
				isPaused == rhs.isPaused;
		}
	};

	HashStats getStats() const noexcept;

	// Get TTH for a file synchronously (and optionally stores the hash information)
	// Throws HashException/FileException
	void getFileTTH(const string& aFile, int64_t aSize, bool aAddStore, TTHValue& tth_, int64_t& sizeLeft_, const bool& aCancel, std::function<void(int64_t /*timeLeft*/, const string& /*fileName*/)> updateF = nullptr);

	/**
	 * Rebuild hash data file
	 */
	void startMaintenance(bool aVerify);

	// Throws Exception in case of fatal errors
	void startup(StartupLoader& aLoader);
	void stop() noexcept;
	void shutdown(ProgressFunction progressF) noexcept;

	struct HashPauser {
		HashPauser();
		~HashPauser();
	};
	
	/// @return whether hashing was already paused
	bool pauseHashing() noexcept;
	void resumeHashing(bool forced = false);	
	bool isHashingPaused(bool lock = true) const noexcept;

	string getDbStats() noexcept;
	void compact() noexcept;

	void close() noexcept;
	void onScheduleRepair(bool aSchedule) noexcept;
	bool isRepairScheduled() const noexcept;
	void getDbSizes(int64_t& fileDbSize_, int64_t& hashDbSize_) const noexcept;
	bool maintenanceRunning() const noexcept;

	// Throws HashException
	bool addFile(const string& aFilePathLower, const HashedFile& fi_);

	// Throws HashException
	void addTree(const TigerTree& tree);
private:
	int pausers = 0;

	friend class Hasher;

	void onFileHashed(const string& aPath, HashedFile& aFile, const TigerTree& aTree, int aHasherId) noexcept override;
	void onFileFailed(const string& aPath, const string& aErrorId, const string& aMessage, int aHasherId) noexcept override;
	void onDirectoryHashed(const string& aPath, const HasherStats&, int aHasherId) noexcept override;
	void onHasherFinished(int aDirectoriesHashed, const HasherStats&, int aHasherId) noexcept override;
	void removeHasher(int aHasherId) noexcept override;
	void logHasher(const string& aMessage, int aHasherID, LogMessage::Severity aSeverity, bool aLock) const noexcept override;

	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;

	Hasher* createHasher() noexcept;
	Hasher* getFileHasher(int64_t aDeviceId, int64_t aSize) const noexcept;
	bool isPathQueued(const string& aPathLower) const noexcept;

	bool hashFile(const string& filePath, const string& pathLower, int64_t size);
	bool isShutdown = false;

	using HasherList = vector<Hasher *>;
	HasherList hashers;

	unique_ptr<HashStore> store;

	/** Single node tree where node = root, no storage in HashData.dat */
	static const int64_t SMALL_TREE = -1;

	class Optimizer : public Thread {
	public:
		Optimizer();
		~Optimizer() override;

		void startMaintenance(bool verify);
		bool isRunning() const noexcept { return running; }
	private:
		bool verify = true;
		atomic<bool> running = { false };
		int run() override;
	};

	Optimizer optimizer;
};

} // namespace dcpp

#endif // !defined(HASH_MANAGER_H)