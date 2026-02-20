# Array

`Xi::Array<T>` is an advanced, fragment-based dynamic list structure. Unlike a standard contiguous vector, `Xi::Array` is specifically engineered to defeat memory fragmentation on embedded systems by storing elements across a sparse collection of `InlineArray<T>` chunks.

## Architectural Overview: Fragmented Arrays

If you push 1,000 items to a `std::vector`, it will reallocate memory exponentially (e.g., space for 1, 2, 4, 8, 16 elements). Each reallocation copies the entire array to the new location and frees the old block, rapidly shredding the ESP32's available heap with checkerboard fragmentation.

`Xi::Array<T>` solves this by acting as a manager for multiple `InlineArray<T>` fragments. When you `allocate()` more space, it creates a _new_ contiguous fragment link rather than copying and destroying the existing data.

- **No Reallocation Copies:** Expanding the array leaves existing elements precisely where they are in RAM.
- **Deterministic Smoothing:** When contiguous memory is explicitly required (e.g., for direct C-Pointer math), you can invoke `data()`, which forcefully flattens the fragmented chunks into a single contiguous block safely.
- **1:1 STL Drop-In Compatibility:** Despite the fragmented architecture under the hood, `Xi::Array` implements standard C++11 iterators (`begin()`, `end()`), `operator[]`, and `.size()`. It functions as a complete 1:1 drop-in replacement for `std::vector` in almost any standard C++ algorithm (`std::sort`, `std::find`) or POSIX operation.

---

## 📖 Complete API Reference

### 1. Memory & Allocation Management

- `bool allocate(usz len)`
  Explicitly resizes the array logic to support `len` elements. If the requested length exceeds the current fragment threshold, a new internal `InlineArray<T>` chunk is spawned to hold the difference without re-allocating the old items. Returns `true` on success.
- `usz size()` / `usz length()`
  Returns the total number of currently occupied elements across all active memory fragments.
- `T* data()`
  **Flattening Operation:** If the array consists of multiple sparse fragments, this method transparently allocates a single contiguous block, copies all fragments sequentially into it, replaces the internal fragment list with this single block, and returns the raw `T*` pointer.

### 2. Mutation Methods

- `void push(const T &val)`
  Appends a single element to the end of the logical array, spawning a new minimal fragment if the current capacity is exhausted.
- `void pushEach(const T *vals, usz count)`
  Efficient bulk append from a raw C-pointer.
- `bool pop(T &out)` / `T pop()`
  Removes the last element across all fragments. Returns the element or assigns it to `out`.
- `void unshift(const T &val)` / `T shift()`
  Prepends (`unshift`) or removes (`shift`) elements exclusively from the very front of the array.
- `void splice(usz start, usz length)`
  Removes `length` elements starting at logical index `start`.
- `bool remove(usz idx)`
  Removes the element at the global logical index `idx`. This shifts all subsequent elements across fragment boundaries down by one.
- `bool insert(usz idx, const T &val)`
  Inserts the element at index `idx`, pushing all subsequent fragments up.
- `void clear()`
  Safely truncates the array to zero length, destructing active objects without destroying the underlying fragment memory capacity.

### 3. Data Retrieval & Access

- `T& operator[](usz idx)`
  Retrieves a reference to the element at the specified logical index. This automatically resolves which memory fragment houses the target element. Does **not** perform bounds checking.
- `T* get(usz idx)`
  Safely targets the logical index across fragments. Returns a pointer to the element if the index is valid, or `nullptr` if it exceeds `size()`.
- `long long find(const T &val)`
  Performs a linear scan and returns the global index of the first matched element, or `-1` if absent.

### 4. Advanced Iterators

`Xi::Array` supports standard C++11 range-based `for` loops through custom iterators that seamlessly jump across underlying memory fragments:

- `Iterator begin()` / `Iterator end()`
  Spawns a stateful fragment-hopping iterator.

---

## 💻 Example: Seamless Iteration Over Fragments

```cpp
#include "Xi/Array.hpp"
#include <Arduino.h>

void processSensors() {
  Xi::Array<uint32_t> readings;

  // Allocates a chunk of 500
  readings.allocate(500);

  // Pushing past 500 automatically spawns a NEW fragment
  // instead of copying the first 500. Zero fragmentation!
  for(int i = 0; i < 600; i++) {
    readings.push(analogRead(A0));
  }

  // Range-based for automatically crosses fragment boundaries
  // completely transparently.
  for(auto& val : readings) {
    Serial.println(val);
  }

  // Need contiguous memory for a legacy C library? Flatten it explicitly:
  uint32_t* flatBuffer = readings.data();
  // ... pass flatBuffer to a hardware DMA engine ...
}
```
