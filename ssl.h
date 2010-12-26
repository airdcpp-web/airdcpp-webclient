#ifndef SSL_H_
#define SSL_H_

// enable this 2 lines for compiling with GnuTLS
//#define ssize_t size_t // this is needed for MSVC, maybe other compilers doesn't need it
//#include <gnutls/openssl.h>

// enable this for compiling with OpenSSL
#include <openssl/ssl.h>

namespace dcpp {

namespace ssl {

#ifdef YASSL_VERSION
using namespace yaSSL;

#define SSL_is_init_finished(a)		(a->getStates().getHandShake() == handShakeReady)
#elif !defined HEADER_OPENSSLV_H
#define SSL_is_init_finished(a)		finished
#endif

template<typename T, void (__cdecl *Release)(T*)>
class scoped_handle {
public:
	explicit scoped_handle(T* t_ = 0) : t(t_) { }
	~scoped_handle() { if(t) Release(t); }
	
	operator T*() { return t; }
	operator const T*() const { return t; }
	
	T* operator->() { return t; }
	const T* operator->() const { return t; }
	
	void reset(T* t_ = NULL) { if(t) Release(t); t = t_; }
private:
	scoped_handle(const scoped_handle<T, Release>&);
	scoped_handle<T, Release>& operator=(const scoped_handle<T, Release>&);
	
	T* t;
};

typedef scoped_handle<SSL, SSL_free> SSL;
typedef scoped_handle<SSL_CTX, SSL_CTX_free> SSL_CTX;

#ifdef HEADER_OPENSSLV_H
typedef scoped_handle<DH, DH_free> DH;
typedef scoped_handle<BIGNUM, BN_free> BIGNUM;

typedef scoped_handle<DSA, DSA_free> DSA;
typedef scoped_handle<EVP_PKEY, EVP_PKEY_free> EVP_PKEY;
typedef scoped_handle<RSA, RSA_free> RSA;
typedef scoped_handle<X509, X509_free> X509;
typedef scoped_handle<X509_NAME, X509_NAME_free> X509_NAME;

std::string X509_digest(::X509* x509, const ::EVP_MD* md);
#endif
}
}

#endif /*SSL_H_*/
