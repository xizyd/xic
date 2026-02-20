# Random

`Xi` abstracts deterministic Random Number Generation (RNG) and cryptographically secure True Random Number Generation (TRNG) hardware endpoints depending on the architecture structure.

## Why it Shines on ESP32/Arduino

Calling the standard `rand()` function on an Arduino returns a pseudo-random integer sequence. Without a proper noise seed source, `rand()` will produce the precise same set of "random" numbers every time the microcontroller boots.
If you use this sequence to generate Cryptographic Nonces for AEAD or Session IDs, it collapses your entire security channel to replay attacks immediately upon boot!

`Xi` incorporates mechanisms to source True Randomness (entropy) efficiently based on the hardware compilation target without resorting to chaotic floating analog pins.

### ESP32 Target

On ESP32 architectures, `Xi` expects the use of `esp_random()`, which samples thermal noise from the WiFi/Bluetooth RF hardware block, providing mathematically unpredictable byte streams instantly.

### Linux/POSIX Target

On 64-bit Linux counterparts, `Xi` leverages OS-level `/dev/urandom` calls, securely piped into the native wrappers, alongside `MADV_WIPEONFORK` memory protection to prevent entropy duplication across child processes.

---

## 📖 Complete API Reference

The `Xi::Random` module offers two tiers of randomness: a high-speed XorShift mathematical engine for general purposes (games, UI), and a secure TRNG engine for cryptography.

### 1. Seeding the Engine

- `void randomSeed()`
  Automatically grabs the best available hardware entropy (e.g., `/dev/urandom` or `esp_random()`) and fills the internal 80-byte entropy pool.
- `void randomSeed(u32 s)`
  Manually spreads a deterministic 32-bit seed across the entire pool using a Knuth LCG-style multiplier. Useful for deterministic testing or procedural generation.

### 2. Fast General-Purpose RNG (XorShift)

These functions are exceptionally fast and avoid system calls, but are **not** cryptographically secure. They draw from the internal engine state initialized during seeding.

- `u32 randomNext()`
  Retrieves a raw, fast 32-bit random integer.
- `u32 random(u32 max)`
  Returns a random integer between `0` and `max - 1`.
- `i32 random(i32 min, i32 max)`
  Returns a random integer bounded within `[min, max - 1]`.
- `f32 randomFloat()`
  Returns a 32-bit floating-point number between `0.0f` and `1.0f`.
- `void randomFill(u8 *buffer, usz size)`
  Efficiently fills a raw memory block with fast XorShift randomness, operating on 4-byte boundaries for maximum throughput.

### 3. Cryptographic True RNG

For generating Nonces and Keying Material, refer to `Xi::Crypto` functions which explicitly consume the secured TRNG pool, such as:

- `Xi::randomBytes(len)`
- `Xi::secureRandomFill(buffer, size)`

---

## 💻 Example: Generating Randomness

```cpp
#include "Xi/Random.hpp"
#include "Xi/Log.hpp"

void gameLogic() {
    // 1. Seed correctly at application boot!
    Xi::randomSeed();

    // 2. Fast logic randoms
    int damage = Xi::random(10, 50);
    float chance = Xi::randomFloat();

    if (chance > 0.8f) {
        Xi::print("Critical Hit! Damage: ");
        Xi::println(damage);
    }
}
```
