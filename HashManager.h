/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
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

#include "Singleton.h"
#include "MerkleTree.h"
#include "Thread.h"
#include "Semaphore.h"
#include "TimerManager.h"
#include "FastAlloc.h"
#include "Text.h"
#include "Streams.h"
#include "ZUtils.h"
#include "GetSet.h"

namespace dcpp {

STANDARD_EXCEPTION(HashException);
class File;

class HashManagerListener {
public:
	virtual ~HashManagerListener() { }
	template<int I>	struct X { enum { TYPE = I };  };

	typedef X<0> TTHDone;
	typedef X<1> HashFailed;

	virtual void on(TTHDone, const string& /* fileName */, const TTHValue& /* root */) noexcept { }
	virtual void on(HashFailed, const string& /* fileName */) noexcept { }
};

class HashLoader;
class FileException;

class HashManager : public Singleton<HashManager>, public Speaker<HashManagerListener>,
	private TimerManagerListener 
{
public:

	/** We don't keep leaves for blocks smaller than this... */
	static const int64_t MIN_BLOCK_SIZE;

	HashManager(): lastSave(0) {
		TimerManager::getInstance()->addListener(this);
	}
	~HashManager() {
		TimerManager::getInstance()->removeListener(this);
		hasher.join();
	}

	/**
	 * Check if the TTH tree associated with the filename is current.
	 */
	bool checkTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp);

	void stopHashing(const string& baseDir) { hasher.stopHashing(baseDir); }
	void setPriority(Thread::Priority p) { hasher.setThreadPriority(p); }

	/** @return TTH root */
	TTHValue getTTH(const string& aFileName, int64_t aSize);

	bool getTree(const TTHValue& root, TigerTree& tt);

	/** Return block size of the tree associated with root, or 0 if no such tree is in the store */
	size_t getBlockSize(const TTHValue& root);

	void addTree(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tt) {
		hashDone(aFileName, aTimeStamp, tt, -1, -1);
	}
	void addTree(const TigerTree& tree) { Lock l(cs); store.addTree(tree); }

	void getStats(string& curFile, int64_t& bytesLeft, size_t& filesLeft, int64_t& speed) {
		hasher.getStats(curFile, bytesLeft, filesLeft, speed);
	}

	/**
	 * Rebuild hash data file
	 */
	void rebuild() { hasher.scheduleRebuild(); }

	void startup() { hasher.start(); store.load(); }

	void Stop() {
		hasher.clear();
	}

	void shutdown() {
		hasher.clear();
		hasher.shutdown();
		{	
		Lock l(cs); //lock only here, prevent deadlock on shutdown (while hashing, hashdone has a lock too if we join inside a lock....)
		store.save(); 
		}
		hasher.join();
	}

	struct HashPauser {
		HashPauser();
		~HashPauser();

	private:
		bool resume;
	};
	
	/// @return whether hashing was already paused
	bool pauseHashing();
	void resumeHashing();	
	bool isHashingPaused() const;

private:
	class Hasher : public Thread {
	public:
		Hasher() : stop(false), running(false), paused(false), rebuild(false), currentSize(0), saveData(false), totalBytesLeft(0), lastSpeed(0) { }

		void hashFile(const string& fileName, int64_t size);

		/// @return whether hashing was already paused
		bool pause();
		void resume();
		bool isPaused() const;
		
		void clear() {
			Lock l(cs);
			w.clear();
			totalBytesLeft = 0;
		}

		void stopHashing(const string& baseDir);
		int run();
		void getStats(string& curFile, int64_t& bytesLeft, size_t& filesLeft, int64_t& speed);
		void shutdown() { stop = true; if(paused) resume(); s.signal(); }
		void scheduleRebuild() { rebuild = true; s.signal(); if(paused) t_resume(); }
		void save() { saveData = true; s.signal(); if(paused) t_resume(); }

	private:
		// Case-sensitive (faster), it is rather unlikely that case changes, and if it does it's harmless.
		// map because it's sorted (to avoid random hash order that would create quite strange shares while hashing)
		typedef map<string, int64_t> WorkMap;	
		typedef WorkMap::iterator WorkIter;

		WorkMap w;
		mutable CriticalSection cs;
		Semaphore s;

		bool stop;
		bool running;
		bool paused;
		bool rebuild;
		bool saveData;
		string currentFile;
		int64_t currentSize;
		int64_t totalBytesLeft;
		int64_t lastSpeed;

		void instantPause();
	};

	friend class Hasher;

	class HashStore {
	public:
		HashStore();
		void addFile(const string& aFileName, uint64_t aTimeStamp, const TigerTree& tth, bool aUsed);

		void load();
		void save();

		void rebuild();

		bool checkTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp);

		void addTree(const TigerTree& tt) noexcept;
		const TTHValue* getTTH(const string& aFileName);
		bool getTree(const TTHValue& root, TigerTree& tth);
		size_t getBlockSize(const TTHValue& root) const;
		bool isDirty() { return dirty; }
	private:
		/** Root -> tree mapping info, we assume there's only one tree for each root (a collision would mean we've broken tiger...) */
		struct TreeInfo {
			TreeInfo() : size(0), index(0), blockSize(0) { }
			TreeInfo(int64_t aSize, int64_t aIndex, int64_t aBlockSize) : size(aSize), index(aIndex), blockSize(aBlockSize) { }
			TreeInfo(const TreeInfo& rhs) : size(rhs.size), index(rhs.index), blockSize(rhs.blockSize) { }
			TreeInfo& operator=(const TreeInfo& rhs) { size = rhs.size; index = rhs.index; blockSize = rhs.blockSize; return *this; }

			GETSET(int64_t, size, Size);
			GETSET(int64_t, index, Index);
			GETSET(int64_t, blockSize, BlockSize);
		};

		/** File -> root mapping info */
		struct FileInfo {
		public:
			FileInfo(const string& aFileName, const TTHValue& aRoot, uint64_t aTimeStamp, bool aUsed) :
			  fileName(aFileName), root(aRoot), timeStamp(aTimeStamp), used(aUsed) { }

			bool operator==(const string& name) { return name == fileName; }

			GETSET(string, fileName, FileName);
			GETSET(TTHValue, root, Root);
			GETSET(uint64_t, timeStamp, TimeStamp);
			GETSET(bool, used, Used);
		};

		typedef vector<FileInfo> FileInfoList;
		typedef FileInfoList::iterator FileInfoIter;
		typedef FileInfoList::const_iterator FileInfoIterC;

		typedef unordered_map<string, FileInfoList> DirMap;
		typedef DirMap::iterator DirIter;
		typedef DirMap::const_iterator DirIterC;

		typedef unordered_map<TTHValue, TreeInfo> TreeMap;
		typedef TreeMap::iterator TreeIter;
		typedef TreeMap::const_iterator TreeIterC;

		friend class HashLoader;

		DirMap fileIndex;
		TreeMap treeIndex;

		bool dirty;

		void createDataFile(const string& name);

		bool loadTree(File& dataFile, const TreeInfo& ti, const TTHValue& root, TigerTree& tt);
		int64_t saveTree(File& dataFile, const TigerTree& tt);

		static string getIndexFile();
		static string getDataFile();
	};

	friend class HashLoader;

	Hasher hasher;
	HashStore store;
	
	mutable CriticalSection cs;

	/** Single node tree where node = root, no storage in HashData.dat */
	static const int64_t SMALL_TREE = -1;

	void hashDone(const string& aFileName, uint64_t aTimeStamp, const TigerTree& tth, int64_t speed, int64_t size);

	void doRebuild() {
		Lock l(cs);
		store.rebuild();
	}
	void SaveData() {
		Lock l(cs);
		store.save();
	}

	uint64_t  lastSave;

	void on(TimerManagerListener::Minute, uint64_t) noexcept {
		if(GET_TICK() - lastSave > 10*60*1000 && store.isDirty()) { 
			Lock l(cs);
			hasher.save();
			lastSave = GET_TICK();
		}
	}
};

} // namespace dcpp

#endif // !defined(HASH_MANAGER_H)

/**
 * @file
 * $Id: HashManager.h 568 2011-07-24 18:28:43Z bigmuscle $
 */
