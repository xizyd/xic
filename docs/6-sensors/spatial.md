# Spatial

`Xi::Spatial` is an embedded-first real-time 3D orientation tracker and IMU (Inertial Measurement Unit) fusion engine.

## Architectural Overview: Madgwick Sensor Fusion

When calculating the 3D orientation (Pitch, Roll, Yaw) of a drone or IoT device, most systems rely on simple Complementary Filters or computationally exorbitant Kalman Filters.
`Xi::Spatial` provides an ultra-fast, floating-point optimized implementation of the **Madgwick AHRS (Attitude and Heading Reference System)** filter.

By aggressively processing raw Accelerometer, Gyroscope, and Magnetometer streams through an algorithmic gradient descent, `Xi::Spatial` resolves a perfectly stable quaternion—completely immune to the notorious Gimbal Lock problem inherent in Euler angle math—while maintaining execution times well within a 1kHz embedded RTOS interrupt loop.

### Core Features

1. **Singleton Architecture:** The Spatial engine acts as a unified `Xi::Spatial::getInstance()` block, holding the entire physical state of the device in memory for instant querying.
2. **I2C/SPI Hardware Agnosticism:** While natively pre-configured for the MPU6050 and MPU9250 on Arduino, the `madgwickUpdate()` method accepts purely dimensionless floating-point inputs, making it instantly portable to any proprietary hardware sensors tracking across a Linux/Python pipeline via UART or UDP.
3. **GPS & PPS Synchronization:** Beyond rotation, the `Spatial` module integrates deeply with Pulse-Per-Second (PPS) interrupts to establish coordinate timestamps bound to the Earth's atomic clock.
4. **Environmental Tracking:** Central hub for altitude, humidity, geolocation, and precise timezone logic.

---

## 📖 Complete API Reference

### 1. Orientation & Environmental State

Properties are exposed directly on the `Spatial` singleton for immediate zero-cost access:

#### Orientation (Euler Angles)

- `float roll, pitch, yaw`
  The current 3D Eulerian rotation in degrees (`°`).

#### IMU Sensor Data

- `float ax, ay, az`
  Raw Accelerometer G-forces (`g`).
- `float gx, gy, gz`
  Raw Gyroscope angular velocities (`deg/s`).
- `float mx, my, mz`
  Raw Magnetometer flux density (`uT`).

#### Environmental Data

- `float temp` (Celsius `°C`)
- `float humidity` (`%`)
- `float pressure` (`hPa`)
- `float altitude` (`Meters`)

#### Geolocation

- `double lat, lng`
  Current Latitude and Longitude from an attached GPS.
- `int gmtOffset`
  Calculated or manually enforced timezone offset (in hours).
- `bool hasFix`
  Is the GPS actively tracking satellites.

### 2. Sensor Fusion Engine

- `static Spatial& getInstance()`
  Retrieves the global spatial singleton.
- `void madgwickUpdate(float dt)`
  The core fusion engine. Pass in the delta time (`dt`) since the last loop iteration in seconds. This method automatically derives gradients based on the currently loaded `ax, ay, az, gx, gy, gz` and `mx, my, mz` variables and calculates the updated inner quaternion.
- `void computeAngles()`
  A mathematical utility. It takes the current internal Madgwick quaternion state and immediately projects it out into human-readable `roll`, `pitch`, and `yaw` variables (converted automatically to degrees via `SP_RAD_TO_DEG`).
- `void update()`
  A high-level wrapper. It sequentially calls `readSensors()`, determines `dt`, runs `madgwickUpdate(dt)`, computes the angles, and conditionally auto-updates the GMT offset.

### 3. Hardware Interoperability

- `void setMPUI2C(u8 addr = 0x68)`
  Attaches the internal `Spatial` engine to a specific I2C MPU sensor natively on Arduino variants.
- `void initMPU()`
  Fires the necessary I2C wire commands to wake up the sensor hardware.
- `void readSensors()`
  Polls the I2C bus and populates the acceleration and gyro data.

### 4. Spatiotemporal Configuration

The precise physical coordinate requires synchronous atomic time (`Xi::Time`).

- `void setPPS(int pin)`
  Configures an external Pulse-Per-Second hardware interrupt (commonly from a GPS module) to continuously discipline the internal microsecond clock precisely on the rising edge.
- `void setGMT(int offset)`
  Forces the system timezone offset logic perfectly.
- `int getGMT() const`
  Retrieves the target GMT hours offset.
- `void autoGMT()`
  Automatically calculates the GMT offset mechanically (`round(lng / 15.0)`) based on the current GPS coordinates if `hasFix` is active.

---

## 💻 Example: 500Hz RTOS Sensor Loop

```cpp
#include "Xi/Spatial.hpp"
#include "Xi/Log.hpp"

// We acquire the underlying space reference once
Xi::Spatial& space = Xi::Spatial::getInstance();

void setup() {
  // Bind I2C to the MPU at 0x68
  space.setMPUI2C(0x68);

  // Attach GPS Pulse-Per-Second to pin 15
  space.setPPS(15);
}

void loop() {
  // At 500Hz:
  // 1. Reads the latest raw Gyro/Accel byte streams.
  // 2. Pumps the data through the Madgwick Gradient Descent Filter.
  // 3. Spits out Euler degree measurements.
  space.update();

  // Instantly dump the pitch to the local console or UDP
  Xi::print("Pitch Degrees: ");
  Xi::println(space.pitch);

  // Access global environmental values mapped from other systems
  Xi::print("Altitude: ");
  Xi::println(space.altitude);
}
```
