#ifndef XI_MATH_H
#define XI_MATH_H

#include <Xi/Primitives.hpp>
#include <cmath>

namespace Xi {
    namespace Math {
        // --- Fondamentaux ---
        template <typename T>
        inline T sqrt(T value) { return __builtin_sqrt(value); }

        template <typename T>
        inline T pow(T base, T exp) { return __builtin_pow(base, exp); }

        // --- Trigonométrie ---
        template <typename T>
        inline T sin(T x) { return __builtin_sin(x); }

        template <typename T>
        inline T asin(T x) { return __builtin_asin(x); }

        template <typename T>
        inline T cos(T x) { return __builtin_cos(x); }

        template <typename T>
        inline T acos(T x) { return __builtin_acos(x); }

        template <typename T>
        inline T tan(T x) { return __builtin_tan(x); }

        template <typename T>
        inline T atan(T x) { return __builtin_atan(x); }

        // --- Logarithmes ---
        template <typename T>
        inline T ln(T x) { return __builtin_log(x); }

        template <typename T>
        inline T log2(T x) { return __builtin_log2(x); }

        template <typename T>
        inline T log10(T x) { return __builtin_log10(x); }

        template <typename T>
        inline T log(T p, T v) { return __builtin_log(v) / __builtin_log(p); }
    }
}
#endif