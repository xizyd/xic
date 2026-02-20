# Time

`Xi::Time` is a high-performance timestamp and calendar toolkit. It is the embedded-safe alternative to standard C++ `<chrono>` or the archaic `<ctime>` POSIX headers.

## Architectural Overview: High-Performance Civil Time

Parsing and manipulating dates on an embedded system usually involves slow POSIX `mktime` functions, hidden dynamic allocations for strings, or reliance on heavy Real-Time Clock (RTC) libraries.

`Xi::Time` relies exclusively on heavily optimized, inline bit-math for converting between UNIX Epoch microseconds and Civil formats (Year, Month, Day, Hour, Minute).

### Core Features

1. **Epoch Centralized:** All timestamps internally resolve down to a 64-bit signed integer (`i64 us`), representing microseconds since the UNIX Epoch (`1970-01-01T00:00:00Z`).
2. **Auto-Dynamic Parsing:** The `Xi::Time` constructor accepts advanced format strings. It parses date formats like `"2025-05-12T14:30.00"` directly from raw payloads, with zero dynamic memory allocation.
3. **Hardware Abstraction:** Through `Xi::epochMicros()`, `Xi::Time` pulls the highest resolution monotonic tick available—whether that is the `clock_gettime()` on Linux, or FreeRTOS timers on ESP32 microcontrollers.

---

## 📖 Complete API Reference

### 1. Global Timestamping

- `i64 Xi::epochMicros()`
  Returns the universally synced epoch in microseconds since 1970.
- `int Xi::getGMT()`
  Returns the globally tracked Timezone offset in hours (typically orchestrated via `Xi::Spatial`).
- `void Xi::Time::sleep(double seconds)`
  Suspends the current execution thread safely without busy-waiting. Uses `vTaskDelay` on ESP32/FreeRTOS, `Sleep` on Windows, and `nanosleep` on Linux.

### 2. Time Synchronization

Microcontrollers drift over time. `Xi::Time` offers built-in hardware synchronization tracking without overwriting low-level system monotonic timers.

- `void syncClock()`
  Automatically polls the host OS (Linux/Windows) real-time clock to calibrate the `Xi` epoch baseline.
- `void syncClock(i64 now)`
  Updates the system offset manually from an external source (like an NTP server or GPS module).
- `void syncPPS()`
  Snaps the current sub-second offset to the exact microsecond perfectly upon a hardware interrupt (Pulse Per Second), establishing atomic-level chronometry.

### 3. Instantiation & Parsing

- `Xi::Time()` / `Xi::Time(i64 unixMicroseconds)`
  Constructs a timestamp. Defaults to "Now".
- `Xi::Time(const Xi::String &date, const Xi::String &fmt)`
  A powerful inline parser. Automatically derives the epoch microseconds from text without allocating strings dynamically.
  _Format Specifiers:_
  - `yyyy`: 4-digit Year
  - `mm`: 2-digit Month or Minute (inferred via proximity to `h`)
  - `dd`: 2-digit Day
  - `hh`: 2-digit Hour
  - `ss`: 2-digit Second
  - `rr`: AM/PM indicator
  - `zz`: Timezone Offset (e.g. `+02:00` or `-05:00`)

### 4. Civil Calendar Properties (C#-Style)

Instead of clumsy getters and setters, `Xi::Time` provides C#-style proxy properties for direct syntax manipulation:

- `t.year = 2025;`
- `int y = t.year;`
- `t.month = 5;`
- `t.day = 12; // (Day in month)`
- `t.hour = 14;`
- `t.minute = 30;`
- `t.second = 0;`

These proxies instantly convert bounds (e.g., adding `90` to `.minute` correctly rolls over `.hour` and recalculates the microsecond epoch inline).

### 5. Advanced Component Output

- `int dayInYear()`
  Returns the day of the year (0-365).
- `Xi::String toString(const Xi::String &fmt = "yyyy/mm/dd hh:mm:ss", int targetTzHours = 0)`
  Safely interpolates the timestamp back into a human-readable `Xi::String` applying the requested `targetTzHours` offset.

---

## 💻 Example: Parsing and Calculating Rollovers

```cpp
#include "Xi/Time.hpp"
#include "Xi/Log.hpp"

void handleAppointment() {
  // 1. Synchronize the system clock (especially on embedded systems)
  Xi::Time::syncClock();

  // 2. Parse an ISO8601 string instantly without `malloc()`
  Xi::Time appointment("2026-12-31T23:45:00", "yyyy-mm-ddThh:mm:ss");

  // Output: Day 365 of Year 2026
  Xi::println(appointment.dayInYear());

  // 3. Using the magic proxy properties to add exactly 30 minutes!
  // This seamlessly rolls the clock over a Midnight edge AND a Year edge!
  appointment.minute += 30;

  // The timestamp physically reconstructs its Epoch underneath automatically.
  // Output: Year 2027, Month 1, Day 1, Hour 0, Minute 15.
  Xi::println(appointment.year);  // 2027

  // 4. Format Output with a Timezone Shift (+2 GMT)
  Xi::String finalOutput = appointment.toString("yyyy/mm/dd hh:mm:ss zz", +2);
  Xi::println(finalOutput);
}
```
