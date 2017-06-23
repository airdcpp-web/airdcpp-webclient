/*
 * Copyright (C) 2001-2017 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SSLSOCKET_H
#define DCPLUSPLUS_DCPP_SSLSOCKET_H


#include "CryptoManager.h"
#include "Socket.h"
#include "Singleton.h"

#include "SSL.h"

namespace dcpp {

using std::unique_ptr;
using std::string;

class SSLSocketException : public SocketException {
public:
#ifdef _DEBUG
	SSLSocketException(const string& aError) noexcept : SocketException("SSLSocketException: " + aError) { }
#else //_DEBUG
	SSLSocketException(const string& aError) noexcept : SocketException(aError) { }
#endif // _DEBUG
	SSLSocketException(int aError) noexcept : SocketException(aError) { }
	virtual ~SSLSocketException() noexcept { }
};

class SSLSocket : public Socket {
public:
	SSLSocket(CryptoManager::SSLContext context, bool allowUntrusted, const string& expKP);
	/** Creates an SSL socket without any verification */
	SSLSocket(CryptoManager::SSLContext context);

	virtual ~SSLSocket() { verifyData.reset(); }

	virtual int read(void* aBuffer, int aBufLen) override;
	virtual int write(const void* aBuffer, int aLen) override;
	virtual std::pair<bool, bool> wait(uint64_t millis, bool checkRead, bool checkWrite) override;
	virtual void shutdown() noexcept override;
	virtual void close() noexcept override;

	virtual bool isSecure() const noexcept override { return true; }
	virtual bool isTrusted() const noexcept override;
	virtual bool isKeyprintMatch() const noexcept override;
	virtual string getEncryptionInfo() const noexcept override;
	virtual ByteVector getKeyprint() const noexcept override;
	virtual bool verifyKeyprint(const string& expKeyp, bool allowUntrusted) noexcept override;

	virtual bool waitConnected(uint64_t millis) override;
	virtual bool waitAccepted(uint64_t millis) override;

private:

	SSL_CTX* ctx;
	ssl::SSL ssl;

	unique_ptr<CryptoManager::SSLVerifyData> verifyData;	// application data used by CryptoManager::verify_callback(...)

	int checkSSL(int ret);
	bool waitWant(int ret, uint64_t millis);
};

} // namespace dcpp

#endif // SSLSOCKET_H
