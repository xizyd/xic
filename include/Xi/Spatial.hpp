#ifndef XI_SPATIAL_HPP
#define XI_SPATIAL_HPP

#include "Log.hpp"
#include "Primitives.hpp"
#include <math.h>

#if defined(ARDUINO)
#include <SPI.h>
#include <Wire.h>
#endif

namespace Xi {
// Constants
static constexpr float SP_SEA_LEVEL = 1013.25f;
static constexpr float SP_RAD_TO_DEG = 57.2957795f;
static constexpr float SP_DEG_TO_RAD = 0.01745329f;

class Spatial {
public:
  // Singleton
  static Spatial &getInstance() {
    static Spatial instance;
    return instance;
  }

  // --- Outputs ---
  // Orientation (Euler Angles)
  float roll = 0, pitch = 0, yaw = 0;

  // Sensor Data
  float gyroX = 0, gyroY = 0, gyroZ = 0;    // deg/s
  float accelX = 0, accelY = 0, accelZ = 0; // g
  float magX = 0, magY = 0, magZ = 0;       // uT

  float temp = 0.0f;         // Celsius
  float humidity = 0.0f;     // %
  float pressure = 1013.25f; // hPa
  float altitude = 0.0f;     // Meters

  // Geolocation
  double lat = 0.0, lng = 0.0;
  int gmtOffset = 0; // Hours
  bool hasFix = false;

  // Status
  bool hasMPU = false;
  bool hasBaro = false;
  bool hasMag = false;
  bool hasGPS = false;

  // --- Configuration ---
  void setMPUI2C(u8 addr = 0x68) {
#if defined(ARDUINO)
    // simplified logic
    mpuAddr = addr;
    hasMPU = true; // Assume success for now or add probe
    initMPU();
#endif
  }

  void setPPS(int pin) {
#if defined(ARDUINO)
    ppsPin = pin;
    pinMode(ppsPin, INPUT);
    attachInterrupt(digitalPinToInterrupt(ppsPin), ppsHandler, RISING);
    hasPPS = true;
#endif
  }

  void setGMT(int offset) { gmtOffset = offset; }
  int getGMT() const { return gmtOffset; }

  // Auto-calculate GMT from Longitude if not manually set
  void autoGMT() {
    if (hasFix && gmtOffset == 0) {
      gmtOffset = round(lng / 15.0);
    }
  }

  // --- Update ---
  void update() {
    i64 now = Xi::millis();

    // Linux/System Integration
#if !defined(ARDUINO) && !defined(ESP_PLATFORM)
    // Read system time/loc if needed (simplified)
    // In a real scenario, we'd read /etc/localtime or similar
    // For now, let's assume system clock Is source of truth
    // So we don't need to do much unless we want to pull coords from a service
#endif

    if (now - lastUpdate < 10)
      return; // 100Hz max

    // Time delta calculation
    float dt = (now - lastUpdate) / 1000.0f;
    lastUpdate = now;

    readSensors();

    // Madgwick filter step
    madgwickUpdate(dt);

    // Compute Euler angles
    computeAngles();

    // GMT Auto-update
    autoGMT();
  }

  // Static Interrupt Handler
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  static void IRAM_ATTR ppsHandler() {
    Xi::Time::syncPPS();
    // We can't log here easily as it's ISR
  }
#endif

private:
  Spatial() {
    beta = 0.1f; // Filter gain
    q0 = 1.0f;
    q1 = 0.0f;
    q2 = 0.0f;
    q3 = 0.0f;
  }

  // Madgwick State
  float beta;
  float q0, q1, q2, q3;

  i64 lastUpdate = 0;

  // Hardware State
  u8 mpuAddr = 0x68;
  int ppsPin = -1;
  bool hasPPS = false;
  int lastPPS = 0;

  void readSensors() {
#if defined(ARDUINO)
    if (hasMPU) {
      // Dummy read implementation
      // Real impl involves Wire.requestFrom...
    }
#endif
  }

  // Madgwick Algorithm (Ported from dev/Spatial.h)
  void madgwickUpdate(float dt) {
    float ax = accelX, ay = accelY, az = accelZ;
    float gx = gyroX * SP_DEG_TO_RAD, gy = gyroY * SP_DEG_TO_RAD,
          gz = gyroZ * SP_DEG_TO_RAD;
    float mx = magX, my = magY, mz = magZ;

    float recipNorm, s0, s1, s2, s3, qDot1, qDot2, qDot3, qDot4, hx, hy, _2bx,
        _2bz, _4bx, _4bz, _2q0mx, _2q0my, _2q0mz, _2q1mx, _2q0, _2q1, _2q2,
        _2q3, _4q0, _4q1, _4q2, _8q1, _8q2, _2q0q2, _2q2q3, q0q0, q0q1, q0q2,
        q0q3, q1q1, q1q2, q1q3, q2q2, q2q3, q3q3;

    // Rate of change of quaternion from gyroscope
    qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
    qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
    qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
    qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

    // Compute feedback only if accelerometer measurement valid (avoids NaN in
    // accelerometer normalisation)
    if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
      // Normalise accelerometer measurement
      recipNorm = 1.0f / sqrt(ax * ax + ay * ay + az * az);
      ax *= recipNorm;
      ay *= recipNorm;
      az *= recipNorm;

      // Auxiliary variables to avoid repeated arithmetic
      _2q0 = 2.0f * q0;
      _2q1 = 2.0f * q1;
      _2q2 = 2.0f * q2;
      _2q3 = 2.0f * q3;
      _4q0 = 4.0f * q0;
      _4q1 = 4.0f * q1;
      _4q2 = 4.0f * q2;
      _8q1 = 8.0f * q1;
      _8q2 = 8.0f * q2;
      _2q0q2 = 2.0f * q0 * q2;
      _2q2q3 = 2.0f * q2 * q3;
      q0q0 = q0 * q0;
      q0q1 = q0 * q1;
      q0q2 = q0 * q2;
      q0q3 = q0 * q3;
      q1q1 = q1 * q1;
      q1q2 = q1 * q2;
      q1q3 = q1 * q3;
      q2q2 = q2 * q2;
      q2q3 = q2 * q3;
      q3q3 = q3 * q3;

      // Reference direction of Earth's magnetic field
      // If magnetometer measurement is valid
      if (!((mx == 0.0f) && (my == 0.0f) && (mz == 0.0f))) {
        recipNorm = 1.0f / sqrt(mx * mx + my * my + mz * mz);
        mx *= recipNorm;
        my *= recipNorm;
        mz *= recipNorm;

        // Reference direction of Earth's magnetic field
        hx = mx * q0q0 - _2q0my * q3 + _2q0mz * q2 + mx * q1q1 +
             _2q1 * my * q2 + _2q1 * mz * q3 - mx * q2q2 - mx * q3q3;
        hy = _2q0mx * q3 + my * q0q0 - _2q0mz * q1 + _2q1mx * q2 - my * q1q1 +
             my * q2q2 + _2q2 * mz * q3 - my * q3q3;
        _2bx = sqrt(hx * hx + hy * hy);
        _2bz = -_2q0mx * q2 + _2q0my * q1 + mz * q0q0 + _2q1mx * q3 -
               mz * q1q1 + _2q2 * my * q3 - mz * q2q2 + mz * q3q3;
        _4bx = 2.0f * _2bx;
        _4bz = 2.0f * _2bz;

        // Gradient decent
        s0 = -_2q2 * (2.0f * q1q3 - _2q0q2 - ax) +
             _2q1 * (2.0f * q0q1 + _2q2q3 - ay) -
             _2bz * q2 *
                 (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
             (-_2bx * q3 + _2bz * q1) *
                 (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
             _2bx * q2 *
                 (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        s1 = _2q3 * (2.0f * q1q3 - _2q0q2 - ax) +
             _2q0 * (2.0f * q0q1 + _2q2q3 - ay) -
             4.0f * q1 * (1 - 2.0f * q1q1 - 2.0f * q2q2 - az) +
             _2bz * q3 *
                 (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
             (_2bx * q2 + _2bz * q0) *
                 (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
             (_2bx * q3 - _4bz * q1) *
                 (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        s2 = -_2q0 * (2.0f * q1q3 - _2q0q2 - ax) +
             _2q3 * (2.0f * q0q1 + _2q2q3 - ay) -
             4.0f * q2 * (1 - 2.0f * q1q1 - 2.0f * q2q2 - az) +
             (-_4bx * q2 - _2bz * q0) *
                 (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
             (_2bx * q1 + _2bz * q3) *
                 (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
             (_2bx * q0 - _4bz * q2) *
                 (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
        s3 = _2q1 * (2.0f * q1q3 - _2q0q2 - ax) +
             _2q2 * (2.0f * q0q1 + _2q2q3 - ay) +
             (-_4bx * q3 + _2bz * q1) *
                 (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
             (-_2bx * q0 + _2bz * q2) *
                 (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
             _2bx * q1 *
                 (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
      } else {
        // IMU Algorithm (No Magnetometer)
        s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
        s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 +
             _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
        s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 +
             _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
        s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
      }

      recipNorm = 1.0f / sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
      s0 *= recipNorm;
      s1 *= recipNorm;
      s2 *= recipNorm;
      s3 *= recipNorm;
      qDot1 -= beta * s0;
      qDot2 -= beta * s1;
      qDot3 -= beta * s2;
      qDot4 -= beta * s3;
    }

    q0 += qDot1 * dt;
    q1 += qDot2 * dt;
    q2 += qDot3 * dt;
    q3 += qDot4 * dt;
    recipNorm = 1.0f / sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 *= recipNorm;
    q1 *= recipNorm;
    q2 *= recipNorm;
    q3 *= recipNorm;
  }

  void computeAngles() {
    // Roll (x-axis rotation)
    float sinr_cosp = 2 * (q0 * q1 + q2 * q3);
    float cosr_cosp = 1 - 2 * (q1 * q1 + q2 * q2);
    roll = atan2(sinr_cosp, cosr_cosp) * SP_RAD_TO_DEG;

    // Pitch (y-axis rotation)
    float sinp = 2 * (q0 * q2 - q3 * q1);
    if (abs(sinp) >= 1)
      pitch = copysign(90.0f, sinp); // use 90 degrees if out of range
    else
      pitch = asin(sinp) * SP_RAD_TO_DEG;

    // Yaw (z-axis rotation)
    float siny_cosp = 2 * (q0 * q3 + q1 * q2);
    float cosy_cosp = 1 - 2 * (q2 * q2 + q3 * q3);
    yaw = atan2(siny_cosp, cosy_cosp) * SP_RAD_TO_DEG;
  }

  /* checkPPS removed, using Interrupts */

  void initMPU() {
#if defined(ARDUINO)
    Wire.begin();
    // Wake up MPU
    Wire.beginTransmission(mpuAddr);
    Wire.write(0x6B);
    Wire.write(0);
    Wire.endTransmission();
#endif
  }
};

static Spatial &space = Spatial::getInstance();

// Global Implementation
inline i64 epochMicros() { return Xi::epochMicros(); }

inline int getGMT() { return space.getGMT(); }

} // namespace Xi

#endif // XI_SPATIAL_HPP
