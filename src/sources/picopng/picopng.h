#ifndef _PICOPNG_H
#define _PICOPNG_H

#include "ospi/ospi.h"
#include "supports/karray.h"

typedef struct {
    uint32_t        width;
    uint32_t        height;

    uint32_t        colorType;
    uint32_t        bitDepth;

    uint32_t        compressionMethod;
    uint32_t        filterMethod;
    uint32_t        interlaceMethod;

    uint32_t        key_r;
    uint32_t        key_g;
    uint32_t        key_b;
    bool            key_defined; // is a transparent color key given?

    struct karray   arr_palette;
    struct karray   arr_image;
} PNG_info_t;

typedef struct _png_pix_info {
    uint32_t        width;
    uint32_t        height;

    struct karray   arr_image;
} png_pix_info_t;

int PNG_decode(PNG_info_t *info, const uint8_t *in, uint32_t size);

int loadEmbeddedPngImage(uint8_t *pngData, int pngSize, png_pix_info_t *dinfo);

#endif
