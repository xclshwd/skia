/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkSwizzler_opts_DEFINED
#define SkSwizzler_opts_DEFINED

#include "SkColorPriv.h"

namespace SK_OPTS_NS {

// These variable names in these functions just pretend the input is BGRA.
// They work fine with both RGBA and BGRA.

static void premul_xxxa_portable(uint32_t dst[], const uint32_t src[], int count) {
    for (int i = 0; i < count; i++) {
        uint8_t a = src[i] >> 24,
                r = src[i] >> 16,
                g = src[i] >>  8,
                b = src[i] >>  0;
        r = (r*a+127)/255;
        g = (g*a+127)/255;
        b = (b*a+127)/255;
        dst[i] = (uint32_t)a << 24
               | (uint32_t)r << 16
               | (uint32_t)g <<  8
               | (uint32_t)b <<  0;
    }
}

static void premul_swaprb_xxxa_portable(uint32_t dst[], const uint32_t src[], int count) {
    for (int i = 0; i < count; i++) {
        uint8_t a = src[i] >> 24,
                r = src[i] >> 16,
                g = src[i] >>  8,
                b = src[i] >>  0;
        r = (r*a+127)/255;
        g = (g*a+127)/255;
        b = (b*a+127)/255;
        dst[i] = (uint32_t)a << 24
               | (uint32_t)b << 16
               | (uint32_t)g <<  8
               | (uint32_t)r <<  0;
    }
}

static void swaprb_xxxa_portable(uint32_t dst[], const uint32_t src[], int count) {
    for (int i = 0; i < count; i++) {
        uint8_t a = src[i] >> 24,
                r = src[i] >> 16,
                g = src[i] >>  8,
                b = src[i] >>  0;
        dst[i] = (uint32_t)a << 24
               | (uint32_t)b << 16
               | (uint32_t)g <<  8
               | (uint32_t)r <<  0;
    }
}

#if defined(SK_ARM_HAS_NEON)

// Rounded divide by 255, (x + 127) / 255
static uint8x8_t div255_round(uint16x8_t x) {
    // result = (x + 127) / 255
    // result = (x + 127) / 256 + error1
    //
    // error1 = (x + 127) / (255 * 256)
    // error1 = (x + 127) / (256 * 256) + error2
    //
    // error2 = (x + 127) / (255 * 256 * 256)
    //
    // The maximum value of error2 is too small to matter.  Thus:
    // result = (x + 127) / 256 + (x + 127) / (256 * 256)
    // result = ((x + 127) / 256 + x + 127) / 256
    // result = ((x + 127) >> 8 + x + 127) >> 8
    //
    // Use >>> to represent "rounded right shift" which, conveniently,
    // NEON supports in one instruction.
    // result = ((x >>> 8) + x) >>> 8
    //
    // Note that the second right shift is actually performed as an
    // "add, round, and narrow back to 8-bits" instruction.
    return vraddhn_u16(x, vrshrq_n_u16(x, 8));
}

// Scale a byte by another, (x * y + 127) / 255
static uint8x8_t scale(uint8x8_t x, uint8x8_t y) {
    return div255_round(vmull_u8(x, y));
}

template <bool kSwapRB>
static void premul_xxxa_should_swaprb(uint32_t dst[], const uint32_t src[], int count) {
    while (count >= 8) {
        // Load 8 pixels.
        uint8x8x4_t bgra = vld4_u8((const uint8_t*) src);

        uint8x8_t a = bgra.val[3],
                  r = bgra.val[2],
                  g = bgra.val[1],
                  b = bgra.val[0];

        // Premultiply.
        r = scale(r, a);
        g = scale(g, a);
        b = scale(b, a);

        // Store 8 premultiplied pixels.
        if (kSwapRB) {
            bgra.val[2] = b;
            bgra.val[1] = g;
            bgra.val[0] = r;
        } else {
            bgra.val[2] = r;
            bgra.val[1] = g;
            bgra.val[0] = b;
        }
        vst4_u8((uint8_t*) dst, bgra);
        src += 8;
        dst += 8;
        count -= 8;
    }

    // Call portable code to finish up the tail of [0,8) pixels.
    auto proc = kSwapRB ? premul_swaprb_xxxa_portable : premul_xxxa_portable;
    proc(dst, src, count);
}

static void premul_xxxa(uint32_t dst[], const uint32_t src[], int count) {
    premul_xxxa_should_swaprb<false>(dst, src, count);
}

static void premul_swaprb_xxxa(uint32_t dst[], const uint32_t src[], int count) {
    premul_xxxa_should_swaprb<true>(dst, src, count);
}

static void swaprb_xxxa(uint32_t dst[], const uint32_t src[], int count) {
    while (count >= 16) {
        // Load 16 pixels.
        uint8x16x4_t bgra = vld4q_u8((const uint8_t*) src);

        // Swap r and b.
        SkTSwap(bgra.val[0], bgra.val[2]);

        // Store 16 pixels.
        vst4q_u8((uint8_t*) dst, bgra);
        src += 16;
        dst += 16;
        count -= 16;
    }

    if (count >= 8) {
        // Load 8 pixels.
        uint8x8x4_t bgra = vld4_u8((const uint8_t*) src);

        // Swap r and b.
        SkTSwap(bgra.val[0], bgra.val[2]);

        // Store 8 pixels.
        vst4_u8((uint8_t*) dst, bgra);
        src += 8;
        dst += 8;
        count -= 8;
    }

    swaprb_xxxa_portable(dst, src, count);
}

#elif SK_CPU_SSE_LEVEL >= SK_CPU_SSE_LEVEL_SSSE3

template <bool kSwapRB>
static void premul_xxxa_should_swaprb(uint32_t dst[], const uint32_t src[], int count) {

    auto premul8 = [](__m128i* lo, __m128i* hi) {
        const __m128i zeros = _mm_setzero_si128();
        const __m128i _128 = _mm_set1_epi16(128);
        const __m128i _257 = _mm_set1_epi16(257);
        __m128i planar;
        if (kSwapRB) {
            planar = _mm_setr_epi8(2,6,10,14, 1,5,9,13, 0,4,8,12, 3,7,11,15);
        } else {
            planar = _mm_setr_epi8(0,4,8,12, 1,5,9,13, 2,6,10,14, 3,7,11,15);
        }

        // Swizzle the pixels to 8-bit planar.
        *lo = _mm_shuffle_epi8(*lo, planar);                      // bbbbgggg rrrraaaa
        *hi = _mm_shuffle_epi8(*hi, planar);                      // BBBBGGGG RRRRAAAA
        __m128i bg = _mm_unpacklo_epi32(*lo, *hi),                // bbbbBBBB ggggGGGG
                ra = _mm_unpackhi_epi32(*lo, *hi);                // rrrrRRRR aaaaAAAA

        // Unpack to 16-bit planar.
        __m128i b = _mm_unpacklo_epi8(bg, zeros),                 // b_b_b_b_ B_B_B_B_
                g = _mm_unpackhi_epi8(bg, zeros),                 // g_g_g_g_ G_G_G_G_
                r = _mm_unpacklo_epi8(ra, zeros),                 // r_r_r_r_ R_R_R_R_
                a = _mm_unpackhi_epi8(ra, zeros);                 // a_a_a_a_ A_A_A_A_

        // Premultiply!  (x+127)/255 == ((x+128)*257)>>16 for 0 <= x <= 255*255.
        b = _mm_mulhi_epu16(_mm_add_epi16(_mm_mullo_epi16(b, a), _128), _257);
        g = _mm_mulhi_epu16(_mm_add_epi16(_mm_mullo_epi16(g, a), _128), _257);
        r = _mm_mulhi_epu16(_mm_add_epi16(_mm_mullo_epi16(r, a), _128), _257);

        // Repack into interlaced pixels.
        bg = _mm_or_si128(b, _mm_slli_epi16(g, 8));               // bgbgbgbg BGBGBGBG
        ra = _mm_or_si128(r, _mm_slli_epi16(a, 8));               // rararara RARARARA
        *lo = _mm_unpacklo_epi16(bg, ra);                         // bgrabgra bgrabgra
        *hi = _mm_unpackhi_epi16(bg, ra);                         // BRGABGRA BGRABGRA
    };

    while (count >= 8) {
        __m128i lo = _mm_loadu_si128((const __m128i*) (src + 0)),
                hi = _mm_loadu_si128((const __m128i*) (src + 4));

        premul8(&lo, &hi);

        _mm_storeu_si128((__m128i*) (dst + 0), lo);
        _mm_storeu_si128((__m128i*) (dst + 4), hi);

        src += 8;
        dst += 8;
        count -= 8;
    }

    if (count >= 4) {
        __m128i lo = _mm_loadu_si128((const __m128i*) src),
                hi = _mm_setzero_si128();

        premul8(&lo, &hi);

        _mm_storeu_si128((__m128i*) dst, lo);

        src += 4;
        dst += 4;
        count -= 4;
    }

    // Call portable code to finish up the tail of [0,4) pixels.
    auto proc = kSwapRB ? premul_swaprb_xxxa_portable : premul_xxxa_portable;
    proc(dst, src, count);
}

static void premul_xxxa(uint32_t dst[], const uint32_t src[], int count) {
    premul_xxxa_should_swaprb<false>(dst, src, count);
}

static void premul_swaprb_xxxa(uint32_t dst[], const uint32_t src[], int count) {
    premul_xxxa_should_swaprb<true>(dst, src, count);
}

static void swaprb_xxxa(uint32_t dst[], const uint32_t src[], int count) {
    const __m128i swapRB = _mm_setr_epi8(2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15);

    while (count >= 4) {
        __m128i bgra = _mm_loadu_si128((const __m128i*) src);
        __m128i rgba = _mm_shuffle_epi8(bgra, swapRB);
        _mm_storeu_si128((__m128i*) dst, rgba);

        src += 4;
        dst += 4;
        count -= 4;
    }

    swaprb_xxxa_portable(dst, src, count);
}

#else

static void premul_xxxa(uint32_t dst[], const uint32_t src[], int count) {
    premul_xxxa_portable(dst, src, count);
}

static void premul_swaprb_xxxa(uint32_t dst[], const uint32_t src[], int count) {
    premul_swaprb_xxxa_portable(dst, src, count);
}

static void swaprb_xxxa(uint32_t dst[], const uint32_t src[], int count) {
    swaprb_xxxa_portable(dst, src, count);
}

#endif

}

#endif // SkSwizzler_opts_DEFINED