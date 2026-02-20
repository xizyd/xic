# Random

`Xi` abstracts deterministic Random Number Generation (RNG) and cryptographically secure True Random Number Generation (TRNG) hardware endpoints depending on the architecture structure.

## Why it Shines on ESP32/Arduino

Calling the standard `rand()` function on an Arduino returns a pseudo-random integer sequence. Without a proper noise seed source, `rand()` will produce the precise same set of "random" numbers every time the microcontroller boots.
If you use this sequence to generate Cryptographic Nonces for AEAD or Session IDs, it collapses your entire security channel to replay attacks immediately upon boot!

`Xi` incorporates mechanisms to source True Randomness (entropy) efficiently based on the hardware compilation target without resorting to chaotic floating analog pins.

### ESP32 Target

On ESP32 architectures, `Xi` expects the use of `esp_random()`, which samples thermal noise from the WiFi/Bluetooth RF hardware block, providing mathematically unpredictable byte streams instantly.

### Server Target

On 64-bit Linux counterparts running `cppyy` (i.e. Python servers), `Xi` leverages OS-level `/dev/urandom` via `os.urandom()` calls, securely piped into the native wrappers.

## Cryptographic Nonces

A Nonce (Number Used Once) ensures that if an attacker captures an encrypted Radio packet floating in the air, they cannot replay it tomorrow. Even if the packet payload is identical (e.g., `"Turn On LED"`), a non-repeating 8-byte, 16-byte, or 24-byte prefix is embedded, hashing into vastly different cipher text.

Generating strong nonces relies explicitly on these True Random `Xi` endpoints!
