#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png_decoder_internal.h"

const uint8_t g_png_sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

int
png_load_file_bytes (const char *path, uint8_t **out, size_t *out_size)
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

int
png_append_bytes (
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
