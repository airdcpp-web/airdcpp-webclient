/*
 * Copyright (C) 2006-2011 Crise, crise<at>mail.berlios.de
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

/**
 * The main point of this is to allow easy hashing of strings for
 * say use with web scripts (php, etc).
 */

#ifndef HASHCALC_H
#define HASHCALC_H

#include <string>

#include <boost/scoped_array.hpp>

//#include "MD5Hash.h"
//#include "SHA1Hash.h"
#include "MerkleTree.h"

#include "File.h"
#include "Encoder.h"
#include "Util.h"

namespace dcpp {

using std::string;

template <class Hash>
class SimpleHasher
{
public:
	SimpleHasher(bool bBase32 = false) : base32(bBase32) { }
	~SimpleHasher() { }

	static string getHash(const string& input, bool bBase32) {
		SimpleHasher<Hash> hash(bBase32);
		if(Util::fileExists(input))
			return hash.fromFile(input);

		return hash.fromString(input);
	}

	string fromString(const string& input) {
		updateBlockSize(input.size());

		hash.update(input.c_str(), input.size());
		hash.finalize();
		return toString();
	}

	string fromFile(const string& path) {
		boost::scoped_array<char> buf(new char[512 * 1024]);

		try {
			File f(path, File::READ, File::OPEN);
			updateBlockSize(f.getSize());

			if(f.getSize() > 0) {
				size_t read = 512*1024;
				while((read = f.read(&buf[0], read)) > 0) {
					hash.update(&buf[0], read);
					read = 512*1024;
				}
			}

			f.close();
			hash.finalize();
		} catch(const FileException&) {
			return Util::emptyString;
		}

		return toString();
	}

	void update(const void* data, size_t len) { hash.update(data, len); }
	uint8_t* finalize() { return hash.finalize(); }
	uint8_t* getResult() { return hash.getResult(); }

	string toString() {
		 uint8_t* buf = getResult();

		string ret;
		if(!base32) {
			ret.resize(Hash::BYTES * 2, '\0');
			for(uint32_t i = 0; i < Hash::BYTES; ++i)
				sprintf(&ret[i*2], "%02x", (uint8_t)buf[i]);
		} else Encoder::toBase32(buf, Hash::BYTES, ret);

		return ret;
	}

private:
	Hash hash;
	bool base32;

	void updateBlockSize(int64_t /*size*/) { }
};

template<> inline
uint8_t* SimpleHasher<TigerTree>::getResult() { return hash.getRoot().data; }

template<> inline
void SimpleHasher<TigerTree>::updateBlockSize(int64_t size) { hash.setBlockSize(TigerTree::calcBlockSize(size, 1)); }

//#define MD5(x) SimpleHasher<MD5Hash>::getHash(x, false)
//#define SHA1(x) SimpleHasher<SHA1Hash>::getHash(x, true)
//#define TIGER(x) SimpleHasher<TigerHash>::getHash(x, true)
#define TTH(x) SimpleHasher<TigerTree>::getHash(x, true)

} // namespace dcpp

#endif // !defined(HASHCALC_H)
