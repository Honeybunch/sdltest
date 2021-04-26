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

  cputexture *tex = (cputexture *)malloc(data_size + sizeof(cputexture) +
                                         sizeof(texture_subresource));
  assert(tex);

  tex->subresource_count = 1;
  tex->subresources =
      (const texture_subresource *)(((uint8_t *)tex) + sizeof(cputexture));
  tex->data_size = data_size;
  tex->data = (((uint8_t *)tex->subresources) + sizeof(texture_subresource));

  (*out) = tex;
}

void create_pattern(uint32_t width, uint32_t height, cputexture *out) {
  assert(out);

  // Setup subresource
  texture_subresource *sub = (texture_subresource *)&out->subresources[0];
  sub->width = width;
  sub->height = height;
  sub->depth = 1;
  sub->data = out->data;

  // Create bitmap
  write_pattern(width, height, (uint32_t *)out->data);
}