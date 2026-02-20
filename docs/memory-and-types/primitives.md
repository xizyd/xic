# Primitives

`Xi` abandons platform-dependent standard C types like `int` or `long` in favor of mathematically guaranteed bit-widths.

## Why it Shines on ESP32/Arduino

When you write `unsigned long` on an Arduino Nano (AVR 8-bit), it compiles to 32 bits. When you write `unsigned long` on a Raspberry Pi 4 (AArch64), it compiles to 64 bits.

If you are serializing network packets from a sensor to a server using the `xic` library, using standard C types will result in corrupt networking headers when crossing architectures. `Xi::Primitives` fixes this.

## The Types

Include `Xi/Primitives.hpp` to access these global aliases.

### Fixed-Width Integers

These guarantee the exact same physical byte footprint on an ESP32 as they do on an x86 server:

- `u8` -> Unsigned 8-bit Integer (`uint8_t`)
- `u16` -> Unsigned 16-bit Integer (`uint16_t`)
- `u32` -> Unsigned 32-bit Integer (`uint32_t`)
- `u64` -> Unsigned 64-bit Integer (`uint64_t`)
- `i8`, `i16`, `i32`, `i64` -> Signed variations

### The Platform Size `usz`

- `usz` -> The equivalent of `size_t` or `uintptr_t`.

`usz` defines the native pointer width of the architecture. On an ESP32, `usz` is exactly 32 bits (4 bytes). On an AWS EC2 instance, `usz` is exactly 64 bits (8 bytes). It is critical to use `usz` for memory offsets, bounds checking, and pointer arithmetic to prevent overflow. `Xi` explicitly checks sizing bounds to silence `-Wshift-count-overflow` warnings when manipulating hashes differently.
