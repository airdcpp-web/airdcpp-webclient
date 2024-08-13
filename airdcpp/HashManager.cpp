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

#include "stdinc.h"
#include "HashManager.h"

#include "Exception.h"
#include "File.h"
#include "FileReader.h"
#include "LogManager.h"
#include "Hasher.h"
#include "HashStore.h"
#include "HashedFile.h"
#include "ResourceManager.h"
#include "TimerManager.h"
#include "version.h"

namespace dcpp {

using ranges::find_if;

HashManager::HashManager() {
	store = make_unique<HashStore>();
}

HashManager::~HashManager() {
	optimizer.join();
}

void HashManager::close() noexcept {
	store->closeDb(); 
}

void HashManager::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(HASHING));
}


// Hasher functions
void HashManager::onFileHashed(const string& aPath, HashedFile& aFile, const TigerTree& aTree, int aHasherId) noexcept {

	HashManager::getInstance()->fire(HashManagerListener::FileHashed(), aPath, aFile, aHasherId);
	try {
		store->addHashedFile(Text::toLower(aPath), aTree, aFile);
	} catch (const Exception& e) {
		logHasher(STRING_F(HASHING_FAILED_X, e.getError()), aHasherId, LogMessage::SEV_ERROR, true);
	}
}

void HashManager::onFileFailed(const string& aPath, const string& aErrorId, const string& aMessage, int aHasherId) noexcept {
	fire(HashManagerListener::FileFailed(), aPath, aErrorId, aMessage, aHasherId);
}

void HashManager::onDirectoryHashed(const string& aPath, const HasherStats& aStats, int aHasherId) noexcept {
	fire(HashManagerListener::DirectoryHashed(), aPath, aStats, aHasherId);
}

void HashManager::onHasherFinished(int aDirectoriesHashed, const HasherStats& aStats, int aHasherId) noexcept {
	fire(HashManagerListener::HasherFinished(), aDirectoriesHashed, aStats, aHasherId);
}

void HashManager::removeHasher(int aHasherId) noexcept {
	dcdebug("Hash: removing hasher #%d\n", aHasherId);
	hashers.erase(remove_if(hashers.begin(), hashers.end(), [aHasherId](const Hasher* aHasher) { return aHasher->hasherID == aHasherId; }), hashers.end());
}

void HashManager::logHasher(const string& aMessage, int aHasherID, LogMessage::Severity aSeverity, bool aLock) const noexcept {
	ConditionalRLock l(Hasher::hcs, aLock);
	log((hashers.size() > 1 ? "[" + STRING_F(HASHER_X, aHasherID) + "] " + ": " : Util::emptyString) + aMessage, aSeverity);
}


void HashManager::addTree(const TigerTree& aTree) {
	store->addTree(aTree); 
}

string HashManager::getDbStats() noexcept {
	return store->getDbStats();
}

void HashManager::getDbSizes(int64_t& fileDbSize_, int64_t& hashDbSize_) const noexcept {
	return store->getDbSizes(fileDbSize_, hashDbSize_); 
}

bool HashManager::maintenanceRunning() const noexcept {
	return optimizer.isRunning(); 
}

void HashManager::compact() noexcept {
	store->compact();
}

void HashManager::onScheduleRepair(bool aSchedule) noexcept {
	store->onScheduleRepair(aSchedule);
}

bool HashManager::isRepairScheduled() const noexcept {
	return store->isRepairScheduled();
}

bool HashManager::checkTTH(const string& aFileLower, const string& aFileName, HashedFile& fi_) {
	dcassert(Text::isLower(aFileLower));
	if (!store->checkTTH(aFileLower, fi_)) {
		hashFile(aFileName, aFileLower, fi_.getSize());
		return false;
	}

	return true;
}

void HashManager::getFileInfo(const string& aFileLower, const string& aFileName, HashedFile& fi_) {
	dcassert(Text::isLower(aFileLower));
	auto found = store->getFileInfo(aFileLower, fi_);
	if (!found) {
		auto size = File::getSize(aFileName);
		if (size >= 0) {
			hashFile(aFileName, aFileLower, size);
		}

		throw HashException();
	}
}
void HashManager::renameFileThrow(const string& aOldPath, const string& aNewPath) {
	return store->renameFileThrow(aOldPath, aNewPath);
}

bool HashManager::getTree(const TTHValue& aRoot, TigerTree& tt_) noexcept {
	return store->getTree(aRoot, tt_);
}

size_t HashManager::getBlockSize(const TTHValue& aRoot) noexcept {
	return static_cast<size_t>(store->getRootInfo(aRoot, HashStore::TYPE_BLOCKSIZE));
}

int64_t HashManager::getMinBlockSize() noexcept {
	return Hasher::MIN_BLOCK_SIZE;
}

bool HashManager::hashFile(const string& aPath, const string& aPathLower, int64_t aSize) {
	if (isShutdown) { //we cant allow adding more hashers if we are shutting down, it will result in infinite loop
		return false;
	}

	Hasher* h = nullptr;

	WLock l(Hasher::hcs);

	auto deviceId = File::getDeviceId(aPath);
	if (hashers.size() == 1 && !hashers.front()->hasDevices()) {
		// Always use the main hasher if it's idle
		h = hashers.front();
		dcdebug("Using empty main hasher for file %s\n", aPath.c_str());
	} else {
		auto getLeastLoaded = [](const HasherList& hl) {
			return min_element(hl.begin(), hl.end(), [](const Hasher* h1, const Hasher* h2) { return h1->getBytesLeft() < h2->getBytesLeft(); });
		};

		auto totalHashersExceeded = static_cast<int>(hashers.size()) >= SETTING(MAX_HASHING_THREADS);

		// Get the hashers with this volume
		HasherList volHashers;
		copy_if(hashers.begin(), hashers.end(), back_inserter(volHashers), [deviceId](const Hasher* aHasher) { return aHasher->hasDevice(deviceId); });

		if (volHashers.empty() && totalHashersExceeded) {
			// We just need choose from all hashers
			h = *getLeastLoaded(hashers);
			// dcdebug("Hash: using least loaded hasher #%d for file %s (total hashers exceeded)\n", h->hasherID, aPath.c_str());
		} else if (!volHashers.empty()) {
			// Check that the file isn't queued already
			auto p = find_if(volHashers, [&aPathLower](const Hasher* aHasher) { return aHasher->hasFile(aPathLower); });
			if (p != volHashers.end()) {
				// dcdebug("Hash: ignoring file %s (queued already)\n", h->hasherID, aPath.c_str());
				return false;
			}

			auto minLoaded = getLeastLoaded(volHashers);

			auto volumeHashersExceeded = static_cast<int>(volHashers.size()) >= SETTING(HASHERS_PER_VOLUME) && SETTING(HASHERS_PER_VOLUME) > 0;
			auto reuseExisting = aSize <= Util::convertSize(10, Util::MB) && !volHashers.empty() && (*minLoaded)->getBytesLeft() <= Util::convertSize(200, Util::MB);
			if (totalHashersExceeded || volumeHashersExceeded || reuseExisting) {

				// Use the least loaded hasher that already has this volume
				h = *minLoaded;
				// dcdebug("Hash: using volume hasher #%d for file %s\n", h->hasherID, aPath.c_str());
			}
		}

		if (!h) {
			// Create new one
			int id = 0;
			for (auto i: hashers) {
				if (i->hasherID != id)
					break;
				id++;
			}

			log(STRING_F(HASHER_X_CREATED, id), LogMessage::SEV_INFO);
			h = new Hasher(pausers > 0, id, this);
			hashers.push_back(h);

			dcdebug("Hash: creating hasher #%d\n", id);
		}
	}

	//queue the file for hashing
	dcdebug("Hash: choosing hasher #%d for file %s\n", h->hasherID, aPath.c_str());
	return h->hashFile(aPath, aPathLower, aSize, deviceId);
}

void HashManager::getFileTTH(const string& aFile, int64_t aSize, bool aAddStore, TTHValue& tth_, int64_t& sizeLeft_, const bool& aCancel, std::function<void(int64_t, const string&)> updateF/*nullptr*/) {
	auto pathLower = Text::toLower(aFile);
	HashedFile fi(File::getLastModified(aFile), aSize);

	if (!store->checkTTH(pathLower, fi)) {
		File f(aFile, File::READ, File::OPEN);
		auto timestamp = f.getLastModified();
		if (timestamp < 0) {
			throw FileException(STRING(INVALID_MODIFICATION_DATE));
		}

		int64_t bs = max(TigerTree::calcBlockSize(aSize, 10), Hasher::MIN_BLOCK_SIZE);
		TigerTree tt(bs);

		auto start = GET_TICK();
		int64_t tickHashed = 0;

		FileReader fr(FileReader::ASYNC);
		fr.read(aFile, [&](const void* buf, size_t n) -> bool {
			tt.update(buf, n);

			if (updateF) {
				tickHashed += n;

				uint64_t end = GET_TICK();
				if (end - start > 1000) {
					sizeLeft_ -= tickHashed;
					auto lastSpeed = tickHashed * 1000 / (end - start);

					updateF(lastSpeed > 0 ? (sizeLeft_ / lastSpeed) : 0, aFile);

					tickHashed = 0;
					start = end;
				}
			}
			return !aCancel;
		});

		tt.finalize();
		tth_ = tt.getRoot();

		if (aAddStore && !aCancel) {
			fi = HashedFile(tth_, timestamp, aSize);
			store->addHashedFile(pathLower, tt, fi);
		}
	} else {
		tth_ = fi.getRoot();
	}
}

bool HashManager::addFile(const string& aPath, const HashedFile& fi_) {
	//check that the file exists
	if (File::getSize(aPath) != fi_.getSize()) {
		return false;
	}

	//check that the tree exists
	if (fi_.getSize() < Hasher::MIN_BLOCK_SIZE) {
		TigerTree tt = TigerTree(fi_.getSize(), fi_.getSize(), fi_.getRoot());
		store->addTree(tt);
	} else if (!store->hasTree(fi_.getRoot())) {
		return false;
	}

	store->addFile(Text::toLower(aPath), fi_);
	return true;
}
void HashManager::stopHashing(const string& aBaseDir) noexcept {
	WLock l(Hasher::hcs);
	for (auto h: hashers)
		h->stopHashing(aBaseDir);
}

void HashManager::setPriority(Thread::Priority p) noexcept {
	RLock l(Hasher::hcs);
	for (auto h: hashers)
		h->setThreadPriority(p); 
}

HashManager::HashStats HashManager::getStats() const noexcept {
	HashStats stats;

	RLock l(Hasher::hcs);
	for (auto i: hashers) {
		i->getStats(stats.curFile, stats.bytesLeft, stats.filesLeft, stats.speed, stats.filesAdded, stats.bytesAdded);
		if (!i->isPaused()) {
			stats.isPaused = false;
		}

		if (i->isRunning()) {
			stats.hashersRunning++;
		}
	}

	return stats;
}

void HashManager::startMaintenance(bool aVerify){
	optimizer.startMaintenance(aVerify);
}

HashManager::Optimizer::Optimizer() {

}

HashManager::Optimizer::~Optimizer() {
}

void HashManager::Optimizer::startMaintenance(bool aVerify) {
	if (running)
		return;

	verify = aVerify;
	running = true;
	start();
}

int HashManager::Optimizer::run() {
	auto hm = getInstance();

	hm->fire(HashManagerListener::MaintananceStarted());
	hm->store->optimize(verify);
	hm->fire(HashManagerListener::MaintananceFinished());

	running = false;
	return 0;
}

void HashManager::startup(StartupLoader& aLoader) {
	hashers.push_back(new Hasher(false, 0, this));
	store->load(aLoader); 
}

void HashManager::shutdown(ProgressFunction progressF) noexcept {
	isShutdown = true;

	{
		WLock l(Hasher::hcs);
		for (auto h : hashers) {
			h->shutdown();
		}
	}

	// Wait for the hashers to shut down
	while (true) {
		{
			RLock l(Hasher::hcs);
			if (hashers.empty()) {
				break;
			}
		}
		Thread::sleep(50);
	}
}

void HashManager::stop() noexcept {
	WLock l(Hasher::hcs);
	for (auto h: hashers) {
		h->stop();
	}
}

bool HashManager::pauseHashing() noexcept {
	pausers++;
	if (pausers == 1) {
		RLock l(Hasher::hcs);
		for (auto h : hashers)
			h->pause();
		return isHashingPaused(false);
	}
	return true;
}

void HashManager::resumeHashing(bool aForced) {
	if (aForced)
		pausers = 0;
	else if (pausers > 0)
		pausers--;

	if (pausers == 0) {
		RLock l(Hasher::hcs);
		for (auto h : hashers)
			h->resume();
	}
}

bool HashManager::isHashingPaused(bool aLock /*true*/) const noexcept {
	ConditionalRLock l(Hasher::hcs, aLock);
	return ranges::all_of(hashers, [](const Hasher* h) { return h->isPaused(); });
}

HashManager::HashPauser::HashPauser() {
	HashManager::getInstance()->pauseHashing();
}

HashManager::HashPauser::~HashPauser() {
	HashManager::getInstance()->resumeHashing();
}

} // namespace dcpp
