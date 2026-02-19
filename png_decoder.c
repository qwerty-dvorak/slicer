#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

#include "png_decoder.h"

static const uint8_t k_png_sig[8]
    = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

static uint32_t
read_be32 (const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
           | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int
load_file_bytes (const char *path, uint8_t **out, size_t *out_size)
{
    FILE *f;
    long end_pos;
    size_t size;
    uint8_t *buf;

    *out = NULL;
    *out_size = 0;

    f = fopen (path, "rb");
    if (!f)
        {
            return 0;
        }
    if (fseek (f, 0, SEEK_END) != 0)
        {
            fclose (f);
            return 0;
        }
    end_pos = ftell (f);
    if (end_pos < 0)
        {
            fclose (f);
            return 0;
        }
    if ((unsigned long)end_pos > (unsigned long)SIZE_MAX)
        {
            fclose (f);
            return 0;
        }
    if (fseek (f, 0, SEEK_SET) != 0)
        {
            fclose (f);
            return 0;
        }

    size = (size_t)end_pos;
    if (size == 0)
        {
            fclose (f);
            return 1;
        }

    buf = (uint8_t *)malloc (size);
    if (!buf)
        {
            fclose (f);
            return 0;
        }
    if (fread (buf, 1, size, f) != size)
        {
            free (buf);
            fclose (f);
            return 0;
        }
    fclose (f);

    *out = buf;
    *out_size = size;
    return 1;
}

static int
ensure_capacity (uint8_t **buf, size_t *cap, size_t need)
{
    uint8_t *new_buf;
    size_t new_cap;

    if (need <= *cap)
        {
            return 1;
        }

    new_cap = (*cap == 0) ? 4096U : *cap;
    while (new_cap < need)
        {
            if (new_cap > (SIZE_MAX / 2U))
                {
                    new_cap = need;
                    break;
                }
            new_cap *= 2U;
        }

    new_buf = (uint8_t *)realloc (*buf, new_cap);
    if (!new_buf)
        {
            return 0;
        }
    *buf = new_buf;
    *cap = new_cap;
    return 1;
}

static int
append_bytes (
    uint8_t **dst,
    size_t *dst_size,
    size_t *dst_cap,
    const uint8_t *src,
    size_t src_size
)
{
    size_t new_size;

    if (src_size == 0)
        {
            return 1;
        }
    if (*dst_size > SIZE_MAX - src_size)
        {
            return 0;
        }
    new_size = *dst_size + src_size;
    if (!ensure_capacity (dst, dst_cap, new_size))
        {
            return 0;
        }
    memcpy (*dst + *dst_size, src, src_size);
    *dst_size = new_size;
    return 1;
}

#if defined(__x86_64__) || defined(__i386__)
static int
cpu_has_avx2 (void)
{
#if defined(__GNUC__) || defined(__clang__)
    static int init = 0;
    static int has = 0;
    if (!init)
        {
            __builtin_cpu_init ();
            has = __builtin_cpu_supports ("avx2");
            init = 1;
        }
    return has;
#else
    return 0;
#endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__ ((target ("avx2")))
#endif
static void
add_bytes_avx2 (uint8_t *dst, const uint8_t *a, const uint8_t *b, size_t n)
{
    size_t i = 0;

    for (; i + 32U <= n; i += 32U)
        {
            __m256i va = _mm256_loadu_si256 ((const __m256i *)(a + i));
            __m256i vb = _mm256_loadu_si256 ((const __m256i *)(b + i));
            __m256i vc = _mm256_add_epi8 (va, vb);
            _mm256_storeu_si256 ((__m256i *)(dst + i), vc);
        }
    for (; i < n; i++)
        {
            dst[i] = (uint8_t)(a[i] + b[i]);
        }
}
#else
static int
cpu_has_avx2 (void)
{
    return 0;
}
#endif

#if defined(__x86_64__) || defined(__i386__)
static int
cpu_has_ssse3 (void)
{
#if defined(__GNUC__) || defined(__clang__)
    static int init = 0;
    static int has = 0;
    if (!init)
        {
            __builtin_cpu_init ();
            has = __builtin_cpu_supports ("ssse3");
            init = 1;
        }
    return has;
#else
    return 0;
#endif
}
#else
static int
cpu_has_ssse3 (void)
{
    return 0;
}
#endif

static uint16_t g_abs255[511];
static uint16_t g_abs510[1021];
static pthread_once_t g_paeth_once = PTHREAD_ONCE_INIT;

static void
init_paeth_tables_once (void)
{
    int i;

    for (i = -255; i <= 255; i++)
        {
            g_abs255[i + 255] = (uint16_t)((i < 0) ? -i : i);
        }
    for (i = -510; i <= 510; i++)
        {
            g_abs510[i + 510] = (uint16_t)((i < 0) ? -i : i);
        }
}

/*
 * Two Paeth implementations:
 *   PAETH_USE_ARITHMETIC  – pure ALU, zero memory traffic
 *   default (table)       – three 16-bit LUT loads from L1-resident arrays
 *
 * Build with  -DPAETH_USE_ARITHMETIC  to select the branchless ALU path.
 * On cores with fast L1 (≤4 c latency) the table path often wins;
 * on high-OOO / wide-issue cores the arithmetic path can be faster.
 */
#ifdef PAETH_USE_ARITHMETIC
static inline __attribute__ ((always_inline)) uint8_t
paeth_predictor (uint8_t a, uint8_t b, uint8_t c)
{
    /* All arithmetic, no memory accesses.  Compiles to SUB + CMOV chains. */
    int p = (int)a + (int)b - (int)c;
    int pa = p - (int)a; /* == b - c */
    int pb = p - (int)b; /* == a - c */
    int pc = p - (int)c; /* == a + b - 2c */
    if (pa < 0)
        pa = -pa;
    if (pb < 0)
        pb = -pb;
    if (pc < 0)
        pc = -pc;
    if (pa <= pb && pa <= pc)
        return a;
    if (pb <= pc)
        return b;
    return c;
}
#else
static inline __attribute__ ((always_inline)) uint8_t
paeth_predictor (uint8_t a, uint8_t b, uint8_t c)
{
    int pa = (int)g_abs255[(int)b - (int)c + 255];
    int pb = (int)g_abs255[(int)a - (int)c + 255];
    int pc = (int)g_abs510[(int)a + (int)b - ((int)c << 1) + 510];
    return (pa <= pb && pa <= pc) ? a : ((pb <= pc) ? b : c);
}
#endif

static int
unfilter_row_bpp4 (
    uint8_t *row_dst,
    const uint8_t *row_src,
    const uint8_t *prev,
    size_t row_bytes
)
{
    uint8_t filter = row_src[0];
    const uint8_t *src = row_src + 1U;
    size_t x;

    switch (filter)
        {
        case 0:
            memcpy (row_dst, src, row_bytes);
            return 1;
        case 1:
            if (row_bytes == 0)
                {
                    return 1;
                }
            row_dst[0] = src[0];
            row_dst[1] = src[1];
            row_dst[2] = src[2];
            row_dst[3] = src[3];
            for (x = 4; x < row_bytes; x += 4)
                {
                    row_dst[x + 0] = (uint8_t)(src[x + 0] + row_dst[x - 4]);
                    row_dst[x + 1] = (uint8_t)(src[x + 1] + row_dst[x - 3]);
                    row_dst[x + 2] = (uint8_t)(src[x + 2] + row_dst[x - 2]);
                    row_dst[x + 3] = (uint8_t)(src[x + 3] + row_dst[x - 1]);
                }
            return 1;
        case 2:
            if (!prev)
                {
                    memcpy (row_dst, src, row_bytes);
                    return 1;
                }
#if defined(__x86_64__) || defined(__i386__)
            if (row_bytes >= 64U && cpu_has_avx2 ())
                {
                    add_bytes_avx2 (row_dst, src, prev, row_bytes);
                    return 1;
                }
#endif
            for (x = 0; x < row_bytes; x++)
                {
                    row_dst[x] = (uint8_t)(src[x] + prev[x]);
                }
            return 1;
        case 3:
            if (!prev)
                {
                    if (row_bytes == 0)
                        {
                            return 1;
                        }
                    row_dst[0] = src[0];
                    row_dst[1] = src[1];
                    row_dst[2] = src[2];
                    row_dst[3] = src[3];
                    for (x = 4; x < row_bytes; x += 4)
                        {
                            row_dst[x + 0]
                                = (uint8_t)(src[x + 0]
                                            + (uint8_t)(row_dst[x - 4] >> 1));
                            row_dst[x + 1]
                                = (uint8_t)(src[x + 1]
                                            + (uint8_t)(row_dst[x - 3] >> 1));
                            row_dst[x + 2]
                                = (uint8_t)(src[x + 2]
                                            + (uint8_t)(row_dst[x - 2] >> 1));
                            row_dst[x + 3]
                                = (uint8_t)(src[x + 3]
                                            + (uint8_t)(row_dst[x - 1] >> 1));
                        }
                    return 1;
                }
            row_dst[0] = (uint8_t)(src[0] + (uint8_t)(prev[0] >> 1));
            row_dst[1] = (uint8_t)(src[1] + (uint8_t)(prev[1] >> 1));
            row_dst[2] = (uint8_t)(src[2] + (uint8_t)(prev[2] >> 1));
            row_dst[3] = (uint8_t)(src[3] + (uint8_t)(prev[3] >> 1));
            for (x = 4; x < row_bytes; x += 4)
                {
                    row_dst[x + 0] = (uint8_t)(src[x + 0]
                                               + (uint8_t)(((int)row_dst[x - 4]
                                                            + (int)prev[x + 0])
                                                           >> 1));
                    row_dst[x + 1] = (uint8_t)(src[x + 1]
                                               + (uint8_t)(((int)row_dst[x - 3]
                                                            + (int)prev[x + 1])
                                                           >> 1));
                    row_dst[x + 2] = (uint8_t)(src[x + 2]
                                               + (uint8_t)(((int)row_dst[x - 2]
                                                            + (int)prev[x + 2])
                                                           >> 1));
                    row_dst[x + 3] = (uint8_t)(src[x + 3]
                                               + (uint8_t)(((int)row_dst[x - 1]
                                                            + (int)prev[x + 3])
                                                           >> 1));
                }
            return 1;
        case 4:
            if (!prev)
                {
                    if (row_bytes == 0)
                        {
                            return 1;
                        }
                    row_dst[0] = src[0];
                    row_dst[1] = src[1];
                    row_dst[2] = src[2];
                    row_dst[3] = src[3];
                    for (x = 4; x < row_bytes; x += 4)
                        {
                            row_dst[x + 0]
                                = (uint8_t)(src[x + 0] + row_dst[x - 4]);
                            row_dst[x + 1]
                                = (uint8_t)(src[x + 1] + row_dst[x - 3]);
                            row_dst[x + 2]
                                = (uint8_t)(src[x + 2] + row_dst[x - 2]);
                            row_dst[x + 3]
                                = (uint8_t)(src[x + 3] + row_dst[x - 1]);
                        }
                    return 1;
                }
            row_dst[0] = (uint8_t)(src[0] + prev[0]);
            row_dst[1] = (uint8_t)(src[1] + prev[1]);
            row_dst[2] = (uint8_t)(src[2] + prev[2]);
            row_dst[3] = (uint8_t)(src[3] + prev[3]);
            for (x = 4; x < row_bytes; x += 4)
                {
                    row_dst[x + 0] = (uint8_t)(src[x + 0]
                                               + paeth_predictor (
                                                   row_dst[x - 4],
                                                   prev[x + 0],
                                                   prev[x - 4]
                                               ));
                    row_dst[x + 1] = (uint8_t)(src[x + 1]
                                               + paeth_predictor (
                                                   row_dst[x - 3],
                                                   prev[x + 1],
                                                   prev[x - 3]
                                               ));
                    row_dst[x + 2] = (uint8_t)(src[x + 2]
                                               + paeth_predictor (
                                                   row_dst[x - 2],
                                                   prev[x + 2],
                                                   prev[x - 2]
                                               ));
                    row_dst[x + 3] = (uint8_t)(src[x + 3]
                                               + paeth_predictor (
                                                   row_dst[x - 1],
                                                   prev[x + 3],
                                                   prev[x - 1]
                                               ));
                }
            return 1;
        default:
            return 0;
        }
}

static int
unfilter_row_bpp3 (
    uint8_t *row_dst,
    const uint8_t *row_src,
    const uint8_t *prev,
    size_t row_bytes
)
{
    uint8_t filter = row_src[0];
    const uint8_t *src = row_src + 1U;
    size_t x;

    switch (filter)
        {
        case 0:
            memcpy (row_dst, src, row_bytes);
            return 1;
        case 1:
            if (row_bytes < 3U)
                {
                    return 1;
                }
            row_dst[0] = src[0];
            row_dst[1] = src[1];
            row_dst[2] = src[2];
            {
                const uint8_t *s = src + 3U;
                uint8_t *d = row_dst + 3U;
                uint8_t *d_end = row_dst + row_bytes;
                for (; d < d_end; d += 3, s += 3)
                    {
                        d[0] = (uint8_t)(s[0] + d[-3]);
                        d[1] = (uint8_t)(s[1] + d[-2]);
                        d[2] = (uint8_t)(s[2] + d[-1]);
                    }
            }
            return 1;
        case 2:
            if (!prev)
                {
                    memcpy (row_dst, src, row_bytes);
                    return 1;
                }
#if defined(__x86_64__) || defined(__i386__)
            if (row_bytes >= 64U && cpu_has_avx2 ())
                {
                    add_bytes_avx2 (row_dst, src, prev, row_bytes);
                    return 1;
                }
#endif
            for (x = 0; x < row_bytes; x++)
                {
                    row_dst[x] = (uint8_t)(src[x] + prev[x]);
                }
            return 1;
        case 3:
            if (!prev)
                {
                    if (row_bytes < 3U)
                        {
                            return 1;
                        }
                    row_dst[0] = src[0];
                    row_dst[1] = src[1];
                    row_dst[2] = src[2];
                    {
                        const uint8_t *s = src + 3U;
                        uint8_t *d = row_dst + 3U;
                        uint8_t *d_end = row_dst + row_bytes;
                        for (; d < d_end; d += 3, s += 3)
                            {
                                d[0] = (uint8_t)(s[0] + (uint8_t)(d[-3] >> 1));
                                d[1] = (uint8_t)(s[1] + (uint8_t)(d[-2] >> 1));
                                d[2] = (uint8_t)(s[2] + (uint8_t)(d[-1] >> 1));
                            }
                    }
                    return 1;
                }
            row_dst[0] = (uint8_t)(src[0] + (uint8_t)(prev[0] >> 1));
            row_dst[1] = (uint8_t)(src[1] + (uint8_t)(prev[1] >> 1));
            row_dst[2] = (uint8_t)(src[2] + (uint8_t)(prev[2] >> 1));
            {
                const uint8_t *s = src + 3U;
                const uint8_t *p = prev + 3U;
                uint8_t *d = row_dst + 3U;
                uint8_t *d_end = row_dst + row_bytes;
                for (; d < d_end; d += 3, s += 3, p += 3)
                    {
                        d[0] = (uint8_t)(s[0]
                                         + (uint8_t)(((int)d[-3] + (int)p[0])
                                                     >> 1));
                        d[1] = (uint8_t)(s[1]
                                         + (uint8_t)(((int)d[-2] + (int)p[1])
                                                     >> 1));
                        d[2] = (uint8_t)(s[2]
                                         + (uint8_t)(((int)d[-1] + (int)p[2])
                                                     >> 1));
                    }
            }
            return 1;
        case 4:
            if (!prev)
                {
                    if (row_bytes < 3U)
                        {
                            return 1;
                        }
                    row_dst[0] = src[0];
                    row_dst[1] = src[1];
                    row_dst[2] = src[2];
                    {
                        const uint8_t *s = src + 3U;
                        uint8_t *d = row_dst + 3U;
                        uint8_t *d_end = row_dst + row_bytes;
                        for (; d < d_end; d += 3, s += 3)
                            {
                                d[0] = (uint8_t)(s[0] + d[-3]);
                                d[1] = (uint8_t)(s[1] + d[-2]);
                                d[2] = (uint8_t)(s[2] + d[-1]);
                            }
                    }
                    return 1;
                }
            row_dst[0] = (uint8_t)(src[0] + prev[0]);
            row_dst[1] = (uint8_t)(src[1] + prev[1]);
            row_dst[2] = (uint8_t)(src[2] + prev[2]);
            {
                const uint8_t *s = src + 3U;
                const uint8_t *p = prev + 3U;
                uint8_t *d = row_dst + 3U;
                uint8_t *d_end = row_dst + row_bytes;
                for (; d < d_end; d += 3, s += 3, p += 3)
                    {
                        d[0] = (uint8_t)(s[0]
                                         + paeth_predictor (
                                             d[-3], p[0], p[-3]
                                         ));
                        d[1] = (uint8_t)(s[1]
                                         + paeth_predictor (
                                             d[-2], p[1], p[-2]
                                         ));
                        d[2] = (uint8_t)(s[2]
                                         + paeth_predictor (
                                             d[-1], p[2], p[-1]
                                         ));
                    }
            }
            return 1;
        default:
            return 0;
        }
}

static int
unfilter_row (
    uint8_t *row_dst,
    const uint8_t *row_with_filter,
    const uint8_t *prev,
    size_t row_bytes,
    size_t bpp
)
{
    const uint8_t *row_src = row_with_filter;
    size_t x;

    if (bpp == 4U)
        {
            return unfilter_row_bpp4 (row_dst, row_src, prev, row_bytes);
        }
    if (bpp == 3U)
        {
            return unfilter_row_bpp3 (row_dst, row_src, prev, row_bytes);
        }

    row_src = row_with_filter + 1U;
    switch (row_with_filter[0])
        {
        case 0:
            memcpy (row_dst, row_src, row_bytes);
            return 1;
        case 1:
            for (x = 0; x < row_bytes; x++)
                {
                    uint8_t left = (x >= bpp) ? row_dst[x - bpp] : 0;
                    row_dst[x] = (uint8_t)(row_src[x] + left);
                }
            return 1;
        case 2:
            if (!prev)
                {
                    memcpy (row_dst, row_src, row_bytes);
                    return 1;
                }
            for (x = 0; x < row_bytes; x++)
                {
                    row_dst[x] = (uint8_t)(row_src[x] + prev[x]);
                }
            return 1;
        case 3:
            for (x = 0; x < row_bytes; x++)
                {
                    uint8_t left = (x >= bpp) ? row_dst[x - bpp] : 0;
                    uint8_t up = prev ? prev[x] : 0;
                    row_dst[x]
                        = (uint8_t)(row_src[x]
                                    + (uint8_t)(((int)left + (int)up) >> 1));
                }
            return 1;
        case 4:
            for (x = 0; x < row_bytes; x++)
                {
                    uint8_t left = (x >= bpp) ? row_dst[x - bpp] : 0;
                    uint8_t up = prev ? prev[x] : 0;
                    uint8_t up_left = (prev && x >= bpp) ? prev[x - bpp] : 0;
                    row_dst[x]
                        = (uint8_t)(row_src[x]
                                    + paeth_predictor (left, up, up_left));
                }
            return 1;
        default:
            return 0;
        }
}

#if defined(__x86_64__) || defined(__i386__)
#if defined(__GNUC__) || defined(__clang__)
__attribute__ ((target ("ssse3")))
#endif
static void
convert_rgb_rows_to_rgba_ssse3 (
    uint8_t *restrict rgba,
    const uint8_t *restrict scan,
    uint32_t width,
    size_t y0,
    size_t y1
)
{
    size_t row_bytes = (size_t)width * 3U;
    size_t out_row_bytes = (size_t)width * 4U;
    size_t y;
    /* Shuffle mask: [R0 G0 B0 R1 G1 B1 R2 G2 B2 R3 G3 B3 .. .. .. ..]
       ->            [R0 G0 B0 00 R1 G1 B1 00 R2 G2 B2 00 R3 G3 B3 00] */
    __m128i shuf
        = _mm_setr_epi8 (0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1);
    __m128i alpha = _mm_set1_epi32 ((int)0xFF000000);

    for (y = y0; y < y1; y++)
        {
            const uint8_t *in = scan + y * row_bytes;
            uint8_t *out = rgba + y * out_row_bytes;
            size_t x = 0;

            /* SSSE3: 4 pixels per iteration (load 16 bytes, use 12) */
            for (; x + 4 <= (size_t)width; x += 4)
                {
                    __m128i rgb
                        = _mm_loadu_si128 ((const __m128i *)(in + x * 3));
                    __m128i rgba_v = _mm_shuffle_epi8 (rgb, shuf);
                    rgba_v = _mm_or_si128 (rgba_v, alpha);
                    _mm_storeu_si128 ((__m128i *)(out + x * 4), rgba_v);
                }
            /* Scalar tail */
            for (; x < (size_t)width; x++)
                {
                    out[x * 4 + 0] = in[x * 3 + 0];
                    out[x * 4 + 1] = in[x * 3 + 1];
                    out[x * 4 + 2] = in[x * 3 + 2];
                    out[x * 4 + 3] = 255U;
                }
        }
}
#endif

static void __attribute__ ((hot))
convert_rgb_rows_to_rgba (
    uint8_t *restrict rgba,
    const uint8_t *restrict scan,
    uint32_t width,
    size_t y0,
    size_t y1,
    int has_trns,
    uint8_t tr,
    uint8_t tg,
    uint8_t tb
)
{
    size_t row_bytes = (size_t)width * 3U;
    size_t out_row_bytes = (size_t)width * 4U;
    size_t y;

#if defined(__x86_64__) || defined(__i386__)
    if (!has_trns && cpu_has_ssse3 ())
        {
            convert_rgb_rows_to_rgba_ssse3 (rgba, scan, width, y0, y1);
            return;
        }
#endif

    for (y = y0; y < y1; y++)
        {
            const uint8_t *in = scan + y * row_bytes;
            uint8_t *out = rgba + y * out_row_bytes;
            size_t x;

            if (has_trns)
                {
                    for (x = 0; x < (size_t)width; x++)
                        {
                            uint8_t r = in[0];
                            uint8_t g = in[1];
                            uint8_t b = in[2];
                            out[0] = r;
                            out[1] = g;
                            out[2] = b;
                            out[3]
                                = (r == tr && g == tg && b == tb) ? 0U : 255U;
                            in += 3;
                            out += 4;
                        }
                }
            else
                {
                    for (x = 0; x < (size_t)width; x++)
                        {
                            out[0] = in[0];
                            out[1] = in[1];
                            out[2] = in[2];
                            out[3] = 255U;
                            in += 3;
                            out += 4;
                        }
                }
        }
}

static int
configured_png_threads (void)
{
    static int initialized = 0;
    static int threads = 1;

    if (!initialized)
        {
            const char *env = getenv ("SLICER_PNG_THREADS");
            if (env && env[0] != '\0')
                {
                    char *end = NULL;
                    long v = strtol (env, &end, 10);
                    if (end && *end == '\0' && v > 0 && v <= 128)
                        {
                            threads = (int)v;
                        }
                }
            initialized = 1;
        }
    return threads;
}

typedef struct
{
    uint8_t *rgba;
    const uint8_t *scan;
    uint32_t width;
    size_t y0;
    size_t y1;
    int has_trns;
    uint8_t tr;
    uint8_t tg;
    uint8_t tb;
} rgb_expand_task_t;

static void *
rgb_expand_worker (void *arg)
{
    rgb_expand_task_t *task = (rgb_expand_task_t *)arg;
    convert_rgb_rows_to_rgba (
        task->rgba,
        task->scan,
        task->width,
        task->y0,
        task->y1,
        task->has_trns,
        task->tr,
        task->tg,
        task->tb
    );
    return NULL;
}

static void
convert_rgb_to_rgba_mt (
    uint8_t *rgba,
    const uint8_t *scan,
    uint32_t width,
    uint32_t height,
    int has_trns,
    uint8_t tr,
    uint8_t tg,
    uint8_t tb
)
{
    int req_threads = configured_png_threads ();
    size_t rows = (size_t)height;
    size_t pixels = (size_t)width * rows;
    size_t thread_count;
    size_t i;

    if (req_threads <= 1 || rows < 64U || pixels < 400000U)
        {
            convert_rgb_rows_to_rgba (
                rgba, scan, width, 0, rows, has_trns, tr, tg, tb
            );
            return;
        }

    thread_count = (size_t)req_threads;
    if (thread_count > rows)
        {
            thread_count = rows;
        }

    if (thread_count <= 1)
        {
            convert_rgb_rows_to_rgba (
                rgba, scan, width, 0, rows, has_trns, tr, tg, tb
            );
            return;
        }

    {
        pthread_t *threads
            = (pthread_t *)malloc ((thread_count - 1U) * sizeof (*threads));
        rgb_expand_task_t *tasks
            = (rgb_expand_task_t *)malloc (thread_count * sizeof (*tasks));
        size_t launched = 0;
        size_t chunk = (rows + thread_count - 1U) / thread_count;

        if (!threads || !tasks)
            {
                free (tasks);
                free (threads);
                convert_rgb_rows_to_rgba (
                    rgba, scan, width, 0, rows, has_trns, tr, tg, tb
                );
                return;
            }

        for (i = 0; i < thread_count; i++)
            {
                size_t y0 = i * chunk;
                size_t y1 = y0 + chunk;
                if (y0 > rows)
                    {
                        y0 = rows;
                    }
                if (y1 > rows)
                    {
                        y1 = rows;
                    }
                tasks[i].rgba = rgba;
                tasks[i].scan = scan;
                tasks[i].width = width;
                tasks[i].y0 = y0;
                tasks[i].y1 = y1;
                tasks[i].has_trns = has_trns;
                tasks[i].tr = tr;
                tasks[i].tg = tg;
                tasks[i].tb = tb;
            }

        for (i = 1; i < thread_count; i++)
            {
                if (pthread_create (
                        &threads[i - 1U], NULL, rgb_expand_worker, &tasks[i]
                    )
                    != 0)
                    {
                        break;
                    }
                launched++;
            }

        rgb_expand_worker (&tasks[0]);
        for (i = 0; i < launched; i++)
            {
                pthread_join (threads[i], NULL);
            }

        if (i < thread_count - 1U)
            {
                size_t failed_idx = i + 1U;
                for (; failed_idx < thread_count; failed_idx++)
                    {
                        convert_rgb_rows_to_rgba (
                            rgba,
                            scan,
                            width,
                            tasks[failed_idx].y0,
                            tasks[failed_idx].y1,
                            has_trns,
                            tr,
                            tg,
                            tb
                        );
                    }
            }

        free (tasks);
        free (threads);
    }
}

static int
decode_raw_to_rgba (
    uint8_t *rgba,
    const uint8_t *raw,
    uint32_t width,
    uint32_t height,
    size_t src_channels,
    int has_trns,
    uint8_t tr,
    uint8_t tg,
    uint8_t tb
)
{
    size_t row_bytes = (size_t)width * src_channels;
    size_t out_row_bytes = (size_t)width * 4U;
    size_t y;
    pthread_once (&g_paeth_once, init_paeth_tables_once);

    if (src_channels == 4U)
        {
            for (y = 0; y < (size_t)height; y++)
                {
                    const uint8_t *row_src = raw + y * (row_bytes + 1U);
                    const uint8_t *prev
                        = (y == 0) ? NULL : (rgba + (y - 1U) * out_row_bytes);
                    uint8_t *row_dst = rgba + y * out_row_bytes;
                    if (!unfilter_row (row_dst, row_src, prev, row_bytes, 4U))
                        {
                            return 0;
                        }
                }
            return 1;
        }

    if (src_channels == 3U)
        {
            if (configured_png_threads () <= 1)
                {
                    uint8_t *row_state
                        = (uint8_t *)malloc (row_bytes * 2U + 16U);
                    uint8_t *prev_row;
                    uint8_t *cur_row;

                    if (!row_state)
                        {
                            return 0;
                        }
                    prev_row = row_state;
                    cur_row = row_state + row_bytes;

                    for (y = 0; y < (size_t)height; y++)
                        {
                            const uint8_t *row_src
                                = raw + y * (row_bytes + 1U);
                            const uint8_t *prev = (y == 0) ? NULL : prev_row;
                            uint8_t *row_dst = cur_row;
                            uint8_t *out = rgba + y * out_row_bytes;

                            if (!unfilter_row (
                                    row_dst, row_src, prev, row_bytes, 3U
                                ))
                                {
                                    free (row_state);
                                    return 0;
                                }
                            convert_rgb_rows_to_rgba (
                                out, row_dst, width, 0, 1, has_trns, tr, tg, tb
                            );

                            {
                                uint8_t *tmp = prev_row;
                                prev_row = cur_row;
                                cur_row = tmp;
                            }
                        }

                    free (row_state);
                    return 1;
                }

            {
                size_t decoded_size = row_bytes * (size_t)height;
                uint8_t *scan = (uint8_t *)malloc (decoded_size + 16U);

                if (!scan)
                    {
                        return 0;
                    }

                for (y = 0; y < (size_t)height; y++)
                    {
                        const uint8_t *row_src = raw + y * (row_bytes + 1U);
                        const uint8_t *prev
                            = (y == 0) ? NULL : (scan + (y - 1U) * row_bytes);
                        uint8_t *row_dst = scan + y * row_bytes;
                        if (!unfilter_row (
                                row_dst, row_src, prev, row_bytes, 3U
                            ))
                            {
                                free (scan);
                                return 0;
                            }
                    }

                convert_rgb_to_rgba_mt (
                    rgba, scan, width, height, has_trns, tr, tg, tb
                );
                free (scan);
            }
            return 1;
        }

    return 0;
}

typedef struct libdeflate_decompressor libdeflate_decompressor;

enum libdeflate_result
{
    LIBDEFLATE_SUCCESS = 0,
    LIBDEFLATE_BAD_DATA = 1,
    LIBDEFLATE_SHORT_OUTPUT = 2,
    LIBDEFLATE_INSUFFICIENT_SPACE = 3
};

typedef libdeflate_decompressor *(*libdeflate_alloc_decompressor_fn) (void);
typedef void (*libdeflate_free_decompressor_fn) (
    libdeflate_decompressor *decompressor
);
typedef enum libdeflate_result (*libdeflate_deflate_decompress_fn) (
    libdeflate_decompressor *decompressor,
    const void *in,
    size_t in_nbytes,
    void *out,
    size_t out_nbytes_avail,
    size_t *actual_out_nbytes_ret
);
typedef enum libdeflate_result (*libdeflate_zlib_decompress_fn) (
    libdeflate_decompressor *decompressor,
    const void *in,
    size_t in_nbytes,
    void *out,
    size_t out_nbytes_avail,
    size_t *actual_out_nbytes_ret
);

typedef struct
{
    int attempted;
    int ready;
    void *handle;
    libdeflate_alloc_decompressor_fn alloc_decompressor;
    libdeflate_free_decompressor_fn free_decompressor;
    libdeflate_deflate_decompress_fn deflate_decompress;
    libdeflate_zlib_decompress_fn zlib_decompress;
    libdeflate_decompressor *decompressor;
} libdeflate_api_t;

static libdeflate_api_t g_libdeflate = { 0 };

static void *
load_symbol (void *handle, const char *name)
{
    return dlsym (handle, name);
}

static void
load_fn_ptr (void *dst_fn, void *sym, size_t fn_size)
{
    memcpy (dst_fn, &sym, fn_size);
}

static void
shutdown_libdeflate_api (void)
{
    if (g_libdeflate.free_decompressor && g_libdeflate.decompressor)
        {
            g_libdeflate.free_decompressor (g_libdeflate.decompressor);
        }
    if (g_libdeflate.handle)
        {
            dlclose (g_libdeflate.handle);
        }
    memset (&g_libdeflate, 0, sizeof (g_libdeflate));
}

static int
init_libdeflate_api (void)
{
    if (g_libdeflate.attempted)
        {
            return g_libdeflate.ready;
        }

    g_libdeflate.attempted = 1;
    g_libdeflate.handle = dlopen ("libdeflate.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (!g_libdeflate.handle)
        {
            return 0;
        }

    {
        void *sym;

        sym = load_symbol (
            g_libdeflate.handle, "libdeflate_alloc_decompressor"
        );
        load_fn_ptr (
            &g_libdeflate.alloc_decompressor,
            sym,
            sizeof (g_libdeflate.alloc_decompressor)
        );

        sym = load_symbol (
            g_libdeflate.handle, "libdeflate_free_decompressor"
        );
        load_fn_ptr (
            &g_libdeflate.free_decompressor,
            sym,
            sizeof (g_libdeflate.free_decompressor)
        );

        sym = load_symbol (
            g_libdeflate.handle, "libdeflate_deflate_decompress"
        );
        load_fn_ptr (
            &g_libdeflate.deflate_decompress,
            sym,
            sizeof (g_libdeflate.deflate_decompress)
        );

        sym = load_symbol (g_libdeflate.handle, "libdeflate_zlib_decompress");
        load_fn_ptr (
            &g_libdeflate.zlib_decompress,
            sym,
            sizeof (g_libdeflate.zlib_decompress)
        );
    }

    if (!g_libdeflate.alloc_decompressor || !g_libdeflate.free_decompressor
        || !g_libdeflate.deflate_decompress || !g_libdeflate.zlib_decompress)
        {
            shutdown_libdeflate_api ();
            g_libdeflate.attempted = 1;
            return 0;
        }

    g_libdeflate.decompressor = g_libdeflate.alloc_decompressor ();
    if (!g_libdeflate.decompressor)
        {
            shutdown_libdeflate_api ();
            g_libdeflate.attempted = 1;
            return 0;
        }

    atexit (shutdown_libdeflate_api);
    g_libdeflate.ready = 1;
    return 1;
}

static int
inflate_idat_fast (
    uint8_t *dst,
    size_t dst_size,
    const uint8_t *idat,
    size_t idat_size
)
{
    size_t actual_out_nbytes = 0;
    enum libdeflate_result r;
    uint8_t cmf;
    uint8_t flg;

    if (!init_libdeflate_api ())
        {
            return 0;
        }

    if (idat_size >= 6U)
        {
            cmf = idat[0];
            flg = idat[1];
            if ((cmf & 0x0fU) == 8U && ((cmf >> 4U) & 0x0fU) <= 7U
                && (((((unsigned int)cmf << 8U) | (unsigned int)flg) % 31U)
                    == 0U)
                && (flg & 0x20U) == 0U)
                {
                    r = g_libdeflate.deflate_decompress (
                        g_libdeflate.decompressor,
                        idat + 2U,
                        idat_size - 6U,
                        dst,
                        dst_size,
                        &actual_out_nbytes
                    );
                    if (r == LIBDEFLATE_SUCCESS
                        && actual_out_nbytes == dst_size)
                        {
                            return 1;
                        }
                }
        }

    r = g_libdeflate.zlib_decompress (
        g_libdeflate.decompressor,
        idat,
        idat_size,
        dst,
        dst_size,
        &actual_out_nbytes
    );
    return r == LIBDEFLATE_SUCCESS && actual_out_nbytes == dst_size;
}

int
png_is_signature (const uint8_t *buf, size_t len)
{
    return len >= sizeof (k_png_sig)
           && memcmp (buf, k_png_sig, sizeof (k_png_sig)) == 0;
}

int
png_decode_file (const char *path, image_t *img)
{
    uint8_t *file_buf = NULL;
    size_t file_size = 0;
    size_t pos;
    uint8_t *idat = NULL;
    size_t idat_size = 0;
    size_t idat_cap = 0;
    uint8_t *raw = NULL;
    uint8_t *rgba = NULL;

    uint32_t width = 0, height = 0;
    uint8_t bit_depth = 0, color_type = 0, compression = 0, filter_method = 0,
            interlace = 0;
    int seen_ihdr = 0, seen_iend = 0;
    int has_trns = 0;
    uint8_t tr = 0, tg = 0, tb = 0;
    size_t src_channels;
    size_t row_bytes;
    size_t encoded_size;
    size_t decoded_size;
    size_t pix_count;

    img->width = 0;
    img->height = 0;
    img->rgba = NULL;
    img->has_alpha = 0;

    if (!load_file_bytes (path, &file_buf, &file_size))
        {
            fprintf (
                stderr, "failed to open '%s': %s\n", path, strerror (errno)
            );
            goto fail;
        }
    if (!png_is_signature (file_buf, file_size))
        {
            fprintf (stderr, "not a png: '%s'\n", path);
            goto fail;
        }

    pos = sizeof (k_png_sig);
    while (!seen_iend)
        {
            const uint8_t *chunk_data;
            uint32_t length;
            uint32_t chunk_type;

            if (pos > file_size || file_size - pos < 12U)
                {
                    goto fail;
                }
            length = read_be32 (file_buf + pos);
            chunk_type = read_be32 (file_buf + pos + 4U);
            pos += 8U;
            if ((size_t)length > file_size - pos - 4U)
                {
                    goto fail;
                }

            chunk_data = file_buf + pos;
            pos += (size_t)length;
            pos += 4U;

            if (chunk_type == 0x49484452U)
                {
                    if (seen_ihdr || length != 13U)
                        {
                            goto fail;
                        }
                    width = read_be32 (chunk_data + 0U);
                    height = read_be32 (chunk_data + 4U);
                    bit_depth = chunk_data[8];
                    color_type = chunk_data[9];
                    compression = chunk_data[10];
                    filter_method = chunk_data[11];
                    interlace = chunk_data[12];
                    seen_ihdr = 1;
                }
            else if (chunk_type == 0x49444154U)
                {
                    if (!seen_ihdr
                        || !append_bytes (
                            &idat,
                            &idat_size,
                            &idat_cap,
                            chunk_data,
                            (size_t)length
                        ))
                        {
                            goto fail;
                        }
                }
            else if (chunk_type == 0x74524e53U)
                {
                    if (color_type == 2 && length >= 6U)
                        {
                            uint16_t vr = ((uint16_t)chunk_data[0] << 8)
                                          | (uint16_t)chunk_data[1];
                            uint16_t vg = ((uint16_t)chunk_data[2] << 8)
                                          | (uint16_t)chunk_data[3];
                            uint16_t vb = ((uint16_t)chunk_data[4] << 8)
                                          | (uint16_t)chunk_data[5];
                            tr = (uint8_t)((vr > 255U) ? (vr >> 8) : vr);
                            tg = (uint8_t)((vg > 255U) ? (vg >> 8) : vg);
                            tb = (uint8_t)((vb > 255U) ? (vb >> 8) : vb);
                            has_trns = 1;
                        }
                }
            else if (chunk_type == 0x49454e44U)
                {
                    seen_iend = 1;
                }
        }

    if (!seen_ihdr || !seen_iend || idat_size == 0)
        {
            fprintf (stderr, "invalid png chunk structure: '%s'\n", path);
            goto fail;
        }
    if (width == 0 || height == 0 || width > 1000000U || height > 1000000U)
        {
            fprintf (stderr, "png dimensions unsupported: '%s'\n", path);
            goto fail;
        }
    if (compression != 0 || filter_method != 0 || interlace != 0)
        {
            fprintf (
                stderr,
                "png compression/filter/interlace unsupported: '%s'\n",
                path
            );
            goto fail;
        }
    if (bit_depth != 8 || (color_type != 2 && color_type != 6))
        {
            fprintf (
                stderr,
                "png type unsupported (need RGB/RGBA 8-bit): '%s'\n",
                path
            );
            goto fail;
        }
    if (width > (uint32_t)INT_MAX || height > (uint32_t)INT_MAX)
        {
            goto fail;
        }

    src_channels = (color_type == 6) ? 4U : 3U;
    if ((size_t)width > SIZE_MAX / src_channels)
        {
            goto fail;
        }
    row_bytes = (size_t)width * src_channels;
    if ((size_t)height > SIZE_MAX / row_bytes)
        {
            goto fail;
        }
    decoded_size = (size_t)height * row_bytes;
    if (decoded_size > SIZE_MAX - (size_t)height)
        {
            goto fail;
        }
    encoded_size = decoded_size + (size_t)height;

    raw = (uint8_t *)malloc (encoded_size);
    if (!raw)
        {
            goto fail;
        }

    if (!inflate_idat_fast (raw, encoded_size, idat, idat_size))
        {
            fprintf (
                stderr,
                "png inflate failed: '%s' (libdeflate unavailable or decode "
                "error)\n",
                path
            );
            goto fail;
        }

    pix_count = (size_t)width * (size_t)height;
    if (pix_count == 0 || pix_count > (SIZE_MAX / 4U))
        {
            goto fail;
        }
    rgba = (uint8_t *)malloc (pix_count * 4U);
    if (!rgba)
        {
            goto fail;
        }
    if (!decode_raw_to_rgba (
            rgba, raw, width, height, src_channels, has_trns, tr, tg, tb
        ))
        {
            fprintf (stderr, "png filter decode failed: '%s'\n", path);
            goto fail;
        }

    img->width = (int)width;
    img->height = (int)height;
    img->rgba = rgba;
    img->has_alpha = (color_type == 6) ? 1 : has_trns;

    free (raw);
    free (idat);
    free (file_buf);
    return 1;

fail:
    free (rgba);
    free (raw);
    free (idat);
    free (file_buf);
    return 0;
}
