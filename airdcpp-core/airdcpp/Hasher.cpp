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

#include "stdinc.h"

#include "Hasher.h"

#include "AirUtil.h"
#include "Exception.h"
#include "File.h"
#include "FileReader.h"
#include "HashManager.h"
#include "HashedFile.h"
#include "MerkleTree.h"
#include "ResourceManager.h"
#include "TimerManager.h"
#include "ZUtils.h"

namespace dcpp {

	// using boost::range::find_if;

	SharedMutex Hasher::hcs;
	const int64_t Hasher::MIN_BLOCK_SIZE = 64 * 1024;


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

	void Hasher::stopHashing(const string& baseDir) noexcept {
		for (auto i = w.begin(); i != w.end();) {
			if (Util::strnicmp(baseDir, i->filePath, baseDir.length()) == 0) {
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
		totalHashTime = 0;
		totalSizeHashed = 0;
		totalDirsHashed = 0;
		totalFilesHashed = 0;
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

	Hasher::Hasher(bool aIsPaused, int aHasherID) : paused(aIsPaused), hasherID(aHasherID), totalBytesLeft(0), lastSpeed(0), totalBytesAdded(0), totalFilesAdded(0) {
		start();
	}

	int Hasher::run() {
		setThreadPriority(Thread::IDLE);

		string fname;
		for (;;) {
			s.wait();
			instantPause(); //suspend the thread...
			if (stopping) {
				if (isShutdown) {
					WLock l(hcs);
					HashManager::getInstance()->removeHasher(this);
					break;
				} else {
					stopping = false;
				}
			}

			int64_t originalSize = 0;
			bool failed = true;
			bool dirChanged = false;
			devid curDevID = -1;
			string pathLower;
			{
				WLock l(hcs);
				if (!w.empty()) {
					auto& wi = w.front();
					dirChanged = initialDir.empty() || compare(Util::getFilePath(wi.filePath), Util::getFilePath(fname)) != 0;
					currentFile = fname = move(wi.filePath);
					curDevID = move(wi.deviceId);
					pathLower = move(wi.filePathLower);
					originalSize = wi.fileSize;
					dcassert(curDevID >= 0);
					w.pop_front();
				} else {
					fname.clear();
				}
			}

			HashedFile fi;
			if (fname.empty()) {
				running = false;
			} else {
				running = true;

				int64_t sizeLeft = originalSize;
				try {
					if (initialDir.empty()) {
						initialDir = Util::getFilePath(fname);
					}

					if (dirChanged) {
						sfv.loadPath(Util::getFilePath(fname));
					}

					uint64_t start = GET_TICK();

					File f(fname, File::READ, File::OPEN);

					// size changed since adding?
					int64_t size = f.getSize();
					sizeLeft = size;
					totalBytesLeft += size - originalSize;

					int64_t bs = max(TigerTree::calcBlockSize(size, 10), MIN_BLOCK_SIZE);

					auto timestamp = f.getLastModified();
					if (timestamp < 0) {
						throw FileException(STRING(INVALID_MODIFICATION_DATE));
					}

					TigerTree tt(bs);

					CRC32Filter crc32;

					auto fileCRC = sfv.hasFile(Text::toLower(Util::getFileName(fname)));

					uint64_t lastRead = GET_TICK();

					FileReader fr(true);
					fr.read(fname, [&](const void* buf, size_t n) -> bool {
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

					failed = (fileCRC && crc32.getValue() != *fileCRC) || stopping;

					uint64_t end = GET_TICK();
					int64_t averageSpeed = 0;

					if (!failed) {
						totalSizeHashed += size;
						dirSizeHashed += size;

						dirFilesHashed++;
						totalFilesHashed++;
					}

					if (end > start) {
						totalHashTime += (end - start);
						dirHashTime += (end - start);
						averageSpeed = size * 1000 / (end - start);
					}

					if (!stopping) {
						if (failed) {
							HashManager::getInstance()->logHasher(STRING(ERROR_HASHING) + fname + ": " + STRING(ERROR_HASHING_CRC32), hasherID, true, true);
							HashManager::getInstance()->fire(HashManagerListener::FileFailed(), fname, fi);
						} else {
							fi = HashedFile(tt.getRoot(), timestamp, size);
							HashManager::getInstance()->hasherDone(fname, pathLower, tt, averageSpeed, fi, hasherID);
						}
					}
				} catch (const FileException& e) {
					totalBytesLeft -= sizeLeft;
					HashManager::getInstance()->logHasher(STRING(ERROR_HASHING) + " " + fname + ": " + e.getError(), hasherID, true, true);
					HashManager::getInstance()->fire(HashManagerListener::FileFailed(), fname, fi);
					failed = true;
				}

			}

			auto onDirHashed = [&]() -> void {
				if ((SETTING(HASHERS_PER_VOLUME) == 1 || w.empty()) && (dirFilesHashed > 1 || !failed)) {
					HashManager::getInstance()->fire(HashManagerListener::DirectoryHashed(), initialDir, dirFilesHashed, dirSizeHashed, dirHashTime, hasherID);
					if (dirFilesHashed == 1) {
						HashManager::getInstance()->logHasher(STRING_F(HASHING_FINISHED_FILE, currentFile %
							Util::formatBytes(dirSizeHashed) %
							Util::formatTime(dirHashTime / 1000, true) %
							(Util::formatBytes(dirHashTime > 0 ? ((dirSizeHashed * 1000) / dirHashTime) : 0) + "/s")), hasherID, false, false);
					} else {
						HashManager::getInstance()->logHasher(STRING_F(HASHING_FINISHED_DIR, Util::getFilePath(initialDir) %
							dirFilesHashed %
							Util::formatBytes(dirSizeHashed) %
							Util::formatTime(dirHashTime / 1000, true) %
							(Util::formatBytes(dirHashTime > 0 ? ((dirSizeHashed * 1000) / dirHashTime) : 0) + "/s")), hasherID, false, false);
					}
				}

				totalDirsHashed++;
				dirHashTime = 0;
				dirSizeHashed = 0;
				dirFilesHashed = 0;
				initialDir.clear();
			};

			bool deleteThis = false;
			{
				WLock l(hcs);
				if (!fname.empty()) {
					removeDevice(curDevID);
				}

				if (w.empty()) {
					// Finished hashing
					running = false;
					HashManager::getInstance()->fire(HashManagerListener::HasherFinished(), totalDirsHashed, totalFilesHashed, totalSizeHashed, totalHashTime, hasherID);

					if (totalSizeHashed > 0) {
						if (totalDirsHashed == 0) {
							onDirHashed();
							//log(STRING(HASHING_FINISHED_TOTAL_PLAIN), LogMessage::SEV_INFO);
						} else {
							onDirHashed();
							HashManager::getInstance()->logHasher(STRING_F(HASHING_FINISHED_TOTAL, totalFilesHashed % Util::formatBytes(totalSizeHashed) % totalDirsHashed %
								Util::formatTime(totalHashTime / 1000, true) %
								(Util::formatBytes(totalHashTime > 0 ? ((totalSizeHashed * 1000) / totalHashTime) : 0) + "/s")), hasherID, false, false);
						}
					} else if (!fname.empty()) {
						// All files failed to hash?
						HashManager::getInstance()->logHasher(STRING(HASHING_FINISHED), hasherID, false, false);

						// Always clear the directory so that there will be a fresh start when more files are added for hashing
						initialDir.clear();
					}

					clearStats();

					deleteThis = hasherID != 0;
					sfv.unload();
				} else if (!AirUtil::isParentOrExactLocal(initialDir, w.front().filePath)) {
					onDirHashed();
				}

				currentFile.clear();
			}

			if (!failed && !fname.empty()) {
				HashManager::getInstance()->fire(HashManagerListener::FileHashed(), fname, fi);
			}

			if (deleteThis) {
				// Check again if we have added new items while this was unlocked

				WLock l(hcs);
				if (w.empty()) {
					// Nothing more to hash, delete this hasher
					HashManager::getInstance()->removeHasher(this);
					break;
				}
			}
		}

		delete this;
		return 0;
	}

} // namespace dcpp
