#include "stdinc.h"
#include "SSL.h"

#include "Util.h"

#ifdef HEADER_OPENSSLV_H

namespace dcpp {
namespace ssl {

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
