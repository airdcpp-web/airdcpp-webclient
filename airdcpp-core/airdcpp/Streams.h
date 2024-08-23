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

#ifndef DCPLUSPLUS_DCPP_STREAMS_H
#define DCPLUSPLUS_DCPP_STREAMS_H

#include <algorithm>

#include "typedefs.h"

#include "StreamBase.h"
#include "ResourceManager.h"

#include "SettingsManager.h"
#include "Exception.h"

namespace dcpp {

using std::min;

class MemoryInputStream : public InputStream {
public:
	MemoryInputStream(const uint8_t* src, size_t len) : size(len), buf(new uint8_t[len]) {
		memcpy(buf, src, len);
	}
	MemoryInputStream(const string& src) : size(src.size()), buf(new uint8_t[src.size()]) {
		memcpy(buf, src.data(), src.size());
	}

	~MemoryInputStream() {
		delete[] buf;
	}

	size_t read(void* tgt, size_t& len) override {
		len = min(len, size - pos);
		memcpy(tgt, buf+pos, len);
		pos += len;
		return len;
	}

	int64_t getSize() const noexcept { return static_cast<int64_t>(size); }

private:
	size_t pos = 0;
	size_t size;
	uint8_t* buf;
};

/** Count how many bytes have been read. */
template<bool managed>
class CountedInputStream : public InputStream {
public:
	CountedInputStream(InputStream* is) {
		s.reset(is);
	}

	virtual ~CountedInputStream() { 
		if(!managed) 
			s.release(); 
	}

	size_t read(void* buf, size_t& len) override {
		auto ret = s->read(buf, len);
		readBytes += len;
		return ret;
	}

	uint64_t getReadBytes() const { return readBytes; }
	InputStream* releaseRootStream() override {
		auto as = s.release();
		return as->releaseRootStream();
	}
private:
	unique_ptr<InputStream> s;
	uint64_t readBytes = 0;
};

template<bool managed>
class LimitedInputStream : public InputStream {
public:
	LimitedInputStream(InputStream* is, int64_t aMaxBytes) : maxBytes(aMaxBytes) {
		s.reset(is);
	}

	~LimitedInputStream() {
		if (!managed)
			s.release();
	}

	size_t read(void* buf, size_t& len) override {
		dcassert(maxBytes >= 0);
		len = (size_t)min(maxBytes, (int64_t)len);
		if(len == 0)
			return 0;
		size_t x = s->read(buf, len);
		maxBytes -= x;
		return x;
	}
	InputStream* releaseRootStream() override { 
		auto as = s.release();
		return as->releaseRootStream();
	}

	int64_t getSize() const noexcept {
		return s->getSize();
	}
private:
	unique_ptr<InputStream> s;
	int64_t maxBytes;
};

/** Limits the number of bytes that are requested to be written (not the number actually written!) */
template<bool managed>
class LimitedOutputStream : public OutputStream {
public:
	LimitedOutputStream(OutputStream* os, uint64_t aMaxBytes) : maxBytes(aMaxBytes) {
		s.reset(os);
	}
	virtual ~LimitedOutputStream() { 
		if (!managed)
			s.release();
	}

	virtual size_t write(const void* buf, size_t len) override {
		if(maxBytes < len) {
			throw FileException(STRING(TOO_MUCH_DATA));
		}
		maxBytes -= len;
		return s->write(buf, len);
	}
	
	virtual size_t flushBuffers(bool aForce) override {
		return s->flushBuffers(aForce);
	}
	
	virtual bool eof() override { return maxBytes == 0; }
	OutputStream* releaseRootStream() override { 
		auto as = s.release();
		return as->releaseRootStream();
	}
private:
	unique_ptr<OutputStream> s;
	uint64_t maxBytes;
};

template<bool managed>
class BufferedOutputStream : public OutputStream {
public:
	using OutputStream::write;

	BufferedOutputStream(OutputStream* aStream, size_t aBufSize = SETTING(BUFFER_SIZE) * 1024) : buf(aBufSize) { 
		s.reset(aStream);
	}

	~BufferedOutputStream() {
		try {
			// We must do this in order not to lose bytes when a download
			// is disconnected prematurely
			flushBuffers(false);
		} catch(const Exception&) { }

		if (!managed)
			s.release();
	}

	size_t flushBuffers(bool aForce) override {
		if(pos > 0)
			s->write(&buf[0], pos);
		pos = 0;
		s->flushBuffers(aForce);
		return 0;
	}

	size_t write(const void* wbuf, size_t len) override {
		uint8_t* b = (uint8_t*)wbuf;
		size_t l2 = len;
		size_t bufSize = buf.size();
		while(len > 0) {
			if(pos == 0 && len >= bufSize) {
				s->write(b, len);
				break;
			} else {
				size_t n = min(bufSize - pos, len);
				memcpy(&buf[pos], b, n);
				b += n;
				pos += n;
				len -= n;
				if(pos == bufSize) {
					s->write(&buf[0], bufSize);
					pos = 0;
				}
			}
		}
		return l2;
	}

	OutputStream* releaseRootStream() override {
		auto as = s.release();
		return as->releaseRootStream();
	}
private:
	unique_ptr<OutputStream> s;
	size_t pos = 0;
	ByteVector buf;
};

class StringOutputStream : public OutputStream {
public:
	StringOutputStream(string& out) : str(out) { }
	~StringOutputStream() { }
	using OutputStream::write;

	size_t flushBuffers(bool) override { return 0; }
	size_t write(const void* buf, size_t len) override {
		str.append((char*)buf, len);
		return len;
	}
private:
	string& str;
};

} // namespace dcpp

#endif // !defined(STREAMS_H)