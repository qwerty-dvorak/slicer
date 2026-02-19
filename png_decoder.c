#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png_decoder.h"
#include "png_decoder_internal.h"

/* ------------------------------------------------------------------ */
/* IHDR / tRNS chunk parsers                                          */
/* ------------------------------------------------------------------ */

static int
parse_ihdr (const uint8_t *data, uint32_t length, png_ihdr_t *out)
{
    if (length != 13U)
        return 0;

    out->width = png_read_be32 (data + 0U);
    out->height = png_read_be32 (data + 4U);
    out->bit_depth = data[8];
    out->color_type = data[9];
    out->compression = data[10];
    out->filter_method = data[11];
    out->interlace = data[12];
    return 1;
}

static void
parse_trns_rgb (const uint8_t *data, uint32_t length, png_trns_t *out)
{
    uint16_t vr, vg, vb;

    if (length < 6U)
        return;

    vr = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
    vg = ((uint16_t)data[2] << 8) | (uint16_t)data[3];
    vb = ((uint16_t)data[4] << 8) | (uint16_t)data[5];

    out->r = (uint8_t)((vr > 255U) ? (vr >> 8) : vr);
    out->g = (uint8_t)((vg > 255U) ? (vg >> 8) : vg);
    out->b = (uint8_t)((vb > 255U) ? (vb >> 8) : vb);
    out->present = 1;
}

/* ------------------------------------------------------------------ */
/* IHDR validation                                                     */
/* ------------------------------------------------------------------ */

static int
validate_ihdr (const png_ihdr_t *ihdr, const char *path)
{
    if (ihdr->width == 0 || ihdr->height == 0 || ihdr->width > 1000000U
        || ihdr->height > 1000000U)
        {
            fprintf (stderr, "png dimensions unsupported: '%s'\n", path);
            return 0;
        }
    if (ihdr->compression != 0 || ihdr->filter_method != 0
        || ihdr->interlace != 0)
        {
            fprintf (
                stderr,
                "png compression/filter/interlace unsupported: '%s'\n",
                path
            );
            return 0;
        }
    if (ihdr->bit_depth != 8
        || (ihdr->color_type != 2 && ihdr->color_type != 6))
        {
            fprintf (
                stderr,
                "png type unsupported (need RGB/RGBA 8-bit): '%s'\n",
                path
            );
            return 0;
        }
    if (ihdr->width > (uint32_t)INT_MAX || ihdr->height > (uint32_t)INT_MAX)
        {
            fprintf (stderr, "png dimensions overflow int: '%s'\n", path);
            return 0;
        }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int
png_is_signature (const uint8_t *buf, size_t len)
{
    return len >= sizeof (g_png_sig)
           && memcmp (buf, g_png_sig, sizeof (g_png_sig)) == 0;
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

    png_ihdr_t ihdr = { 0 };
    png_trns_t trns = { 0 };
    int seen_ihdr = 0;
    int seen_iend = 0;

    size_t src_channels;
    size_t row_bytes;
    size_t encoded_size;
    size_t decoded_size;
    size_t pix_count;

    img->width = 0;
    img->height = 0;
    img->rgba = NULL;
    img->has_alpha = 0;

    /* ---- load raw bytes ------------------------------------------ */

    if (!png_load_file_bytes (path, &file_buf, &file_size))
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

    /* ---- chunk loop ---------------------------------------------- */

    pos = sizeof (g_png_sig);
    while (!seen_iend)
        {
            uint32_t length;
            uint32_t chunk_type;
            const uint8_t *chunk_data;

            if (pos > file_size || file_size - pos < 12U)
                goto fail;

            length = png_read_be32 (file_buf + pos);
            chunk_type = png_read_be32 (file_buf + pos + 4U);
            pos += 8U;

            if ((size_t)length > file_size - pos - 4U)
                goto fail;

            chunk_data = file_buf + pos;
            pos += (size_t)length + 4U; /* data + CRC */

            switch (chunk_type)
                {
                case PNG_CHUNK_IHDR:
                    if (seen_ihdr || !parse_ihdr (chunk_data, length, &ihdr))
                        goto fail;
                    seen_ihdr = 1;
                    break;

                case PNG_CHUNK_IDAT:
                    if (!seen_ihdr
                        || !png_append_bytes (
                            &idat,
                            &idat_size,
                            &idat_cap,
                            chunk_data,
                            (size_t)length
                        ))
                        goto fail;
                    break;

                case PNG_CHUNK_tRNS:
                    if (ihdr.color_type == 2)
                        parse_trns_rgb (chunk_data, length, &trns);
                    break;

                case PNG_CHUNK_IEND:
                    seen_iend = 1;
                    break;

                default:
                    break; /* ancillary chunk — skip */
                }
        }

    /* ---- structural checks --------------------------------------- */

    if (!seen_ihdr || !seen_iend || idat_size == 0)
        {
            fprintf (stderr, "invalid png chunk structure: '%s'\n", path);
            goto fail;
        }
    if (!validate_ihdr (&ihdr, path))
        goto fail;

    /* ---- size arithmetic ----------------------------------------- */

    src_channels = (ihdr.color_type == 6) ? 4U : 3U;
    if ((size_t)ihdr.width > SIZE_MAX / src_channels)
        goto fail;

    row_bytes = (size_t)ihdr.width * src_channels;
    if ((size_t)ihdr.height > SIZE_MAX / row_bytes)
        goto fail;

    decoded_size = (size_t)ihdr.height * row_bytes;
    if (decoded_size > SIZE_MAX - (size_t)ihdr.height)
        goto fail;

    encoded_size = decoded_size + (size_t)ihdr.height; /* +1 filter byte/row */

    /* ---- inflate ------------------------------------------------- */

    raw = (uint8_t *)malloc (encoded_size);
    if (!raw)
        goto fail;

    if (!png_inflate_idat_fast (raw, encoded_size, idat, idat_size))
        {
            fprintf (
                stderr,
                "png inflate failed: '%s' (libdeflate unavailable or error)\n",
                path
            );
            goto fail;
        }

    /* ---- pixel decode -------------------------------------------- */

    pix_count = (size_t)ihdr.width * (size_t)ihdr.height;
    if (pix_count == 0 || pix_count > (SIZE_MAX / 4U))
        goto fail;

    rgba = (uint8_t *)malloc (pix_count * 4U);
    if (!rgba)
        goto fail;

    if (!png_decode_raw_to_rgba (
            rgba,
            raw,
            ihdr.width,
            ihdr.height,
            src_channels,
            trns.present,
            trns.r,
            trns.g,
            trns.b
        ))
        {
            fprintf (stderr, "png filter decode failed: '%s'\n", path);
            goto fail;
        }

    /* ---- success ------------------------------------------------- */

    img->width = (int)ihdr.width;
    img->height = (int)ihdr.height;
    img->rgba = rgba;
    img->has_alpha = (ihdr.color_type == 6) ? 1 : trns.present;

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
