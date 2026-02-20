# Getting Started

Welcome to the documentation for the `Xi` Core Primitives and the `Rho` Networking Protocol. These libraries form the foundational memory, data structures, and mesh-networking architecture of the `xic` project.

Use the sidebar to explore how the architecture differs from the standard C++ library and how it provides extreme-performance routing for AAA games while specifically targeting embedded microcontrollers like the ESP32 and Arduino families.

---

## 🚀 Getting Started with Xi

If you are new to the `xic` framework, we recommend starting by understanding the core principles of extreme zero-allocation performance:

1. **[Architecture & Philosophy](architecture.md)**: Read about why `Xi` completely abandons the C++ Standard Template Library (`std::`).
2. **[Memory & Data Types](../3-memory-and-types/README.md)**: Discover how Strings, Arrays, and Paths are deterministically managed without heap fragmentation.
3. **[Rho Networking](../2-rho/README.md)**: Explore the modern, hardware-agnostic replacement for TCP, TLS, and WebSockets.

---

## 🤝 Community & Support

Have questions, looking for architectural guidance, or want to showcase a project you built using `Rho` or the `Xi` core primitives?

Join our official community on Discord! We are highly active and eager to help developers build extreme-performance software.

👉 **[Join the xic Discord Server](https://discord.gg/s7Rg4DHuej)**

---

## 💻 Contribution is Welcome!

`xic` is actively being developed, and community contributions are highly encouraged!

Whether you are fixing typos in this documentation, writing cross-platform wrappers for `Xi::FS` in `File.hpp`, or optimizing the Poly1305 hashing routines in `Xi::Crypto`, your pull requests are welcome.

### How to Contribute

1. Join the Discord to discuss major architectural changes before spending hours coding them.
2. Fork the primary repository.
3. Ensure your code compiles cleanly across both Unix/Linux targets and ESP-IDF embedded targets.
4. Submit a Pull Request with a clear description of the zero-allocation approach you took.
