// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "helpers.h"

bool rect_is_empty(Rect rect)
{
    return rect.left >= rect.right || rect.top >= rect.bottom;
}

bool rect_contains(Rect rect, Point point)
{
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

Rect rect_intersect(Rect lhs, Rect rhs)
{
#if 1
    CoordType l = max(lhs.left, rhs.left);
    CoordType t = max(lhs.top, rhs.top);
    CoordType r = min(lhs.right, rhs.right);
    CoordType b = min(lhs.bottom, rhs.bottom);

    // Ensure that the size is non-negative. This avoids bugs,
    // because some height/width is negative all of a sudden.
    r = max(l, r);
    b = max(t, b);

    return (Rect){l, t, r, b};
#else
    // I wrote some SSE2 code for fun. It's a lot faster and more compact than the scalar code.
    // I've tested it a little bit, and I believe it's correct. It's left disabled until I'm at
    // a point were vectorization can be justified (= the editor not being complete trash).
    __m128i a = _mm_loadu_si128((__m128i*)&lhs);
    __m128i b = _mm_loadu_si128((__m128i*)&rhs);

    // Compute the min of left/top and max of right/bottom.
    __m128i lt = _mm_max_epi32(a, b);
    __m128i rb = _mm_min_epi32(a, b);

    // Duplicate the min() of left/top into the upper half and...
    lt = _mm_shuffle_epi32(lt, _MM_SHUFFLE(1, 0, 1, 0));
    // ...ensure that the size is non-negative by ensuring that
    // the upper half of `rb` is at least as large as `lt`.
    rb = _mm_max_epi32(lt, rb);

    // Combine the lower half `lt` (max of left/top) and
    // the upper half of `rb` (min of right/bottom).
    __m128i res = _mm_blend_epi16(lt, rb, 0xf0);

    Rect result;
    _mm_storeu_si128((__m128i*)&result, res);
    return result;
#endif
}

s8 s8_from_ptr(const c8* ptr)
{
    usize len = strlen((const char*)ptr);
    return (s8){(c8*)ptr, len, len};
}

s8 s8_slice(s8 str, usize beg, usize end)
{
    end = min(end, str.len);
    beg = min(beg, end);
    return (s8){str.beg + beg, end - beg, end - beg};
}

bool s8_starts_with(s8 str, s8 prefix)
{
    return str.len >= prefix.len && memcmp(str.beg, prefix.beg, prefix.len) == 0;
}

void s8_transform_lowercase_ascii(s8 str)
{
    for (usize i = 0; i < str.len; ++i) {
        if (str.beg[i] >= 'A' && str.beg[i] <= 'Z') {
            str.beg[i] += 'a' - 'A';
        }
    }
}

usize s8_find(s8 str, usize off, c8 ch)
{
    for (; off < str.len; ++off) {
        if (str.beg[off] == ch) {
            return off;
        }
    }
    return str.len;
}

u64 s8_to_u64(s8 str, int base) {
    c8* ptr = str.beg;
    const c8* end = ptr + str.len;
    u64 accumulator = 0;
    u64 base64 = base;

    if (base <= 0)
    {
        base64 = 10;

        if (ptr != end && *ptr == '0')
        {
            base64 = 8;
            ptr += 1;

            if (ptr != end && (*ptr == 'x' || *ptr == 'X'))
            {
                base64 = 16;
                ptr += 1;
            }
        }
    }

    if (ptr == end)
    {
        return 0;
    }

    for (;;)
    {
        u64 value = 0;
        if (*ptr >= '0' && *ptr <= '9')
        {
            value = *ptr - '0';
        }
        else if (*ptr >= 'A' && *ptr <= 'F')
        {
            value = *ptr - 'A' + 10;
        }
        else if (*ptr >= 'a' && *ptr <= 'f')
        {
            value = *ptr - 'a' + 10;
        }
        else
        {
            return 0;
        }

        const u64 acc = accumulator * base64 + value;
        if (acc < accumulator)
        {
            return 0;
        }

        accumulator = acc;
        ptr += 1;

        if (ptr == end)
        {
            return accumulator;
        }
    }
}

static u64 wyr3(const u8* p, usize k)
{
    return ((u64)p[0] << 16) | ((u64)p[k >> 1] << 8) | p[k - 1];
}

static u64 wyr4(const u8* p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static u64 wyr8(const u8* p)
{
    u64 v;
    memcpy(&v, p, 8);
    return v;
}

static u64 wymix(u64 lhs, u64 rhs)
{
#if os_UNIX
    __uint128_t r = (__uint128_t)lhs * (__uint128_t)rhs;
    return (u64)(r >> 64) ^ (u64)r;
#elif PLATFORM_X64
    extern u64 _umul128(u64 Multiplier, u64 Multiplicand, u64 * HighProduct);
    u64 hi = 0;
    u64 lo = _umul128(lhs, rhs, &hi);
    return lo ^ hi;
#elif os_ARM64
    extern u64 __umulh(u64 a, u64 b);
    const u64 lo = lhs * rhs;
    const u64 hi = __umulh(lhs, rhs);
    return lo ^ hi;
#endif
}

// The venerable wyhash hash function. It's fast and has good statistical properties.
// It's in the public domain.
u64 hash(u64 seed, void* data, usize len)
{
    static const u64 s0 = 0xa0761d6478bd642f;
    static const u64 s1 = 0xe7037ed1a0b428db;
    static const u64 s2 = 0x8ebc6af09c88c6e3;
    static const u64 s3 = 0x589965cc75374cc3;

    const u8* p = data;
    seed ^= s0;
    u64 a;
    u64 b;

    if (len <= 16) {
        if (len >= 4) {
            a = (wyr4(p) << 32) | wyr4(p + ((len >> 3) << 2));
            b = (wyr4(p + len - 4) << 32) | wyr4(p + len - 4 - ((len >> 3) << 2));
        } else if (len > 0) {
            a = wyr3(p, len);
            b = 0;
        } else {
            a = b = 0;
        }
    } else {
        usize i = len;
        if (i > 48) {
            u64 seed1 = seed;
            u64 seed2 = seed;
            do {
                seed = wymix(wyr8(p) ^ s1, wyr8(p + 8) ^ seed);
                seed1 = wymix(wyr8(p + 16) ^ s2, wyr8(p + 24) ^ seed1);
                seed2 = wymix(wyr8(p + 32) ^ s3, wyr8(p + 40) ^ seed2);
                p += 48;
                i -= 48;
            } while (i > 48);
            seed ^= seed1 ^ seed2;
        }
        while (i > 16) {
            seed = wymix(wyr8(p) ^ s1, wyr8(p + 8) ^ seed);
            i -= 16;
            p += 16;
        }
        a = wyr8(p + i - 16);
        b = wyr8(p + i - 8);
    }

    return wymix(s1 ^ len, wymix(a ^ s1, b ^ seed));
}

u64 hash_s8(u64 seed, s8 str)
{
    return hash(seed, str.beg, str.len);
}

u64 u64_log10(u64 v)
{
    // This implements https://graphics.stanford.edu/~seander/bithacks.html#IntegerLog10 but with lzcnt for log2.
    static const u64 powers_of_10[] = {
        0u,
        10u,
        100u,
        1000u,
        10000u,
        100000u,
        1000000u,
        10000000u,
        100000000u,
        1000000000u,
        10000000000u,
        100000000000u,
        1000000000000u,
        10000000000000u,
        100000000000000u,
        1000000000000000u,
        10000000000000000u,
        100000000000000000u,
        1000000000000000000u,
        10000000000000000000u,
    };
    unsigned long index;
#ifdef _MSC_VER
    _BitScanReverse64(&index, v | 1);
#else
    index = 64 - __builtin_clzll(v | 1);
#endif
    const u64 t = (index + 1) * 1233 >> 12;
    return t - (v < powers_of_10[t]);
}
