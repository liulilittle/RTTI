#pragma once
// Shared structural-character scanning primitives for the JSON/XML format
// adapters. Replaces hand-rolled per-byte scanner loops with a compact set
// of zero-dependency, noexcept, const-input helpers.
//
// Two execution paths are provided for every helper:
//   * A portable scalar fallback (plain byte loop) that is used whenever the
//     platform does not guarantee SSE2.
//   * An SSE2 path (16-byte blocks via _mm_loadu_si128 / _mm_cmpeq_epi8 /
//     _mm_movemask_epi8) on x86/x86-64. Blocks are always unaligned-safe
//     (loadu) and the final <16-byte tail is finished by the scalar loop,
//     so a helper never reads beyond [begin, end).
//
// SIMD detection:
//   * MSVC x86/x64 (_M_IX86/_M_X64): SSE2 is part of the baseline ABI, so
//     RTTI_DETAIL_HAS_SSE2 is defined unconditionally.
//   * GCC/Clang: RTTI_DETAIL_HAS_SSE2 is defined only when the compiler
//     reports __SSE2__ (x86-64 always; x86-32 with -msse2 or -march=...).
//   * Any other target (e.g. AArch64, no-SIMD, unknown compiler) leaves the
//     macro undefined and compiles the scalar fallback. Portability is
//     guaranteed: the header never uses platform-private declarations, and
//     <emmintrin.h> is only included under RTTI_DETAIL_HAS_SSE2.
//   * Defining RTTI_DETAIL_DISABLE_SIMD before inclusion forces the scalar
//     fallback even on x86 (useful for debugging or cross-checking).
//
// Mask semantics:
//   * char_mask is a 128-bit bitmask over ASCII byte values: bit i (0..127)
//     set means byte value i is a target. The width is 128 bits, not 64,
//     because JSON structural bytes { } [ ] occupy ASCII positions 91..125.
//   * Bytes >= 128 never match any mask; the NUL byte is an ordinary byte
//     whose membership depends only on its mask bit. A NUL terminator
//     outside the scanned range is never inspected.
//
// All functions are header-inline and noexcept; they own no memory and make
// no library calls.

#include <cstddef>
#include <cstdint>

#if defined(RTTI_DETAIL_DISABLE_SIMD)
    // SIMD explicitly disabled by the consumer: scalar fallback only.
#elif defined(_M_X64) || defined(_M_IX86)
    // MSVC: SSE2 is part of the baseline x86/x86-64 ABI.
#  define RTTI_DETAIL_HAS_SSE2
#  include <emmintrin.h>
#elif (defined(__x86_64__) || defined(__i386__)) && defined(__SSE2__)
    // GCC/Clang x86 with SSE2 enabled.
#  define RTTI_DETAIL_HAS_SSE2
#  include <emmintrin.h>
#endif

namespace rtti {
namespace detail {

// 128-bit membership mask over ASCII byte values (bit i -> byte value i).
struct char_mask {
    std::uint64_t lo = 0;  // bytes 0..63
    std::uint64_t hi = 0;  // bytes 64..127

    char_mask() noexcept = default;

    // Mask containing exactly one ASCII byte value.
    static char_mask single(int ascii) noexcept {
        char_mask m;
        m.set(ascii);
        return m;
    }

    // Mask containing each byte of the NUL-terminated string chars.
    static char_mask from_chars(const char* chars) noexcept {
        char_mask m;
        if (chars != nullptr) {
            for (const char* p = chars; *p != '\0'; ++p) {
                m.set(static_cast<unsigned char>(*p));
            }
        }
        return m;
    }

    void set(int ascii) noexcept {
        if (ascii >= 0 && ascii < 64) {
            lo |= (std::uint64_t{1} << ascii);
        } else if (ascii >= 64 && ascii < 128) {
            hi |= (std::uint64_t{1} << (ascii - 64));
        }
    }

    bool test(int ascii) const noexcept {
        if (ascii < 0 || ascii >= 128) return false;
        if (ascii < 64) return ((lo >> ascii) & std::uint64_t{1}) != 0;
        return ((hi >> (ascii - 64)) & std::uint64_t{1}) != 0;
    }
};

namespace {

#if defined(RTTI_DETAIL_HAS_SSE2)
// Position of the lowest set bit in m (m must be non-zero; returns 0 if
// m is zero, which never happens at the call sites).
inline std::uint32_t first_set_bit(std::uint32_t m) noexcept {
    std::uint32_t i = 0;
    while (m != 0u && (m & 1u) == 0u) {
        m >>= 1;
        ++i;
    }
    return i;
}

// Fill out with every byte value present in the mask (up to 128 entries)
// and return the count. This is the list of cmpeq probe values.
inline std::uint32_t collect_targets(char_mask m, unsigned char* out) noexcept {
    std::uint32_t n = 0;
    const std::uint64_t lo = m.lo;
    const std::uint64_t hi = m.hi;
    for (int i = 0; i < 64; ++i) {
        if ((lo & (std::uint64_t{1} << i)) != 0) {
            out[n++] = static_cast<unsigned char>(i);
        }
        if ((hi & (std::uint64_t{1} << i)) != 0) {
            out[n++] = static_cast<unsigned char>(64 + i);
        }
    }
    return n;
}

// 16-byte block match: bit k of the result is 1 iff byte k of the block at p
// is one of the targets. Unaligned-safe (loadu).
inline std::uint32_t sse2_match_block(const char* p,
                                      const unsigned char* targets,
                                      std::uint32_t n) noexcept {
    const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
    std::uint32_t m = 0;
    for (std::uint32_t i = 0; i < n; ++i) {
        m |= static_cast<std::uint32_t>(_mm_movemask_epi8(
            _mm_cmpeq_epi8(v, _mm_set1_epi8(static_cast<char>(targets[i])))));
    }
    return m;
}
#endif

} // namespace

// Mask of JSON structural bytes plus the string-quote byte: { } [ ] : , ".
// Useful with find_first_of to locate the next token separator in one shot.
inline char_mask json_structural_mask() noexcept {
    char_mask m;
    m.set('{');
    m.set('}');
    m.set('[');
    m.set(']');
    m.set(':');
    m.set(',');
    m.set('"');
    return m;
}

// Mask of the characters that terminate a JSON string literal: the closing
// quote and the escape backslash. Useful with find_first_of to locate the
// end of a string body (or an escape that needs decoding) in one shot.
inline char_mask json_string_mask() noexcept {
    char_mask m;
    m.set('"');
    m.set('\\');
    return m;
}

// Mask of XML special bytes: < > & " '.
inline char_mask xml_special_mask() noexcept {
    char_mask m;
    m.set('<');
    m.set('>');
    m.set('&');
    m.set('"');
    m.set('\'');
    return m;
}

// Mask of JSON/XML whitespace: space, tab, CR, LF.
inline char_mask whitespace_mask() noexcept {
    char_mask m;
    m.set(' ');
    m.set('\t');
    m.set('\r');
    m.set('\n');
    return m;
}

// First position in [begin, end) whose byte is in mask, or end if none.
// Precondition: begin <= end.
inline const char* find_first_of(const char* begin, const char* end,
                                 char_mask mask) noexcept {
#if defined(RTTI_DETAIL_HAS_SSE2)
    unsigned char targets[128];
    const std::uint32_t n = collect_targets(mask, targets);
    if (n == 0) return end;
    const char* p = begin;
    if (end - begin >= 16) {
        const char* last = end - 16;
        while (p <= last) {
            const std::uint32_t m = sse2_match_block(p, targets, n);
            if (m != 0) {
                return p + static_cast<std::ptrdiff_t>(first_set_bit(m));
            }
            p += 16;
        }
    }
    while (p < end) {
        if (mask.test(static_cast<unsigned char>(*p))) return p;
        ++p;
    }
    return end;
#else
    const char* p = begin;
    while (p < end) {
        if (mask.test(static_cast<unsigned char>(*p))) return p;
        ++p;
    }
    return end;
#endif
}

// First position in [begin, end) whose byte is NOT in mask, or end if every
// byte is in the mask. Precondition: begin <= end.
inline const char* find_first_not_of(const char* begin, const char* end,
                                     char_mask mask) noexcept {
#if defined(RTTI_DETAIL_HAS_SSE2)
    unsigned char targets[128];
    const std::uint32_t n = collect_targets(mask, targets);
    const char* p = begin;
    if (end - begin >= 16) {
        const char* last = end - 16;
        while (p <= last) {
            const std::uint32_t m = sse2_match_block(p, targets, n);
            const std::uint32_t nm = (~m) & 0xFFFFu;
            if (nm != 0) {
                return p + static_cast<std::ptrdiff_t>(first_set_bit(nm));
            }
            p += 16;
        }
    }
    while (p < end) {
        if (!mask.test(static_cast<unsigned char>(*p))) return p;
        ++p;
    }
    return end;
#else
    const char* p = begin;
    while (p < end) {
        if (!mask.test(static_cast<unsigned char>(*p))) return p;
        ++p;
    }
    return end;
#endif
}

// First position at or after p whose byte is not whitespace, or end if the
// rest is all whitespace. Precondition: p <= end.
inline const char* skip_whitespace(const char* p, const char* end) noexcept {
    return find_first_not_of(p, end, whitespace_mask());
}

// Returns true if the byte at p (p < end) is a JSON structural byte or the
// string-quote byte; false if p >= end or the byte is not one of them. The
// scanning counterpart is find_first_of(begin, end, json_structural_mask()).
inline bool is_structural_or_string(const char* p, const char* end) noexcept {
    if (p == nullptr || p >= end) return false;
    return json_structural_mask().test(static_cast<unsigned char>(*p));
}

} // namespace detail
} // namespace rtti
