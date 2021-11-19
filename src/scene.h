#pragma once

#include <stdint.h>

#include "allocator.h"
#include "simd.h"

typedef struct VkDevice_T *VkDevice;
typedef struct VmaAllocator_T *VmaAllocator;
typedef struct VmaPool_T *VmaPool;
typedef struct gpumesh gpumesh;
typedef struct gputexture gputexture;
typedef struct gpumaterial gpumaterial;
typedef struct VkAllocationCallbacks VkAllocationCallbacks;

enum component_type {
  COMPONENT_TYPE_NONE = 0x00000000,
  COMPONENT_TYPE_TRANSFORM = 0x00000001,
  COMPONENT_TYPE_STATIC_MESH = 0x00000002,
  COMPONENT_TYPE_MATERIAL = 0x00000004,
};

typedef struct scene_transform scene_transform;
typedef struct scene_transform {
  transform t;
  uint32_t child_count;
  scene_transform **children;
} scene_transform;

#define MAX_CHILD_COUNT 256
typedef struct SceneTransform2 {
  transform t;
  uint32_t child_count;
  uint32_t children[MAX_CHILD_COUNT];
} SceneTransform2;

typedef struct scene_static_mesh {
  gpumesh *mesh;
  // TODO: material, pipeline, aabb, etc
} scene_static_mesh;

typedef struct DemoAllocContext {
  VkDevice device;
  allocator tmp_alloc;
  allocator std_alloc;
  const VkAllocationCallbacks *vk_alloc;
  VmaAllocator vma_alloc;
  VmaPool up_pool;
  VmaPool tex_pool;
} DemoAllocContext;

typedef struct scene {
  DemoAllocContext alloc_ctx;

  uint32_t max_entity_count;
  uint32_t entity_count;
  uint64_t *components;
  scene_transform *transforms;
  scene_static_mesh *static_meshes;

  SceneTransform2 *transforms_2;
  uint32_t *static_mesh_indices;

  uint32_t max_mesh_count;
  uint32_t mesh_count;
  gpumesh *meshes;

  uint32_t max_texture_count;
  uint32_t texture_count;
  gputexture *textures;

  uint32_t max_material_count;
  uint32_t material_count;
  gpumaterial *materials;
} scene;

int32_t create_scene(DemoAllocContext alloc_ctx, scene *out_scene);

int32_t scene_append_gltf(scene *s, const char *filename);
void destroy_scene2(scene *s);

int32_t load_scene(VkDevice device, allocator tmp_alloc, allocator std_alloc,
                   const VkAllocationCallbacks *vk_alloc,
                   VmaAllocator vma_alloc, VmaPool up_pool, VmaPool tex_pool,
                   const char *filename, scene **scene);
void destroy_scene(VkDevice device, allocator std_alloc, VmaAllocator alloc,
                   const VkAllocationCallbacks *vk_alloc, scene *s);