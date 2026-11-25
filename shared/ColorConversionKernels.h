#pragma once
#include <cstddef>
#include <cstring>
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#include <tmmintrin.h>
#define AFX_COLOR_SSSE3 1
#define AFX_COLOR_TARGET
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))
#include <tmmintrin.h>
#define AFX_COLOR_SSSE3 1
#define AFX_COLOR_TARGET __attribute__((target("ssse3")))
#else
#define AFX_COLOR_SSSE3 0
#define AFX_COLOR_TARGET
#endif

namespace advancedfx { namespace ColorConversion {
inline void StripAlphaScalar(const unsigned char *src, unsigned char *dst, size_t pixels) noexcept {
    for (size_t x=0;x<pixels;++x,src+=4,dst+=3) {
        dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2];
    }
}
inline bool HasSsse3() noexcept {
#if AFX_COLOR_SSSE3 && defined(_MSC_VER)
    static const bool supported=[] { int regs[4]; __cpuid(regs,0); if (regs[0]<1) return false; __cpuid(regs,1); return (regs[2] & (1<<9))!=0; }();
    return supported;
#elif AFX_COLOR_SSSE3
    static const bool supported=__builtin_cpu_supports("ssse3");
    return supported;
#else
    return false;
#endif
}
#if AFX_COLOR_SSSE3
// 16 pixels: four 16-byte loads, three 16-byte stores. No overread or
// overwrite at the end of a row, and no alignment requirement.
AFX_COLOR_TARGET inline void StripAlphaSsse3(const unsigned char *src, unsigned char *dst, size_t pixels) noexcept {
    const __m128i mask=_mm_setr_epi8(0,1,2,4,5,6,8,9,10,12,13,14,-1,-1,-1,-1);
    for (;pixels>=16; pixels-=16,src+=64,dst+=48) {
        const __m128i a=_mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src)),mask);
        const __m128i b=_mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src+16)),mask);
        const __m128i c=_mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src+32)),mask);
        const __m128i d=_mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src+48)),mask);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst),_mm_or_si128(a,_mm_slli_si128(b,12)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst+16),_mm_or_si128(_mm_srli_si128(b,4),_mm_slli_si128(c,8)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst+32),_mm_or_si128(_mm_srli_si128(c,8),_mm_slli_si128(d,4)));
    }
    StripAlphaScalar(src,dst,pixels);
}
#endif
inline void StripAlpha(const unsigned char *src, unsigned char *dst, size_t pixels) noexcept {
#if AFX_COLOR_SSSE3
    if (HasSsse3()) { StripAlphaSsse3(src,dst,pixels); return; }
#endif
    StripAlphaScalar(src,dst,pixels);
}
} }
#undef AFX_COLOR_TARGET
#undef AFX_COLOR_SSSE3
