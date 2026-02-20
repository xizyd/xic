# Map

`Xi::Map<K, V>` is a highly tuned, state-of-the-art **Robin Hood Hash Table** operating purely on contiguous inline arrays. It is the embedded-safe alternative to `std::unordered_map` or `std::map`.

## Architectural Overview: Robin Hood Hashing

`std::map` is typically implemented as a Red-Black tree. Every time you insert a new key-value pair, it executes a tiny, isolated `malloc()` to create a tree node. Inserting 500 items into a `std::map` creates 500 tiny memory fragments scattered randomly across the ESP32's SRAM.

`Xi::Map` resolves this via an open-addressing approach combined with "Robin Hood" collision resolution:

1. **Single Allocation:** The Map stores its entries in a perfectly contiguous internal array (`MapEntry<K, V>`).
2. **Deterministic Lookups:** When hash collisions occur, Robin Hood hashing steals from "rich" entries (those close to their ideal bucket) to give to "poor" entries (those far from their ideal bucket). This guarantees exceptionally tight upper bounds on lookup times.
3. **Cache-Friendly:** Iterating over the map is a literal linear scan over contiguous memory, resulting in perfect CPU cache hits.
4. **1:1 STL Drop-In Compatibility:** The map exposes standard `.size()` and C++11 range-based `Iterator` objects (`begin()`, `end()`). You can drop `Xi::Map` into almost any Linux/Windows codebase in place of `std::unordered_map` and standard loop paradigms will function identically with zero headaches.

---

## 📖 Complete API Reference

### 1. Memory & Structure

- `usz size()` / `usz length()`
  Returns the number of active key-value pairs currently stored in the map.
- `void clear()`
  Erases all entries, invoking destructors on keys and values, but **retains the underlying bucket capacity** to prevent future re-allocations.
- `void resize(usz newCap)`
  Forcefully resizes the hash buckets and re-hashes all active entries.

### 2. Insertion & Retrieval

- `void put(K key, V val)` / `void set(const K &key, const V &val)`
  Inserts a new key-value pair or updates an existing one. Automatically handles Robin Hood displacement if a collision occurs.
- `V* get(const K &key)`
  Attempts to find the key. Returns a raw pointer to the value inside the Map's memory block, or `nullptr` if it doesn't exist. This allows **in-place mutation** without expensive copy constructors.
- `bool has(const K &key)`
  Fast boolean check. Equivalent to `get(key) != nullptr`.
- `bool remove(const K &key)`
  Deletes the key-value pair and executes backward-shifting to heal the Robin Hood displacement chain. Returns `true` if an item was successfully deleted.

### 3. Iterators

The map provides `Xi::Map::Iterator` structures that skip empty hash buckets automatically, yielding references to `Xi::MapEntry<K, V>`.

- `Iterator begin()` / `Iterator end()`
  Enables C++11 range-based iterations.

  **MapEntry Fields:**
  Each iterator yields a `MapEntry<K,V>` which exposes:
  - `.key` (The Key)
  - `.value` (The Value)
  - `.fnvHash` (The cached FNV-1a hash to speed up rehashing)

---

## 💻 Example: In-Place Mutation & Iteration

```cpp
#include "Xi/Map.hpp"
#include "Xi/String.hpp"

Xi::Map<uint16_t, Xi::String> config;

void setup() {
  // Insert keys. The Robin Hood hasher organizes these
  // automatically inside a contiguous memory buffer.
  config.put(8888, Xi::String("Server"));
  config.put(8889, Xi::String("ClientProxy"));

  // Direct Pointer Mutation (Zero Copies)
  // We manipulate the String memory directly inside the hash table!
  Xi::String* rule = config.get(8888);
  if(rule) {
    rule->concat("_Active");
  }

  // High-performance contiguous iteration
  // The Iterator skips over empty hash buckets automatically.
  for(auto& entry : config) {
    Serial.printf("Port %d routes to %s\n", entry.key, entry.value.c_str());
  }
}
```
