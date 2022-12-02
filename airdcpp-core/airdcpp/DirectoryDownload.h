/*
 * Copyright (C) 2011-2022 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_DIRECTORYDOWNLOAD_H_
#define DCPLUSPLUS_DCPP_DIRECTORYDOWNLOAD_H_

#include "forward.h"

#include "QueueAddInfo.h"
#include "GetSet.h"
#include "Priority.h"

namespace dcpp {
	typedef uint32_t DirectoryDownloadId;
	class DirectoryDownload {
	public:
		enum class State {
			PENDING,
			QUEUED,
			FAILED
		};

		enum class ErrorMethod {
			NONE,
			LOG,
		};

		DirectoryDownload(const FilelistAddData& aListData, const string& aBundleName, const string& aTarget, Priority p, ErrorMethod aErrorMethod);

		IGETSET(QueueItemPtr, queueItem, QueueItem, nullptr);
		IGETSET(uint64_t, processedTick, ProcessedTick, 0);
		IGETSET(State, state, State, State::PENDING);

		GETSET(optional<DirectoryBundleAddResult>, queueInfo, QueueInfo);
		GETSET(string, error, Error);

		struct HasOwner {
			HasOwner(void* aOwner, const string& s) : a(s), owner(aOwner) { }
			bool operator()(const DirectoryDownloadPtr& ddi) const noexcept;

			const string& a;
			void* owner;

			HasOwner& operator=(const HasOwner&) = delete;
		};

		const HintedUser& getUser() const noexcept { return listData.user; }
		const string& getBundleName() const noexcept { return bundleName; }
		const string& getTarget() const noexcept { return target; }
		const string& getListPath() const noexcept { return listData.listPath; }
		Priority getPriority() const noexcept { return priority; }
		const void* getOwner() const noexcept { return listData.caller; }
		DirectoryDownloadId getId() const noexcept { return id; }
		ErrorMethod getErrorMethod() const noexcept { return errorMethod; }
		const FilelistAddData& getListData() const noexcept { return listData; }
	private:
		const DirectoryDownloadId id;
		const Priority priority;
		const string target;
		const string bundleName;
		const time_t created;
		const FilelistAddData listData;
		const ErrorMethod errorMethod;
	};
}

#endif /*DCPLUSPLUS_DCPP_DIRECTORYDOWNLOAD_H_ */