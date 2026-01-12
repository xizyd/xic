#ifndef XI_RANDOM
#define XI_RANDOM

#include "Primitives.hpp"

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>

namespace Xi
{
    alignas(64) static u32 _randomPool[20] = {
        123456789, 362436069, 521288629, 88675123,     // First 4 for XorShift
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 // Rest for Crypto
    };
    static bool _randomInitialized = false;
    static u32 _secureCounter = 0;

    // --- Fast Engine (Uses only words 0-3) ---
    inline u32 randomNext()
    {
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

    // Manual seed: Spreads the seed across the whole 20-word pool
    void randomSeed(u32 s)
    {
        for (int i = 0; i < 20; i++)
        {
            // Using a Knuth LCG-style multiplier to spread entropy
            s = 1812433253U * (s ^ (s >> 30)) + i;
            _randomPool[i] = s;
        }
        // Warm up the fast engine
        for (int i = 0; i < 10; i++)
            randomNext();
    }

    // System seed: Fills all 80 bytes from /dev/urandom at once
    void randomSeed()
    {
#if defined(__linux__) || defined(__APPLE__)
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd >= 0)
        {
            read(fd, _randomPool, sizeof(_randomPool));
            close(fd);
        }
#elif defined(ESP_PLATFORM)
        for (int i = 0; i < 20; i++)
            _randomPool[i] = esp_random();
#else
        randomSeed(987654321);
#endif

#if defined(__linux__)
        madvise(_randomPool, sizeof(_randomPool), MADV_WIPEONFORK);
#endif
    }

    inline u32 random(u32 max)
    {
        if (max == 0)
            return 0;
        return randomNext() % max;
    }

    inline i32 random(i32 min, i32 max)
    {
        if (min >= max)
            return min;
        return min + (i32)(randomNext() % (u32)(max - min));
    }

    inline f32 randomFloat()
    {
        return (f32)randomNext() / 4294967295.0f; // 2^32 - 1
    }

    void randomFill(u8 *buffer, usz size)
    {
        usz i = 0;
        // Optimization: Fill 4 bytes at a time using the CPU's native word size
        while (i + 4 <= size)
        {
            u32 r = randomNext();
            memcpy(buffer + i, &r, 4);
            i += 4;
        }
        // Fill remaining bytes (1, 2, or 3 bytes)
        if (i < size)
        {
            u32 r = randomNext();
            while (i < size)
            {
                buffer[i++] = (u8)(r & 0xFF);
                r >>= 8;
            }
        }
    }
}

#endif