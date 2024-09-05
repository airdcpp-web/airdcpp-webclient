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

#include "Hasher.h"

#include "Exception.h"
#include "File.h"
#include "FileReader.h"
#include "HasherStats.h"
#include "HashedFile.h"
#include "MerkleTree.h"
#include "PathUtil.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "SFVReader.h"
#include "TimerManager.h"
#include "ZUtils.h"

namespace dcpp {

#define HASH_ERROR_CRC "crc_error"
#define HASH_ERROR_IO "io_error"

SharedMutex Hasher::hcs;
const int64_t Hasher::MIN_BLOCK_SIZE = 64 * 1024;


Hasher::Hasher(bool aIsPaused, int aHasherID, HasherManager* aManager) : paused(aIsPaused), hasherID(aHasherID), manager(aManager) {
	start();
}

bool Hasher::pause() noexcept {
	paused = true;
	return paused;
}

void Hasher::resume() {
	paused = false;
	t_resume();
}

bool Hasher::isPaused() const noexcept {
	return paused;
}

bool Hasher::isRunning() const noexcept {
	return running;
}

void Hasher::removeDevice(devid aDevice) noexcept {
	dcassert(aDevice >= 0);
	auto dp = devices.find(aDevice);
	if (dp != devices.end()) {
		dp->second--;
		if (dp->second == 0)
			devices.erase(dp);
	}
}

void Hasher::logHasher(const string& aMessage, LogMessage::Severity aSeverity, bool aLock) const noexcept {
	manager->logHasher(aMessage, hasherID, aSeverity, aLock);
}


void Hasher::logHashedDirectory(const string& aPath, const string& aLastFilePath, const HasherStats& aStats) const noexcept {
	if (aStats.filesHashed == 1) {
		logHasher(
			STRING_F(HASHING_FINISHED_FILE,
				aLastFilePath %
				aStats.formatSize() %
				aStats.formatDuration() %
				aStats.formatSpeed()
			),
			LogMessage::SEV_INFO,
			false
		);
	} else {
		logHasher(
			STRING_F(HASHING_FINISHED_DIR,
				aPath %
				aStats.filesHashed %
				aStats.formatSize() %
				aStats.formatDuration() %
				aStats.formatSpeed()
			),
			LogMessage::SEV_INFO,
			false
		);
	}
}

void Hasher::logHashedFile(const string& aPath, int64_t aSpeed) const noexcept {
	if (!SETTING(LOG_HASHING)) {
		return;
	}

	auto fn = aPath;
	if (count(fn.begin(), fn.end(), PATH_SEPARATOR) >= 2) {
		auto i = fn.rfind(PATH_SEPARATOR);
		i = fn.rfind(PATH_SEPARATOR, i - 1);
		fn.erase(0, i);
		fn.insert(0, "...");
	}

	if (aSpeed > 0) {
		logHasher(STRING_F(HASHING_FINISHED_X, fn) + " (" + Util::formatBytes(aSpeed) + "/s)", LogMessage::SEV_INFO, true);
	} else {
		logHasher(STRING_F(HASHING_FINISHED_X, fn), LogMessage::SEV_INFO, true);
	}
}

void Hasher::logFailedFile(const string& aPath, const string& aError) const noexcept {
	auto message = STRING(ERROR_HASHING) + aPath + ": " + aError;
	logHasher(message, LogMessage::SEV_ERROR, true);
}

bool Hasher::hashFile(const string& fileName, const string& filePathLower, int64_t size, devid aDeviceId) noexcept {
	// always locked
	auto ret = w.emplace_sorted(filePathLower, fileName, size, aDeviceId);
	if (ret.second) {
		devices[(*ret.first).deviceId]++;
		totalBytesLeft += size;
		totalBytesAdded += size;
		totalFilesAdded++;
		s.signal();
		return true;
	}

	return false;
}

void Hasher::stopHashing(const string& aBaseDir) noexcept {
	for (auto i = w.begin(); i != w.end();) {
		if (PathUtil::isParentOrExact(aBaseDir, i->filePath, PATH_SEPARATOR)) {
			totalBytesLeft -= i->fileSize;
			removeDevice(i->deviceId);
			i = w.erase(i);
		} else {
			++i;
		}
	}
}

void Hasher::stop() noexcept {
	clear();
	stopping = true;
}

void Hasher::shutdown() {
	isShutdown = true;

	stop();
	if (paused) {
		resume();
	}

	s.signal();
}

int64_t Hasher::getTimeLeft() const noexcept {
	return lastSpeed > 0 ? (totalBytesLeft / lastSpeed) : 0;
}

bool Hasher::hasFile(const string& aPath) const noexcept {
	return w.find(aPath) != w.end();
}

bool Hasher::hasDevice(int64_t aDeviceId) const noexcept {
	return devices.find(aDeviceId) != devices.end();
}

bool Hasher::hasDevices() const noexcept {
	return !devices.empty();
}

void Hasher::clear() noexcept {
	w.clear();
	devices.clear();

	clearStats();
}

void Hasher::clearStats() noexcept {
	totalBytesLeft = 0;
	totalBytesAdded = 0;
	totalFilesAdded = 0;
	lastSpeed = 0;
}

void Hasher::getStats(string& curFile_, int64_t& bytesLeft_, size_t& filesLeft_, int64_t& speed_, size_t& filesAdded_, int64_t& bytesAdded_) const noexcept {
	curFile_ = currentFile;
	filesLeft_ += w.size();
	if (running) {
		filesLeft_++;
		speed_ += lastSpeed;
	}

	bytesLeft_ += totalBytesLeft;

	filesAdded_ += totalFilesAdded;
	bytesAdded_ += totalBytesAdded;
}

void Hasher::instantPause() {
	if (paused) {
		running = false;
		t_suspend();
	}
}

optional<HashedFile> Hasher::hashFile(const WorkItem& aItem, HasherStats& stats_, const DirSFVReader& aSFV) noexcept {
	auto start = GET_TICK();
	auto sizeLeft = aItem.fileSize;
	try {
		File f(aItem.filePath, File::READ, File::OPEN);

		// size changed since adding?
		auto size = f.getSize();
		sizeLeft = size;
		totalBytesLeft += size - aItem.fileSize;

		auto blockSize = max(TigerTree::calcBlockSize(size, 10), MIN_BLOCK_SIZE);

		auto timestamp = f.getLastModified();
		if (timestamp < 0) {
			throw FileException(STRING(INVALID_MODIFICATION_DATE));
		}

		TigerTree tt(blockSize);

		CRC32Filter crc32;

		auto fileCRC = aSFV.hasFile(Text::toLower(PathUtil::getFileName(aItem.filePath)));

		uint64_t lastRead = GET_TICK();

		FileReader fr(FileReader::ASYNC);
		fr.read(aItem.filePath, [&](const void* buf, size_t n) -> bool {
			if (SETTING(MAX_HASH_SPEED) > 0) {
				uint64_t now = GET_TICK();
				uint64_t minTime = n * 1000LL / Util::convertSize(SETTING(MAX_HASH_SPEED), Util::MB);

				if (lastRead + minTime > now) {
					Thread::sleep(minTime - (now - lastRead));
				}
				lastRead = lastRead + minTime;
			} else {
				lastRead = GET_TICK();
			}

			tt.update(buf, n);

			if (fileCRC) {
				crc32(buf, n);
			}

			sizeLeft -= n;
			uint64_t end = GET_TICK();

			if (totalBytesLeft > 0)
				totalBytesLeft -= n;
			if (end > start)
				lastSpeed = (size - sizeLeft) * 1000 / (end - start);

			return !stopping;
		});

		tt.finalize();

		auto failed = (fileCRC && crc32.getValue() != *fileCRC) || stopping;

		auto end = GET_TICK();
		auto duration = end - start;
		if (!failed) {
			stats_.addFile(size, duration);
		}

		if (!stopping) {
			if (failed) {
				logFailedFile(aItem.filePath, STRING(ERROR_HASHING_CRC32));
				manager->onFileFailed(aItem.filePath, HASH_ERROR_CRC, STRING(ERROR_HASHING_CRC32), hasherID);
			} else {
				// Log
				auto averageSpeed = duration > 0 ? size * 1000 / duration : 0;
				logHashedFile(aItem.filePath, averageSpeed);

				// Save the tree
				auto fi = HashedFile(tt.getRoot(), timestamp, size);
				manager->onFileHashed(aItem.filePath, fi, tt, hasherID);
				return fi;
			}
		}
	} catch (const FileException& e) {
		totalBytesLeft -= sizeLeft;

		logFailedFile(aItem.filePath, e.getError());
		manager->onFileFailed(aItem.filePath, HASH_ERROR_IO, e.getError(), hasherID);
	}

	return nullopt;
}
void Hasher::processQueue() noexcept {
	int totalDirsHashed = 0;
	string initialDir;

	HasherStats totalStats;
	HasherStats dirStats(&totalStats);

	string fname;
	DirSFVReader sfv;
	for (;;) {
		instantPause(); //suspend the thread...
		if (stopping) {
			return;
		}

		WorkItem wi;
		{
			WLock l(hcs);
			if (!w.empty()) {
				wi = std::move(w.front());
				w.pop_front();
			} else {
				break;
			}
		}

		auto dirChanged = initialDir.empty() || compare(PathUtil::getFilePath(wi.filePath), PathUtil::getFilePath(fname)) != 0;
		if (dirChanged) {
			sfv.loadPath(PathUtil::getFilePath(wi.filePath));
		}

		fname = wi.filePath;
		running = true;

		if (initialDir.empty()) {
			initialDir = PathUtil::getFilePath(wi.filePath);
		}

		auto fi = hashFile(wi, dirStats, sfv);

		auto onDirHashed = [&]() -> void {
			manager->onDirectoryHashed(initialDir, dirStats, hasherID);
			logHashedDirectory(initialDir, wi.filePath, dirStats);

			totalDirsHashed++;
			dirStats = HasherStats(&totalStats);

			initialDir.clear();
		};

		{
			WLock l(hcs);
			removeDevice(wi.deviceId);

			if (w.empty()) {
				// Finished hashing
				running = false;

				if (totalStats.sizeHashed > 0) {
					onDirHashed();
					if (totalDirsHashed > 0) {
						logHasher(
							STRING_F(HASHING_FINISHED_TOTAL, 
								totalStats.filesHashed % 
								totalStats.formatSize() %
								totalDirsHashed %
								totalStats.formatDuration() %
								totalStats.formatSpeed()
							),
							LogMessage::SEV_INFO,
							false
						);
					}
				} else {
					// All files failed to hash?
					logHasher(STRING(HASHING_FINISHED), LogMessage::SEV_INFO, false);
				}

				clearStats();
				manager->onHasherFinished(totalDirsHashed, totalStats, hasherID);
			} else if (!PathUtil::isParentOrExactLocal(initialDir, w.front().filePath)) {
				onDirHashed();
			}

			currentFile.clear();
		}
	}
}

string HasherStats::formatDuration() const noexcept {
	return Util::formatDuration(hashTime / 1000, true);
}

string HasherStats::formatSpeed() const noexcept {
	return Util::formatBytes(hashTime > 0 ? ((sizeHashed * 1000) / hashTime) : 0) + "/s";
}

string HasherStats::formatSize() const noexcept {
	return Util::formatBytes(sizeHashed);
}

void HasherStats::addFile(int64_t aSize, uint64_t aHashTime) noexcept {
	filesHashed++;
	hashTime += aHashTime;
	sizeHashed += aSize;

	if (parent) {
		parent->addFile(aSize, aHashTime);
	}
}

int Hasher::run() {
	setThreadPriority(Thread::IDLE);

	for (;;) {
		s.wait();
		processQueue();

		{
			WLock l(hcs);
			if (isShutdown || (w.empty() && hasherID != 0)) {
				manager->removeHasher(hasherID);
				break;
			}
		}

		stopping = false;
	}

	delete this;
	return 0;
}

} // namespace dcpp
