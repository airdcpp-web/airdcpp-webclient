/*
* Copyright (C) 2011-2024 AirDC++ Project
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

#include <api/QueueBundleUtils.h>
#include <api/common/Format.h>
#include <api/common/Serializer.h>

#include <airdcpp/queue/Bundle.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/queue/QueueItem.h>
#include <airdcpp/queue/QueueManager.h>


namespace webserver {
	const PropertyList QueueBundleUtils::properties = {
		{ PROP_NAME, "name", TYPE_TEXT, SERIALIZE_TEXT, SORT_CUSTOM },
		{ PROP_TARGET, "target", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_TYPE, "type", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_SIZE, "size", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_STATUS, "status", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_BYTES_DOWNLOADED, "downloaded_bytes", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_PRIORITY, "priority", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_TIME_ADDED, "time_added", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_TIME_FINISHED, "time_finished", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_SPEED, "speed", TYPE_SPEED, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_SECONDS_LEFT, "seconds_left", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_SOURCES, "sources", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
	};

	const PropertyItemHandler<BundlePtr> QueueBundleUtils::propertyHandler = {
		properties,
		QueueBundleUtils::getStringInfo, QueueBundleUtils::getNumericInfo, QueueBundleUtils::compareBundles, QueueBundleUtils::serializeBundleProperty
	};

	std::string QueueBundleUtils::formatBundleSources(const BundlePtr& aBundle) noexcept {
		return QueueManager::getInstance()->getSourceCount(aBundle).format();
	}

	std::string QueueBundleUtils::getStringInfo(const BundlePtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_NAME: return b->getName();
		case PROP_TARGET: return b->getTarget();
		case PROP_TYPE: return formatBundleType(b);
		case PROP_STATUS: return b->getStatusString();
		case PROP_PRIORITY: return Util::formatPriority(b->getPriority());
		case PROP_SOURCES: return formatBundleSources(b);
		default: dcassert(0); return Util::emptyString;
		}
	}

	std::string QueueBundleUtils::formatBundleType(const BundlePtr& aBundle) noexcept {
		if (aBundle->isFileBundle()) {
			return Util::formatFileType(aBundle->getTarget());
		}
		
		return Util::formatDirectoryContent(QueueManager::getInstance()->getBundleContent(aBundle));
	}

	double QueueBundleUtils::getNumericInfo(const BundlePtr& b, int aPropertyName) noexcept {
		dcassert(b->getSize() != 0);
		switch (aPropertyName) {
		case PROP_SIZE: return (double)b->getSize();
		case PROP_PRIORITY: return (double)b->getPriority();
		case PROP_TIME_ADDED: return (double)b->getTimeAdded();
		case PROP_TIME_FINISHED: return (double)b->getTimeFinished();
		case PROP_BYTES_DOWNLOADED: return (double)b->getDownloadedBytes();
		case PROP_SPEED: return (double)b->getSpeed();
		case PROP_SECONDS_LEFT: return (double)b->getSecondsLeft();
		default: dcassert(0); return 0;
		}
	}

#define COMPARE_IS_DOWNLOADED(a, b) if (a->isDownloaded() != b->isDownloaded()) return a->isDownloaded() ? 1 : -1
#define COMPARE_TYPE(a, b) if (a->isFileBundle() != b->isFileBundle()) return a->isFileBundle() ? 1 : -1

	int QueueBundleUtils::compareBundles(const BundlePtr& a, const BundlePtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_NAME: {
			COMPARE_TYPE(a, b);

			return Util::stricmp(a->getName(), b->getName());
		}
		case PROP_TYPE: {
			COMPARE_TYPE(a, b);
			
			if (!a->isFileBundle() && !b->isFileBundle()) {
				// Directory bundles
				auto contentA = QueueManager::getInstance()->getBundleContent(a);
				auto contentB = QueueManager::getInstance()->getBundleContent(b);

				return DirectoryContentInfo::Sort(contentA, contentB);
			}

			return Util::stricmp(PathUtil::getFileExt(a->getTarget()), PathUtil::getFileExt(b->getTarget()));
		}
		case PROP_PRIORITY: {
			COMPARE_IS_DOWNLOADED(a, b);

			return compare(static_cast<int>(a->getPriority()), static_cast<int>(b->getPriority()));
		}
		case PROP_STATUS: {
			if (a->getStatus() != b->getStatus()) {
				return compare(a->getStatus(),  b->getStatus());
			}

			return compare(
				a->getPercentage(a->getDownloadedBytes()), 
				b->getPercentage(b->getDownloadedBytes())
			);
		}
		case PROP_SOURCES: {
			COMPARE_IS_DOWNLOADED(a, b);

			auto countsA = QueueManager::getInstance()->getSourceCount(a);
			auto countsB = QueueManager::getInstance()->getSourceCount(b);

			return QueueItemBase::SourceCount::compare(countsA, countsB);
		}
		default:
			dcassert(0);
		}

		return 0;
	}

	string QueueBundleUtils::formatStatusId(const BundlePtr& aBundle) noexcept {
		switch (aBundle->getStatus()) {
			case Bundle::STATUS_NEW: return "new";
			case Bundle::STATUS_QUEUED: return "queued";
			case Bundle::STATUS_RECHECK: return "recheck";
			case Bundle::STATUS_DOWNLOADED: return "downloaded";
			case Bundle::STATUS_DOWNLOAD_ERROR: return "download_error";
			case Bundle::STATUS_VALIDATION_RUNNING: return "completion_validation_running";
			case Bundle::STATUS_VALIDATION_ERROR: return "completion_validation_error";
			case Bundle::STATUS_COMPLETED: return "completed";
			case Bundle::STATUS_SHARED: return "shared";
		}

		dcassert(0);
		return Util::emptyString;
	}

	json QueueBundleUtils::serializeBundleProperty(const BundlePtr& aBundle, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_SOURCES:
		{
			auto c = QueueManager::getInstance()->getSourceCount(aBundle);
			return Serializer::serializeSourceCount(c);
		}

		case PROP_STATUS:
		{
			return {
				{ "id", formatStatusId(aBundle) },
				{ "failed", aBundle->isFailed() },
				{ "downloaded", aBundle->isDownloaded() },
				{ "completed", aBundle->isCompleted() },
				{ "str", aBundle->getStatusString() },
				{ "hook_error", Serializer::serializeActionHookError(aBundle->getHookError()) }
			};
		}

		case PROP_TYPE:
		{
			if (aBundle->isFileBundle()) {
				return Serializer::serializeFileType(aBundle->getTarget());
			}

			return Serializer::serializeFolderType(QueueManager::getInstance()->getBundleContent(aBundle));
		}
		case PROP_PRIORITY: {
			return Serializer::serializePriority(*aBundle.get());
		}
		}

		dcassert(0);
		return nullptr;
	}
}