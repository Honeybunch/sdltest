#pragma once

#include <stdint.h>

struct allocator;

typedef struct dynarray_header {
  size_t count;
  size_t capacity;
  size_t element_size;
} dynarray_header;

void *_alloc_dynarray(dynarray_header header, const allocator *a);

#define dynarray(T) T *
#define alloc_dynarray(T, cap, a)                                              \
  _alloc_dynarray(                                                             \
      (dynarray_header){.capacity = cap, .element_size = sizeof(t)}, a);