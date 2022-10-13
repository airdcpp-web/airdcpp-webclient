#pragma once
/*
 * Copyright (C) 2001-2022 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_STREAMBASE_H
#define DCPLUSPLUS_DCPP_STREAMBASE_H

#include <boost/noncopyable.hpp>

#include "typedefs.h"

namespace dcpp {

/**
	* A simple output stream. Intended to be used for nesting streams one inside the other.
	*/
class OutputStream : boost::noncopyable {
public:
	OutputStream() { }
	virtual ~OutputStream() { }

	/**
		* @return The actual number of bytes written. len bytes will always be
		*         consumed, but fewer or more bytes may actually be written,
		*         for example if the stream is being compressed.
		*/
	virtual size_t write(const void* buf, size_t len) = 0;
	/**
		* This must be called before destroying the object to make sure all data
		* is properly written (we don't want destructors that throw exceptions
		* and the last flush might actually throw). Note that some implementations
		* might not need it...
		*
		* If aForce is false, only data that is subject to be deleted otherwise will be flushed.
		* This applies especially for files for which the operating system should generally decide
		* when the buffered data is flushed on disk.
		*/
	virtual size_t flushBuffers(bool aForce) = 0;

	/* This only works for file streams */
	virtual void setPos(int64_t /*pos*/) noexcept { }

	/**
		* @return True if stream is at expected end
		*/
	virtual bool eof() { return false; }

	size_t write(std::string&& str) { return write(str.c_str(), str.size()); }
	size_t write(const std::string& str) { return write(str.c_str(), str.size()); }
	virtual OutputStream* releaseRootStream() { return this; }
};

class InputStream : boost::noncopyable {
public:
	InputStream() { }
	virtual ~InputStream() { }
	/**
		* Call this function until it returns 0 to get all bytes.
		* @return The number of bytes read. len reflects the number of bytes
		*		   actually read from the stream source in this call.
		*/
	virtual size_t read(void* buf, size_t& len) = 0;

	/* This only works for file streams */
	virtual void setPos(int64_t /*pos*/) noexcept { }
	virtual InputStream* releaseRootStream() { return this; }
};

class IOStream : public InputStream, public OutputStream {
};


} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_STREAMBASE_H)