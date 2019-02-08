/*
* Copyright (C) 2011-2019 AirDC++ Project
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

#include <api/TransferUtils.h>

#include <api/TransferApi.h>
#include <api/common/Format.h>


namespace webserver {
	const PropertyList TransferUtils::properties = {
		{ PROP_NAME, "name", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_TARGET, "target", TYPE_TEXT, SERIALIZE_TEXT, SORT_TEXT },
		{ PROP_TYPE, "type", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
		{ PROP_DOWNLOAD, "download", TYPE_NUMERIC_OTHER, SERIALIZE_BOOL, SORT_NUMERIC },
		{ PROP_SIZE, "size", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_STATUS, "status", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_BYTES_TRANSFERRED, "bytes_transferred", TYPE_SIZE, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_USER, "user", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_TIME_STARTED, "time_started", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_SPEED, "speed", TYPE_SPEED, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_SECONDS_LEFT, "seconds_left", TYPE_TIME, SERIALIZE_NUMERIC, SORT_NUMERIC },
		{ PROP_IP, "ip", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
		{ PROP_FLAGS, "flags", TYPE_LIST_TEXT, SERIALIZE_CUSTOM, SORT_CUSTOM },
		{ PROP_ENCRYPTION, "encryption", TYPE_TEXT, SERIALIZE_CUSTOM, SORT_TEXT },
		{ PROP_QUEUE_ID, "queue_file_id", TYPE_NUMERIC_OTHER, SERIALIZE_CUSTOM, SORT_NUMERIC },
	};

	const PropertyItemHandler<TransferInfoPtr> TransferUtils::propertyHandler = {
		properties,
		TransferUtils::getStringInfo, TransferUtils::getNumericInfo, TransferUtils::compareItems, TransferUtils::serializeProperty
	};

	std::string TransferUtils::getStringInfo(const TransferInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_NAME: return aItem->getName();
		case PROP_TARGET: return aItem->getTarget();
		case PROP_TYPE: return Util::formatFileType(aItem->getTarget());
		case PROP_STATUS: return aItem->getStatusString();
		case PROP_IP: return aItem->getIp();
		case PROP_USER: return Format::formatNicks(aItem->getHintedUser());
		case PROP_ENCRYPTION: return aItem->getEncryption();
		default: dcassert(0); return Util::emptyString;
		}
	}

	double TransferUtils::getNumericInfo(const TransferInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_SIZE: return (double)aItem->getSize();
		case PROP_DOWNLOAD: return (double)aItem->isDownload();
		case PROP_STATUS: return (double)aItem->getState();
		case PROP_BYTES_TRANSFERRED: return (double)aItem->getBytesTransferred();
		case PROP_TIME_STARTED: return (double)aItem->getStarted();
		case PROP_SPEED: return (double)aItem->getSpeed();
		case PROP_SECONDS_LEFT: return (double)aItem->getTimeLeft();
		case PROP_QUEUE_ID: return (double)aItem->getQueueToken();
		default: dcassert(0); return 0;
		}
	}

	int TransferUtils::compareItems(const TransferInfoPtr& a, const TransferInfoPtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case PROP_FLAGS: {
			return compare(Util::listToString(a->getFlags()), Util::listToString(b->getFlags()));
		}
		case PROP_USER: {
			if (a->isDownload() != b->isDownload()) {
				return a->isDownload() ? -1 : 1;
			}

			return Util::DefaultSort(Format::formatNicks(a->getHintedUser()), Format::formatNicks(b->getHintedUser()));
		}
		case PROP_STATUS: {
			if (a->getState() != b->getState()) {
				return compare(a->getState(), b->getState());
			}

			if (a->getState() == TransferInfo::STATE_RUNNING) {
				return compare(a->getPercentage(), b->getPercentage());
			}

			return Util::DefaultSort(a->getStatusString(), b->getStatusString());
		}
		default: dcassert(0);
		}
		return 0;
	}

	json TransferUtils::serializeProperty(const TransferInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
			case PROP_IP: return Serializer::serializeIp(aItem->getIp());
			case PROP_USER: return Serializer::serializeHintedUser(aItem->getHintedUser());
			case PROP_STATUS:
			{
				return {
					{ "id", aItem->getStateKey() },
					{ "str", aItem->getStatusString() },
				};
			}
			case PROP_TYPE: {
				if (aItem->getTarget().empty()) {
					return nullptr;
				}

				if (aItem->isFilelist()) {
					return{
						{ "id", "file" },
						{ "content_type", "filelist" },
						{ "str", aItem->getName() }
					};
				}

				return Serializer::serializeFileType(aItem->getTarget());
			}
			case PROP_FLAGS: return aItem->getFlags();
			case PROP_ENCRYPTION:
			{
				auto trusted = aItem->getFlags().find("S") != aItem->getFlags().end();
				return Serializer::serializeEncryption(aItem->getEncryption(), trusted);
			}
			case PROP_QUEUE_ID:
			{
				if (aItem->getQueueToken() == 0) {
					return nullptr;
				}

				return aItem->getQueueToken();
			}
		}

		dcassert(0);
		return nullptr;
	}
}