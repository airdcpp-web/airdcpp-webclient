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

#ifndef DCPLUSPLUS_DCPP_CRYPTO_MANAGER_H
#define DCPLUSPLUS_DCPP_CRYPTO_MANAGER_H

#include "Message.h"
#include "Singleton.h"
#include "SSL.h"

//This is for earlier OpenSSL versions that don't have this error code yet..
#ifndef X509_V_ERR_UNSPECIFIED
#define X509_V_ERR_UNSPECIFIED 1
#endif

namespace dcpp {

class CryptoManager : public Singleton<CryptoManager>
{
public:
	using SSLVerifyData = pair<bool, string>;

	enum TLSTmpKeys {
		KEY_FIRST = 0,
		KEY_DH_2048 = KEY_FIRST,
		KEY_DH_4096,
		KEY_RSA_2048,
		KEY_LAST
	};

	enum SSLContext {
		SSL_CLIENT,
		SSL_SERVER
	};

	static string makeKey(const string& aLock);
	const string& getLock() const noexcept { return lock; }
	const string& getPk() const noexcept { return pk; }
	bool isExtended(const string& aLock) const noexcept { return strncmp(aLock.c_str(), "EXTENDEDPROTOCOL", 16) == 0; }

	SSL_CTX* getSSLContext(SSLContext wanted);

	void loadCertificates() noexcept;
	void generateCertificate();
	static bool checkCertificate(int minValidityDays) noexcept;
	const ByteVector& getKeyprint() const noexcept;

	bool TLSOk() const noexcept;

	static int verify_callback(int preverify_ok, X509_STORE_CTX *ctx);

	static void setCertPaths();

	static int idxVerifyData;

	// Options that can also be shared with external contexts
	static void setContextOptions(SSL_CTX* aSSL, bool aServer);
	static string keyprintToString(const ByteVector& aKP) noexcept;
private:
	friend class Singleton<CryptoManager>;

	CryptoManager();
	~CryptoManager() override;

	ssl::SSL_CTX clientContext;
	ssl::SSL_CTX clientVerContext;
	ssl::SSL_CTX serverContext;
	ssl::SSL_CTX serverVerContext;

	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;
	static void sslRandCheck() noexcept;

	static int getKeyLength(TLSTmpKeys key) noexcept;

	bool certsLoaded = false;

	static char idxVerifyDataName[];
	static SSLVerifyData trustedKeyprint;

	ByteVector keyprint;
	const string lock;
	const string pk;

	static string keySubst(const uint8_t* aKey, size_t len, size_t n) noexcept;
	static constexpr bool isExtra(uint8_t b) noexcept {
		return (b == 0 || b==5 || b==124 || b==96 || b==126 || b==36);
	}

	static string formatError(const X509_STORE_CTX *ctx, const string& message);
	static string getNameEntryByNID(X509_NAME* name, int nid) noexcept;

	void loadKeyprint(const string& file) noexcept;

};

} // namespace dcpp

#endif // !defined(CRYPTO_MANAGER_H)
