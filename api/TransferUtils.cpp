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

#include <api/TransferUtils.h>

#include <api/TransferApi.h>
#include <api/common/Format.h>


namespace webserver {
	std::string TransferUtils::getStringInfo(const TransferInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case TransferApi::PROP_NAME: return aItem->getName();
		case TransferApi::PROP_TARGET: return aItem->getTarget();
		case TransferApi::PROP_STATUS: return aItem->getStatusString();
		case TransferApi::PROP_IP: return aItem->getIp();
		case TransferApi::PROP_USER: return Format::formatNicks(aItem->getHintedUser());
		case TransferApi::PROP_ENCRYPTION: return aItem->getEncryption();
		default: dcassert(0); return Util::emptyString;
		}
	}

	double TransferUtils::getNumericInfo(const TransferInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case TransferApi::PROP_SIZE: return (double)aItem->getSize();
		case TransferApi::PROP_DOWNLOAD: return (double)aItem->isDownload();
		case TransferApi::PROP_STATUS: return (double)aItem->getState();
		case TransferApi::PROP_BYTES_TRANSFERRED: return (double)aItem->getBytesTransferred();
		case TransferApi::PROP_TIME_STARTED: return (double)aItem->getStarted();
		case TransferApi::PROP_SPEED: return (double)aItem->getSpeed();
		case TransferApi::PROP_SECONDS_LEFT: return (double)aItem->getTimeLeft();
		default: dcassert(0); return 0;
		}
	}

	int TransferUtils::compareItems(const TransferInfoPtr& a, const TransferInfoPtr& b, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case TransferApi::PROP_FLAGS: {
			return compare(Util::listToString(a->getFlags()), Util::listToString(b->getFlags()));
		}
		case TransferApi::PROP_USER: {
			if (a->isDownload() != b->isDownload()) {
				return a->isDownload() ? -1 : 1;
			}

			return Util::stricmp(Format::formatNicks(a->getHintedUser()), Format::formatNicks(b->getHintedUser()));
		}
		case TransferApi::PROP_STATUS: {
			if (a->getState() != b->getState()) {
				return compare(a->getState(), b->getState());
			}

			return Util::stricmp(a->getStatusString(), b->getStatusString());
		}
		default: dcassert(0); return 0;
		}
		return 0;
	}

	json TransferUtils::serializeProperty(const TransferInfoPtr& aItem, int aPropertyName) noexcept {
		switch (aPropertyName) {
		case TransferApi::PROP_IP: return Serializer::serializeIp(aItem->getIp());
		case TransferApi::PROP_USER: return Serializer::serializeHintedUser(aItem->getHintedUser());
		case TransferApi::PROP_STATUS:
		{
			return {
				{ "id", aItem->getStateKey() },
				{ "str", aItem->getStatusString() },
			};
		}
		case TransferApi::PROP_FLAGS: return aItem->getFlags();
		}

		dcassert(0);
		return json();
	}
}