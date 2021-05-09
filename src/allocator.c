#include "allocator.h"

#include <assert.h>

#include <mimalloc.h>

void *arena_alloc(void *user_data, size_t size) {
  arena_allocator *arena = (arena_allocator *)user_data;
  size_t cur_size = arena->size;
  if (cur_size + size >= arena->max_size) {
    arena->grow = true; // Signal that on the next reset we need to actually do
                        // a resize as the arena is unable to meet demand
    assert(false);
    return NULL;
  }
  void *ptr = &arena->data[cur_size];
  arena->size += size;
  return ptr;
}

void arena_free(void *user_data, void *ptr) {
  // Do nothing, the arena will reset
  (void)user_data;
  (void)ptr;
}

arena_allocator create_arena_allocator(size_t max_size) {
  mi_heap_t *heap = mi_heap_new();
  assert(heap);
  void *data = mi_heap_calloc(heap, 1, max_size);
  assert(data);

  arena_allocator a = {
      .max_size = max_size,
      .heap = heap,
      .data = data,
      .alloc =
          (allocator){
              .alloc = arena_alloc,
              .free = arena_free,
              .user_data = &a,
          },
  };
  return a;
}

void reset_arena(arena_allocator a, bool allow_grow) {
  if (allow_grow) {
    if (a.grow) {
      a.max_size *= 2;
    }
  }
  a.grow = false;
  a.data = mi_heap_recalloc(a.heap, a.data, 1, a.max_size);
  assert(a.data);
}

void destroy_arena_allocator(arena_allocator a) {
  mi_free(a.data);
  mi_heap_destroy(a.heap);
}

void *standard_alloc(void *user_data, size_t size) {
  standard_allocator *alloc = (standard_allocator *)user_data;
  return mi_heap_calloc(alloc->heap, 1, size);
}

void standard_free(void *user_data, void *ptr) {
  (void)user_data;
  mi_free(ptr);
}

standard_allocator create_standard_allocator() {
  standard_allocator a = {.heap = mi_heap_new(),
                          .alloc = {
                              .alloc = standard_alloc,
                              .free = standard_free,
                              .user_data = &a,
                          }};
  return a;
}
void destroy_standard_allocator(standard_allocator a) {
  mi_heap_delete(a.heap);
}