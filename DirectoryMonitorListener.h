/* 
 * Copyright (C) 2013-2015 AirDC++ Project 
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


#ifndef DIRECTORY_MONITOR_LISTENER_H
#define DIRECTORY_MONITOR_LISTENER_H

#include "forward.h"
#include "noexcept.h"

#include <string>

namespace dcpp {

class DirectoryMonitorListener {
public:
	virtual ~DirectoryMonitorListener() { }
	template<int I>	struct X { enum { TYPE = I }; };

	typedef X<0> FileCreated;
	typedef X<1> FileModified;
	typedef X<2> FileRenamed;
	typedef X<3> FileDeleted;
	typedef X<4> Overflow;
	typedef X<5> DirectoryFailed;

	virtual void on(FileCreated, const std::string&) noexcept { }
	virtual void on(FileModified, const std::string&) noexcept { }
	virtual void on(FileRenamed, const std::string& /*old name*/, const std::string& /*newName*/) noexcept { }
	virtual void on(FileDeleted, const std::string&) noexcept { }
	virtual void on(Overflow, const std::string& /*rootPath*/) noexcept { }
	virtual void on(DirectoryFailed, const std::string& /*rootPath*/, const std::string& /*error*/) noexcept { }
};

} // namespace dcpp

#endif // !defined(DIRECTORY_MONITOR_LISTENER_H)