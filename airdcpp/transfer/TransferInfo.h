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

#ifndef DCPLUSPLUS_DCPP_TRANSFERINFO_H
#define DCPLUSPLUS_DCPP_TRANSFERINFO_H

#include <airdcpp/core/header/typedefs.h>

#include <airdcpp/user/HintedUser.h>
#include <airdcpp/util/PathUtil.h>
#include <airdcpp/core/classes/IncrementingIdCounter.h>
#include <airdcpp/core/localization/ResourceManager.h>
#include <airdcpp/transfer/Transfer.h>


namespace dcpp {
	using TransferInfoToken = uint32_t;
	class TransferInfo {
	public:

		enum UpdateFlags {
			STATE = 0x01,
			TARGET = 0x02,
			TYPE = 0x04,
			SIZE = 0x08,
			STATUS = 0x10,
			BYTES_TRANSFERRED = 0x40,
			USER = 0x80,
			TIME_STARTED = 0x100,
			SPEED = 0x200,
			SECONDS_LEFT = 0x400,
			IP = 0x800,
			FLAGS = 0x1000,
			ENCRYPTION = 0x2000,
			QUEUE_ID = 0x4000,
			SUPPORTS = 0x8000,
		};

		enum ItemState {
			STATE_WAITING,
			STATE_FAILED,
			STATE_RUNNING,
			STATE_FINISHED,
			STATE_LAST,
		};

		using Ptr = shared_ptr<TransferInfo>;
		using List = vector<Ptr>;
		using Map = unordered_map<string, Ptr>;

		TransferInfo(const HintedUser& aUser, bool aIsDownload, const std::string& aStringToken) :
			user(aUser), download(aIsDownload), stringToken(aStringToken), token(idCounter.next())
		{ }

		IGETSET(int64_t, timeLeft, TimeLeft, -1);
		IGETSET(int64_t, size, Size, -1);

		GETSET(string, encryption, Encryption);
		GETSET(string, ip, Ip);
		GETSET(string, target, Target);
		GETSET(string, statusString, StatusString)
		GETSET(string, bundle, Bundle);
		GETSET(OrderedStringSet, flags, Flags);
		GETSET(StringList, supports, Supports);

		IGETSET(Transfer::Type, type, Type, Transfer::TYPE_LAST)

		IGETSET(int64_t, started, Started, 0);
		IGETSET(int64_t, bytesTransferred, BytesTransferred, -1);
		IGETSET(int64_t, speed, Speed, 0);
		IGETSET(ItemState, state, State, STATE_WAITING);

		IGETSET(QueueToken, queueToken, QueueToken, 0);

		TransferInfoToken getToken() const noexcept {
			return token;
		}

		double getPercentage() const noexcept {
			return size > 0 ? static_cast<double>(bytesTransferred) * 100.0 / static_cast<double>(size) : 0;
		}

		const string& getStringToken() const noexcept {
			return stringToken;
		}

		bool isDownload() const noexcept {
			return download;
		}

		bool isFilelist() const noexcept {
			return type == Transfer::TYPE_PARTIAL_LIST || type == Transfer::TYPE_FULL_LIST;
		}

		const HintedUser& getHintedUser() const noexcept {
			return user;
		}

		void setHubUrl(const string& aHubUrl) noexcept {
			user.hint = aHubUrl;
		}

		string getName() const noexcept {
			switch (type) {
			case Transfer::TYPE_TREE: return "TTH: " + PathUtil::getFileName(target);
			case Transfer::TYPE_FULL_LIST: return STRING(TYPE_FILE_LIST);
			case Transfer::TYPE_PARTIAL_LIST: return STRING(TYPE_FILE_LIST_PARTIAL);
			case Transfer::TYPE_TTH_LIST: return STRING(TYPE_TTHLIST);
			default: return PathUtil::getFileName(target);
			}
		}
	private:
		HintedUser user;
		const bool download;

		const TransferInfoToken token;
		const std::string stringToken;

		static IncrementingIdCounter<TransferInfoToken> idCounter;
	};

	using TransferInfoPtr = TransferInfo::Ptr;
}

#endif