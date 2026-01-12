// include/Xi/String.hpp

#ifndef STRING_HPP
#define STRING_HPP

#include "Array.hpp"
#include "Random.hpp"

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

    struct VarLongResult
    {
        long long value;
        int bytes;
        bool error;
    };

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
        String(const char *s, usz len) { append_raw((const u8 *)s, len); }
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

        const char *c_str()
        {
            if (length == 0)
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
            if (length != other.length)
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
            if (length != oLen)
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

        long long find(const char *needle, usz start = 0) const
        {
            usz nLen = str_len(needle);
            if (nLen == 0 || length < nLen)
                return -1;
            const u8 *h = const_cast<String *>(this)->data();
            const u8 *n = (const u8 *)needle;
            for (usz i = start; i <= length - nLen; ++i)
            {
                usz j = 0;
                while (j < nLen && h[i + j] == n[j])
                    j++;
                if (j == nLen)
                    return (long long)i;
            }
            return -1;
        }

        String begin(usz from, usz to) const
        {
            // We remove the const qualifier from 'this' so we can call the non-const Array::begin
            return static_cast<String>(const_cast<String *>(this)->Array<u8>::begin(from, to));
        }

        Array<String> split(const char *sep) const
        {
            Array<String> res;
            usz sLen = str_len(sep);
            if (sLen == 0)
                return res;

            String *mut = const_cast<String *>(this);
            usz totalLen = length;

            // Phase 1: Collect indices (Read-Only / Flattening)
            // We gather all split points first so we don't interleave finding (flattening)
            // with slicing (fragmenting).
            Array<long long> indices;
            long long pos = find(sep, 0);
            while (pos != -1)
            {
                indices.push(pos);
                pos = find(sep, (usz)pos + sLen);
            }

            // Phase 2: Slice (Fragmenting)
            // Now we can safely fragment the array without re-flattening it in between.
            usz curr = 0;
            for (usz i = 0; i < indices.length; i++)
            {
                usz idx = (usz)indices[i];
                if (idx > totalLen)
                    break;

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
        bool constantTimeEquals(const Xi::String &b, int length = 0) const
        {
            const u8 *ad = data();
            const u8 *bd = b.data();

            usz aLen = length;
            usz bLen = b.length;

            // Determine the actual number of bytes we must check
            usz compareLen = (length > 0) ? static_cast<usz>(length) : ((aLen > bLen) ? aLen : bLen);

            // If length is provided, we only care if our buffers are at least that large.
            // Otherwise, we check if the lengths are identical.
            u8 result = 0;
            if (length > 0)
            {
                if (aLen < (usz)length || bLen < (usz)length)
                    result = 1;
            }
            else
            {
                if (aLen != bLen)
                    result = 1;
            }

            for (usz i = 0; i < compareLen; ++i)
            {
                u8 aByte = (i < aLen) ? ad[i] : 0;
                u8 bByte = (i < bLen) ? bd[i] : 0;

                result |= (aByte ^ bByte);
            }

            return result == 0;
        }
        // --- Encoding

        // Inside class String : public Array<u8>

        // --- VarLong (Base-128 VLQ) ---

        String *pushVarLong(long long v)
        {
            unsigned long long n = (unsigned long long)v;
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

        long long shiftVarLong()
        {
            unsigned long long r = 0;
            int s = 0;
            u8 b;
            do
            {
                // Security: Ensure we do not shift beyond 64 bits (max 10 bytes for 7-bit groups)
                if (s >= 70)
                    return 0;
                // Security: Array::shift() must be checked for bounds internally or here
                if (length == 0)
                    return 0;

                b = shift();
                r |= (unsigned long long)(b & 0x7f) << s;
                s += 7;
            } while (b & 0x80);
            return (long long)r;
        }

        // Prefixing with unshifted VarLong for length-prefixed buffers
        String *unshiftVarLong(long long v)
        {
            unsigned long long n = (unsigned long long)v;
            // We must encode in reverse to unshift correctly at the head
            Array<u8> temp;
            do
            {
                u8 t = n & 0x7f;
                n >>= 7;
                if (n)
                    t |= 0x80;
                temp.push(t);
            } while (n);

            // Unshift into the main array from last to first
            for (long long i = (long long)temp.length - 1; i >= 0; i--)
            {
                unshift(temp[i]);
            }
            return this;
        }

        String *unshiftVarLong()
        {
            return unshiftVarLong((long long)length);
        }

        // --- VarString ---

        String *pushVarString(const String &s)
        {
            pushVarLong((long long)s.length);
            append_raw(const_cast<String &>(s).data(), s.length);
            return this;
        }

        String shiftVarString()
        {
            long long lengthToRead = shiftVarLong();
            // Security check: ensure the requested length exists in the remaining buffer
            if (lengthToRead < 0 || (usz)lengthToRead > length)
            {
                return String(); // Return empty string on corruption/attack
            }
            // Using begin(start, end) to return a fragmented view (Array slice)
            String result = begin(0, (usz)lengthToRead);

            // Advance the current buffer by removing the bytes we just "read"
            // Since begin() does not remove, we must drop them manually
            for (long long i = 0; i < lengthToRead; i++)
            {
                shift();
            }
            return result;
        }

        // --- Fixed Size Types ---

        String *pushBool(bool v)
        {
            push(v ? 1 : 0);
            return this;
        }
        bool shiftBool() { return length > 0 ? (shift() != 0) : false; }

        String *pushI64(long long v)
        {
            for (int i = 0; i < 8; i++)
                push((u8)((v >> (i * 8)) & 0xff));
            return this;
        }
        long long shiftI64()
        {
            if (length < 8)
                return 0;
            unsigned long long r = 0;
            for (int i = 0; i < 8; i++)
                r |= (unsigned long long)shift() << (i * 8);
            return (long long)r;
        }

        String *pushF64(f64 v)
        {
            union
            {
                f64 f;
                long long i;
            } u;
            u.f = v;
            return pushI64(u.i);
        }
        f64 shiftF64()
        {
            union
            {
                f64 f;
                long long i;
            } u;
            u.i = shiftI64();
            return u.f;
        }

        String *pushF32(f32 v)
        {
            union
            {
                f32 f;
                u32 i;
            } u;
            u.f = v;
            for (int i = 0; i < 4; i++)
                push((u8)((u.i >> (i * 8)) & 0xff));
            return this;
        }
        f32 shiftF32()
        {
            if (length < 4)
                return 0.0f;
            u32 r = 0;
            for (int i = 0; i < 4; i++)
                r |= (u32)shift() << (i * 8);
            union
            {
                f32 f;
                u32 i;
            } u;
            u.i = r;
            return u.f;
        }

        VarLongResult peekVarLong(usz offset = 0) const
        {
            unsigned long long r = 0;
            int s = 0;
            int i = 0;

            // Safety check for offset
            if (offset >= length)
                return {0, 0, true};

            const u8 *d = const_cast<String *>(this)->data();
            usz available = length;

            for (i = (int)offset; i < (int)available; ++i)
            {
                u8 b = d[i];
                r |= (unsigned long long)(b & 0x7f) << s;
                s += 7;

                if (!(b & 0x80))
                {
                    // Success: return value and total bytes (including offset)
                    return {(long long)r, (i - (int)offset) + 1, false};
                }

                // Security: Prevent overflow (max 10 bytes for 64-bit)
                if (s >= 70)
                    break;
            }

            // Return error if the buffer ended before the VarLong was finished
            return {0, 0, true};
        }

        /**
         * Converts the string buffer into a space-separated decimal string.
         * Example: {0x48, 0x69} becomes "72 105"
         */
        Xi::String toDeci() const
        {
            Xi::String result;
            for (usz i = 0; i < length; ++i)
            {
                // Get the numeric value of the current byte
                u8 value = (*this)[i];

                // Manual conversion to decimal string to avoid heavy dependencies
                if (value == 0)
                {
                    result.push('0');
                }
                else
                {
                    char buffer[4]; // Max 3 digits for u8 (255) + null terminator
                    int pos = 0;
                    while (value > 0)
                    {
                        buffer[pos++] = (value % 10) + '0';
                        value /= 10;
                    }
                    // Reverse the digits and push to result
                    for (int j = pos - 1; j >= 0; --j)
                    {
                        result.push(buffer[j]);
                    }
                }

                // Add a space between values, but not after the last one
                if (i < length - 1)
                {
                    result.push(' ');
                }
            }
            return result;
        }
    };

    template <>
    struct FNVHasher<String>
    {
        static usz hash(const String &s)
        {
            usz h = FNV_OFFSET;
            // data() is safe here because we handle the pointer access
            const u8 *d = const_cast<String &>(s).data();
            for (usz i = 0; i < s.length; ++i)
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
            len = s.length;
        if (s.length < len)
        {
            len = s.length;
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
            len = s.length;
        if (s.length < len)
        {
            len = s.length;
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
        usz length = s.length;
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
        usz length = s.length;
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