#ifndef XI_CRYPTO_HPP
#define XI_CRYPTO_HPP

#include "Log.hpp"
#include "Random.hpp"
#include "String.hpp"

extern "C" {
#include "../../packages/monocypher/monocypher.h"
}

namespace Xi {

// -------------------------------------------------------------------------
// Structs
// -------------------------------------------------------------------------

struct AEADOptions {
  Xi::String text;
  Xi::String ad;
  Xi::String tag;
  int tagLength = 16;
};

struct KeyPair {
  Xi::String publicKey;
  Xi::String secretKey;
};

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

inline Xi::String zeros(usz len) {
  Xi::String s;
  if (len == 0)
    return s;
  u8 *tmp = new u8[len]();
  s.pushEach(tmp, len);
  delete[] tmp;
  return s;
}

// -------------------------------------------------------------------------
// Core Functions (Internal)
// -------------------------------------------------------------------------

inline Xi::String createIetfNonce(u64 nonce) {
  Xi::String buffer = zeros(12);
  u8 *d = buffer.data();
  for (int i = 0; i < 8; ++i) {
    d[4 + i] = (nonce >> (i * 8)) & 0xFF;
  }
  return buffer;
}

inline Xi::String streamXor(const Xi::String &key, u64 nonce,
                            const Xi::String &text, int counter = 0) {
  if (key.length != 32)
    return Xi::String();
  Xi::String result = zeros(text.length);
  Xi::String cryptoNonce = createIetfNonce(nonce);
  crypto_chacha20_ietf(result.data(), text.data(), text.length, key.data(),
                       cryptoNonce.data(), counter);
  return result;
}

inline Xi::String createPoly1305Key(const Xi::String &key, u64 nonce) {
  return streamXor(key, nonce, zeros(32), 0);
}

/**
 * Hashing (BLAKE2b)
 * Matching: Xi::hash(input, length)
 */
inline Xi::String hash(const Xi::String &input, int length = 64,
                       const Xi::String &key = Xi::String()) {
  if (length > 64 || length < 1)
    return Xi::String();
  Xi::String result = zeros(length);
  if (key.length == 0)
    crypto_blake2b(result.data(), length, input.data(), input.length);
  else
    crypto_blake2b_keyed(result.data(), length, key.data(), key.length,
                         input.data(), input.length);
  return result;
}

/**
 * Random Bytes
 * Matching: Xi::randomBytes(len)
 */
inline Xi::String randomBytes(usz len) {
  Xi::String s = zeros(len);
  Xi::secureRandomFill(s.data(), len);
  return s;
}

/**
 * KDF (HKDF-BLAKE2b)
 * Matching: Xi::kdf(secret, info, length) -> assumes salt is empty
 *           Xi::kdf(secret, salt, info, length) -> full
 */
inline Xi::String kdf(const Xi::String &secret, const Xi::String &salt,
                      const Xi::String &info, int length) {
  const int hashLen = 64;
  if (length > 255 * hashLen)
    return Xi::String();

  Xi::String prk = hash(secret, hashLen, salt); // PRK = Hash(salt, IKM)

  int numBlocks = (length + hashLen - 1) / hashLen;
  Xi::String okm;
  Xi::String t;

  for (int i = 1; i <= numBlocks; i++) {
    Xi::String expandInput;
    expandInput += t;
    expandInput += info;
    expandInput.push((u8)i);
    t = hash(expandInput, hashLen, prk);
    okm += t;
  }
  return okm.begin(0, length);
}

inline Xi::String kdf(const Xi::String &secret, const Xi::String &info,
                      int length) {
  return kdf(secret, Xi::String(), info, length);
}

/**
 * X25519 Public Key Derivation
 */
inline Xi::String publicKey(const Xi::String &privateKey) {
  if (privateKey.length != 32)
    return Xi::String();
  Xi::String pub = zeros(32);
  crypto_x25519_public_key(pub.data(), privateKey.data());
  return pub;
}

/**
 * X25519 Key Pair Generation
 * Matching: Xi::generateKeyPair()
 */
inline KeyPair generateKeyPair() {
  Xi::String secret = randomBytes(32);
  Xi::String pub = publicKey(secret);
  return {pub, secret};
}

/**
 * X25519 Shared Secret
 * Matching: Xi::sharedKey(sec, pub)
 */
inline Xi::String sharedKey(const Xi::String &privateKey,
                            const Xi::String &publicKey) {
  if (privateKey.length != 32 || publicKey.length != 32)
    return Xi::String();
  Xi::String shared = zeros(32);
  crypto_x25519(shared.data(), privateKey.data(), publicKey.data());
  return shared;
}

/**
 * AEAD Encrypt (ChaCha20-Poly1305)
 * Matching: Xi::aeadSeal(key, nonce, aad, plaintext) -> Returns
 * [Ciphertext][Tag]
 */
inline bool aeadSeal(const Xi::String &key, u64 nonce, AEADOptions &options) {
  // 1. Encrypt (Counter 1)
  Xi::String ciphertext = streamXor(key, nonce, options.text, 1);

  // 2. Poly Key (Counter 0)
  Xi::String oneTimeKey = createPoly1305Key(key, nonce);

  // 3. Auth Data Construction
  usz aad_len = options.ad.length;
  usz cipher_len = ciphertext.length;
  usz aad_pad = (16 - (aad_len % 16)) % 16;
  usz cipher_pad = (16 - (cipher_len % 16)) % 16;

  Xi::String dataToAuth;
  dataToAuth += options.ad;
  dataToAuth += zeros(aad_pad);
  dataToAuth += ciphertext;
  dataToAuth += zeros(cipher_pad);
  for (int i = 0; i < 8; ++i)
    dataToAuth.push((aad_len >> (i * 8)) & 0xFF);
  for (int i = 0; i < 8; ++i)
    dataToAuth.push((cipher_len >> (i * 8)) & 0xFF);

  // std::cout << "DataToAuth-Seal (Deci): " << dataToAuth.toDeci() << "\n";
  //  4. Calc Tag
  Xi::String tag = zeros(16);
  crypto_poly1305(tag.data(), dataToAuth.data(), dataToAuth.length,
                  oneTimeKey.data());

  options.text = ciphertext;
  options.tag = tag.begin(0, options.tagLength);

  // std::cout << "Sealed TAG: " << options.tag.toDeci() << "\n";
  return true;
}

/**
 * AEAD Decrypt (ChaCha20-Poly1305)
 * Matching: Xi::aeadOpen(key, nonce, aad, sealed) -> Returns Plaintext or Empty
 */
inline bool aeadOpen(const Xi::String &key, u64 nonce, AEADOptions &options) {
  // info("M-A");
  if (options.text.length < 16)
    return Xi::String();

  // info("M-B");
  //  1. Poly Key
  Xi::String oneTimeKey = createPoly1305Key(key, nonce);

  // 2. Auth Data
  usz aad_len = options.ad.length;
  usz aad_pad = (16 - (aad_len % 16)) % 16;
  usz cipher_pad = (16 - (options.text.length % 16)) % 16;

  Xi::String dataToAuth;
  dataToAuth += options.ad;
  dataToAuth += zeros(aad_pad);
  dataToAuth += options.text;
  dataToAuth += zeros(cipher_pad);
  for (int i = 0; i < 8; ++i)
    dataToAuth.push((options.ad.length >> (i * 8)) & 0xFF);
  for (int i = 0; i < 8; ++i)
    dataToAuth.push((options.text.length >> (i * 8)) & 0xFF);

  // std::cout << "DataToAuth-Open (Deci): " << dataToAuth.toDeci() << "\n";
  //  3. Verify
  Xi::String calculatedTag = zeros(16);
  crypto_poly1305(calculatedTag.data(), dataToAuth.data(), dataToAuth.length,
                  oneTimeKey.data());

  // std::cout << "Recv TAG: " << options.tag.toDeci() << "\n";
  // std::cout << "Calc TAG: " << calculatedTag.toDeci() << "\n";

  // info("M- calculated: ");
  // info(calculatedTag.toDeci());
  // info("M- tagLength: ");
  // info(options.tagLength);
  // info("M- options.tag: ");
  // info(options.tag.toDeci());

  if (!options.tag.constantTimeEquals(calculatedTag, options.tagLength))
    return false;
  // info("M-C");
  //  std::cout << "Pass \n";

  // 4. Decrypt
  options.text = streamXor(key, nonce, options.text, 1);
  return true;
}

inline void secureRandomFill(u8 *buffer, usz size) {
  if (!_randomInitialized)
    randomSeed();
  const u8 *key = reinterpret_cast<const u8 *>(&_randomPool[4]);
  const u8 *nonce = reinterpret_cast<const u8 *>(&_randomPool[12]);
#if defined(__GNUC__) || defined(__clang__)
  __builtin_memset(buffer, 0, size);
#else
  for (usz i = 0; i < size; ++i)
    buffer[i] = 0;
#endif
  crypto_chacha20_ietf(buffer, buffer, size, key, nonce, _secureCounter);
  u32 blocks = (u32)((size + 63) / 64);
  _secureCounter += blocks;
}

} // namespace Xi

#endif // XI_CRYPTO_HPP