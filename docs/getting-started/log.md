# Log

`Xi::Log` is a unified, cross-platform diagnostic logging utility. It is designed to provide identical macro-like simplicity across completely disparate hardware targets, routing output seamlessly to the hardware's native console.

## Architectural Overview: Hardware Abstraction

On an ESP32 or Arduino, developers print diagnostics using `Serial.println()`. On a Linux server or desktop environment, developers use `std::cerr` or `std::cout`.

`Xi::Log` completely abstracts this difference. You write your business logic once using `Xi::println()`, and the framework automatically resolves the output:

- **On Microcontrollers (`ARDUINO` defined):** Output is piped directly to the UART hardware `Serial`.
- **On Linux/OSX/Windows (`ARDUINO` undefined):** Output is piped to standard error (`std::cerr`), keeping standard output (`std::cout`) clean for your application's binary pipeline.

---

## 📖 Complete API Reference

### 1. Global Shortcuts

You do not need to instantiate the `Log` class. `Xi` provides global namespace functions for immediate, ergonomic usage that mimic standard output syntax.

- `Xi::print(const T &msg)`
  Prints the message without a trailing newline. Safely formats any primitive or `Xi::String`.
- `Xi::println()`
  Emits a bare newline character.
- `Xi::println(const T &msg)`
  Prints the message followed by a newline.

### 2. Log Leveling

Instead of commenting out `print` statements when moving from debug to release, `Xi::Log` provides dedicated semantic logging levels.

- `Xi::verbose(const T &msg)`
- `Xi::info(const T &msg)`
- `Xi::warn(const T &msg)`
- `Xi::error(const T &msg)`
- `Xi::critical(const T &msg)`

**Setting the Global Level Threshold:**
You can globally silence logs below a certain importance using the Singleton configuration.

- `Xi::Log::getInstance().setLevel(Xi::LogLevel l)`
  Levels include: `Verbose`, `Info`, `Warning`, `Error`, `Critical`, and `None`. Any log emitted below the active threshold is completely ignored.

---

## 💻 Example: Cross-Platform Logging

```cpp
#include "Xi/Log.hpp"
#include "Xi/String.hpp"

void setup() {
  // Only show Warnings, Errors, and Criticals
  Xi::Log::getInstance().setLevel(Xi::LogLevel::Warning);

  Xi::String packet = "Incoming Data: 0xFF";

  // This will be SILENT because the global level is Warning
  Xi::info("System booted successfully.");

  // This will PRINT to either Unix std::cerr or ESP Serial
  Xi::warn("Packet malformed!");

  // Directly print custom formatted objects
  Xi::println(packet.c_str());
}
```
