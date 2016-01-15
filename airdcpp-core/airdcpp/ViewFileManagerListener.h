/*
* Copyright (C) 2012-2016 AirDC++ Project
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

#ifndef FILEVIEWER_MANAGER_LISTENER_H
#define FILEVIEWER_MANAGER_LISTENER_H

#include "forward.h"

namespace dcpp {

	class ViewFileManagerListener {
	public:
		virtual ~ViewFileManagerListener() { }
		template<int I>	struct X { enum { TYPE = I }; };

		typedef X<0> FileAdded;
		typedef X<1> FileUpdated;
		typedef X<2> FileClosed;
		typedef X<3> FileFinished;
		typedef X<4> FileRead;

		virtual void on(FileAdded, const ViewFilePtr&) noexcept { }
		virtual void on(FileUpdated, const ViewFilePtr&) noexcept { }
		virtual void on(FileClosed, const ViewFilePtr&) noexcept { }
		virtual void on(FileFinished, const ViewFilePtr&) noexcept { }
		virtual void on(FileRead, const ViewFilePtr&) noexcept { }
	};

} // namespace dcpp

#endif // !defined(FILEVIEWER_MANAGER_LISTENER_H)