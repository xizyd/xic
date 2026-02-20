# String

`Xi::String` is the primary byte-buffer and text manipulation structure. As a direct inheritor of `Xi::InlineArray<u8>`, it boasts identical **Copy-on-Write (CoW)** optimization and explicit memory management, providing an embedded-safe alternative to both `std::string` and Arduino's notoriously fragmented `String` class.

## Architectural Overview: CoW & Network Buffers

The standard Arduino `String` class crashes devices because every operation (`myString += "a"`) allocates a brand new heap buffer, copies the contents, and shreds the SRAM.
`std::string` avoids this for tiny strings via Small String Optimization (SSO), but suffers the same exponential doubling fragmentation for larger binary payloads.

### Inheritance from `InlineArray`

Because `Xi::String` inherits from `InlineArray<u8>`:

1. **Zero-Copy Passing:** Passing a `Xi::String` into a function only increments a reference count. The underlying string bytes are never duplicated unless mutated.
2. **Explicit Capacity vs Size:** You can `alloc(1024)` to reserve exactly 1 KB once, then populate the string up to that threshold linearly without a single allocation occurring dynamically.
3. **Binary Safe:** It natively handles `\0` null bytes perfectly, meaning it doubles as a raw UDP packet buffer or AES ciphertext container.
4. **1:1 STL Drop-In Compatibility:** Providing `c_str()`, `data()`, `.size()`, and `length()`, logic built around standard `std::string` drops directly onto `Xi::String` without friction to securely integrate into legacy C functions like `printf` or Linux networking sockets.

---

## 📖 Complete API Reference

### 1. Structure & Size

- `usz length()` / `usz size()`
  Returns the physical number of bytes actively occupied (excluding hidden capacity or null terminators).
- `bool isEmpty()`
  Fast checking for zero-length strings.
- `void fill(u8 val)`
  Memsets the entire string buffer to a specific byte value.

### 2. Concatenation & Appending

These operations modify the string. If the string is shared (CoW), it automatically detaches and reallocates a unique memory block securely before applying the changes.

- `void concat(const String &other)`
  Appends another string.
- `void append_raw(const u8 *b, usz count)`
  Binary-safe bulk append.
- `void append_int(I n)`
  Performs allocation-free `itoa` conversion and appends the digits.
- `void append_f32(f64 n, int precision = 6)`
  Appends a floating-point integer with determined precision.

### 3. C-String Interop & Mutability

- `char* c_str()`
  Retrieves a legacy null-terminated `char*`. Unlike `std::string`, if the `Xi::String` does not currently end with `\0`, this method **mutates the underlying buffer**, safely appending a null byte without increasing the logical `size()` representation, then returns the pointer.
- `void recompute()`
  If you pass `c_str()` or `data()` to an external C-library (like reading a UDP socket directly into the string buffer), call `.recompute()` to force the string to recount its logical length using `strlen()`.

### 4. Advanced Searching & Parsing

- `long long indexOf(const char *needle, usz start = 0)`
  Returns the exact index of the substring, or `-1` if absent.
- `String substring(usz start, usz end = -1)`
  Returns a brand new, detached string containing the specified boundaries. (Negative values count backwards from the end).
- `Array<String> split(const char *delim)`
  Splits the string into a dynamic array of distinct string tokens.
- `String replace(const char *find, const char *rep)`
  Returns a new string where all occurrences of the target are replaced.

### 5. Formatting & Static Utilities

- `String toHex()` / `String toDeci()`
  Dumps the string bytes into legible Hexadecimal or Decimal representation strings. Perfect for dumping AES ciphertext or UDP packet nonces.
- `static int parseInt(const String &s)` / `static f64 parseDouble(const String &s)`
  Static utility parsing methods.
- `static void secureRandomFill(u8 *buffer, usz size)`
  Interfaces directly with the `Crypto` module's True Random Generator.

### 6. Binary Streams & Buffers

The `Xi::String` class is heavily optimized for constructing binary networking packets payload by payload:

- `String* pushVarLong(long long v)` / `long long shiftVarLong()`
  Pushes and Pops variable-length compressed integer bytes. Extremely effective for IoT network transmission packing.
- `String* pushBool(bool v)` / `bool shiftBool()`
- `String* pushF32(f32 v)` / `f32 shiftF32()`
- `String* pushI64(i64 v)` / `i64 shiftI64()`

---

## 💻 Example: Parsing Network Traffic

```cpp
#include "Xi/String.hpp"
#include "Xi/Array.hpp"
#include <Arduino.h>

void executeCommand(Xi::String payload) {
  // Example Payload: "SET_COLOR:255:128:0"

  // Splitting returns an Array<String>.
  // The Strings internally manage their own exact memory blocks.
  Xi::Array<Xi::String> tokens = payload.split(":");

  if(tokens.size() == 4 && tokens[0] == "SET_COLOR") {
    int r = Xi::parseInt(tokens[1]);
    int g = Xi::parseInt(tokens[2]);
    int b = Xi::parseInt(tokens[3]);

    Serial.printf("Colors Output: R%d G%d B%d\n", r, g, b);
  }
}
```
