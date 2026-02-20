# Crypto

`Xi::Crypto` provides exceptionally fast, mathematically proven, embedded-friendly cryptographic primitives by wrapping the **Monocypher** C-library into safe, byte-bounded `Xi::String` C++ abstractions.

## Architectural Overview: Defeating TLS Bloat

Massive dependencies like OpenSSL or mbedTLS will quickly blow through an ESP32's available flash storage. Furthermore, they suffer from severe dynamic allocation spam during TLS handshakes, fragmenting the heap.

`Xi::Crypto` solves this by:

1. **Zero Malloc Bindings:** `Xi::Crypto` wraps the Monocypher state blocks entirely on the stack. Return values are securely packed into `Xi::String` CoW buffers, ensuring absolute memory predictability.
2. **Modern Primitives Only:** Relying exclusively on X25519, ChaCha20, Poly1305, and BLAKE2b, the binary footprint is exceptionally small.

---

## 📖 Complete API Reference

### 1. Key Exchange (X25519 ECDH)

Instead of RSA, `Xi` relies on Elliptic-Curve Diffie-Hellman (ECDH) which offers equivalent security at a fraction of the key size (32 bytes) and computational cost.

- `KeyPair generateKeyPair()`
  Generates a completely new cryptographically secure 32-byte Private Key and mathematically derives its accompanying 32-byte Public Key. Returns a `KeyPair` struct (`{ publicKey, secretKey }`).
- `String publicKey(const String &privateKey)`
  Derives a 32-byte public key from an existing 32-byte private key.
- `String sharedKey(const String &privateKey, const String &publicKey)`
  The core of the handshake. Takes your local private key and the remote peer's public key to generate an identical 32-byte symmetric Shared Secret on both machines across a public network.

### 2. Authenticated Encryption (AEAD ChaCha20-Poly1305)

Instead of managing raw block ciphers and calculating HMACs manually, `Xi` provides a unified AEAD wrapper. It encrypts the payload _and_ attaches a tamper-proof signature in one operation.

All parameters (Plaintext/Ciphertext, Additional Data, MAC Tags) are orchestrated through the `AEADOptions` struct.

```cpp
struct AEADOptions {
  Xi::String text;       // The plaintext to encrypt, or ciphertext to decrypt
  Xi::String ad;         // Additional Authentication Data (unencrypted, but signed)
  Xi::String tag;        // Pol1305 MAC Hash Tag output
  int tagLength = 16;    // Defaults to 16 bytes
};
```

- `bool aeadSeal(const String &key, u64 nonce, AEADOptions &options)`
  Encrypts `options.text` in-place, modifying it into ciphertext. Generates a 16-byte Message Authentication Code (MAC) and stores it in `options.tag`. Additional Authentication Data (AAD) can be passed via `options.ad` to guarantee it isn't tampered with mid-flight. Returns `true` on success.
- `bool aeadOpen(const String &key, u64 nonce, AEADOptions &options)`
  The decryption equivalent. Calculates the MAC of the incoming ciphertext and strictly compares it against `options.tag` over constant time. If it fails, or if the `nonce` was duplicated (Replay Attack), it aborts instantly and returns `false`. If successful, `options.text` is decrypted in-place back to plaintext.

### 3. Hashing & KDF (BLAKE2b)

`Xi` uses BLAKE2b, which is significantly faster than SHA-2 and SHA-3 on both 32-bit microcontrollers and 64-bit servers.

- `String hash(const String &input, int length = 64, const String &key = String())`
  Provides standard cryptographic hashing, outputting up to 64 bytes natively. Passing a `key` miraculously upgrades it to an authenticated keyed-hash (MAC).
- `String kdf(const String &secret, const String &salt, const String &info, int length)`
  A rigorous HMAC-based Extract-and-Expand Key Derivation Function (HKDF). It forcefully expands a small shared secret (like the one generated from X25519) into massive, cryptographically uniform key material.
- `String kdf(const String &secret, const String &info, int length)`
  Convenience overload for HKDF without a salt.

### 4. Random Primitives

Working securely requires strong, unpredictable values. These functions ensure hardware-backed true randomness.

- `String randomBytes(usz len)`
  Returns a string of exact `len` size filled with cryptographically secure random bytes.
- `void secureRandomFill(u8 *buffer, usz size)`
  Fills a raw memory block with true randomness. On ESP32 systems, this taps exactly into the WiFi hardware radio noise floor `esp_random()` rather than fake pseudo-random `rand()` algorithms, guaranteeing cryptographically flawless IVs and Nonces.

---

## 💻 Example: Secure Handshake & AEAD Payload

```cpp
#include "Xi/Crypto.hpp"
#include "Xi/String.hpp"
#include "Xi/Log.hpp"

void establishSecureChannel() {
    // 1. Generate local keys
    Xi::KeyPair alice = Xi::generateKeyPair();

    // Imagine Alice receives Bob's public key securely over the network
    Xi::String bobPublicKey = "...";

    // 2. Derive Shared Secret! Same on both sides
    Xi::String sharedSecret = Xi::sharedKey(alice.secretKey, bobPublicKey);

    // 3. Prepare Encrypted Message
    u64 exactNonce = 1; // Strict incrementing counter for every message
    Xi::AEADOptions opts;
    opts.text = "Hello Secure World!";
    opts.ad   = "RoutingID:99"; // Unencrypted, but Cryptographically Signed!

    // Encrypt
    if (Xi::aeadSeal(sharedSecret, exactNonce, opts)) {
        // opts.text is now chaotic ciphertext!
        // opts.tag holds the 16-byte Poly1305 signature
        network.send(opts.ad);
        network.send(opts.tag);
        network.send(opts.text);
    }
}
```
