// Shared structural-character scanning primitives implementation. The
// non-template helpers declared by <rtti/detail/scan.hpp> (find_first_of /
// find_first_not_of / skip_whitespace) with their SIMD/scalar dual path. The
// SSE2 detection block, the anonymous-namespace block helpers, and the
// RTTI_DETAIL_HAS_SSE2 gate moved here with the definitions; char_mask and the
// mask factories stay header-inline. Behavior is identical to the former
// inline header definitions.
// English comments only; C++17; no third-party dependencies.

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

#include <rtti/detail/scan.hpp>

namespace rtti {
namespace detail {

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

const char* find_first_of(const char* begin, const char* end,
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

const char* find_first_not_of(const char* begin, const char* end,
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

const char* skip_whitespace(const char* p, const char* end) noexcept {
    return find_first_not_of(p, end, whitespace_mask());
}

} // namespace detail
} // namespace rtti