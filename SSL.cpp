/*
 * Copyright (C) 2001-2014 Jacek Sieka, arnetheduck on gmail point com
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

#include "stdinc.h"
#include "SSL.h"
#include "File.h"

#include <vector>

#ifdef HEADER_OPENSSLV_H

namespace dcpp {
namespace ssl {

using std::vector;

bool SSL_CTX_use_certificate_file(::SSL_CTX* ctx, const char* file, int /*type*/) {
	auto x509 = getX509(file);
	if(!x509) {
		return false;
	}
	return SSL_CTX_use_certificate(ctx, x509) == SSL_SUCCESS;
}

bool SSL_CTX_use_PrivateKey_file(::SSL_CTX* ctx, const char* file, int /*type*/) {
	FILE* f = dcpp_fopen(file, "r");
	if(!f) {
		return false;
	}

	::EVP_PKEY* tmpKey = nullptr;
	PEM_read_PrivateKey(f, &tmpKey, nullptr, nullptr);
	fclose(f);

	if(!tmpKey) {
		return false;
	}
	EVP_PKEY key(tmpKey);

	return SSL_CTX_use_PrivateKey(ctx, key) == SSL_SUCCESS;
}

X509 getX509(const char* file) {
	::X509* ret = nullptr;
	FILE* f = dcpp_fopen(file, "r");
	if(f) {
		PEM_read_X509(f, &ret, nullptr, nullptr);
		fclose(f);
	}
	return X509(ret);
}

vector<uint8_t> X509_digest(::X509* x509, const ::EVP_MD* md) {
	unsigned int n;
	unsigned char buf[EVP_MAX_MD_SIZE];

	if (!X509_digest(x509, md, buf, &n)) {
		return vector<uint8_t>(); // Throw instead?
	}

	return vector<uint8_t>(buf, buf+n);
}

}
}

#endif
