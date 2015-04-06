/*
 * Copyright (C) 2001-2015 Jacek Sieka, arnetheduck on gmail point com
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
#include "HashedFile.h"
#include "MerkleTree.h"
#include "Semaphore.h"
#include "SFVReader.h"
#include "Singleton.h"
#include "SortedVector.h"
#include "Speaker.h"
#include "Thread.h"

#include "atomic.h"

namespace dcpp {

STANDARD_EXCEPTION(HashException);
class File;

class HashManagerListener {
public:
	virtual ~HashManagerListener() { }
	template<int I>	struct X { enum { TYPE = I };  };

	typedef X<0> TTHDone;
	typedef X<1> HashFailed;
	typedef X<2> MaintananceFinished;
	typedef X<3> MaintananceStarted;

	virtual void on(TTHDone, const string& /* filePath */, HashedFile& /* fileInfo */) noexcept { }
	virtual void on(HashFailed, const string& /* filePath */, HashedFile& /*null*/) noexcept { }
	virtual void on(MaintananceStarted) noexcept { }
	virtual void on(MaintananceFinished) noexcept { }
};

class HashLoader;
class FileException;

class HashManager : public Singleton<HashManager>, public Speaker<HashManagerListener> {

public:

	/** We don't keep leaves for blocks smaller than this... */
	static const int64_t MIN_BLOCK_SIZE;

	HashManager();
	~HashManager();

	/**
	 * Check if the TTH tree associated with the filename is current.
	 */
	bool checkTTH(const string& fileLower, const string& aFileName, HashedFile& fi_);

	void stopHashing(const string& baseDir) noexcept;
	void setPriority(Thread::Priority p) noexcept;

	/** @return HashedFileInfo */
	void getFileInfo(const string& fileLower, const string& aFileName, HashedFile& aFileInfo) throw(HashException);

	bool getTree(const TTHValue& root, TigerTree& tt) noexcept;

	/** Return block size of the tree associated with root, or 0 if no such tree is in the store */
	size_t getBlockSize(const TTHValue& root) noexcept;

	//void addTree(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tt);
	void addTree(const TigerTree& tree) throw(HashException) { store.addTree(tree); }

	void getStats(string& curFile, int64_t& bytesLeft, size_t& filesLeft, int64_t& speed, int& hashers) const noexcept;

	void getFileTTH(const string& aFile, int64_t aSize, bool addStore, TTHValue& tth_, int64_t& sizeLeft_, const bool& aCancel, std::function<void(int64_t /*timeLeft*/, const string& /*fileName*/)> updateF = nullptr)  throw(HashException);

	/**
	 * Rebuild hash data file
	 */
	void startMaintenance(bool verify);

	void startup(StepFunction stepF, ProgressFunction progressF, MessageFunction messageF) throw(HashException);
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

	void renameFile(const string& aOldPath, const string& aNewPath, const HashedFile& fi) throw(HashException);
	bool addFile(const string& aFilePathLower, const HashedFile& fi_) throw(HashException);
private:
	int pausers = 0;
	class Hasher : public Thread {
	public:
		Hasher(bool isPaused, int aHasherID);

		bool hashFile(const string& filePath, const string& filePathLower, int64_t size, const string& devID) noexcept;

		/// @return whether hashing was already paused
		bool pause() noexcept;
		void resume();
		bool isPaused() const noexcept;
		
		void clear() noexcept;

		void stopHashing(const string& baseDir) noexcept;
		int run();
		void getStats(string& curFile, int64_t& bytesLeft, size_t& filesLeft, int64_t& speed) const noexcept;
		void shutdown();

		bool hasFile(const string& aPath) const noexcept;
		bool getPathVolume(const string& aPath, string& vol_) const noexcept;
		bool hasDevice(const string& aID) const noexcept { return devices.find(aID) != devices.end(); }
		bool hasDevices() const noexcept { return !devices.empty(); }
		int64_t getTimeLeft() const noexcept;

		int64_t getBytesLeft() const noexcept { return totalBytesLeft; }
		static SharedMutex hcs;

		const int hasherID;
	private:
		class WorkItem {
		public:
			WorkItem(const string& aFilePathLower, const string& aFilePath, int64_t aSize, const string& aDevID) : filePath(aFilePath), fileSize(aSize), devID(aDevID), filePathLower(aFilePathLower) { }
			WorkItem(WorkItem&& rhs) noexcept;
			WorkItem& operator=(WorkItem&&) noexcept;

			string filePath;
			int64_t fileSize;
			string devID;
			string filePathLower;

			struct NameLower {
				const string& operator()(const WorkItem& a) const { return a.filePathLower; }
			};
		private:
			WorkItem(const WorkItem&);
			WorkItem& operator=(const WorkItem&);
		};

		SortedVector<WorkItem, std::deque, string, Util::PathSortOrderInt, WorkItem::NameLower> w;

		Semaphore s;
		void removeDevice(const string& aID) noexcept;

		bool closing = false;
		bool running = false;
		bool paused;

		string currentFile;
		atomic<int64_t> totalBytesLeft;
		atomic<int64_t> lastSpeed;

		void instantPause();

		int64_t sizeHashed = 0;
		int64_t hashTime = 0;
		int dirsHashed = 0;
		int filesHashed = 0;

		int64_t dirSizeHashed = 0;
		int64_t dirHashTime = 0;
		int dirFilesHashed = 0;
		string initialDir;

		DirSFVReader sfv;

		StringIntMap devices;
	};

	friend class Hasher;
	void removeHasher(Hasher* aHasher);
	void log(const string& aMessage, int hasherID, bool isError, bool lock);

	void optimize(bool doVerify) noexcept { store.optimize(doVerify); }

	class HashStore {
	public:
		HashStore();
		~HashStore();

		void addHashedFile(const string& aFilePathLower, const TigerTree& tt, const HashedFile& fi_) throw(HashException);
		void addFile(const string& aFilePathLower, const HashedFile& fi_) throw(HashException);
		void renameFile(const string& oldPath, const string& newPath, const HashedFile& fi)  throw(HashException);
		void removeFile(const string& aFilePathLower) throw(HashException);

		void load(StepFunction stepF, ProgressFunction progressF, MessageFunction messageF) throw(HashException);

		void optimize(bool doVerify) noexcept;

		bool checkTTH(const string& aFileNameLower, HashedFile& fi_);

		void addTree(const TigerTree& tt) throw(HashException);
		bool getFileInfo(const string& aFileLower, HashedFile& aFile);
		bool getTree(const TTHValue& root, TigerTree& tth);
		bool hasTree(const TTHValue& root) throw(HashException);

		enum InfoType {
			TYPE_FILESIZE,
			TYPE_BLOCKSIZE
		};
		int64_t getRootInfo(const TTHValue& root, InfoType aType);

		string getDbStats() noexcept;

		void openDb(StepFunction stepF, MessageFunction messageF) throw(DbException);
		void closeDb();

		void onScheduleRepair(bool schedule);
		bool isRepairScheduled() const noexcept;

		void getDbSizes(int64_t& fileDbSize_, int64_t& hashDbSize_) const noexcept;
		void compact() noexcept;
	private:
		std::unique_ptr<DbHandler> fileDb;
		std::unique_ptr<DbHandler> hashDb;


		friend class HashLoader;

		/** FOR CONVERSION ONLY: Root -> tree mapping info, we assume there's only one tree for each root (a collision would mean we've broken tiger...) */
		void loadLegacyTree(File& dataFile, int64_t aSize, int64_t aIndex, int64_t aBlockSize, size_t datLen, const TTHValue& root, TigerTree& tt) throw(HashException);



		static bool loadTree(const void* src, size_t len, const TTHValue& aRoot, TigerTree& aTree, bool reportCorruption);

		static bool loadFileInfo(const void* src, size_t len, HashedFile& aFile);
		static void saveFileInfo(void *dest, const HashedFile& aTree);
		static uint32_t getFileInfoSize(const HashedFile& aTree);
	};

	friend class HashLoader;

	bool hashFile(const string& filePath, const string& pathLower, int64_t size);
	bool aShutdown = false;

	typedef vector<Hasher*> HasherList;
	HasherList hashers;

	HashStore store;

	/** Single node tree where node = root, no storage in HashData.dat */
	static const int64_t SMALL_TREE = -1;

	void hashDone(const string& aFileName, const string& pathLower, const TigerTree& tt, int64_t speed, HashedFile& aFileInfo, int hasherID = 0) noexcept;

	class Optimizer : public Thread {
	public:
		Optimizer();
		~Optimizer();

		void startMaintenance(bool verify);
		bool isRunning() const noexcept { return running; }
	private:
		bool verify;
		atomic<bool> running;
		virtual int run();
	};

	Optimizer optimizer;
};

} // namespace dcpp

#endif // !defined(HASH_MANAGER_H)