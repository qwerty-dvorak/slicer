#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image.h"
#include "png_decoder.h"

static int
parse_pos_int (const char *s, int *out)
{
    char *end = NULL;
    long v = strtol (s, &end, 10);
    if (!s[0] || (end && *end != '\0') || v <= 0 || v > 1000000L)
        {
            return 0;
        }
    *out = (int)v;
    return 1;
}

static int
read_token (FILE *f, char *buf, size_t n)
{
    int c;
    size_t i = 0;

    if (n == 0)
        {
            return 0;
        }

    c = fgetc (f);
    while (c != EOF)
        {
            if (isspace (c))
                {
                    c = fgetc (f);
                    continue;
                }
            if (c == '#')
                {
                    do
                        {
                            c = fgetc (f);
                        }
                    while (c != EOF && c != '\n');
                    c = fgetc (f);
                    continue;
                }
            break;
        }

    if (c == EOF)
        {
            return 0;
        }

    while (c != EOF && !isspace (c) && c != '#')
        {
            if (i + 1 < n)
                {
                    buf[i++] = (char)c;
                }
            c = fgetc (f);
        }
    if (c == '#')
        {
            do
                {
                    c = fgetc (f);
                }
            while (c != EOF && c != '\n');
        }

    buf[i] = '\0';
    return i > 0;
}

static int
load_ppm_p6 (const char *path, image_t *img)
{
    FILE *f = fopen (path, "rb");
    char tok[64];
    int width, height, maxval;
    size_t pix_count;
    size_t need, got;
    uint8_t *rgb_data;
    uint8_t *rgba_data;
    size_t i;

    if (!f)
        {
            fprintf (
                stderr, "failed to open '%s': %s\n", path, strerror (errno)
            );
            return 0;
        }
    if (!read_token (f, tok, sizeof (tok)) || strcmp (tok, "P6") != 0)
        {
            fprintf (
                stderr,
                "unsupported format in '%s' (need PNG or PPM P6)\n",
                path
            );
            fclose (f);
            return 0;
        }
    if (!read_token (f, tok, sizeof (tok)) || !parse_pos_int (tok, &width))
        {
            fprintf (stderr, "invalid ppm width in '%s'\n", path);
            fclose (f);
            return 0;
        }
    if (!read_token (f, tok, sizeof (tok)) || !parse_pos_int (tok, &height))
        {
            fprintf (stderr, "invalid ppm height in '%s'\n", path);
            fclose (f);
            return 0;
        }
    if (!read_token (f, tok, sizeof (tok)))
        {
            fclose (f);
            return 0;
        }
    maxval = atoi (tok);
    if (maxval <= 0 || maxval > 255)
        {
            fprintf (stderr, "invalid ppm maxval in '%s'\n", path);
            fclose (f);
            return 0;
        }

    pix_count = (size_t)width * (size_t)height;
    if (pix_count == 0 || pix_count > (SIZE_MAX / 4U))
        {
            fclose (f);
            return 0;
        }
    need = pix_count * 3U;
    rgb_data = (uint8_t *)malloc (need);
    if (!rgb_data)
        {
            fclose (f);
            return 0;
        }
    got = fread (rgb_data, 1, need, f);
    fclose (f);
    if (got != need)
        {
            fprintf (stderr, "short read in '%s'\n", path);
            free (rgb_data);
            return 0;
        }

    rgba_data = (uint8_t *)malloc (pix_count * 4U);
    if (!rgba_data)
        {
            free (rgb_data);
            return 0;
        }
    for (i = 0; i < pix_count; i++)
        {
            rgba_data[i * 4U + 0] = rgb_data[i * 3U + 0];
            rgba_data[i * 4U + 1] = rgb_data[i * 3U + 1];
            rgba_data[i * 4U + 2] = rgb_data[i * 3U + 2];
            rgba_data[i * 4U + 3] = 255U;
        }
    free (rgb_data);

    img->width = width;
    img->height = height;
    img->rgba = rgba_data;
    img->has_alpha = 0;
    return 1;
}

int
image_load (const char *path, image_t *img)
{
    FILE *f;
    uint8_t sig[8];
    size_t n;

    img->width = 0;
    img->height = 0;
    img->rgba = NULL;
    img->has_alpha = 0;

    f = fopen (path, "rb");
    if (!f)
        {
            fprintf (
                stderr, "failed to open '%s': %s\n", path, strerror (errno)
            );
            return 0;
        }
    n = fread (sig, 1, sizeof (sig), f);
    fclose (f);

    if (png_is_signature (sig, n))
        {
            return png_decode_file (path, img);
        }
    return load_ppm_p6 (path, img);
}

void
image_free (image_t *img)
{
    if (!img)
        {
            return;
        }
    free (img->rgba);
    img->rgba = NULL;
    img->width = 0;
    img->height = 0;
    img->has_alpha = 0;
}
