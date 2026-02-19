#ifndef PNG_DECODER_H
#define PNG_DECODER_H

#include <stddef.h>
#include <stdint.h>

#include "image.h"

int png_is_signature (const uint8_t *buf, size_t len);
int png_decode_file (const char *path, image_t *img);

#endif
