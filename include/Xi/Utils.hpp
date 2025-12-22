// src/Xi/Utils.hpp

#ifndef XI_UTILS_HPP
#define XI_UTILS_HPP 1

#include "Constants.hpp"

#if defined(ARDUINO)
    #include <Arduino.h>
#elif defined(ESP_PLATFORM)
    #include <esp_timer.h>
    #include <esp_system.h>
#elif defined(_WIN32)
    // Minimal Windows headers to avoid bloat
    extern "C" {
        __declspec(dllimport) unsigned long __stdcall GetTickCount();
        __declspec(dllimport) int __stdcall QueryPerformanceCounter(long long*);
        __declspec(dllimport) int __stdcall QueryPerformanceFrequency(long long*);
    }
#else 
    // POSIX (Linux/Mac)
    #include <time.h>
    #include <sys/time.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <string.h>
    #include <sys/mman.h>
#endif

namespace Xi
{
    // Forward declare String to use in fillRandom signature
    class String; 

    // -------------------------------------------------------------------------
    // Metaprogramming Utilities
    // -------------------------------------------------------------------------
    template <typename T> struct RemoveRef { using Type = T; };
    template <typename T> struct RemoveRef<T &> { using Type = T; };
    template <typename T> struct RemoveRef<T &&> { using Type = T; };

    template <typename T>
    inline typename RemoveRef<T>::Type &&Move(T &&arg) {
        return static_cast<typename RemoveRef<T>::Type &&>(arg);
    }

    template <typename T>
    inline void Swap(T &a, T &b) {
        T temp = Xi::Move(a);
        a = Xi::Move(b);
        b = Xi::Move(temp);
    }

    template <typename U, typename V> struct IsSame { static const bool Value = false; };
    template <typename U> struct IsSame<U, U> { static const bool Value = true; };

    template <typename T> struct Equal { static bool eq(const T &a, const T &b) { return a == b; } };
    template <> struct Equal<const char *> {
        static bool eq(const char *a, const char *b) {
            if (a == b) return true;
            if (!a || !b) return false;
            while (*a && *b) { if (*a != *b) return false; a++; b++; }
            return *a == *b;
        }
    };

    // -------------------------------------------------------------------------
    // Memory Utils
    // -------------------------------------------------------------------------
    inline void MemCopy(void *dest, const void *src, usz bytes) {
        #if defined(__GNUC__) || defined(__clang__)
            __builtin_memcpy(dest, src, bytes);
        #else
            char* d = (char*)dest; const char* s = (const char*)src;
            while(bytes--) *d++ = *s++;
        #endif
    }

    inline void MemMove(void *dest, const void *src, usz bytes) {
        char *d = (char *)dest; const char *s = (const char *)src;
        if (d < s) {
            for (usz i = 0; i < bytes; i++) d[i] = s[i];
        } else if (d > s) {
            for (usz i = bytes; i > 0; i--) d[i - 1] = s[i - 1];
        }
    }

    // -------------------------------------------------------------------------
    // Time Functions (Platform Agnostic)
    // -------------------------------------------------------------------------
    
    inline u64 millis() {
        #if defined(ARDUINO)
            return ::millis();
        #elif defined(ESP_PLATFORM)
            return esp_timer_get_time() / 1000ULL;
        #elif defined(_WIN32)
            return ::GetTickCount();
        #else
            // POSIX
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return (u64)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
        #endif
    }

    inline u64 micros() {
        #if defined(ARDUINO)
            return ::micros();
        #elif defined(ESP_PLATFORM)
            return esp_timer_get_time();
        #elif defined(_WIN32)
            static long long freq = 0;
            if (freq == 0) ::QueryPerformanceFrequency(&freq);
            long long counter;
            ::QueryPerformanceCounter(&counter);
            return (u64)(counter * 1000000 / freq);
        #else
            // POSIX
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return (u64)(ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
        #endif
    }

    u64 systemStartMicros = 0;

    void syncClock(u64 now) {
        u64 uptimeMicros = micros();
        systemStartMicros = now - uptimeMicros;
    }

    void syncClock() {
        #if defined(_WIN32)
            // Windows FileTime is 100ns intervals since Jan 1, 1601
            // Epoch (1970) offset is 116444736000000000 * 100ns
            unsigned long long ft;
            ::GetSystemTimePreciseAsFileTime(&ft);
            syncClock((ft - 116444736000000000ULL) / 10ULL);
        #else
            // Modern Linux/POSIX approach
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            syncClock((u64)ts.tv_sec * 1000000ULL + (u64)(ts.tv_nsec / 1000));
        #endif
    }

    // Wall Clock (Since Jan 1 1970) - Used for Time/Date
    // On embedded platform, remember to `syncClock(x)`
    inline u64 epochMicros() {
        if(systemStartMicros == 0) {
            syncClock();
        }

        return micros() + systemStartMicros; 
    }

    // -------------------------------------------------------------------------
    // Randomness
    // -------------------------------------------------------------------------
    
    alignas(64) static u32 _randomPool[20] = {
        123456789, 362436069, 521288629, 88675123, // First 4 for XorShift
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 // Rest for Crypto
    };
    static bool _randomInitialized = false;
    static u32 _secureCounter = 0;

    // --- Fast Engine (Uses only words 0-3) ---
    inline u32 randomNext() {
        u32 t = _randomPool[3];
        u32 s = _randomPool[0];
        _randomPool[3] = _randomPool[2];
        _randomPool[2] = _randomPool[1];
        _randomPool[1] = s;
        t ^= t << 11;
        t ^= t >> 8;
        _randomPool[0] = t ^ s ^ (s >> 19);
        return _randomPool[0];
    }

    // --- Seeding ---

    // Manual seed: Spreads the seed across the whole 20-word pool
    void randomSeed(u32 s) {
        for (int i = 0; i < 20; i++) {
            // Using a Knuth LCG-style multiplier to spread entropy
            s = 1812433253U * (s ^ (s >> 30)) + i;
            _randomPool[i] = s;
        }
        // Warm up the fast engine
        for (int i = 0; i < 10; i++) randomNext();
    }

    // System seed: Fills all 80 bytes from /dev/urandom at once
    void randomSeed() {
        #if defined(__linux__) || defined(__APPLE__)
            int fd = open("/dev/urandom", O_RDONLY);
            if (fd >= 0) {
                read(fd, _randomPool, sizeof(_randomPool));
                close(fd);
            }
        #elif defined(ESP_PLATFORM)
            for(int i = 0; i < 20; i++) _randomPool[i] = esp_random();
        #else
            randomSeed(987654321); 
        #endif

        #if defined(__linux__)
            madvise(_randomPool, sizeof(_randomPool), MADV_WIPEONFORK);
        #endif
    }


    // --- Utilities ---

    inline u32 random(u32 max) {
        if (max == 0) return 0;
        return randomNext() % max;
    }

    inline i32 random(i32 min, i32 max) {
        if (min >= max) return min;
        return min + (i32)(randomNext() % (u32)(max - min));
    }

    inline f32 randomFloat() {
        return (f32)randomNext() / 4294967295.0f; // 2^32 - 1
    }

    void randomFill(u8 *buffer, usz size) {
        usz i = 0;
        // Optimization: Fill 4 bytes at a time using the CPU's native word size
        while (i + 4 <= size) {
            u32 r = randomNext();
            memcpy(buffer + i, &r, 4);
            i += 4;
        }
        // Fill remaining bytes (1, 2, or 3 bytes)
        if (i < size) {
            u32 r = randomNext();
            while (i < size) {
                buffer[i++] = (u8)(r & 0xFF);
                r >>= 8;
            }
        }
    }
}

#endif // XI_UTILS_HPP