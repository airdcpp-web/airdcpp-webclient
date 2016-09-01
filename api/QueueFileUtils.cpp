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

#include <api/QueueFileUtils.h>
#include <api/common/Format.h>
#include <api/common/Serializer.h>

#include <airdcpp/AirUtil.h>
#include <airdcpp/Bundle.h>
#include <airdcpp/QueueItem.h>
#include <airdcpp/QueueManager.h>


namespace webserver {
	const PropertyList QueueFileUtils::properties = {
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
		{ PROP_BUNDLE, "bundle", TYPE_NUMERIC_OTHER, SERIALIZE_NUMERIC, SORT_NUMERIC },
	};

	const PropertyItemHandler<QueueItemPtr> QueueFileUtils::propertyHandler = {
		properties,
		QueueFileUtils::getStringInfo, QueueFileUtils::getNumericInfo, QueueFileUtils::compareFiles, QueueFileUtils::serializeFileProperty
	};

	std::string QueueFileUtils::formatDisplayStatus(const QueueItemPtr& aItem) noexcept {
		if (aItem->isSet(QueueItem::FLAG_FINISHED)) {
			return STRING(FINISHED);
		} 
		
		auto percentage = aItem->getPercentage(QueueManager::getInstance()->getDownloadedBytes(aItem));
		if (aItem->isPausedPrio()) {
			return STRING_F(PAUSED_PCT, percentage);
		} else if (QueueManager::getInstance()->isWaiting(aItem)) {
			return STRING_F(WAITING_PCT, percentage);
		} else {
			return STRING_F(RUNNING_PCT, percentage);
		}
	}

	std::string QueueFileUtils::formatFileSources(const QueueItemPtr& aItem) noexcept {
		return QueueManager::getInstance()->getSourceCount(aItem).format();
	}

	std::string QueueFileUtils::getStringInfo(const QueueItemPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_NAME: return getDisplayName(aItem);
		case PROP_TARGET: return aItem->getTarget();
		case PROP_TYPE: return Format::formatFileType(aItem->getTarget());
		case PROP_STATUS: return formatDisplayStatus(aItem);
		case PROP_PRIORITY: return AirUtil::getPrioText(aItem->getPriority());
		case PROP_SOURCES: return formatFileSources(aItem);
		default: dcassert(0); return Util::emptyString;
		}
	}

	double QueueFileUtils::getNumericInfo(const QueueItemPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_SIZE: return (double)aItem->getSize();
		case PROP_BYTES_DOWNLOADED: return (double)QueueManager::getInstance()->getDownloadedBytes(aItem);
		case PROP_PRIORITY: return aItem->getPriority();
		case PROP_TIME_ADDED: return (double)aItem->getTimeAdded();
		case PROP_TIME_FINISHED: return (double)aItem->getTimeFinished();
		case PROP_SPEED: return (double)QueueManager::getInstance()->getAverageSpeed(aItem);
		case PROP_SECONDS_LEFT: return (double)QueueManager::getInstance()->getSecondsLeft(aItem);
		case PROP_BUNDLE: return (double)(aItem->getBundle() ? aItem->getBundle()->getToken() : -1);
		default: dcassert(0); return 0;
		}
	}

#define COMPARE_FINISHED(a, b) if (a->isSet(QueueItem::FLAG_FINISHED) != b->isSet(QueueItem::FLAG_FINISHED)) return a->isSet(QueueItem::FLAG_FINISHED) ? 1 : -1;

	string QueueFileUtils::getDisplayName(const QueueItemPtr& aItem) noexcept {
		if (aItem->getBundle() && !aItem->getBundle()->isFileBundle()) {
			return aItem->getTarget().substr(aItem->getBundle()->getTarget().size(), aItem->getTarget().size());
		}

		return aItem->getTargetFileName();
	}

	int QueueFileUtils::compareFiles(const QueueItemPtr& a, const QueueItemPtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_NAME: {
			return Util::pathSort(a->getTarget(), b->getTarget());
		}
		case PROP_TYPE: {
			return Util::stricmp(Util::getFileExt(a->getTarget()), Util::getFileExt(b->getTarget()));
		}
		case PROP_PRIORITY: {
			COMPARE_FINISHED(a, b);

			return compare(static_cast<int>(a->getPriority()), static_cast<int>(b->getPriority()));
		}
		case PROP_STATUS: {
			COMPARE_FINISHED(a, b);
			return compare(
				a->getPercentage(QueueManager::getInstance()->getDownloadedBytes(a)), 
				b->getPercentage(QueueManager::getInstance()->getDownloadedBytes(b))
			);
		}
		case PROP_SOURCES: {
			COMPARE_FINISHED(a, b);

			auto countsA = QueueManager::getInstance()->getSourceCount(a);
			auto countsB = QueueManager::getInstance()->getSourceCount(b);

			return QueueItemBase::SourceCount::compare(countsA, countsB);
		}
		default:
			dcassert(0);
		}

		return 0;
	}

	json QueueFileUtils::serializeFileProperty(const QueueItemPtr& aFile, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_SOURCES:
		{
			auto c = QueueManager::getInstance()->getSourceCount(aFile);
			return Serializer::serializeSourceCount(c);
		}

		case PROP_STATUS:
		{
			return {
				{ "finished", aFile->isSet(QueueItem::FLAG_FINISHED) },
				{ "str", formatDisplayStatus(aFile) },
			};
		}
		case PROP_PRIORITY: {
			return Serializer::serializePriority(*aFile.get());
		}
		case PROP_TYPE:
		{
			return Serializer::serializeFileType(aFile->getTarget());
		}
		}

		dcassert(0);
		return nullptr;
	}
}