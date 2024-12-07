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
#include <airdcpp/util/CryptoUtil.h>

#include <airdcpp/core/header/debug.h>

#include <airdcpp/hash/value/Encoder.h>
#include <airdcpp/core/io/File.h>
#include <airdcpp/core/classes/ScopedFunctor.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <boost/scoped_array.hpp>


namespace dcpp {

optional<ByteVector> CryptoUtil::calculateSha1(const string& aData) noexcept {
	ByteVector ret(SHA_DIGEST_LENGTH);

	EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
	const EVP_MD* md = EVP_get_digestbyname("SHA1");
	ScopedFunctor([mdctx] { EVP_MD_CTX_free(mdctx); });

	auto len = static_cast<unsigned int>(aData.size());
	auto res = EVP_DigestInit_ex(mdctx, md, NULL);
	if (res != 1)
		return nullopt;
	res = EVP_DigestUpdate(mdctx, aData.c_str(), len);
	if (res != 1)
		return nullopt;
	res = EVP_DigestFinal_ex(mdctx, ret.data(), &len);
	if (res != 1)
		return nullopt;

	return ret;
}

bool CryptoUtil::verifyDigest(const ByteVector& aDigest, const ByteVector& aSignature, const uint8_t* aPublicKey, size_t aKeySize) noexcept {
#define CHECK(n) if(!(n)) { dcassert(0); }
	EVP_PKEY* pkey = EVP_PKEY_new();
	CHECK(d2i_PublicKey(EVP_PKEY_RSA, &pkey, &aPublicKey, aKeySize));

	auto verify_ctx = EVP_PKEY_CTX_new(pkey, nullptr);
	CHECK(EVP_PKEY_verify_init(verify_ctx));
	CHECK(EVP_PKEY_CTX_set_rsa_padding(verify_ctx, RSA_PKCS1_PADDING));
	CHECK(EVP_PKEY_CTX_set_signature_md(verify_ctx, EVP_sha1()));
#undef CHECK

	auto res = EVP_PKEY_verify(verify_ctx, aSignature.data(), aSignature.size(), aDigest.data(), aDigest.size());

	EVP_PKEY_free(pkey);
	EVP_PKEY_CTX_free(verify_ctx);

	return (res == 1);
}

optional<CryptoUtil::SignatureData> CryptoUtil::signDigest(const ByteVector& aDigest, const string& aPrivateKeyFilePath) {

	FILE* f = dcpp_fopen(aPrivateKeyFilePath.c_str(), "r");
	if (!f) {
		// aErrorF("Could not open private key file");
		return nullopt;
	}

#define CHECK(n) if(!(n)) { return nullopt; }

	auto pkey = EVP_PKEY_new();
	CHECK(PEM_read_PrivateKey(f, &pkey, nullptr, nullptr));
	fclose(f);

	if (!pkey) {
		// aErrorF("Could not read private key");
		return nullopt;
	}

	auto ctx = EVP_PKEY_CTX_new(pkey, nullptr /* no engine */);
	if (!ctx) {
		CHECK(0);
	}

	CHECK(EVP_PKEY_sign_init(ctx));
	CHECK(EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING));
	CHECK(EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha1()));

	// Determine buffer length
	size_t siglen = 0;
	CHECK(EVP_PKEY_sign(ctx, NULL, &siglen, &aDigest[0], aDigest.size()));


	ByteVector sig(siglen);
	CHECK(EVP_PKEY_sign(ctx, sig.data(), &siglen, &aDigest[0], aDigest.size()));

	if (siglen <= 0) {
		// aErrorF("Could not generate signature");
		return nullopt;
	}

	auto pubkeyBufferSize = i2d_PublicKey(pkey, NULL);
	ByteVector publicKey(pubkeyBufferSize);

	{
		auto buf_ptr = publicKey.data();
		CHECK(i2d_PublicKey(pkey, &buf_ptr));
	}
#undef CHECK

	EVP_PKEY_free(pkey);
	EVP_PKEY_CTX_free(ctx);
	return SignatureData({ sig, publicKey });
}


#ifdef _DEBUG
void CryptoUtil::testSUDP() {
	uint8_t keyChar[16];
	string data = "URES SI30744059452 SL8 FN/Downloads/ DM1644168099 FI440 FO124 TORLHTR7KH7GV7W";
	Encoder::fromBase32("DR6AOECCMYK5DQ2VDATONKFSWU", keyChar, 16);
	auto encrypted = encryptSUDP(keyChar, data);

	string result;
	auto success = decryptSUDP(keyChar, ByteVector(begin(encrypted), end(encrypted)), encrypted.length(), result);
	dcassert(success);
	dcassert(compare(data, result) == 0);
}
#endif

string CryptoUtil::encryptSUDP(const uint8_t* aKey, const string& aCmd) {
	string inData = aCmd;
	uint8_t ivd[16] = { };

	// prepend 16 random bytes to message
	RAND_bytes(ivd, 16);
	inData.insert(0, (char*)ivd, 16);

	// use PKCS#5 padding to align the message length to the cypher block size (16)
	uint8_t pad = 16 - (aCmd.length() & 15);
	inData.append(pad, (char)pad);

	// encrypt it
	boost::scoped_array<uint8_t> out(new uint8_t[inData.length()]);
	memset(ivd, 0, 16);
	auto commandLength = inData.length();

#define CHECK(n) if(!(n)) { dcassert(0); }

	int len, tmpLen;
	auto ctx = EVP_CIPHER_CTX_new();
	CHECK(EVP_CipherInit_ex(ctx, EVP_aes_128_cbc(), NULL, aKey, ivd, 1));
	CHECK(EVP_CIPHER_CTX_set_padding(ctx, 0));
	CHECK(EVP_EncryptUpdate(ctx, out.get(), &len, (unsigned char*)inData.c_str(), inData.length()));
	CHECK(EVP_EncryptFinal_ex(ctx, out.get() + len, &tmpLen));
	EVP_CIPHER_CTX_free(ctx);
#undef CHECK

	dcassert((commandLength & 15) == 0);

	inData.clear();
	inData.insert(0, (char*)out.get(), commandLength);
	return inData;
}

bool CryptoUtil::decryptSUDP(const uint8_t* aKey, const ByteVector& aData, size_t aDataLen, string& result_) {
	boost::scoped_array<uint8_t> out(new uint8_t[aData.size()]);

	uint8_t ivd[16] = { };

	auto ctx = EVP_CIPHER_CTX_new();

#define CHECK(n) if(!(n)) { dcassert(0); }
	int len;
	CHECK(EVP_CipherInit_ex(ctx, EVP_aes_128_cbc(), NULL, aKey, ivd, 0));
	CHECK(EVP_CIPHER_CTX_set_padding(ctx, 0));
	CHECK(EVP_DecryptUpdate(ctx, out.get(), &len, aData.data(), aDataLen));
	CHECK(EVP_DecryptFinal_ex(ctx, out.get() + len, &len));
	EVP_CIPHER_CTX_free(ctx);
#undef CHECK

	// Validate padding and replace with 0-bytes.
	int padlen = out[aDataLen - 1];
	if (padlen < 1 || padlen > 16) {
		return false;
	}

	bool valid = true;
	for (auto r = 0; r < padlen; r++) {
		if (out[aDataLen - padlen + r] != padlen) {
			valid = false;
			break;
		} else {
			out[aDataLen - padlen + r] = 0;
		}
	}

	if (valid) {
		result_ = (char*)&out[0] + 16;
		return true;
	}

	return false;
}

CryptoUtil::SUDPKey CryptoUtil::generateSUDPKey() {
	auto key = std::make_unique<uint8_t[]>(16);
	RAND_bytes(key.get(), 16);
	return key;
}

} // namespace dcpp
