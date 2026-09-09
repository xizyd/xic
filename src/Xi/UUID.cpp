#include "Xi/UUID.hpp"
#include "Xi/Random.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>

namespace Xi {

// Standard Namespaces (RFC 4122)
const UUID UUID::NamespaceDNS  = UUID{0x6ba7b8109dad11d1ULL, 0x80b400c04fd430c8ULL};
const UUID UUID::NamespaceURL  = UUID{0x6ba7b8119dad11d1ULL, 0x80b400c04fd430c8ULL};
const UUID UUID::NamespaceOID  = UUID{0x6ba7b8129dad11d1ULL, 0x80b400c04fd430c8ULL};
const UUID UUID::NamespaceX500 = UUID{0x6ba7b8149dad11d1ULL, 0x80b400c04fd430c8ULL};

// -------------------------------------------------------------------------
// Constructors
// -------------------------------------------------------------------------

UUID::UUID(const String& str) {
    if (!tryParse(str, *this)) {
        hi = 0;
        lo = 0;
    }
}

UUID::UUID(const char* str) {
    if (!str || !tryParse(String(str), *this)) {
        hi = 0;
        lo = 0;
    }
}

UUID::UUID(const u8 bytes[16]) {
    *this = fromBytes(bytes);
}

// -------------------------------------------------------------------------
// Byte Conversions
// -------------------------------------------------------------------------

void UUID::toBytes(u8 bytes[16]) const {
    for (int i = 0; i < 8; i++) {
        bytes[i]     = (u8)((hi >> (56 - i * 8)) & 0xFF);
        bytes[i + 8] = (u8)((lo >> (56 - i * 8)) & 0xFF);
    }
}

UUID UUID::fromBytes(const u8 bytes[16]) {
    UUID u;
    u.hi = 0;
    u.lo = 0;
    for (int i = 0; i < 8; i++) u.hi = (u.hi << 8) | bytes[i];
    for (int i = 8; i < 16; i++) u.lo = (u.lo << 8) | bytes[i];
    return u;
}

// -------------------------------------------------------------------------
// Generation
// -------------------------------------------------------------------------

UUID UUID::random() {
    UUID u;
    u8 buf[16];
    randomFill(buf, 16);

    u = fromBytes(buf);
    // RFC 4122 v4: version = 4 in hi, variant = 2 (10xx) in lo
    u.hi = (u.hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    u.lo = (u.lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    return u;
}

UUID UUID::v7() {
    auto now = std::chrono::system_clock::now();
    u64 ms = (u64)std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    u8 randBuf[10];
    randomFill(randBuf, 10);

    UUID u;
    // 48-bit timestamp in high 48 bits of hi
    u.hi = (ms & 0x0000FFFFFFFFFFFFULL) << 16;
    // 4-bit version = 7
    u.hi |= 0x0000000000007000ULL;
    // 12 bits of rand_a
    u16 rand_a = ((u16)randBuf[0] << 8) | randBuf[1];
    u.hi |= (rand_a & 0x0FFF);

    // 64 bits of lo: 2-bit variant (10xx) + 62 bits of rand_b
    u.lo = 0;
    for (int i = 2; i < 10; i++) u.lo = (u.lo << 8) | randBuf[i];
    u.lo = (u.lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    return u;
}

// Self-contained RFC 3174 SHA-1 for RFC 4122 v5 UUID
namespace {
    struct SHA1Ctx {
        u32 state[5];
        u32 count[2];
        u8  buffer[64];

        static inline u32 rol(u32 val, int bits) {
            return (val << bits) | (val >> (32 - bits));
        }

        void transform(const u8 data[64]) {
            u32 a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
            u32 w[80];

            for (int i = 0; i < 16; i++) {
                w[i] = ((u32)data[i * 4] << 24) |
                       ((u32)data[i * 4 + 1] << 16) |
                       ((u32)data[i * 4 + 2] << 8) |
                       ((u32)data[i * 4 + 3]);
            }
            for (int i = 16; i < 80; i++) {
                w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            }

            for (int i = 0; i < 80; i++) {
                u32 f, k;
                if (i < 20) {
                    f = (b & c) | ((~b) & d);
                    k = 0x5A827999;
                } else if (i < 40) {
                    f = b ^ c ^ d;
                    k = 0x6ED9EBA1;
                } else if (i < 60) {
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8F1BBCDC;
                } else {
                    f = b ^ c ^ d;
                    k = 0xCA62C1D6;
                }
                u32 temp = rol(a, 5) + f + e + k + w[i];
                e = d;
                d = c;
                c = rol(b, 30);
                b = a;
                a = temp;
            }

            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
        }

        void init() {
            state[0] = 0x67452301;
            state[1] = 0xEFCDAB89;
            state[2] = 0x98BADCFE;
            state[3] = 0x10325476;
            state[4] = 0xC3D2E1F0;
            count[0] = count[1] = 0;
        }

        void update(const u8* data, usz len) {
            u32 i = 0;
            u32 j = (count[0] >> 3) & 63;
            if ((count[0] += (u32)(len << 3)) < (u32)(len << 3)) count[1]++;
            count[1] += (u32)(len >> 29);
            if ((j + len) > 63) {
                std::memcpy(&buffer[j], data, (i = 64 - j));
                transform(buffer);
                for (; i + 63 < len; i += 64) transform(&data[i]);
                j = 0;
            }
            std::memcpy(&buffer[j], &data[i], len - i);
        }

        void final(u8 digest[20]) {
            u8 finalCount[8];
            for (int i = 0; i < 8; i++) {
                finalCount[i] = (u8)((count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 0xFF);
            }
            update((const u8*)"\200", 1);
            while ((count[0] & 504) != 448) update((const u8*)"\0", 1);
            update(finalCount, 8);
            for (int i = 0; i < 20; i++) {
                digest[i] = (u8)((state[i >> 2] >> ((3 - (i & 3)) * 8)) & 0xFF);
            }
        }
    };
} // anonymous namespace

UUID UUID::v5(const UUID& ns, const String& name) {
    u8 nsBytes[16];
    ns.toBytes(nsBytes);

    SHA1Ctx sha1;
    sha1.init();
    sha1.update(nsBytes, 16);
    sha1.update((const u8*)name.c_str(), name.length());

    u8 digest[20];
    sha1.final(digest);

    // RFC 4122 v5: version = 5, variant = 2 (10xx)
    digest[6] = (digest[6] & 0x0F) | 0x50;
    digest[8] = (digest[8] & 0x3F) | 0x80;

    return fromBytes(digest);
}

UUID UUID::fromName(const String& name, const UUID& ns) {
    return v5(ns, name);
}

UUID UUID::fromName(const String& name) {
    return v5(NamespaceDNS, name);
}

// -------------------------------------------------------------------------
// Parsing
// -------------------------------------------------------------------------

static inline int parseHexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool UUID::tryParse(const String& str, UUID& out) {
    String s = str.trim();
    if (s.startsWith("urn:uuid:") || s.startsWith("URN:UUID:")) {
        s = s.substring(9).trim();
    }
    if (s.startsWith("{") && s.endsWith("}")) {
        s = s.substring(1, s.length() - 1).trim();
    }

    u8 bytes[16];
    if (s.length() == 36) {
        // Canonical format: 8-4-4-4-12
        if (s.data()[8] != '-' || s.data()[13] != '-' ||
            s.data()[18] != '-' || s.data()[23] != '-') {
            return false;
        }
        int byteIdx = 0;
        for (usz i = 0; i < 36; i++) {
            if (i == 8 || i == 13 || i == 18 || i == 23) continue;
            int hiNibble = parseHexNibble((char)s.data()[i++]);
            int loNibble = parseHexNibble((char)s.data()[i]);
            if (hiNibble < 0 || loNibble < 0) return false;
            bytes[byteIdx++] = (u8)((hiNibble << 4) | loNibble);
        }
    } else if (s.length() == 32) {
        // Compact hex format: 32 hex chars
        for (int i = 0; i < 16; i++) {
            int hiNibble = parseHexNibble((char)s.data()[i * 2]);
            int loNibble = parseHexNibble((char)s.data()[i * 2 + 1]);
            if (hiNibble < 0 || loNibble < 0) return false;
            bytes[i] = (u8)((hiNibble << 4) | loNibble);
        }
    } else {
        return false;
    }

    out = fromBytes(bytes);
    return true;
}

UUID UUID::fromString(const String& str) {
    UUID out;
    if (tryParse(str, out)) return out;
    return nil();
}

bool UUID::isValid(const String& str) {
    UUID dummy;
    return tryParse(str, dummy);
}

// -------------------------------------------------------------------------
// Formatting
// -------------------------------------------------------------------------

String UUID::toString() const {
    char buf[37];
    snprintf(buf, sizeof(buf),
        "%08x-%04x-%04x-%04x-%012llx",
        (unsigned)(hi >> 32),
        (unsigned)((hi >> 16) & 0xFFFF),
        (unsigned)(hi & 0xFFFF),
        (unsigned)(lo >> 48),
        (unsigned long long)(lo & 0x0000FFFFFFFFFFFFULL));
    return String(buf);
}

String UUID::toCompactString() const {
    char buf[33];
    snprintf(buf, sizeof(buf),
        "%016llx%016llx",
        (unsigned long long)hi,
        (unsigned long long)lo);
    return String(buf);
}

String UUID::toUrn() const {
    return "urn:uuid:" + toString();
}

// -------------------------------------------------------------------------
// Introspection & Hash
// -------------------------------------------------------------------------

u64 UUID::timestamp() const {
    if (version() == 7) {
        return hi >> 16;
    }
    if (version() == 1) {
        u64 time_low = hi >> 32;
        u64 time_mid = (hi >> 16) & 0xFFFF;
        u64 time_hi  = hi & 0x0FFF;
        return (time_hi << 48) | (time_mid << 32) | time_low;
    }
    return 0;
}

usz UUID::hash() const {
    usz h = (usz)(hi ^ (hi >> 32));
    h *= 2654435761ULL;
    h ^= (usz)(lo ^ (lo >> 32));
    h *= 2246822519ULL;
    return h;
}

} // namespace Xi
