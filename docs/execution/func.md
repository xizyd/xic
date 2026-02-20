# Func

`Xi::Func<R(Args...)>` is a highly optimized, type-safe function wrapper and lambda state manager. It is the embedded-safe alternative to `std::function`.

## Architectural Overview: Small Buffer Optimization (SBO)

When you define a callback using `std::function` on an ESP32 and capture complex state variables, the C++ compiler typically calls `malloc()` to store that captured state on the heap, instantly fragmenting memory.

`Xi::Func` circumvents this through aggressive **Small Buffer Optimization (SBO)** combined with custom Type Erasure.

1. **The Inline Buffer:** Every `Xi::Func` instance contains a static 32-byte internal memory buffer (`SBO_Size = 32`).
2. **Auto Dynamics \u0026 Type Erasure:** When you assign a generic `Callable` lambda (`auto` closure) or a function pointer to the `Func`, the assignment relies on a generic constructor template (`template <typename Callable> Func(Callable f)`).
3. **Zero-Malloc Callbacks:** If the captured lambda state fits within those 32 bytes (which covers 99% of `[this]`, `[&]`, or primitive captures), it perfectly constructs the object _inside_ the local buffer using Placement New. `malloc` is never called.
4. **VTable Dispatch:** It abstracts the execution behind three inline function pointers (`invoke`, `destroy`, `clone`), providing identical polymorphism to `std::function` without the RTTI bloat.

## 📖 Complete API Reference

### 1. Construction \u0026 Auto Dynamics

- `Func()`
  Constructs an empty function object. Calling `isValid()` returns `false`.
- `Func(R (*f)(Args...))`
  Constructs from a raw piece of code (C-style function pointer). This inherently takes exactly 4 or 8 bytes and perfectly anchors into the SBO buffer.
- `template <typename Callable> Func(Callable f)`
  The core **Auto Dynamic** constructor. It accepts literally anything that respects the `operator()` signature. If `sizeof(Callable) <= 32`, it constructs inline. If it massively exceeds the buffer, it gracefully performs an isolated heap allocation.

### 2. State Transfer \u0026 Rule of 5

- `Func(Func &&o)`
  **Move Semantics:** Transfers ownership of the captured state instantly. If the state was inside the 32-byte buffer, it byte-copies it over. If it was on the heap, it transfers the underlying pointer safely, leaving the origin empty.
- `Func(const Func &o)`
  **Copy Semantics:** Clones the underlying callable state using the internal VTable dispatcher to ensure captured parameters are correctly duplicated.
- `void _clear()`
  Manually triggers the destruction of the captured closure state and frees any heap pointers if SBO was breached.

### 3. Execution

- `bool isValid() const`
  Returns `true` if a valid closure or function pointer has been assigned.
- `R operator()(Args... args) const`
  Executes the underlying callable, unpacking the auto-dynamically captured state seamlessly. If called on an empty `Func`, returns `R()` safely.

---

## 💻 Example: Stack-Safe Callbacks

```cpp
#include "Xi/Func.hpp"
#include "Xi/String.hpp"

// Define a type-erased signature explicitly.
using DataHandler = Xi::Func<int(Xi::String)>;

void listen(DataHandler callback) {
  if(callback.isValid()) {
    callback(Xi::String("PacketFromNetwork"));
  }
}

void setup() {
  int sessionID = 42;
  bool isConnected = true;

  // Creates an anonymous generic lambda.
  // The 'sessionID' and 'isConnected' capture state sums to 5 bytes.
  // 5 <= 32 bytes! This constructs entirely inline within Xi::Func's SBO.
  // ZERO heap allocations occur here.
  listen([sessionID, isConnected](auto packet) -> int {
    if(!isConnected) return 0;
    return sessionID + packet.length();
  });
}
```
