# Crypto

`Xi::Crypto` provides lightweight wrappers around the incredibly fast **Monocypher** cryptographic suite.

## Why it Shines on ESP32/Arduino

OpenSSL or mbedTLS are massive dependencies that will quickly blow through your ESP32 partition limits. They also suffer from severe dynamic allocation spam during TLS handshakes.

Monocypher is a tiny, modern crypto library targeting embedded usage. `Xi` wraps this C-library into safe, byte-bounded C++ structures (like `Xi::String`) to prevent buffer-overflows and eliminate pointer manipulation errors without incurring OpenSSL bloat.

## Key Mechanisms (AEAD)

### Authenticated Encryption with Associated Data

Instead of managing raw block ciphers, Initialization Vectors (IVs), and HMACs separately, `Xi` provides a unified `crypto_aead` wrapper.

It automatically performs:

1. **Encryption** (ChaCha20)
2. **Authentication** (Poly1305 MAC)
3. **Nonce State Validation**

### Basic Usage

```cpp
#include "Xi/String.hpp"
// Include Crypto bindings
// (Depends on monocypher under the hood)

void demo() {
  Xi::String key = ...; // 32 bytes
  Xi::String msg = "Secret";
  Xi::String nonce = ...; // 24 bytes

  // Encrypt
  Xi::String cipherText = encrypt(key, msg, nonce);

  // Decrypt
  Xi::String original = decrypt(key, cipherText, nonce);

  if (original.size() == 0) {
    // Decryption failed.
    // Either the key was wrong, the Nonce was reused,
    // or the packet was tampered and the MAC failed!
  }
}
```
