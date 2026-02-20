# InlineArray

`Xi::InlineArray<T>` is the highest-performance, continuous dynamic array in the `Xi` ecosystem. It is a Reference-Counted, Copy-on-Write (CoW) buffer designed to replace `std::vector` when zero-copy slicing and exact memory control are paramount.

## Architectural Overview: Reference Counting & CoW

Unlike standard vectors that deep-copy their contents when passed by value, `Xi::InlineArray` operates via a tiny pointer shell to a shared heap `Block`.

1. **Pass-by-Value is Free:** Passing an `InlineArray` into a function increments a strong reference count (`useCount`) instead of duplicating memory.
2. **Slicing is Free:** You can create "Views" (sub-arrays) that point to the exact same shared memory block, simply adjusting their internal logical `offset` and logical `length`.
3. **Copy On Write (CoW):** If you attempt to mutate (`push()`, `operator[]`) an `InlineArray` that shares its `Block` with another instance, it will transparently detach, allocate its own contiguous block, copy the data, and then apply the mutation. This guarantees perfect safety with immense performance benefits for read-heavy operations.
4. **1:1 C-API \u0026 STL Compatibility:** Because internal memory is strictly contiguous, invoking `.data()` returns a standard `T*` pointer. This means `InlineArray` seamlessly interoperates with all legacy C-Libraries (like `memcpy`), POSIX system calls, and Linux headers directly with zero headaches.

---

## 📖 Complete API Reference

### 1. Memory & Block Management

- `bool allocate(usz len, bool safe = false)`
  Allocates contiguous memory for `len` elements. If `safe` is true, the method will instantly fail and return `false` if the array shares memory with another instance (preventing CoW detachments).
- `T* data()`
  Returns the raw `T*` pointer starting precisely at the array's logical `offset`.
- `usz size()` / `usz length_js()`
  Returns the logical size of the array or view.
- `void retain()` / `void destroy()`
  Manual reference count overrides (typically handled automatically by C++ destructors and copy constructors).

### 2. Mutation Methods

- `void push(const T &val)`
  Appends an element. Triggers a CoW detachment if the memory block is shared.
- `void pushEach(const T *vals, usz count)`
  Efficient bulk append from a raw C-pointer.
- `T pop()`
  Removes and returns the last element.
- `void unshift(const T &val)`
  Prepends an element to the front of the array, shifting everything right.
- `T shift()`
  Removes and returns the first element of the array, shifting everything left.

### 3. Data Retrieval & Lookup

- `bool has(usz idx)`
  Returns `true` if the global index is strictly within the bounds of this logical view.
- `long long indexOf(const T &val)`
  Performs a linear scan and returns the index of the first matched element, or `-1` if not found.
- `T& operator[](usz idx)`
  Direct access. Triggers CoW detachment if writing to a shared block!

### 4. Zero-Copy Slicing & Views

`InlineArray` acts as its own View proxy. Slicing returns a lightweight `InlineArray` object that shares the original block.

- `InlineArray begin(usz start, usz end)`
  Creates a bounded sub-view from logical index `start` to `end`.
- `InlineArray begin(usz start)`
  Creates a sub-view from index `start` to the end of the array.

---

## 💻 Example: Zero-Copy Views

```cpp
#include "Xi/InlineArray.hpp"
#include <Arduino.h>

void setup() {
  Xi::InlineArray<uint8_t> packet;
  packet.allocate(100);

  // Fill with dummy data
  for(int i = 0; i < 100; i++) packet.push(i);

  // CRITICAL: This is virtually FREE. It does NOT copy the 50 bytes.
  // It simply creates a shell pointing to the same memory block
  // with an offset of 10 and a length of 50.
  Xi::InlineArray<uint8_t> payloadView = packet.begin(10, 60);

  // Memory is shared!
  Serial.printf("Payload Size: %d\n", payloadView.size()); // 50

  // The moment we mutate `payloadView`, the CoW mechanism detaches it
  // into its own physically separate heap allocation automatically!
  payloadView.push(0xFF);
}
```
