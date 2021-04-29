#pragma once

#include <stdint.h>

#include "simd.h"

typedef struct VkDevice_T *VkDevice;
typedef struct VmaAllocator_T *VmaAllocator;
typedef struct gpumesh gpumesh;
typedef struct gputexture gputexture;

enum component_type {
  COMPONENT_TYPE_NONE = 0x00000000,
  COMPONENT_TYPE_TRANSFORM = 0x00000001,
  COMPONENT_TYPE_STATIC_MESH = 0x00000002,
};

typedef struct scene_transform scene_transform;
typedef struct scene_transform {
  transform t;
  uint32_t child_count;
  scene_transform **children;
} scene_transform;

typedef struct scene_static_mesh {
  gpumesh *mesh;
  // TODO: material, pipeline, aabb, etc
} scene_static_mesh;

typedef struct scene {
  uint32_t max_entity_count;
  uint32_t entity_count;
  uint64_t *components;
  scene_transform *transforms;
  scene_static_mesh *static_meshes;

  uint32_t max_mesh_count;
  uint32_t mesh_count;
  gpumesh *meshes;

  uint32_t max_texture_count;
  uint32_t texture_count;
  gputexture *textures;
} scene;

int32_t load_scene(VkDevice device, VmaAllocator alloc, const char *filename,
                   scene **scene);
void destroy_scene(VkDevice device, VmaAllocator alloc, scene *s);