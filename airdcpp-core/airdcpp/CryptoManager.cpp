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

#include "stdinc.h"
#include <airdcpp/CryptoManager.h>

#include <boost/scoped_array.hpp>

#include <airdcpp/ClientManager.h>
#include <airdcpp/Encoder.h>
#include <airdcpp/File.h>
#include <airdcpp/LogManager.h>
#include <airdcpp/SystemUtil.h>
#include <airdcpp/version.h>

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>


namespace dcpp {

int CryptoManager::idxVerifyData = 0;
char CryptoManager::idxVerifyDataName[] = "AirDC.VerifyData";
CryptoManager::SSLVerifyData CryptoManager::trustedKeyprint = { false, "trusted_keyp" };


CryptoManager::CryptoManager()
:
	lock("EXTENDEDPROTOCOLABCABCABCABCABCABC"),
	pk("DCPLUSPLUS" + VERSIONSTRING)
{
	clientContext.reset(SSL_CTX_new(SSLv23_client_method()));
	serverContext.reset(SSL_CTX_new(SSLv23_server_method()));

	idxVerifyData = SSL_get_ex_new_index(0, idxVerifyDataName, NULL, NULL, NULL);

	if(clientContext && serverContext) {
		// Check that openssl rng has been seeded with enough data
		sslRandCheck();

		SSL_CTX_set_options(clientContext, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION);
		SSL_CTX_set_options(serverContext, SSL_OP_SINGLE_DH_USE | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION);

		setContextOptions(clientContext, false);
		setContextOptions(serverContext, true);

		SSL_CTX_set_verify(clientContext, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, verify_callback);
		SSL_CTX_set_verify(serverContext, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, verify_callback);
	}
}

void CryptoManager::setContextOptions(SSL_CTX* aCtx, bool aServer) noexcept {
	// Only require TLS 1.2 => for now, other requirements need to be tested first for compatibility issues
	SSL_CTX_set_min_proto_version(aCtx, TLS1_2_VERSION);
	// SSL_CTX_set_security_level(aCtx, 2);

	// From DC++
	// Connections with an unsupported cipher would just time out without any error, so don't use these yet

	// const char ciphersuitesTls12[] = "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA256";
	// SSL_CTX_set_cipher_list(aCtx, ciphersuitesTls12);

	const char ciphersuitesTls12[] =
		"ECDHE-ECDSA-AES128-GCM-SHA256:"
		"ECDHE-RSA-AES128-GCM-SHA256:"
		"ECDHE-ECDSA-AES128-SHA256:"
		"ECDHE-RSA-AES128-SHA256:"
		"ECDHE-ECDSA-AES128-SHA:"
		"ECDHE-RSA-AES128-SHA:"
		"DHE-RSA-AES128-SHA:"
		"AES128-SHA:"
		"ECDHE-ECDSA-AES256-GCM-SHA384:"
		"ECDHE-RSA-AES256-GCM-SHA384:"
		"ECDHE-ECDSA-AES256-SHA384:"
		"ECDHE-RSA-AES256-SHA384:"
		"ECDHE-ECDSA-AES256-SHA:"
		"ECDHE-RSA-AES256-SHA:"
		"AES256-GCM-SHA384:"
		"AES256-SHA256:"
		"AES256-SHA";

	SSL_CTX_set_cipher_list(aCtx, ciphersuitesTls12);
	SSL_CTX_set1_curves_list(aCtx, "P-256");

	if (aServer) {
#if OPENSSL_VERSION_NUMBER >= 0x1010100fL
		// TLS 1.3 ciphers
		const char ciphersuitesTls13[] =
			"TLS_AES_128_GCM_SHA256:"
			"TLS_AES_256_GCM_SHA384:"
			"TLS_CHACHA20_POLY1305_SHA256";

		SSL_CTX_set_ciphersuites(aCtx, ciphersuitesTls13);

		// Groups
		const int g_kexch_groups[] = {
			NID_X9_62_prime256v1,   /* secp256r1 */
			NID_secp384r1,          /* secp384r1 */
			NID_secp521r1,          /* secp521r1 */
			NID_X25519,             /* x25519 */
			NID_X448                /* x448 */
		};

		auto ret = SSL_CTX_set1_groups(aCtx, g_kexch_groups, sizeof(g_kexch_groups) / sizeof(g_kexch_groups[0]));
		if (!ret) {
			dcassert(0);
		}
#else
		EC_KEY* tmp_ecdh;
		if ((tmp_ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1)) != NULL) {
			SSL_CTX_set_options(aCtx, SSL_OP_SINGLE_ECDH_USE);

#if OPENSSL_VERSION_NUMBER >= 0x1010100fL
			SSL_set1_groups(aCtx, tmp_ecdh);
#else
			SSL_CTX_set_tmp_ecdh(aCtx, tmp_ecdh);
#endif

			EC_KEY_free(tmp_ecdh);
		}
#endif
	}
}

CryptoManager::~CryptoManager() {
	clientContext.reset();
	serverContext.reset();
}

string CryptoManager::keyprintToString(const ByteVector& aKP) noexcept {
	return "SHA256/" + Encoder::toBase32(&aKP[0], aKP.size());
}

bool CryptoManager::TLSOk() const noexcept{
	return SETTING(TLS_MODE) > 0 && certsLoaded && !keyprint.empty();
}

void CryptoManager::generateCertificate() {
	// Generate certificate using OpenSSL
	if (SETTING(TLS_PRIVATE_KEY_FILE).empty()) {
		throw CryptoException("No private key file chosen");
	}
	if (SETTING(TLS_CERTIFICATE_FILE).empty()) {
		throw CryptoException("No certificate file chosen");
	}

	int days = 360;
	int keylength = 2048;

	ssl::BIGNUM bn(BN_new());

#if OPENSSL_VERSION_NUMBER < 0x30000000L
	ssl::RSA rsa(RSA_new());
	ssl::EVP_PKEY pkey(EVP_PKEY_new());
#else
	ssl::EVP_PKEY pkey(EVP_RSA_gen(keylength));
#endif

	ssl::X509_NAME nm(X509_NAME_new());
	ssl::X509 x509ss(X509_new());
	ssl::ASN1_INTEGER serial(ASN1_INTEGER_new());

	if (!bn || !pkey || !nm || !x509ss || !serial) {
		throw CryptoException("Error generating certificate");
	}

#define CHECK(n) if(!(n)) { throw CryptoException(#n); }
	CHECK((BN_set_word(bn, RSA_F4)))

#if OPENSSL_VERSION_NUMBER < 0x30000000L
	// Generate key pair
	CHECK((RSA_generate_key_ex(rsa, keylength, bn, NULL)))
	CHECK((EVP_PKEY_set1_RSA(pkey, rsa)))
#endif

	ByteVector fieldBytes;

	// Add common name (use cid)
	string name = ClientManager::getInstance()->getMyCID().toBase32().c_str();
	fieldBytes.assign(name.begin(), name.end());
	CHECK((X509_NAME_add_entry_by_NID(nm, NID_commonName, MBSTRING_ASC, &fieldBytes[0], fieldBytes.size(), -1, 0)))

	// Add an organisation
	string org = "DCPlusPlus (OSS/SelfSigned)";
	fieldBytes.assign(org.begin(), org.end());
	CHECK((X509_NAME_add_entry_by_NID(nm, NID_organizationName, MBSTRING_ASC, &fieldBytes[0], fieldBytes.size(), -1, 0)))

	// Generate unique serial
	CHECK((BN_rand(bn, 64, 0, 0)))
	CHECK((BN_to_ASN1_INTEGER(bn, serial)))
		
	// Prepare self-signed cert
	CHECK((X509_set_version(x509ss, 0x02))) // This is actually V3
	CHECK((X509_set_serialNumber(x509ss, serial)))
	CHECK((X509_set_issuer_name(x509ss, nm)))
	CHECK((X509_set_subject_name(x509ss, nm)))
	CHECK((X509_gmtime_adj(X509_getm_notBefore(x509ss), 0)))
	CHECK((X509_gmtime_adj(X509_getm_notAfter(x509ss), (long)60*60*24*days)))
	CHECK((X509_set_pubkey(x509ss, pkey)))

	// Sign using own private key
	CHECK((X509_sign(x509ss, pkey, EVP_sha256())))

#undef CHECK
		// Write the key and cert
	{
		File::ensureDirectory(SETTING(TLS_PRIVATE_KEY_FILE));
		FILE* f = dcpp_fopen(SETTING(TLS_PRIVATE_KEY_FILE).c_str(), "w");
		if (!f) {
			throw CryptoException(SystemUtil::formatLastError() + " (" + SETTING(TLS_PRIVATE_KEY_FILE) + ")");
		}

		PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL);
		fclose(f);
	}
	{
		File::ensureDirectory(SETTING(TLS_CERTIFICATE_FILE));
		FILE* f = dcpp_fopen(SETTING(TLS_CERTIFICATE_FILE).c_str(), "w");
		if (!f) {
			File::deleteFile(SETTING(TLS_PRIVATE_KEY_FILE));
			throw CryptoException(SystemUtil::formatLastError() + " (" + SETTING(TLS_CERTIFICATE_FILE) + ")");
		}
		PEM_write_X509(f, x509ss);
		fclose(f);
	}
}

void CryptoManager::sslRandCheck() noexcept {
	if (!RAND_status()) {
#ifdef _WIN32
		RAND_poll();
#endif
	}
}

int CryptoManager::getKeyLength(TLSTmpKeys key) noexcept {
	switch (key) {
	case KEY_DH_2048:
	case KEY_RSA_2048:
		return 2048;
	case KEY_DH_4096:
		return 4096;
	default:
		dcassert(0); return 0;
	}
}

void CryptoManager::log(const string& aMsg, LogMessage::Severity aSeverity) noexcept {
	LogManager::getInstance()->message(aMsg, aSeverity, STRING(ENCRYPTION));
}

void CryptoManager::loadCertificates() noexcept{
	setCertPaths();
	if (!clientContext || !serverContext)
		return;

	keyprint.clear();
	certsLoaded = false;

	const string& cert = SETTING(TLS_CERTIFICATE_FILE);
	const string& key = SETTING(TLS_PRIVATE_KEY_FILE);

	if (cert.empty() || key.empty()) {
		log(STRING(NO_CERTIFICATE_FILE_SET), LogMessage::SEV_WARNING);
		return;
	}

	if (File::getSize(cert) == -1 || File::getSize(key) == -1 || !checkCertificate(90)) {
		// Try to generate them...
		try {
			generateCertificate();
			log(STRING(CERTIFICATE_GENERATED), LogMessage::SEV_INFO);
		}
		catch (const CryptoException& e) {
			log(STRING(CERTIFICATE_GENERATION_FAILED) + " " + e.getError(), LogMessage::SEV_ERROR);
		}
	}

	if (!ssl::SSL_CTX_use_certificate_file(serverContext, cert.c_str(), SSL_FILETYPE_PEM)) {
		log(STRING(FAILED_TO_LOAD_CERTIFICATE), LogMessage::SEV_WARNING);
		return;
	}
	if (!ssl::SSL_CTX_use_certificate_file(clientContext, cert.c_str(), SSL_FILETYPE_PEM)) {
		log(STRING(FAILED_TO_LOAD_CERTIFICATE), LogMessage::SEV_WARNING);
		return;
	}

	if (!ssl::SSL_CTX_use_PrivateKey_file(serverContext, key.c_str(), SSL_FILETYPE_PEM)) {
		log(STRING(FAILED_TO_LOAD_PRIVATE_KEY), LogMessage::SEV_WARNING);
		return;
	}
	if (!ssl::SSL_CTX_use_PrivateKey_file(clientContext, key.c_str(), SSL_FILETYPE_PEM)) {
		log(STRING(FAILED_TO_LOAD_PRIVATE_KEY), LogMessage::SEV_WARNING);
		return;
	}

	auto certs = File::findFiles(SETTING(TLS_TRUSTED_CERTIFICATES_PATH), "*.pem", File::TYPE_FILE);
	auto certs2 = File::findFiles(SETTING(TLS_TRUSTED_CERTIFICATES_PATH), "*.crt", File::TYPE_FILE);
	certs.insert(certs.end(), certs2.begin(), certs2.end());

	for (const auto& i : certs) {
		if (
			SSL_CTX_load_verify_locations(clientContext, i.c_str(), NULL) != SSL_SUCCESS ||
			SSL_CTX_load_verify_locations(serverContext, i.c_str(), NULL) != SSL_SUCCESS
		) {
			log("Failed to load trusted certificate from " + Util::addBrackets(i), LogMessage::SEV_WARNING);
		}
	}

	loadKeyprint(cert);

	certsLoaded = true;
}

bool CryptoManager::checkCertificate(int minValidityDays) noexcept{
	auto x509 = ssl::getX509(SETTING(TLS_CERTIFICATE_FILE).c_str());
	if (!x509) {
		return false;
	}

	const ASN1_INTEGER* sn = X509_get_serialNumber(x509);
	if (!sn || !ASN1_INTEGER_get(sn)) {
		return false;
	}

	X509_NAME* name = X509_get_subject_name(x509);
	if (!name) {
		return false;
	}

	string cn = getNameEntryByNID(name, NID_commonName);
	if (cn != ClientManager::getInstance()->getMyCID().toBase32()) {
		return false;
	}

	const ASN1_TIME* t = X509_get0_notAfter(x509);
	if (t) {
		time_t minValid = GET_TIME() + 60 * 60 * 24 * minValidityDays;
		if (X509_cmp_time(t, &minValid) < 0) {
			return false;
		}
	}
	return true;
}

const ByteVector& CryptoManager::getKeyprint() const noexcept{
	return keyprint;
}

void CryptoManager::loadKeyprint(const string& /*file*/) noexcept{
	auto x509 = ssl::getX509(SETTING(TLS_CERTIFICATE_FILE).c_str());
	if (x509) {
		keyprint = ssl::X509_digest(x509, EVP_sha256());
	}
}

SSL_CTX* CryptoManager::getSSLContext(SSLContext wanted) {
	switch(wanted) {
		case SSL_CLIENT: return clientContext;
		case SSL_SERVER: return serverContext;
		default: return NULL;
	}
}

void CryptoManager::setCertPaths() {
	if (!SETTING(USE_DEFAULT_CERT_PATHS))
		return;

	auto privPath = AppUtil::getPath(AppUtil::PATH_USER_LOCAL) + "Certificates" PATH_SEPARATOR_STR "client.key";
	auto certPath = AppUtil::getPath(AppUtil::PATH_USER_LOCAL) + "Certificates" PATH_SEPARATOR_STR "client.crt";

	SettingsManager::getInstance()->set(SettingsManager::TLS_CERTIFICATE_FILE, certPath);
	SettingsManager::getInstance()->set(SettingsManager::TLS_PRIVATE_KEY_FILE, privPath);
}

int CryptoManager::verify_callback(int preverify_ok, X509_STORE_CTX *ctx) {
	int err = X509_STORE_CTX_get_error(ctx);
	auto ssl = (SSL*)X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
	auto verifyData = (SSLVerifyData*)SSL_get_ex_data(ssl, CryptoManager::idxVerifyData);

	// verifyData is unset only when KeyPrint has been pinned and we are not skipping errors due to incomplete chains
	// we can fail here f.ex. if the certificate has expired but is still pinned with KeyPrint
	if (!verifyData || SSL_get_shutdown(ssl) != 0)
		return preverify_ok;

	bool allowUntrusted = verifyData->first;
	string keyp = verifyData->second;
	string error = Util::emptyString;

	if (!keyp.empty()) {
		X509* cert = X509_STORE_CTX_get_current_cert(ctx);
		if (!cert)
			return 0;

		if (keyp.compare(0, 12, "trusted_keyp") == 0) {
			// Possible follow up errors, after verification of a partial chain
			if (err == X509_V_ERR_CERT_UNTRUSTED || err == X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE) {
				X509_STORE_CTX_set_error(ctx, X509_V_OK);
				return 1;
			}
			return preverify_ok;
		}
		else if (keyp.compare(0, 7, "SHA256/") != 0)
			return allowUntrusted ? 1 : 0;

		ByteVector kp = ssl::X509_digest(cert, EVP_sha256());
		string expected_keyp = "SHA256/" + Encoder::toBase32(&kp[0], kp.size());

		// Do a full string comparison to avoid potential false positives caused by invalid inputs
		if (keyp.compare(expected_keyp) == 0) {
			// KeyPrint validated, we can get rid of it (to avoid unnecessary passes)
			SSL_set_ex_data(ssl, CryptoManager::idxVerifyData, NULL);

			if (err != X509_V_OK) {
				// This is the right way to get the certificate store, although it is rather roundabout
				auto ssl_ctx = SSL_get_SSL_CTX(ssl);

#if OPENSSL_VERSION_NUMBER >= 0x1000200fL
				X509_STORE* store = X509_STORE_CTX_get0_store(ctx);
#else
				X509_STORE* store = ctx->ctx;
#endif
				
				// Hide the potential library error about trying to add a dupe
				ERR_set_mark();
				if (X509_STORE_add_cert(store, cert)) {
					// We are fine, but can't leave mark on the stack
					ERR_pop_to_mark();

					// After the store has been updated, perform a recheck of the current certificate, the existing context can be in mid recursion, so hands off!
					X509_STORE_CTX* vrfy_ctx = X509_STORE_CTX_new();

					if (vrfy_ctx && X509_STORE_CTX_init(vrfy_ctx, store, cert, NULL)) {
						X509_STORE_CTX_set_ex_data(vrfy_ctx, SSL_get_ex_data_X509_STORE_CTX_idx(), ssl);
						X509_STORE_CTX_set_verify_cb(vrfy_ctx, SSL_CTX_get_verify_callback(ssl_ctx));

						int verify_result = X509_verify_cert(vrfy_ctx);
						err = X509_STORE_CTX_get_error(vrfy_ctx);
						if (verify_result <= 0 && err == X509_V_OK) {
							// Watch out for weird library errors that might not set the context error code
							err = X509_V_ERR_UNSPECIFIED;
						}
					}
					// Set the current cert error to the context being verified.
					X509_STORE_CTX_set_error(ctx, err); 
					if (vrfy_ctx) X509_STORE_CTX_free(vrfy_ctx);
				}
				else ERR_pop_to_mark();

				// KeyPrint was not root certificate or we don't have the issuer certificate, the best we can do is trust the pinned KeyPrint
				if (err == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN || err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY || err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT
					|| err == X509_V_ERR_CERT_NOT_YET_VALID || err == X509_V_ERR_CERT_HAS_EXPIRED) { // Ignore certificate validity period, when KeyPrint is trusted
					X509_STORE_CTX_set_error(ctx, X509_V_OK);
					// Set this to allow ignoring any follow up errors caused by the incomplete chain
					SSL_set_ex_data(ssl, CryptoManager::idxVerifyData, &CryptoManager::trustedKeyprint);
					return 1;
				}
			}

			if (err == X509_V_OK)
				return 1;

			preverify_ok = 0;
		} else {
			if (X509_STORE_CTX_get_error_depth(ctx) > 0)
				return 1;

			//KeyPrint was a mismatch, we're not happy with this
			preverify_ok = 0;
			err = X509_V_ERR_APPLICATION_VERIFICATION;
			error = STRING(KEYPRINT_MISMATCH);
			X509_STORE_CTX_set_error(ctx, err);
		}
	}
	// We let untrusted certificates through unconditionally, when allowed, but we like to complain
	if (!preverify_ok && (!allowUntrusted || err != X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT)) {
		if (error.empty()) {
			error = err != X509_V_ERR_UNSPECIFIED ? X509_verify_cert_error_string(err) : "unspecified certificate verification error";
		}

		auto fullError = formatError(ctx, error);
		if (!fullError.empty() && (!keyp.empty() || !allowUntrusted)) {
			log(fullError, LogMessage::SEV_ERROR);
		}
	}

	// Don't allow untrusted connections on keyprint mismatch
	if (allowUntrusted && err != X509_V_ERR_APPLICATION_VERIFICATION)
		return 1;

	return preverify_ok;

}

string CryptoManager::formatError(const X509_STORE_CTX *ctx, const string& message) {
	X509* cert = NULL;
	if ((cert = X509_STORE_CTX_get_current_cert(ctx)) != NULL) {
		X509_NAME* subject = X509_get_subject_name(cert);
		string tmp, line;

		tmp = getNameEntryByNID(subject, NID_commonName);
		if (!tmp.empty()) {
			CID certCID(tmp);
			if (tmp.length() == 39)
				tmp = Util::listToString(ClientManager::getInstance()->getNicks(certCID, true));
			line += (!line.empty() ? ", " : "") + tmp;
		}

		tmp = getNameEntryByNID(subject, NID_organizationName);
		if (!tmp.empty())
			line += (!line.empty() ? ", " : "") + tmp;

		ByteVector kp = ssl::X509_digest(cert, EVP_sha256());
		string keyp = keyprintToString(kp);

		return STRING_F(VERIFY_CERT_FAILED, line % message % keyp);
	}

	return Util::emptyString;
}

string CryptoManager::getNameEntryByNID(X509_NAME* name, int nid) noexcept{
	int i = X509_NAME_get_index_by_NID(name, nid, -1);
	if (i == -1) {
		return Util::emptyString;
	}

	const X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, i);
	const ASN1_STRING* str = X509_NAME_ENTRY_get_data(entry);
	if (!str) {
		return Util::emptyString;
	}

	unsigned char* buf = 0;
	i = ASN1_STRING_to_UTF8(&buf, str);
	if (i < 0) {
		return Util::emptyString;
	}

	std::string out((char*)buf, i);
	OPENSSL_free(buf);

	return out;
}

string CryptoManager::keySubst(const uint8_t* aKey, size_t len, size_t n) noexcept {
	boost::scoped_array<uint8_t> temp(new uint8_t[len + n * 10]);

	size_t j=0;

	for(size_t i = 0; i<len; i++) {
		if(isExtra(aKey[i])) {
			temp[j++] = '/'; temp[j++] = '%'; temp[j++] = 'D';
			temp[j++] = 'C'; temp[j++] = 'N';
			switch(aKey[i]) {
			case 0: temp[j++] = '0'; temp[j++] = '0'; temp[j++] = '0'; break;
			case 5: temp[j++] = '0'; temp[j++] = '0'; temp[j++] = '5'; break;
			case 36: temp[j++] = '0'; temp[j++] = '3'; temp[j++] = '6'; break;
			case 96: temp[j++] = '0'; temp[j++] = '9'; temp[j++] = '6'; break;
			case 124: temp[j++] = '1'; temp[j++] = '2'; temp[j++] = '4'; break;
			case 126: temp[j++] = '1'; temp[j++] = '2'; temp[j++] = '6'; break;
			}
			temp[j++] = '%'; temp[j++] = '/';
		} else {
			temp[j++] = aKey[i];
		}
	}
	return string((const char*)&temp[0], j);
}

string CryptoManager::makeKey(const string& aLock) {
	if(aLock.size() < 3)
		return Util::emptyString;

	boost::scoped_array<uint8_t> temp(new uint8_t[aLock.length()]);
	uint8_t v1;
	size_t extra=0;

	v1 = (uint8_t)(aLock[0]^5);
	v1 = (uint8_t)(((v1 >> 4) | (v1 << 4)) & 0xff);
	temp[0] = v1;

	string::size_type i;

	for(i = 1; i<aLock.length(); i++) {
		v1 = (uint8_t)(aLock[i]^aLock[i-1]);
		v1 = (uint8_t)(((v1 >> 4) | (v1 << 4))&0xff);
		temp[i] = v1;
		if(isExtra(temp[i]))
			extra++;
	}

	temp[0] = (uint8_t)(temp[0] ^ temp[aLock.length()-1]);

	if(isExtra(temp[0])) {
		extra++;
	}

	return keySubst(&temp[0], aLock.length(), extra);
}

} // namespace dcpp
