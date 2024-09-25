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

#ifndef DCPLUSPLUS_DCPP_VIEWFILE_H
#define DCPLUSPLUS_DCPP_VIEWFILE_H

#include <airdcpp/forward.h>

#include <airdcpp/GetSet.h>
#include <airdcpp/MerkleTree.h>
#include <airdcpp/TrackableDownloadItem.h>

namespace dcpp {
	class ViewFile : public TrackableDownloadItem {

	using UpdateF = std::function<void (const TTHValue &)>;
	public:
		ViewFile(const string& aFileName, const string& aPath, const TTHValue& aTTH, bool aIsText, bool aIsLocalFile, UpdateF&& aUpdateFunction) noexcept;
		~ViewFile() noexcept final;

		const string& getPath() const noexcept {
			return path;
		}

		const string& getFileName() const noexcept {
			return fileName;
		}

		bool isText() const noexcept {
			return text;
		}

		bool isLocalFile() const noexcept {
			return localFile;
		}

		const TTHValue& getTTH() const noexcept {
			return tth;
		}

		time_t getTimeCreated() const noexcept {
			return timeCreated;
		}

		IGETSET(bool, read, Read, false);
	protected:
		void onStateChanged() noexcept override;
	private:
		const string fileName;
		const string path;
		const UpdateF updateFunction;
		const TTHValue tth;
		const bool text;
		const bool localFile;
		const time_t timeCreated;
	};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_VIEWFILE_H)
