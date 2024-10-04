#pragma once

#include <util/datetime/base.h>

#include <contrib/libs/libsodium/include/sodium/crypto_sign.h>

namespace NYT::NSignatureService {

////////////////////////////////////////////////////////////////////////////////

constexpr static size_t PublicKeySize = crypto_sign_PUBLICKEYBYTES;
constexpr static size_t PrivateKeySize = crypto_sign_SECRETKEYBYTES;
constexpr static size_t SignatureSize = crypto_sign_BYTES;

////////////////////////////////////////////////////////////////////////////////

constexpr static auto CryptoInitializeTimeout = TDuration::Seconds(30);

////////////////////////////////////////////////////////////////////////////////

void InitializeCryptography();

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NSignatureService
