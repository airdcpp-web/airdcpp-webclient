/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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

#include "HashedFile.h"
#include "Singleton.h"
#include "MerkleTree.h"
#include "Thread.h"
#include "Semaphore.h"
#include "TimerManager.h"
#include "FastAlloc.h"
#include "GetSet.h"
#include "SFVReader.h"
#include "typedefs.h"

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

	virtual void on(TTHDone, const string& /* filePath */, HashedFilePtr& /* fileInfo */) noexcept { }
	virtual void on(HashFailed, const string& /* filePath */, HashedFilePtr& /*null*/) noexcept { }
};

class HashLoader;
class FileException;

class HashManager : public Singleton<HashManager>, public Speaker<HashManagerListener>,
	private TimerManagerListener 
{
public:

	/** We don't keep leaves for blocks smaller than this... */
	static const int64_t MIN_BLOCK_SIZE;

	HashManager();
	~HashManager();

	/**
	 * Check if the TTH tree associated with the filename is current.
	 */
	bool checkTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp);

	void stopHashing(const string& baseDir);
	void setPriority(Thread::Priority p);

	/** @return TTH root */
	HashedFilePtr getFileInfo(const string& aFileName, int64_t aSize);

	bool getTree(const TTHValue& root, TigerTree& tt);

	/** Return block size of the tree associated with root, or 0 if no such tree is in the store */
	size_t getBlockSize(const TTHValue& root);

	void addTree(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tt) {
		hashDone(aFileName, aTimeStamp, tt, -1, -1);
	}
	void addTree(const TigerTree& tree) { store.addTree(tree); }

	void getStats(string& curFile, int64_t& bytesLeft, size_t& filesLeft, int64_t& speed, int& hashers);

	void getFileTTH(const string& aFile, int64_t aSize, bool addStore, TTHValue& tth_, int64_t& sizeLeft_, const bool& aCancel, std::function<void (int64_t /*timeLeft*/, const string& /*fileName*/)> updateF = nullptr);

	/**
	 * Rebuild hash data file
	 */
	void rebuild();

	void startup(function<void (float)> progressF);
	void stop();
	void shutdown(function<void (float)> progressF);

	struct HashPauser {
		HashPauser();
		~HashPauser();
	};
	
	/// @return whether hashing was already paused
	bool pauseHashing();
	void resumeHashing(bool forced = false);	
	bool isHashingPaused() const;

	GETSET(uint64_t, nextSave, NextSave);
private:
	int pausers;
	class Hasher : public Thread {
	public:
		Hasher(bool isPaused, int aHasherID);

		void hashFile(const string& fileName, int64_t size, const string& devID);

		/// @return whether hashing was already paused
		bool pause();
		void resume();
		bool isPaused() const;
		
		void clear();

		void stopHashing(const string& baseDir);
		int run();
		void getStats(string& curFile, int64_t& bytesLeft, size_t& filesLeft, int64_t& speed);
		void shutdown();
		void scheduleRebuild();
		void save();

		bool hasDevice(const string& aID) const { return devices.find(aID) != devices.end(); }
		bool hasDevices() { return !devices.empty(); }
		int64_t getTimeLeft() const;

		int64_t getBytesLeft() const { return totalBytesLeft; }
		static CriticalSection hcs;

		int hasherID;
	private:
		struct WorkItem {
			WorkItem(const string& aFilePath, int64_t aSize, const string& aDevID) : filePath(aFilePath), fileSize(aSize), devID(aDevID) { }

			string devID;
			string filePath;
			int64_t fileSize;
		};

		//typedef pair<string, int64_t> WorkItem;
		deque<WorkItem> w;
		struct HashSortOrder {
			bool operator()(const WorkItem& left, const WorkItem& right) const;
		};

		Semaphore s;
		void removeDevice(const string& aID);

		bool stop;
		bool running;
		bool paused;
		bool rebuild;
		bool saveData;

		string currentFile;
		int64_t totalBytesLeft;
		int64_t lastSpeed;

		void instantPause();

		int64_t sizeHashed;
		int64_t hashTime;
		int dirsHashed;
		int filesHashed;

		int64_t dirSizeHashed;
		int64_t dirHashTime;
		int dirFilesHashed;
		string initialDir;

		DirSFVReader sfv;

		StringIntMap devices;
	};

	friend class Hasher;
	void removeHasher(Hasher* aHasher);
	void log(const string& aMessage, int hasherID, bool isError);

	class HashStore {
	public:
		HashStore();
		HashedFilePtr& addFile(const string&& aFileName, uint64_t aTimeStamp, const TigerTree& tth, bool aUsed);

		void load(function<void (float)> progressF);
		void save(function<void (float)> progressF = nullptr);
		void countNextSave();

		void rebuild();

		bool checkTTH(const string&& aFileName, int64_t aSize, uint32_t aTimeStamp);

		void addTree(const TigerTree& tt) noexcept;
		const HashedFilePtr getFileInfo(string&& aFileName);
		bool getTree(const TTHValue& root, TigerTree& tth);
		size_t getBlockSize(const TTHValue& root) const;
		bool isDirty() { return dirty; }
	private:
		/** Root -> tree mapping info, we assume there's only one tree for each root (a collision would mean we've broken tiger...) */
		struct TreeInfo {
			TreeInfo() : size(0), index(0), blockSize(0) { }
			TreeInfo(int64_t aSize, int64_t aIndex, int64_t aBlockSize) : size(aSize), index(aIndex), blockSize(aBlockSize) { }

			GETSET(int64_t, size, Size);
			GETSET(int64_t, index, Index);
			GETSET(int64_t, blockSize, BlockSize);
		};

		typedef vector<HashedFilePtr> FileInfoList;

		typedef unordered_map<string, FileInfoList> DirMap;

		typedef unordered_map<TTHValue, TreeInfo> TreeMap;

		friend class HashLoader;
		mutable SharedMutex cs;

		DirMap fileIndex;
		TreeMap treeIndex;

		bool dirty;

		void createDataFile(const string& name);

		bool loadTree(File& dataFile, const TreeInfo& ti, const TTHValue& root, TigerTree& tt, bool rebuilding = false);
		int64_t saveTree(File& dataFile, const TigerTree& tt);

		static string getIndexFile();
		static string getDataFile();
		static atomic_flag saving;
	};

	friend class HashLoader;

	void hashFile(const string& fileName, int64_t size);
	bool aShutdown;

	typedef vector<Hasher*> HasherList;
	HasherList hashers;

	HashStore store;

	/** Single node tree where node = root, no storage in HashData.dat */
	static const int64_t SMALL_TREE = -1;

	HashedFilePtr hashDone(const string& aFileName, uint64_t aTimeStamp, const TigerTree& tth, int64_t speed, int64_t size, int hasherID = 0);

	void doRebuild();
	void SaveData() {
		store.save();
	}

	void on(TimerManagerListener::Minute, uint64_t) noexcept;
};

} // namespace dcpp

#endif // !defined(HASH_MANAGER_H)