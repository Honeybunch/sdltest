#include "pattern.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "cpuresources.h"

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

void write_pattern(uint32_t width, uint32_t height, uint32_t *bitmap) {
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      int l = (0x1FF >>
               MIN(MIN(MIN(MIN(x, y), width - 1 - x), height - 1 - y), 31u));
      int d =
          MIN(50, MAX(0, 255 - 50 * powf(hypotf(x / (float)(width / 2) - 1.f,
                                                y / (float)(height / 2) - 1.f) *
                                             4,
                                         2.f)));
      int r = (~x & ~y) & 255, g = (x & ~y) & 255, b = (~x & y) & 255;
      bitmap[y * width + x] = MIN(MAX(r - d, l), 255) * 65536 +
                              MIN(MAX(g - d, l), 255) * 256 +
                              MIN(MAX(b - d, l), 255);
    }
  }
}

void alloc_pattern(uint32_t width, uint32_t height, cputexture **out) {
  assert(out);
  uint64_t data_size = width * height * sizeof(uint32_t);

  cputexture *tex =
      (cputexture *)malloc(data_size + sizeof(cputexture) +
                           sizeof(texture_layer) + sizeof(texture_mip));
  assert(tex);

  uint64_t offset = sizeof(cputexture);

  tex->layer_count = 1;
  tex->mip_count = 1;
  tex->layers = (const texture_layer *)(((uint8_t *)tex) + offset);
  offset += sizeof(texture_layer) * tex->layer_count;

  texture_layer *layer = (texture_layer *)&tex->layers[0];
  layer->mips = (const texture_mip *)(((uint8_t *)tex) + offset);
  offset += sizeof(texture_mip) * tex->mip_count;

  tex->data_size = data_size;
  tex->data = (((uint8_t *)tex) + offset);

  (*out) = tex;
}

void create_pattern(uint32_t width, uint32_t height, cputexture *out) {
  assert(out);

  // Setup subresource
  texture_layer *layer = (texture_layer *)&out->layers[0];
  layer->width = width;
  layer->height = height;
  layer->depth = 1;

  texture_mip *mip = (texture_mip *)&layer->mips[0];
  mip->width = width;
  mip->height = height;
  mip->depth = 1;
  mip->data = out->data;

  // Create bitmap
  write_pattern(width, height, (uint32_t *)out->data);
}