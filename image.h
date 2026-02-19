#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

typedef struct
{
    int width;
    int height;
    uint8_t *rgba;
    int has_alpha;
} image_t;

int image_load (const char *path, image_t *img);
void image_free (image_t *img);

#endif
