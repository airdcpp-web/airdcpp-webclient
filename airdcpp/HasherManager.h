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

#ifndef DCPLUSPLUS_DCPP_HASHERHANDLER_H
#define DCPLUSPLUS_DCPP_HASHERHANDLER_H

#include <airdcpp/typedefs.h>

#include <airdcpp/MerkleTree.h>
#include <airdcpp/Message.h>

namespace dcpp {
	class HasherStats;
	struct HasherManager {
		virtual void onFileHashed(const string& aPath, HashedFile& aFile, const TigerTree& aTree, int aHasherId) noexcept = 0;
		virtual void onFileFailed(const string& aPath, const string& aErrorId, const string& aMessage, int aHasherId) noexcept = 0;
		virtual void onDirectoryHashed(const string& aPath, const HasherStats&, int aHasherId) noexcept = 0;
		virtual void onHasherFinished(int aDirectoriesHashed, const HasherStats&, int aHasherId) noexcept = 0;
		virtual void logHasher(const string& aMessage, int aHasherID, LogMessage::Severity aSeverity, bool aLock) const noexcept = 0;
		virtual void removeHasher(int aHasherId) noexcept = 0;
	};
} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_HASHERHANDLER_H)