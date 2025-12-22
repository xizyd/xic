#ifndef XI_HASHER
#define XI_HASHER

#include "Utils.hpp"

namespace Xi
{
    template <typename T>
    struct Hasher
    {
        static usz hash(const T &key)
        {
            const char *ptr = (const char *)&key;
            usz hash = FNV_OFFSET;
            for (usz i = 0; i < sizeof(T); ++i)
            {
                hash ^= (usz)ptr[i];
                hash *= FNV_PRIME;
            }
            return hash;
        }
    };

    // Specialization for raw pointers (Murmur3 Mixer)
    template <typename T>
    struct Hasher<T *>
    {
        static usz hash(T *key)
        {
            usz k = (usz)key;
            k ^= k >> 33;
            k *= 0xff51afd7ed558ccdULL;
            k ^= k >> 33;
            k *= 0xc4ceb9fe1a85ec53ULL;
            k ^= k >> 33;
            return k;
        }
    };

    // Specialization for common integers
    template <>
    struct Hasher<int>
    {
        static usz hash(const int &k) { return Hasher<usz *>::hash((usz *)((usz)k)); }
    };
    template <>
    struct Hasher<long>
    {
        static usz hash(const long &k) { return Hasher<usz *>::hash((usz *)((usz)k)); }
    };
    template <>
    struct Hasher<long long>
    {
        static usz hash(const long long &k) { return Hasher<usz *>::hash((usz *)((usz)k)); }
    };

    // Specialization for C-Strings
    template <>
    struct Hasher<const char *>
    {
        static usz hash(const char *key)
        {
            usz hash = FNV_OFFSET;
            while (*key)
            {
                hash ^= (usz)(*key++);
                hash *= FNV_PRIME;
            }
            return hash;
        }
    };
}

#endif