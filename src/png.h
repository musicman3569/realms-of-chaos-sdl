/* Minimal PNG writer (from ptomb-port): 8-bit RGB with uncompressed ("stored") deflate
 * blocks, so no zlib is needed. Used for screenshots. */
#ifndef ROC_PNG_H
#define ROC_PNG_H

#include <stdint.h>

/* Write width x height pixels 0xAARRGGBB (alpha ignored). Returns 0 on success. */
int png_write_argb(const char *path, const uint32_t *px, int width, int height);

#endif
