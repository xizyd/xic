// src/Xi/Constants.hpp

#ifndef XI_CONSTANTS
#define XI_CONSTANTS

namespace Xi
{
    using usz = decltype(sizeof(0));
    
    using u8  = unsigned char;
    using i8   = signed char;
    
    // Auto-detect integer sizes for 16/32 bit types
    #if __SIZEOF_INT__ == 2
        using u16 = unsigned int;
        using i16  = int;
        using u32 = unsigned long;
        using i32  = long;
    #else
        using u16 = unsigned short;
        using i16  = short;
        using u32 = unsigned int;
        using i32  = int;
    #endif

    using u64 = unsigned long long;
    using i64  = long long;

    using f32 = float;
    using f64 = double;

    static constexpr decltype(nullptr) null = nullptr;

    #define FNV_OFFSET 14695981039346656037ULL
    #define FNV_PRIME 1099511628211ULL   
}

#ifndef __PLACEMENT_NEW_INLINE
#define __PLACEMENT_NEW_INLINE
    inline void *operator new(decltype(sizeof(0)), void *p) noexcept { return p; }
    inline void operator delete(void *, void *) noexcept {}
#endif

#endif // XI_CONSTANTS