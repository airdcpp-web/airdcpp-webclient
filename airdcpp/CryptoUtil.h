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

#ifndef DCPLUSPLUS_DCPP_CRYPTO_UTIL_H
#define DCPLUSPLUS_DCPP_CRYPTO_UTIL_H

#include <airdcpp/typedefs.h>

namespace dcpp {

class CryptoUtil
{
public:
	static optional<ByteVector> calculateSha1(const string& aData) noexcept;

	using SignatureData = pair<ByteVector, ByteVector>;
	static optional<SignatureData> signDigest(const ByteVector& aDigest, const string& aPrivateKeyFilePath);
	static bool verifyDigest(const ByteVector& aDigest, const ByteVector& aSignature, const uint8_t* aPublicKey, size_t aKeySize) noexcept;

	// SUDP
	static string encryptSUDP(const uint8_t* aKey, const string& aCmd);
	static bool decryptSUDP(const uint8_t* aKey, const ByteVector& aData, size_t aDataLen, string& result_);

	using SUDPKey = std::unique_ptr<uint8_t[]>;
	static SUDPKey generateSUDPKey();

#ifdef _DEBUG
	static void testSUDP();
#endif
};

} // namespace dcpp

#endif // !defined(CRYPTO_MANAGER_H)
