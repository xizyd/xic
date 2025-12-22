// src/Xi/String.hpp

#ifndef STRING_HPP
#define STRING_HPP

#include "Array.hpp"
#include "Hasher.hpp"

namespace Xi
{
    template <typename T>
    struct HasToString
    {
        template <typename U>
        static char test(decltype(&U::toString));
        template <typename U>
        static long test(...);
        static const bool value = sizeof(test<T>(0)) == sizeof(char);
    };

    class String;

    int parseInt(const String &s);
    f64 parseDouble(const String &s);
    void secureRandomFill(u8 *buffer, usz size);

    class String : public Array<u8>
    {
    private:
        static usz str_len(const char *s)
        {
            usz l = 0;
            if (s)
                while (s[l])
                    l++;
            return l;
        }
        void append_raw(const u8 *b, usz c) { pushEach(b, c); }
        template <typename I>
        void append_int(I n)
        {
            if (n == 0)
            {
                push('0');
                return;
            }
            char buf[32];
            int i = 0;
            bool neg = (n < 0);
            unsigned long long un = neg ? (unsigned long long)(-(n + 1)) + 1 : (unsigned long long)n;
            while (un)
            {
                buf[i++] = (un % 10) + '0';
                un /= 10;
            }
            if (neg)
                buf[i++] = '-';
            while (i > 0)
                push(buf[--i]);
        }

        void append_f32(f64 n, int precision = 6)
        {
            if (n < 0)
            {
                push('-');
                n = -n;
            }
            // Integer part
            append_int((long long)n);
            push('.');
            // Fractional part
            f64 frac = n - (long long)n;
            for (int i = 0; i < precision; i++)
            {
                frac *= 10;
                int digit = (int)frac;
                push(digit + '0');
                frac -= digit;
            }
        }

    public:
        using Array<u8>::push;
        using Array<u8>::shift;
        using Array<u8>::alloc;

        String() : Array<u8>() {}
        String(const char *s) { append_raw((const u8 *)s, str_len(s)); }
        String(const u8 *b, usz l) { append_raw(b, l); }
        String(const String &o) : Array<u8>(o) {}
        String(String &&o) : Array<u8>(Xi::Move(o)) {}
        String(const Array<u8> &o) : Array<u8>(o) {}
        String(int n) : Array<u8>() { append_int(n); }
        String(long long n) : Array<u8>() { append_int(n); }
        String(f64 n) : Array<u8>() { append_f32(n); }
        String(f32 n) : Array<u8>() { append_f32((f64)n); }
        String &operator=(const String &o)
        {
            Array<u8>::operator=(o);
            return *this;
        }
        String &operator=(String &&o)
        {
            Array<u8>::operator=(Xi::Move(o));
            return *this;
        }

        usz len() const { return length; }

        const char *c_str()
        {
            if (len() == 0)
                return "";
            push(0); // Ensure null terminator exists
            char *ptr = (char *)data();
            pop(); // Restore length, but the '0' byte remains in allocated memory
            return ptr;
        }
        const char *c_str() const
        {
            return const_cast<String *>(this)->c_str();
        }
        operator const char *() const { return const_cast<String *>(this)->c_str(); }

        String &operator+=(const char *s)
        {
            append_raw((const u8 *)s, str_len(s));
            return *this;
        }
        String &operator+=(const String &o)
        {
            concat(o);
            return *this;
        }
        String &operator+=(char c)
        {
            push((u8)c);
            return *this;
        }
        String &operator+=(int n)
        {
            append_int(n);
            return *this;
        }
        String &operator+=(long long n)
        {
            append_int(n);
            return *this;
        }
        template <typename T>
        auto operator+=(const T &obj) -> decltype(obj.toString(), *this)
        {
            *this += obj.toString();
            return *this;
        }

        // Inside class String

        bool operator==(const String &other) const
        {
            // 1. Check identity
            if (this == &other)
                return true;

            // 2. Check length
            if (len() != other.len())
                return false;

            // 3. Compare bytes
            // We cast away const to use data() which handles CoW/Flattening internally
            const u8 *d1 = const_cast<String *>(this)->data();
            const u8 *d2 = const_cast<String &>(other).data();

            // Safety check
            if (!d1 || !d2)
                return d1 == d2;

            for (usz i = 0; i < length; ++i)
            {
                if (d1[i] != d2[i])
                    return false;
            }
            return true;
        }
        bool operator!=(const String &other) const { return !(*this == other); }
        bool operator==(const char *other) const
        {
            usz oLen = str_len(other);
            if (len() != oLen)
                return false;
            const u8 *d = const_cast<String *>(this)->data();
            for (usz i = 0; i < oLen; ++i)
                if (d[i] != (u8)other[i])
                    return false;
            return true;
        }
        bool operator!=(const char *other) const { return !(*this == other); }

        friend String operator+(const String &lhs, const String &rhs)
        {
            String s = lhs;
            s += rhs;
            return s;
        }
        friend String operator+(const String &lhs, const char *rhs)
        {
            String s = lhs;
            s += rhs;
            return s;
        }
        friend String operator+(const char *lhs, const String &rhs)
        {
            String s(lhs);
            s += rhs;
            return s;
        }

        String *pushByte(u8 v)
        {
            push(v);
            return this;
        }
        String *pushVarInt(int v)
        {
            unsigned int n = (unsigned int)v;
            do
            {
                u8 t = n & 0x7f;
                n >>= 7;
                if (n)
                    t |= 0x80;
                push(t);
            } while (n);
            return this;
        }

        u8 shiftByte() { return shift(); }
        int shiftVarInt()
        {
            int r = 0, s = 0;
            u8 b;
            do
            {
                b = shift();
                r |= (b & 0x7f) << s;
                s += 7;
            } while (b & 0x80);
            return r;
        }

        long long find(const char *needle, usz start = 0) const
        {
            usz nLen = str_len(needle);
            if (nLen == 0 || len() < nLen)
                return -1;
            const u8 *h = const_cast<String *>(this)->data();
            const u8 *n = (const u8 *)needle;
            for (usz i = start; i <= len() - nLen; ++i)
            {
                usz j = 0;
                while (j < nLen && h[i + j] == n[j])
                    j++;
                if (j == nLen)
                    return (long long)i;
            }
            return -1;
        }

        // src/Xi/String.hpp

        Array<String> split(const char *sep) const
        {
            Array<String> res;
            usz sLen = str_len(sep);
            if (sLen == 0) return res;

            String *mut = const_cast<String *>(this);
            usz totalLen = len();
            
            // Phase 1: Collect indices (Read-Only / Flattening)
            // We gather all split points first so we don't interleave finding (flattening)
            // with slicing (fragmenting).
            Array<long long> indices;
            long long pos = find(sep, 0);
            while(pos != -1) {
                indices.push(pos);
                pos = find(sep, (usz)pos + sLen);
            }

            // Phase 2: Slice (Fragmenting)
            // Now we can safely fragment the array without re-flattening it in between.
            usz curr = 0;
            for(usz i = 0; i < indices.length; i++) {
                usz idx = (usz)indices[i];
                if(idx > totalLen) break;
                
                res.push(mut->begin(curr, idx));
                curr = idx + sLen;
            }

            // Remainder
            if (curr <= totalLen)
            {
                res.push(mut->begin(curr, totalLen));
            }

            return res;
        }

        String replace(const char *tgt, const char *rep) const
        {
            Array<String> parts = split(tgt);
            String res;
            bool first = true;
            for (auto part : parts)
            {
                if (!first)
                    res += rep;
                res += part;
                first = false;
            }
            return res;
        }

        int toInt() const { return Xi::parseInt(*this); }
        f64 toDouble() const { return Xi::parseDouble(*this); }

        bool constantTimeEquals(const Xi::String &b) const
        {
            const u8 *ad = data();
            const u8 *bd = b.data();
            usz aLen = len();
            usz bLen = b.len();

            // We use a length that covers the data, but we must still
            // access memory carefully to avoid out-of-bounds.
            usz maxLen = (aLen > bLen) ? aLen : bLen;
            usz minLen = (aLen < bLen) ? aLen : bLen;

            u8 result = (aLen ^ bLen); // If lengths differ, result will be non-zero

            for (usz i = 0; i < minLen; ++i)
            {
                result |= ad[i] ^ bd[i];
            }

            return result == 0;
        }
    };

    // [FIXED] Specialization is now valid because we don't depend on it in the class body
    template <>
    struct Hasher<String>
    {
        static usz hash(const String &s)
        {
            usz h = FNV_OFFSET;
            // data() is safe here because we handle the pointer access
            const u8 *d = const_cast<String &>(s).data();
            for (usz i = 0; i < s.len(); ++i)
            {
                h ^= d[i];
                h *= FNV_PRIME;
            }
            return h;
        }
    };

    inline void randomFill(String &s, usz len = 0)
    {
        if (len == 0)
            len = s.len();
        if (s.len() < len)
        {
            len = s.len();
        }

        u8 *raw = const_cast<u8 *>(reinterpret_cast<const u8 *>(s.data()));
        if (!raw && len > 0)
        {
            return;
        }
        randomFill(raw, len);
    }

    inline void secureRandomFill(String &s, usz len = 0)
    {
        if (len == 0)
            len = s.len();
        if (s.len() < len)
        {
            len = s.len();
        }

        u8 *raw = const_cast<u8 *>(reinterpret_cast<const u8 *>(s.data()));
        if (!raw && len > 0)
        {
            return;
        }
        secureRandomFill(raw, len);
    }

    inline int parseInt(const String &s)
    {
        const u8 *d = const_cast<String &>(s).data();
        usz length = s.len();
        if (length == 0 || !d)
            return 0;

        int result = 0;
        int sign = (d[0] == '-') ? -1 : 1;
        usz i = (d[0] == '-' || d[0] == '+') ? 1 : 0;

        for (; i < length; ++i)
        {
            if (d[i] >= '0' && d[i] <= '9')
            {
                result = result * 10 + (d[i] - '0');
            }
            else if (d[i] == 'e' || d[i] == 'E' || d[i] == '.')
            {
                break; // Handled as base for f64, or just stop for int
            }
            else
                break;
        }
        return result * sign;
    }

    inline f64 parseDouble(const String &s)
    {
        const u8 *d = const_cast<String &>(s).data();
        usz length = s.len();
        if (length == 0 || !d)
            return 0.0;

        f64 result = 0.0;
        f64 sign = (d[0] == '-') ? -1.0 : 1.0;
        usz i = (d[0] == '-' || d[0] == '+') ? 1 : 0;

        // 1. Parse Integer part
        while (i < length && d[i] >= '0' && d[i] <= '9')
        {
            result = result * 10.0 + (d[i] - '0');
            i++;
        }

        // 2. Parse Fractional part
        if (i < length && d[i] == '.')
        {
            i++;
            f64 weight = 0.1;
            while (i < length && d[i] >= '0' && d[i] <= '9')
            {
                result += (d[i] - '0') * weight;
                weight /= 10.0;
                i++;
            }
        }

        // 3. Parse Exponent (e/E)
        if (i < length && (d[i] == 'e' || d[i] == 'E'))
        {
            i++;
            int expSign = 1;
            if (i < length && d[i] == '-')
            {
                expSign = -1;
                i++;
            }
            else if (i < length && d[i] == '+')
            {
                i++;
            }

            int expVal = 0;
            while (i < length && d[i] >= '0' && d[i] <= '9')
            {
                expVal = expVal * 10 + (d[i] - '0');
                i++;
            }

            // Apply exponent: result = result * 10^exp
            f64 factor = 1.0;
            f64 base = 10.0;
            int p = expVal;
            while (p > 0)
            {
                if (p & 1)
                    factor *= base;
                base *= base;
                p >>= 1;
            }
            if (expSign == -1)
                result /= factor;
            else
                result *= factor;
        }

        return result * sign;
    }

    void randomSeed(String str)
    {
        u32 h = 5381;
        int c;
        unsigned char *d = str.data();
        while ((c = *d++))
        {
            h = ((h << 5) + h) + c;
        }
        randomSeed(h);
    }
}
#endif