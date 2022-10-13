/*
* Copyright (C) 2014-2022 AirDC++ Project
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

#ifndef DCPLUSPLUS_DCPP_HASHMANAGERLISTENER_H
#define DCPLUSPLUS_DCPP_HASHMANAGERLISTENER_H

#include "typedefs.h"

namespace dcpp {
	class HashManagerListener {
	public:
		virtual ~HashManagerListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> FileHashed;
		typedef X<1> FileFailed;
		typedef X<2> MaintananceFinished;
		typedef X<3> MaintananceStarted;
		typedef X<4> DirectoryHashed;
		typedef X<5> HasherFinished;

		virtual void on(FileHashed, const string& /* aFilePath */, HashedFile& /* aFileInfo */) noexcept { }
		virtual void on(FileFailed, const string& /* aFilePath */, HashedFile& /*null*/) noexcept { }
		virtual void on(MaintananceStarted) noexcept { }
		virtual void on(MaintananceFinished) noexcept { }
		virtual void on(DirectoryHashed, const string& /*aPath*/, int /*aFilesHashed*/, int64_t /*aSizeHashed*/, time_t /*aHashDuration*/, int /*aHasherId*/) noexcept { }
		virtual void on(HasherFinished, int /*aPath*/, int /*aFilesHashed*/, int64_t /*aSizeHashed*/, time_t /*aHashDuration*/, int /*aHasherId*/) noexcept { }
	};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_SHAREMANAGERLISTENER_H)