#ifndef CLI_H
#define CLI_H

#include "renderer.h"

typedef struct
{
    const char *image_path;
    bg_config_t bg;
} app_options_t;

int app_options_parse (int argc, char **argv, app_options_t *out);
void app_options_usage (const char *argv0);

#endif
