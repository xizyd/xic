#ifndef XI_CRYPTO_HPP
#define XI_CRYPTO_HPP

#include "String.hpp"
#include "Utils.hpp"
extern "C" {
    #include "../../src/external/monocypher.h"
}

namespace Xi {

    // -------------------------------------------------------------------------
    // Structs
    // -------------------------------------------------------------------------

    struct AEADResult {
        Xi::String text; 
        Xi::String tag;  
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
        if (len == 0) return s;
        u8* tmp = new u8[len](); 
        s.pushEach(tmp, len);
        delete[] tmp;
        return s;
    }

    // -------------------------------------------------------------------------
    // Core Functions (Internal)
    // -------------------------------------------------------------------------

    inline Xi::String createIetfNonce(u64 nonce) {
        Xi::String buffer = zeros(12);
        u8* d = buffer.data();
        for (int i = 0; i < 8; ++i) {
            d[4 + i] = (nonce >> (i * 8)) & 0xFF;
        }
        return buffer;
    }

    inline Xi::String streamXor(const Xi::String& key, u64 nonce, const Xi::String& text, int counter = 0) {
        if (key.len() != 32) return Xi::String();
        Xi::String result = zeros(text.len());
        Xi::String cryptoNonce = createIetfNonce(nonce);
        crypto_chacha20_ietf(result.data(), text.data(), text.len(), key.data(), cryptoNonce.data(), counter);
        return result;
    }

    inline Xi::String createPoly1305Key(const Xi::String& key, u64 nonce) {
        return streamXor(key, nonce, zeros(32), 0);
    }

    /** 
     * Hashing (BLAKE2b)
     * Matching: Xi::hash(input, length)
     */
    inline Xi::String hash(const Xi::String& input, int length = 64, const Xi::String& key = Xi::String()) {
        if (length > 64 || length < 1) return Xi::String();
        Xi::String result = zeros(length);
        if (key.len() == 0) crypto_blake2b(result.data(), length, input.data(), input.len());
        else crypto_blake2b_keyed(result.data(), length, key.data(), key.len(), input.data(), input.len());
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
    inline Xi::String kdf(const Xi::String& secret, const Xi::String& salt, const Xi::String& info, int length) {
        const int hashLen = 64;
        if (length > 255 * hashLen) return Xi::String();
        
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

    inline Xi::String kdf(const Xi::String& secret, const Xi::String& info, int length) {
        return kdf(secret, Xi::String(), info, length);
    }

    /** 
     * X25519 Public Key Derivation 
     */
    inline Xi::String publicKey(const Xi::String& privateKey) {
        if (privateKey.len() != 32) return Xi::String();
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
        return { pub, secret };
    }

    /** 
     * X25519 Shared Secret
     * Matching: Xi::sharedKey(sec, pub)
     */
    inline Xi::String sharedKey(const Xi::String& privateKey, const Xi::String& publicKey) {
        if (privateKey.len() != 32 || publicKey.len() != 32) return Xi::String();
        Xi::String shared = zeros(32);
        crypto_x25519(shared.data(), privateKey.data(), publicKey.data());
        return shared;
    }

    /**
     * AEAD Encrypt (ChaCha20-Poly1305)
     * Matching: Xi::aeadSeal(key, nonce, aad, plaintext) -> Returns [Ciphertext][Tag]
     */
    inline Xi::String aeadSeal(const Xi::String& key, u64 nonce, const Xi::String& associatedData, const Xi::String& plaintext) {
        // 1. Encrypt (Counter 1)
        Xi::String ciphertext = streamXor(key, nonce, plaintext, 1);
        
        // 2. Poly Key (Counter 0)
        Xi::String oneTimeKey = createPoly1305Key(key, nonce);

        // 3. Auth Data Construction
        usz aad_len = associatedData.len();
        usz cipher_len = ciphertext.len();
        usz aad_pad = (16 - (aad_len % 16)) % 16;
        usz cipher_pad = (16 - (cipher_len % 16)) % 16;

        Xi::String dataToAuth;
        dataToAuth += associatedData;
        dataToAuth += zeros(aad_pad);
        dataToAuth += ciphertext;
        dataToAuth += zeros(cipher_pad);
        for (int i = 0; i < 8; ++i) dataToAuth.push((aad_len >> (i * 8)) & 0xFF);
        for (int i = 0; i < 8; ++i) dataToAuth.push((cipher_len >> (i * 8)) & 0xFF);

        // 4. Calc Tag
        Xi::String tag = zeros(16);
        crypto_poly1305(tag.data(), dataToAuth.data(), dataToAuth.len(), oneTimeKey.data());

        // 5. Return [Ciphertext][Tag]
        Xi::String res = ciphertext;
        res += tag;
        return res;
    }

    /**
     * AEAD Decrypt (ChaCha20-Poly1305)
     * Matching: Xi::aeadOpen(key, nonce, aad, sealed) -> Returns Plaintext or Empty
     */
    inline Xi::String aeadOpen(const Xi::String& key, u64 nonce, const Xi::String& associatedData, const Xi::String& sealed) {
        if (sealed.len() < 16) return Xi::String();
        
        // Split Sealed into Ciphertext and Tag
        usz tagLen = 16;
        usz cipherLen = sealed.len() - tagLen;
        
        Xi::String ciphertext;
        ciphertext.pushEach(sealed.data(), cipherLen);
        
        Xi::String tag;
        tag.pushEach(sealed.data() + cipherLen, tagLen);

        // 1. Poly Key
        Xi::String oneTimeKey = createPoly1305Key(key, nonce);

        // 2. Auth Data
        usz aad_len = associatedData.len();
        usz aad_pad = (16 - (aad_len % 16)) % 16;
        usz cipher_pad = (16 - (cipherLen % 16)) % 16;

        Xi::String dataToAuth;
        dataToAuth += associatedData;
        dataToAuth += zeros(aad_pad);
        dataToAuth += ciphertext;
        dataToAuth += zeros(cipher_pad);
        for (int i = 0; i < 8; ++i) dataToAuth.push((aad_len >> (i * 8)) & 0xFF);
        for (int i = 0; i < 8; ++i) dataToAuth.push((cipherLen >> (i * 8)) & 0xFF);

        // 3. Verify
        Xi::String calculatedTag = zeros(16);
        crypto_poly1305(calculatedTag.data(), dataToAuth.data(), dataToAuth.len(), oneTimeKey.data());

        if (!tag.constantTimeEquals(calculatedTag)) return Xi::String();

        // 4. Decrypt
        return streamXor(key, nonce, ciphertext, 1);
    }

    inline void secureRandomFill(u8 *buffer, usz size) {
        if (!_randomInitialized) randomSeed();
        const u8* key = reinterpret_cast<const u8*>(&_randomPool[4]);
        const u8* nonce = reinterpret_cast<const u8*>(&_randomPool[12]);
        #if defined(__GNUC__) || defined(__clang__)
            __builtin_memset(buffer, 0, size);
        #else
            for(usz i=0; i<size; ++i) buffer[i] = 0;
        #endif
        crypto_chacha20_ietf(buffer, buffer, size, key, nonce, _secureCounter);
        u32 blocks = (u32)((size + 63) / 64);
        _secureCounter += blocks;
    }

} // namespace Xi

#endif // XI_CRYPTO_HPP