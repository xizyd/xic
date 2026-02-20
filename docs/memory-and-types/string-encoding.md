# Network Buffers & String Encoding

While `Xi::String` acts as a fundamental memory abstraction, its true power in the `Xi` framework lies in its ability to behave as a **high-speed, binary network buffer** for IoT communication protocols.

Instead of relying on heavy abstraction layers like Protocol Buffers (Protobuf) or JSON serialization, `Xi::String` provides allocation-free, inline bit-packing algorithms tailored for extreme bandwidth efficiency on LoRa and UDP payloads.

---

## 💡 Architectural Overview: Compression

When transmitting an integer like `42` across a network using standard C++ struct casting, it consumes a full 8 bytes (64 bits) for an `int64_t`. Over slow protocols like LoRaWAN, transmitting zero-padded bytes is an unacceptable waste of airtime.

`Xi::String` natively implements **VarLong (Variable-Length Integer)** encoding.

### How VarLong Works

VarLong dynamically compresses integers based on their actual value magnitude.

- It reserves the **Most Significant Bit (MSB)** of each byte as a "continuation flag".
- The remaining 7 bits hold the actual integer payload.
- If the MSB is `1`, the parser knows there is another byte coming. If the MSB is `0`, the integer sequence terminates.

**Result:**

- Small numbers (e.g., `42`) only consume **1 single byte**.
- Massive numbers seamlessly scale up to 9 bytes without any predefined size casting.

---

## 📖 Binary Streaming API

The `Xi::String` acts as a FIFO (First-In, First-Out) byte stream queue when using the `shift` and `push` paradigms.

### 1. Pushing Serialization (Encoding)

These operations append densely packed binary representations sequentially to the end of the `String`.

- `String* pushVarLong(long long v)`
  Compresses an signed 64-bit integer into its minimal byte-representation and writes it.
- `String* pushBool(bool v)`
  Pushes a strict `0x00` or `0x01` byte.
- `String* pushF32(f32 v)` / `String* pushF64(f64 v)`
  Writes IEEE 754 raw binary floating point data directly into the buffer.
- `String* pushVarString(const String &s)`
  A composite feature: It automatically runs `pushVarLong(s.size())` immediately followed by blasting the raw string bytes. This creates a beautifully framed packet payload that can be perfectly deserialized on the other side.

### 2. Shifting Deserialization (Decoding)

To read data _out_ of an incoming network buffer, `Xi::String` uses `shift()` mechanics that pop bytes physically off the front of the string, automatically advancing the internal data pointers.

- `long long shiftVarLong()`
  Reads bytes sequentially until hitting an MSB of `0`, reconstructs the 64-bit integer, and shifts the string's internal offset forward by precisely that many bytes.
- `bool shiftBool()`
- `f32 shiftF32()`
- `String shiftVarString()`
  Reads the `VarLong` length prefix, uses that length to extract the exact string payload, and returns it as a brand new `CoW` string.

### 3. Non-Destructive Lookahead

- `VarLongResult peekVarLong(usz offset = 0)`
  If you need to check a value without mutating the buffer (e.g., reading a Packet ID header to determine routing), `peekVarLong` parses the integer but leaves the incoming stream entirely intact.

---

## 📖 Text Parsing API

For human-readable data (HTTP, Serial), `String` provides static utility converters without involving `malloc()` or standard C-libraries.

- `static int parseInt(const String &s)`
  Strips preceding whitespace and resolves `+`/`-` ASCII digits dynamically.
- `static f64 parseDouble(const String &s)`
  Resolves decimal points (`.`) completely inline.
- `String toHex()` / `String toDeci()`
  Converts binary ciphertext (like AES keys or hashes) back into `A1:FF:8B` hex representations for logging.

---

## 💻 Example: Serializing a Player State

This packs a complex player update into roughly ~12 bytes instead of a rigid ~50-byte struct!

```cpp
#include "Xi/String.hpp"

// Encodes the payload locally
Xi::String buildPlayerUpdate(u64 playerID, bool isAlive, float health) {
  Xi::String packet;

  // Method chaining is supported!
  packet.pushVarLong(playerID)
        ->pushBool(isAlive)
        ->pushF32(health);

  return packet;
}

// Decodes the payload dynamically from the server
void onNetworkReceive(Xi::String incoming) {
  // `incoming` is destructively shifted down to 0 bytes
  // as we decode it perfectly in order.

  u64 id = incoming.shiftVarLong();
  bool alive = incoming.shiftBool();
  float hp = incoming.shiftF32();
}
```
