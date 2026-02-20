# Developer TODO

An ongoing list of required enhancements and bug fixes for the `xic` library.

### Rho Networking

- Implement packet loss metric counters in `Tunnel`.
- Add Keep-Alive pings automatically when `.flush()` isn't called for $N$ seconds.
- Support deep Anycast wildcarding (e.g. `n+`).

### File System

- Build out full support for `SPIFFS` on ESP32 into `Xi::FS` and `File.hpp`.
- Write dedicated asynchronous `Xi::FileStream` class rather than loading entire blobs into `Xi::String`.
- Fix bug with Reparse pointer depth recursion on Windows.

### Security

- Investigate adopting Ed25519 signing for static Long-Term Key verification before Ephemeral Switch.
- Evaluate RAM requirements for AEAD ChaCha20 streaming contexts rather than one-shot `aeadSeal()`.

### Core Memory

- Expand `Xi::Map` to correctly auto-rebalance internal binary trees or adopt pure Hash Maps.
- Polish `String::replace` inline logic to avoid allocating fragmentation.
