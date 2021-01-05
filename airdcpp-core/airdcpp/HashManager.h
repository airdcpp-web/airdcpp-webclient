/*
 * Copyright (C) 2001-2021 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_HASH_MANAGER_H
#define DCPLUSPLUS_DCPP_HASH_MANAGER_H

#include <functional>
#include "typedefs.h"

#include "DbHandler.h"
#include "HashManagerListener.h"
#include "MerkleTree.h"
#include "Message.h"
#include "Singleton.h"
#include "Speaker.h"
#include "Thread.h"

namespace dcpp {

class File;
class Hasher;
class HashedFile;
class HashLoader;
class FileException;

class HashManager : public Singleton<HashManager>, public Speaker<HashManagerListener> {

public:
	HashManager();
	~HashManager();

	/**
	 * Check if the TTH tree associated with the filename is current.
	 */
	bool checkTTH(const string& fileLower, const string& aFileName, HashedFile& fi_);

	void stopHashing(const string& baseDir) noexcept;
	void setPriority(Thread::Priority p) noexcept;


	// @return HashedFileInfo
	// Throws HashException
	void getFileInfo(const string& fileLower, const string& aFileName, HashedFile& aFileInfo);

	bool getTree(const TTHValue& root, TigerTree& tt) noexcept;

	/** Return block size of the tree associated with root, or 0 if no such tree is in the store */
	size_t getBlockSize(const TTHValue& root) noexcept;

	static int64_t getMinBlockSize() noexcept;

	// Throws HashException
	void addTree(const TigerTree& tree) { store.addTree(tree); }
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
	void getFileTTH(const string& aFile, int64_t aSize, bool addStore, TTHValue& tth_, int64_t& sizeLeft_, const bool& aCancel, std::function<void(int64_t /*timeLeft*/, const string& /*fileName*/)> updateF = nullptr);

	/**
	 * Rebuild hash data file
	 */
	void startMaintenance(bool verify);

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

	string getDbStats() { return store.getDbStats(); }
	void compact() noexcept { store.compact(); }

	void closeDB() { store.closeDb(); }
	void onScheduleRepair(bool schedule) noexcept { store.onScheduleRepair(schedule); }
	bool isRepairScheduled() const noexcept { return store.isRepairScheduled(); }
	void getDbSizes(int64_t& fileDbSize_, int64_t& hashDbSize_) const noexcept { return store.getDbSizes(fileDbSize_, hashDbSize_); }
	bool maintenanceRunning() const noexcept { return optimizer.isRunning(); }

	// Throws HashException
	bool addFile(const string& aFilePathLower, const HashedFile& fi_);
private:
	int pausers = 0;

	friend class Hasher;
	void removeHasher(const Hasher* aHasher);
	void logHasher(const string& aMessage, int aHasherID, bool aIsError, bool aLock);
	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;

	void optimize(bool doVerify) noexcept { store.optimize(doVerify); }

	class HashStore {
	public:
		HashStore();
		~HashStore();

		void addHashedFile(const string& aFilePathLower, const TigerTree& tt, const HashedFile& fi_);
		void addFile(const string& aFilePathLower, const HashedFile& fi_);
		void removeFile(const string& aFilePathLower);

		// Rename a file in the database
		// Throws HashException
		void renameFileThrow(const string& aOldPath, const string& aNewPath);

		void load(StartupLoader& aLoader);

		void optimize(bool doVerify) noexcept;

		bool checkTTH(const string& aFileNameLower, HashedFile& fi_) noexcept;

		void addTree(const TigerTree& tt);
		bool getFileInfo(const string& aFileLower, HashedFile& aFile) noexcept;
		bool getTree(const TTHValue& root, TigerTree& tth);
		bool hasTree(const TTHValue& root);

		enum InfoType {
			TYPE_FILESIZE,
			TYPE_BLOCKSIZE
		};
		int64_t getRootInfo(const TTHValue& aRoot, InfoType aType) noexcept;

		string getDbStats() noexcept;

		void openDb(StartupLoader& aLoader);
		void closeDb() noexcept;

		void onScheduleRepair(bool aSchedule);
		bool isRepairScheduled() const noexcept;

		void getDbSizes(int64_t& fileDbSize_, int64_t& hashDbSize_) const noexcept;
		void compact() noexcept;
	private:
		std::unique_ptr<DbHandler> fileDb;
		std::unique_ptr<DbHandler> hashDb;


		friend class HashLoader;

		/** FOR CONVERSION ONLY: Root -> tree mapping info, we assume there's only one tree for each root (a collision would mean we've broken tiger...) */
		void loadLegacyTree(File& dataFile, int64_t aSize, int64_t aIndex, int64_t aBlockSize, size_t aDataLength, const TTHValue& root, TigerTree& tt_);



		static bool loadTree(const void* src, size_t len, const TTHValue& aRoot, TigerTree& aTree, bool aReportCorruption);

		static bool loadFileInfo(const void* src, size_t len, HashedFile& aFile);
		static void saveFileInfo(void *dest, const HashedFile& aTree);
		static uint32_t getFileInfoSize(const HashedFile& aTree);
	};

	friend class HashLoader;

	bool hashFile(const string& filePath, const string& pathLower, int64_t size);
	bool isShutdown = false;

	typedef vector<Hasher*> HasherList;
	HasherList hashers;

	HashStore store;

	/** Single node tree where node = root, no storage in HashData.dat */
	static const int64_t SMALL_TREE = -1;

	void hasherDone(const string& aFileName, const string& pathLower, const TigerTree& tt, int64_t speed, HashedFile& aFileInfo, int hasherID = 0) noexcept;

	class Optimizer : public Thread {
	public:
		Optimizer();
		~Optimizer();

		void startMaintenance(bool verify);
		bool isRunning() const noexcept { return running; }
	private:
		bool verify = true;
		atomic<bool> running = { false };
		virtual int run();
	};

	Optimizer optimizer;
};

} // namespace dcpp

#endif // !defined(HASH_MANAGER_H)