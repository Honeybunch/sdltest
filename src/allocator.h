#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct mi_heap_s mi_heap_t;

typedef void *alloc_fn(void *user_data, size_t size);
typedef void free_fn(void *user_data, void *ptr);

#define hb_alloc(a, size) a.alloc(a.user_data, size)
#define hb_free(a, ptr) a.free(a.user_data, ptr)

typedef struct allocator {
  void *user_data;
  alloc_fn *alloc;
  free_fn *free;
} allocator;

typedef struct arena_allocator {
  mi_heap_t *heap;
  size_t size;
  size_t max_size;
  uint8_t *data;
  allocator alloc;
  bool grow;
} arena_allocator;

arena_allocator make_arena_allocator(size_t max_size);
void reset_arena(arena_allocator a, bool allow_grow);
void destroy_arena_allocator(arena_allocator a);
