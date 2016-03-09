/*
* Copyright (C) 2011-2016 AirDC++ Project
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

#include <web-server/stdinc.h>

#include <api/QueueUtils.h>

#include <api/QueueApi.h>
#include <api/common/Format.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/Bundle.h>
#include <airdcpp/QueueItem.h>
#include <airdcpp/QueueManager.h>

#include <boost/range/algorithm/copy.hpp>

namespace webserver {
	BundleList QueueUtils::getBundleList() noexcept {
		BundleList bundles;
		auto qm = QueueManager::getInstance();

		RLock l(qm->getCS());
		boost::range::copy(qm->getBundles() | map_values, back_inserter(bundles));
		return bundles;
	}

	std::string QueueUtils::formatDisplayStatus(const BundlePtr& aBundle) noexcept {
		auto getPercentage = [&] {
			return aBundle->getSize() > 0 ? (double)aBundle->getDownloadedBytes() *100.0 / (double)aBundle->getSize() : 0;
		};

		switch (aBundle->getStatus()) {
		case Bundle::STATUS_NEW:
		case Bundle::STATUS_QUEUED: {
			if (aBundle->isPausedPrio())
				return STRING_F(PAUSED_PCT, getPercentage());

			if (aBundle->getSpeed() > 0) { // Bundle->isRunning() ?
				return STRING_F(RUNNING_PCT, getPercentage());
			}
			else {
				return STRING_F(WAITING_PCT, getPercentage());
			}
		}
		case Bundle::STATUS_RECHECK: return STRING(RECHECKING);
		case Bundle::STATUS_DOWNLOADED: return STRING(MOVING);
		case Bundle::STATUS_MOVED: return STRING(DOWNLOADED);
		case Bundle::STATUS_DOWNLOAD_FAILED:
		case Bundle::STATUS_FAILED_MISSING:
		case Bundle::STATUS_SHARING_FAILED: return aBundle->getLastError();
		case Bundle::STATUS_FINISHED: return STRING(FINISHED);
		case Bundle::STATUS_HASHING: return STRING(HASHING);
		case Bundle::STATUS_HASH_FAILED: return STRING(HASH_FAILED);
		case Bundle::STATUS_HASHED: return STRING(HASHING_FINISHED);
		case Bundle::STATUS_SHARED: return STRING(SHARED);
		default:
			return Util::emptyString;
		}
	}

	json QueueUtils::serializePriority(const QueueItemBase& aItem) noexcept {
		return{
			{ "id", aItem.getPriority() },
			{ "str", AirUtil::getPrioText(aItem.getPriority()) },
			{ "auto_prio", aItem.getAutoPriority() }
		};
	}

	void QueueUtils::getBundleSourceInfo(const BundlePtr& aBundle, int& online_, int& total_, string& str_) noexcept {
		auto sources = QueueManager::getInstance()->getBundleSources(aBundle);
		for (const auto& s : sources) {
			if (s.getUser().user->isOnline())
				online_++;
		}

		total_ = sources.size();

		str_ = sources.size() == 0 ? STRING(NONE) : STRING_F(USERS_ONLINE, online_ % sources.size());
	}

	std::string QueueUtils::formatBundleSources(const BundlePtr& aBundle) noexcept {
		int total = 0, online = 0;
		std::string str;
		getBundleSourceInfo(aBundle, online, total, str);
		return str;
	}

	std::string QueueUtils::getStringInfo(const BundlePtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case QueueApi::PROP_NAME: return b->getName();
		case QueueApi::PROP_TARGET: return b->getTarget();
		case QueueApi::PROP_TYPE: return formatBundleType(b);
		case QueueApi::PROP_STATUS: return formatDisplayStatus(b);
		case QueueApi::PROP_PRIORITY: return AirUtil::getPrioText(b->getPriority());
		case QueueApi::PROP_SOURCES: return formatBundleSources(b);
		default: dcassert(0); return Util::emptyString;
		}
	}

	std::string QueueUtils::formatBundleType(const BundlePtr& aBundle) noexcept {
		if (aBundle->isFileBundle()) {
			return Format::formatFileType(aBundle->getTarget());
		} else {
			size_t files = 0, folders = 0;
			QueueManager::getInstance()->getBundleContent(aBundle, files, folders);
			return Format::formatFolderContent(files, folders);
		}
	}

	double QueueUtils::getNumericInfo(const BundlePtr& b, int aPropertyName) noexcept {
		dcassert(b->getSize() != 0);
		switch (aPropertyName) {
		case QueueApi::PROP_SIZE: return (double)b->getSize();
		case QueueApi::PROP_BYTES_DOWNLOADED: return (double)b->getDownloadedBytes();
		case QueueApi::PROP_PRIORITY: return b->getPriority();
		case QueueApi::PROP_TIME_ADDED: return (double)b->getTimeAdded();
		case QueueApi::PROP_TIME_FINISHED: return (double)b->getTimeFinished();
		case QueueApi::PROP_SPEED: return (double)b->getSpeed();
		case QueueApi::PROP_SECONDS_LEFT: return (double)b->getSecondsLeft();
		default: dcassert(0); return 0;
		}
	}

	int QueueUtils::compareBundles(const BundlePtr& a, const BundlePtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case QueueApi::PROP_NAME: {
			if (a->isFileBundle() && !b->isFileBundle()) return 1;
			if (!a->isFileBundle() && b->isFileBundle()) return -1;

			return Util::stricmp(a->getName(), b->getName());
		}
		case QueueApi::PROP_TYPE: {
			if (a->isFileBundle() != b->isFileBundle()) {
				// Directories go first
				return a->isFileBundle() ? 1 : -1;
			} 
			
			if (!a->isFileBundle() && !b->isFileBundle()) {
				// Directory bundles
				RLock l(QueueManager::getInstance()->getCS());
				auto dirsA = QueueManager::getInstance()->bundleQueue.getDirectoryCount(a);
				auto dirsB = QueueManager::getInstance()->bundleQueue.getDirectoryCount(b);

				if (dirsA != dirsB) {
					return compare(dirsA, dirsB);
				}

				auto filesA = a->getQueueItems().size() + a->getFinishedFiles().size();
				auto filesB = b->getQueueItems().size() + b->getFinishedFiles().size();

				return compare(filesA, filesB);
			}

			return Util::stricmp(Util::getFileExt(a->getTarget()), Util::getFileExt(b->getTarget()));
		}
		case QueueApi::PROP_PRIORITY: {
			if (a->isFinished() != b->isFinished()) {
				return a->isFinished() ? 1 : -1;
			}

			return compare(static_cast<int>(a->getPriority()), static_cast<int>(b->getPriority()));
		}
		case QueueApi::PROP_STATUS: {
			if (a->getStatus() != b->getStatus()) {
				return compare(a->getStatus(),  b->getStatus());
			}

			return compare(a->getDownloadedBytes(), b->getDownloadedBytes());
		}
		case QueueApi::PROP_SOURCES: {
			if (a->isFinished() != b->isFinished()) {
				return a->isFinished() ? 1 : -1;
			}

			int onlineA = 0, totalA = 0, onlineB = 0, totalB = 0;
			std::string str;
			getBundleSourceInfo(a, onlineA, totalA, str);
			getBundleSourceInfo(b, onlineB, totalB, str);

			if (onlineA != onlineB) {
				return compare(onlineA, onlineB);
			}

			return compare(totalA, totalB);
		}
		default:
			dcassert(0);
		}

		return 0;
	}

	string QueueUtils::formatStatusId(const BundlePtr& aBundle) noexcept {
		switch (aBundle->getStatus()) {
			case Bundle::STATUS_NEW: return "new";
			case Bundle::STATUS_QUEUED: return "queued";
			case Bundle::STATUS_RECHECK: return "recheck";
			case Bundle::STATUS_DOWNLOADED: return "downloaded";
			case Bundle::STATUS_MOVED: return "moved";
			case Bundle::STATUS_DOWNLOAD_FAILED: return "download_failed";
			case Bundle::STATUS_FAILED_MISSING: return "scan_failed_files_missing";
			case Bundle::STATUS_SHARING_FAILED: return "scan_failed";
			case Bundle::STATUS_FINISHED: return "finished";
			case Bundle::STATUS_HASHING: return "hashing";
			case Bundle::STATUS_HASH_FAILED: return "hash_failed";
			case Bundle::STATUS_HASHED: return "hashed";
			case Bundle::STATUS_SHARED: return "shared";
		}

		dcassert(0);
		return Util::emptyString;
	}

	json QueueUtils::serializeBundleProperty(const BundlePtr& aBundle, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case QueueApi::PROP_SOURCES:
		{
			int total = 0, online = 0;
			std::string str;
			getBundleSourceInfo(aBundle, online, total, str);

			return {
				{ "online", online },
				{ "total", total },
				{ "str", str },
			};
		}

		case QueueApi::PROP_STATUS:
		{
			return{
				{ "id", formatStatusId(aBundle) },
				{ "failed", aBundle->isFailed() },
				{ "finished", aBundle->getStatus() >= Bundle::STATUS_FINISHED },
				{ "str", formatDisplayStatus(aBundle) },
			};
		}

		case QueueApi::PROP_TYPE:
		{
			if (aBundle->isFileBundle()) {
				return Serializer::serializeFileType(aBundle->getTarget());
			} else {
				size_t files = 0, folders = 0;
				QueueManager::getInstance()->getBundleContent(aBundle, files, folders);

				return Serializer::serializeFolderType(static_cast<int>(files), static_cast<int>(folders));
			}
		}
		case QueueApi::PROP_PRIORITY: {
			return serializePriority(*aBundle.get());
		}
		}

		dcassert(0);
		return nullptr;
	}
}