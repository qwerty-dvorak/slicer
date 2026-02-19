#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cli.h"

static int
hex_nibble (char c)
{
    if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
    if (c >= 'a' && c <= 'f')
        {
            return 10 + (c - 'a');
        }
    if (c >= 'A' && c <= 'F')
        {
            return 10 + (c - 'A');
        }
    return -1;
}

static int
parse_hex_color (const char *s, uint8_t *r, uint8_t *g, uint8_t *b)
{
    int values[6];
    int i;

    if (!s || strlen (s) != 7 || s[0] != '#')
        {
            return 0;
        }
    for (i = 0; i < 6; i++)
        {
            values[i] = hex_nibble (s[i + 1]);
            if (values[i] < 0)
                {
                    return 0;
                }
        }

    *r = (uint8_t)((values[0] << 4) | values[1]);
    *g = (uint8_t)((values[2] << 4) | values[3]);
    *b = (uint8_t)((values[4] << 4) | values[5]);
    return 1;
}

static int
parse_bg (const char *arg, bg_config_t *bg)
{
    const char *color_spec;

    if (strcmp (arg, "checkered") == 0)
        {
            bg->mode = BG_MODE_CHECKERED;
            return 1;
        }

    if (strcmp (arg, "solid") == 0)
        {
            bg->mode = BG_MODE_SOLID;
            bg->solid_r = 32U;
            bg->solid_g = 32U;
            bg->solid_b = 32U;
            return 1;
        }

    if (strncmp (arg, "solid:", 6) == 0)
        {
            color_spec = arg + 6;
            if (!parse_hex_color (
                    color_spec, &bg->solid_r, &bg->solid_g, &bg->solid_b
                ))
                {
                    return 0;
                }
            bg->mode = BG_MODE_SOLID;
            return 1;
        }

    return 0;
}

int
app_options_parse (int argc, char **argv, app_options_t *out)
{
    int i;

    out->image_path = NULL;
    out->bg.mode = BG_MODE_CHECKERED;
    out->bg.solid_r = 32U;
    out->bg.solid_g = 32U;
    out->bg.solid_b = 32U;

    for (i = 1; i < argc; i++)
        {
            if (strcmp (argv[i], "--bg") == 0)
                {
                    if (i + 1 >= argc)
                        {
                            fprintf (stderr, "missing value after --bg\n");
                            return 0;
                        }
                    i++;
                    if (!parse_bg (argv[i], &out->bg))
                        {
                            fprintf (
                                stderr, "invalid --bg value '%s'\n", argv[i]
                            );
                            return 0;
                        }
                    continue;
                }

            if (argv[i][0] == '-')
                {
                    fprintf (stderr, "unknown option: %s\n", argv[i]);
                    return 0;
                }

            if (out->image_path != NULL)
                {
                    fprintf (stderr, "only one image path is supported\n");
                    return 0;
                }
            out->image_path = argv[i];
        }

    if (!out->image_path)
        {
            fprintf (stderr, "missing image path\n");
            return 0;
        }

    return 1;
}

void
app_options_usage (const char *argv0)
{
    fprintf (stderr, "usage: %s [--bg mode] image.(png|ppm)\n", argv0);
    fprintf (
        stderr,
        "  --bg checkered | solid | solid:#RRGGBB (default: checkered)\n"
    );
    fprintf (stderr, "supports: PNG (alpha), binary PPM (P6)\n");
}
