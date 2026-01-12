// include/Xi/Primitives.hpp

#ifndef XI_PRIMITIVES
#define XI_PRIMITIVES

#if defined(__has_include)
#if __has_include(<new>)
#include <new>
#define __PLACEMENT_NEW_INLINE
#endif
#elif defined(__cplusplus) && __cplusplus >= 201103L
#include <new>
#define __PLACEMENT_NEW_INLINE
#endif

namespace Xi
{
    using usz = decltype(sizeof(0));

    using u8 = unsigned char;
    using i8 = signed char;

// Auto-detect integer sizes for 16/32 bit types
#if __SIZEOF_INT__ == 2
    using u16 = unsigned int;
    using i16 = int;
    using u32 = unsigned long;
    using i32 = long;
#else
    using u16 = unsigned short;
    using i16 = short;
    using u32 = unsigned int;
    using i32 = int;
#endif

    using u64 = unsigned long long;
    using i64 = long long;

    using f32 = float;
    using f64 = double;

    static constexpr decltype(nullptr) null = nullptr;

    // -------------------------------------------------------------------------
    // Metaprogramming Utilities
    // -------------------------------------------------------------------------
    template <typename T>
    struct RemoveRef
    {
        using Type = T;
    };
    template <typename T>
    struct RemoveRef<T &>
    {
        using Type = T;
    };
    template <typename T>
    struct RemoveRef<T &&>
    {
        using Type = T;
    };

    template <typename T>
    inline typename RemoveRef<T>::Type &&Move(T &&arg)
    {
        return static_cast<typename RemoveRef<T>::Type &&>(arg);
    }

    template <typename T>
    inline void Swap(T &a, T &b)
    {
        T temp = Xi::Move(a);
        a = Xi::Move(b);
        b = Xi::Move(temp);
    }

    template <typename U, typename V>
    struct IsSame
    {
        static const bool Value = false;
    };
    template <typename U>
    struct IsSame<U, U>
    {
        static const bool Value = true;
    };

    template <typename T>
    struct Equal
    {
        static bool eq(const T &a, const T &b) { return a == b; }
    };
    template <>
    struct Equal<const char *>
    {
        static bool eq(const char *a, const char *b)
        {
            if (a == b)
                return true;
            if (!a || !b)
                return false;
            while (*a && *b)
            {
                if (*a != *b)
                    return false;
                a++;
                b++;
            }
            return *a == *b;
        }
    };

    // --- Constantes ---
    static constexpr f64 PI = 3.14159265358979323846;
    static constexpr f64 E = 2.71828182845904523536;

#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL

    template <typename T>
    struct FNVHasher
    {
        static usz fnvHash(const T &key)
        {
            const char *ptr = (const char *)&key;
            usz fnvHash = FNV_OFFSET;
            for (usz i = 0; i < sizeof(T); ++i)
            {
                fnvHash ^= (usz)ptr[i];
                fnvHash *= FNV_PRIME;
            }
            return fnvHash;
        }
    };

    // Specialization for raw pointers (Murmur3 Mixer)
    template <typename T>
    struct FNVHasher<T *>
    {
        static usz fnvHash(T *key)
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

    static inline usz fnvHash64(usz k)
    {
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        k *= 0xc4ceb9fe1a85ec53ULL;
        k ^= k >> 33;
        return k;
    };

    template <>
    struct FNVHasher<u32>
    {
        static usz fnvHash(const u32 &k) { return fnvHash64((usz)k); }
    };

    template <>
    struct FNVHasher<int>
    {
        static usz fnvHash(const int &k) { return fnvHash64((usz)k); }
    };

    template <>
    struct FNVHasher<u64>
    {
        static usz fnvHash(const u64 &k) { return fnvHash64((usz)k); }
    };

    template <>
    struct FNVHasher<const char *>
    {
        static usz fnvHash(const char *key)
        {
            usz fnvHash = FNV_OFFSET;
            while (*key)
            {
                fnvHash ^= (usz)(*key++);
                fnvHash *= FNV_PRIME;
            }
            return fnvHash;
        }
    };

}

#ifndef __PLACEMENT_NEW_INLINE
#define __PLACEMENT_NEW_INLINE
inline void *operator new(decltype(sizeof(0)), void *p) noexcept { return p; }
inline void operator delete(void *, void *) noexcept {}
#endif

#endif // XI_PRIMITIVES