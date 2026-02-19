#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include "png_decoder_internal.h"

/* ------------------------------------------------------------------ */
/* libdeflate type declarations                                        */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* Runtime-loaded API state                                            */
/* ------------------------------------------------------------------ */

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

/*
 * Load a symbol from a dlopen handle into a typed function pointer.
 * Uses memcpy to avoid strict-aliasing UB when converting void* to fn*.
 */
#define LOAD_FN(api, field, sym_name)                                         \
    do                                                                        \
        {                                                                     \
            void *_sym = dlsym ((api)->handle, (sym_name));                   \
            memcpy (&(api)->field, &_sym, sizeof ((api)->field));             \
        }                                                                     \
    while (0)

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void
shutdown_libdeflate_api (void)
{
    if (g_libdeflate.free_decompressor && g_libdeflate.decompressor)
        g_libdeflate.free_decompressor (g_libdeflate.decompressor);

    if (g_libdeflate.handle)
        dlclose (g_libdeflate.handle);

    memset (&g_libdeflate, 0, sizeof (g_libdeflate));
}

static int
init_libdeflate_api (void)
{
    if (g_libdeflate.attempted)
        return g_libdeflate.ready;

    g_libdeflate.attempted = 1;
    g_libdeflate.handle = dlopen ("libdeflate.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (!g_libdeflate.handle)
        return 0;

    LOAD_FN (
        &g_libdeflate, alloc_decompressor, "libdeflate_alloc_decompressor"
    );
    LOAD_FN (&g_libdeflate, free_decompressor, "libdeflate_free_decompressor");
    LOAD_FN (
        &g_libdeflate, deflate_decompress, "libdeflate_deflate_decompress"
    );
    LOAD_FN (&g_libdeflate, zlib_decompress, "libdeflate_zlib_decompress");

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

/* ------------------------------------------------------------------ */
/* Public inflate entry point                                          */
/* ------------------------------------------------------------------ */

/*
 * PNG IDAT data is a zlib stream.  Some encoders embed only the raw
 * deflate bitstream (omitting the zlib header/trailer), so we attempt
 * a raw deflate decode first when the header bytes look plausible,
 * then fall back to full zlib decoding.
 */
int
png_inflate_idat_fast (
    uint8_t *dst,
    size_t dst_size,
    const uint8_t *idat,
    size_t idat_size
)
{
    size_t actual_out_nbytes = 0;
    enum libdeflate_result r;

    if (!init_libdeflate_api ())
        return 0;

    /* Try raw deflate if the zlib header bytes are valid but we want to
       skip the 2-byte header and 4-byte Adler-32 trailer ourselves. */
    if (idat_size >= 6U)
        {
            uint8_t cmf = idat[0];
            uint8_t flg = idat[1];
            int cm = cmf & 0x0fU;
            int cinfo = (cmf >> 4U) & 0x0fU;
            int fcheck_ok
                = (((unsigned int)cmf << 8U | (unsigned int)flg) % 31U) == 0U;
            int fdict = (flg & 0x20U) != 0U;

            if (cm == 8 && cinfo <= 7 && fcheck_ok && !fdict)
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
                        return 1;
                }
        }

    /* Full zlib decode (handles header + Adler-32 verification). */
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
