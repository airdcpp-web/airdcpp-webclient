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

#ifndef DCPLUSPLUS_DCPP_SSLSOCKET_H
#define DCPLUSPLUS_DCPP_SSLSOCKET_H


#include <airdcpp/CryptoManager.h>
#include <airdcpp/Socket.h>

#include <airdcpp/SSL.h>

namespace dcpp {

using std::unique_ptr;
using std::string;

class SSLSocketException : public SocketException {
public:
#ifdef _DEBUG
	explicit SSLSocketException(const string& aError) noexcept : SocketException("SSLSocketException: " + aError) { }
#else //_DEBUG
	SSLSocketException(const string& aError) noexcept : SocketException(aError) { }
#endif // _DEBUG
	explicit SSLSocketException(int aError) noexcept : SocketException(aError) { }
	~SSLSocketException() noexcept final = default;
};

class SSLSocket : public Socket {
public:
	SSLSocket(CryptoManager::SSLContext context, bool allowUntrusted, const string& expKP);
	/** Creates an SSL socket without any verification */
	explicit SSLSocket(CryptoManager::SSLContext context);

	~SSLSocket() override { verifyData.reset(); }

	int read(void* aBuffer, size_t aBufLen) override;
	int write(const void* aBuffer, size_t aLen) override;
	std::pair<bool, bool> wait(uint64_t millis, bool checkRead, bool checkWrite) override;
	void shutdown() noexcept override;
	void close() noexcept override;

	bool isSecure() const noexcept override { return true; }
	bool isTrusted() const noexcept override;
	bool isKeyprintMatch() const noexcept override;
	string getEncryptionInfo() const noexcept override;
	ByteVector getKeyprint() const noexcept override;
	bool verifyKeyprint(const string& expKeyp, bool allowUntrusted) noexcept override;

	void connect(const AddressInfo& aAddr, const string& aPort, const string& aLocalPort = Util::emptyString) override;

	bool waitConnected(uint64_t millis) override;
	bool waitAccepted(uint64_t millis) override;

private:

	SSL_CTX* ctx;
	ssl::SSL ssl;

	unique_ptr<CryptoManager::SSLVerifyData> verifyData;	// application data used by CryptoManager::verify_callback(...)

	int checkSSL(int ret);
	bool waitWant(int ret, uint64_t millis);
	string hostname;
};

} // namespace dcpp

#endif // SSLSOCKET_H
