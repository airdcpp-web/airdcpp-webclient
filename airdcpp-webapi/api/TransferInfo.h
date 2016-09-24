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

#ifndef DCPLUSPLUS_DCPP_TRANSFERINFO_H
#define DCPLUSPLUS_DCPP_TRANSFERINFO_H

#include <web-server/stdinc.h>

#include <airdcpp/typedefs.h>
#include <airdcpp/HintedUser.h>
#include <airdcpp/ResourceManager.h>
#include <airdcpp/Transfer.h>


namespace webserver {
	typedef uint32_t TransferToken;
	class TransferInfo {
	public:
		enum ItemState {
			STATE_WAITING,
			STATE_FAILED,
			STATE_RUNNING,
			STATE_FINISHED,
			STATE_LAST,
		};

		typedef shared_ptr<TransferInfo> Ptr;
		typedef vector<Ptr> List;
		typedef unordered_map<string, Ptr> Map;

		TransferInfo(const HintedUser& aUser, bool aIsDownload, const std::string& aToken) :
			user(aUser), download(aIsDownload), stringToken(aToken)
		{ }

		IGETSET(int64_t, timeLeft, TimeLeft, -1);
		IGETSET(int64_t, size, Size, -1);

		GETSET(string, encryption, Encryption);
		GETSET(string, ip, Ip);
		GETSET(string, target, Target);
		GETSET(string, statusString, StatusString);
		GETSET(OrderedStringSet, flags, Flags);

		IGETSET(Transfer::Type, type, Type, Transfer::TYPE_LAST)

		IGETSET(int64_t, started, Started, 0);
		IGETSET(int64_t, bytesTransferred, BytesTransferred, -1);
		IGETSET(int64_t, speed, Speed, 0);
		IGETSET(ItemState, state, State, STATE_WAITING);

		const TransferToken getToken() const noexcept {
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

		string getName() {
			switch (type) {
				case Transfer::TYPE_TREE: return "TTH: " + Util::getFileName(target);
				case Transfer::TYPE_FULL_LIST: return STRING(FILE_LIST);
				case Transfer::TYPE_PARTIAL_LIST: return STRING(FILE_LIST_PARTIAL);
				default: return Util::getFileName(target);
			}
		}

		string getStateKey() {
			switch (state) {
			case STATE_WAITING: return "waiting";
			case STATE_FINISHED: return "finished";
			case STATE_RUNNING: return "running";
			case STATE_FAILED: return "failed";
			default: dcassert(0); return Util::emptyString;
			}
		}
	private:
		const HintedUser user;
		const bool download;

		const TransferToken token = Util::rand();
		const std::string stringToken;

		bool transferFailed = false;
	};

	typedef TransferInfo::Ptr TransferInfoPtr;
}

#endif