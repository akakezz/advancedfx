#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#if !defined(AFX_SAMPLER_FORCE_SCALAR) && (defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(__SSE2__))
#include <emmintrin.h>
#define AFX_SAMPLER_SSE2 1
#else
#define AFX_SAMPLER_SSE2 0
#endif

namespace advancedfx { namespace SamplerKernels {
// The temporal integrator still decides the weights and order of operations.
// Process only the packed row; source rows may have padding or be unaligned.
enum class Operation { Add, Weighted, Pair };
inline constexpr bool HasSimd = AFX_SAMPLER_SSE2 != 0;

template<Operation Op, bool Simd = HasSimd>
inline void Bytes(float *dst, const unsigned char *a, const unsigned char *b, size_t count, float weight) {
    size_t i = 0;
#if AFX_SAMPLER_SSE2
    if constexpr (Simd) {
        const __m128 w = _mm_set1_ps(weight);
        const __m128i zero = _mm_setzero_si128();
        for (; count-i >= 16; i += 16) {
            const __m128i x = _mm_loadu_si128(reinterpret_cast<const __m128i *>(a+i));
            __m128i lo = _mm_unpacklo_epi8(x,zero), hi = _mm_unpackhi_epi8(x,zero);
            if constexpr (Op == Operation::Pair) {
                const __m128i y = _mm_loadu_si128(reinterpret_cast<const __m128i *>(b+i));
                lo = _mm_add_epi16(lo,_mm_unpacklo_epi8(y,zero));
                hi = _mm_add_epi16(hi,_mm_unpackhi_epi8(y,zero));
            }
            __m128 v0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(lo,zero));
            __m128 v1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(lo,zero));
            __m128 v2 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(hi,zero));
            __m128 v3 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(hi,zero));
            if constexpr (Op != Operation::Add) {
                v0 = _mm_mul_ps(v0,w); v1 = _mm_mul_ps(v1,w);
                v2 = _mm_mul_ps(v2,w); v3 = _mm_mul_ps(v3,w);
            }
            _mm_storeu_ps(dst+i,_mm_add_ps(_mm_loadu_ps(dst+i),v0));
            _mm_storeu_ps(dst+i+4,_mm_add_ps(_mm_loadu_ps(dst+i+4),v1));
            _mm_storeu_ps(dst+i+8,_mm_add_ps(_mm_loadu_ps(dst+i+8),v2));
            _mm_storeu_ps(dst+i+12,_mm_add_ps(_mm_loadu_ps(dst+i+12),v3));
        }
    }
#endif
    for (; i<count; ++i) {
        if constexpr (Op == Operation::Add) dst[i] = dst[i] + a[i];
        if constexpr (Op == Operation::Weighted) dst[i] = dst[i] + weight*a[i];
        if constexpr (Op == Operation::Pair) dst[i] = dst[i] + weight*(static_cast<unsigned>(a[i])+static_cast<unsigned>(b[i]));
    }
}

template<Operation Op, bool Simd = HasSimd>
inline void Floats(float *dst, const float *a, const float *b, size_t count, float weight) {
    size_t i=0;
#if AFX_SAMPLER_SSE2
    if constexpr (Simd) {
        const __m128 w=_mm_set1_ps(weight);
        for (; count-i>=4; i+=4) {
            __m128 v=_mm_loadu_ps(a+i);
            if constexpr (Op == Operation::Pair) v=_mm_add_ps(v,_mm_loadu_ps(b+i));
            if constexpr (Op != Operation::Add) v=_mm_mul_ps(v,w);
            _mm_storeu_ps(dst+i,_mm_add_ps(_mm_loadu_ps(dst+i),v));
        }
    }
#endif
    for (; i<count; ++i) {
        if constexpr (Op == Operation::Add) dst[i] = dst[i]+a[i];
        if constexpr (Op == Operation::Weighted) dst[i] = dst[i]+weight*a[i];
        if constexpr (Op == Operation::Pair) dst[i] = dst[i]+weight*(a[i]+b[i]);
    }
}
// Fuse two different-weight contributions without changing their per-pixel
// operation order: dst = (dst + wa*a) + wb*b.  No combined weight or FMA.
template<bool Simd = HasSimd>
inline void BytesWeightedPair(float *dst, const unsigned char *a, const unsigned char *b, size_t count, float wa, float wb) {
    size_t i=0;
#if AFX_SAMPLER_SSE2
    if constexpr (Simd) {
        const __m128 w1=_mm_set1_ps(wa), w2=_mm_set1_ps(wb);
        const __m128i zero=_mm_setzero_si128();
        for (; count-i>=4; i+=4) {
            int32_t x,y;
            std::memcpy(&x,a+i,4); std::memcpy(&y,b+i,4);
            const __m128 va=_mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(x),zero),zero));
            const __m128 vb=_mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(y),zero),zero));
            const __m128 first=_mm_add_ps(_mm_loadu_ps(dst+i),_mm_mul_ps(va,w1));
            _mm_storeu_ps(dst+i,_mm_add_ps(first,_mm_mul_ps(vb,w2)));
        }
    }
#endif
    for (;i<count;++i) {dst[i]=dst[i]+wa*a[i]; dst[i]=dst[i]+wb*b[i];}
}
template<bool Simd = HasSimd>
inline void FloatsWeightedPair(float *dst, const float *a, const float *b, size_t count, float wa, float wb) {
    size_t i=0;
#if AFX_SAMPLER_SSE2
    if constexpr (Simd) {
        const __m128 w1=_mm_set1_ps(wa), w2=_mm_set1_ps(wb);
        for (;count-i>=4;i+=4) {
            const __m128 first=_mm_add_ps(_mm_loadu_ps(dst+i),_mm_mul_ps(_mm_loadu_ps(a+i),w1));
            _mm_storeu_ps(dst+i,_mm_add_ps(first,_mm_mul_ps(_mm_loadu_ps(b+i),w2)));
        }
    }
#endif
    for (;i<count;++i) {dst[i]=dst[i]+wa*a[i]; dst[i]=dst[i]+wb*b[i];}
}
} }
#undef AFX_SAMPLER_SSE2
