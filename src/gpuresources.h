#pragma once

#include <stdint.h>

typedef struct VkBuffer_T *VkBuffer;
typedef struct VkDevice_T *VkDevice;
typedef struct VkFence_T *VkFence;
typedef struct VkImage_T *VkImage;
typedef struct VkImageView_T *VkImageView;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct VkDescriptorSet_T *VkDescriptorSet;

typedef struct VmaAllocator_T *VmaAllocator;
typedef struct VmaAllocation_T *VmaAllocation;
typedef struct VmaPool_T *VmaPool;

typedef struct VkImageCreateInfo VkImageCreateInfo;
typedef struct VmaAllocationCreateInfo VmaAllocationCreateInfo;

typedef struct cpumesh cpumesh;
typedef struct cgltf_mesh cgltf_mesh;
typedef struct cputexture cputexture;
typedef struct cgltf_texture cgltf_texture;
typedef struct cgltf_material cgltf_material;

typedef struct gpubuffer {
  VkBuffer buffer;
  VmaAllocation alloc;
} gpubuffer;

typedef struct gpumesh {
  size_t idx_count;
  size_t vtx_count;
  int32_t idx_type;
  size_t size;
  size_t idx_size;
  size_t vtx_size;
  gpubuffer host;
  gpubuffer gpu;
} gpumesh;

typedef struct gpuimage {
  VkImage image;
  VmaAllocation alloc;
} gpuimage;

typedef struct gputexture {
  gpubuffer host;
  gpuimage device;
  VkImageView view;
  uint32_t width;
  uint32_t height;
  uint32_t mip_levels;
  uint32_t layer_count;
  uint32_t format;
} gputexture;

#define MAX_MATERIAL_TEXTURES 8

/*
  TODO: Fiugre out how to best represent materials
  For now I'm imagining that a material will be able to be implemented
  mostly with 1 uniform buffer for parameters and up to 8 textures for
  other resource bindings.
*/
typedef struct gpumaterial {
  // All material parameters go into one uniform buffer
  // The uniform buffer takes up location 0
  size_t consts_size;
  gpubuffer host_consts;
  gpubuffer gpu_consts;

  // textures take up locations 1-8 in the descriptor set
  uint32_t texture_count;
  gputexture *textures[MAX_MATERIAL_TEXTURES];
} gpumaterial;

int32_t create_gpubuffer(VmaAllocator allocator, uint64_t size,
                         int32_t mem_usage, int32_t buf_usage, gpubuffer *out);
void destroy_gpubuffer(VmaAllocator allocator, const gpubuffer *buffer);

int32_t create_gpumesh(VkDevice device, VmaAllocator allocator,
                       const cpumesh *src_mesh, gpumesh *dst_mesh);
int32_t create_gpumesh_cgltf(VkDevice device, VmaAllocator allocator,
                             const cgltf_mesh *src_mesh, gpumesh *dst_mesh);
void destroy_gpumesh(VkDevice device, VmaAllocator allocator,
                     const gpumesh *mesh);

int32_t create_gpuimage(VmaAllocator alloc,
                        const VkImageCreateInfo *img_create_info,
                        const VmaAllocationCreateInfo *alloc_create_info,

                        gpuimage *i);
void destroy_gpuimage(VmaAllocator allocator, const gpuimage *image);

int32_t load_texture(VkDevice device, VmaAllocator alloc, const char *filename,
                     VmaPool up_pool, VmaPool tex_pool, gputexture *t);
int32_t load_skybox(VkDevice device, VmaAllocator alloc,
                    const char *folder_path, VmaPool up_pool, VmaPool tex_pool,
                    gputexture *t);
int32_t create_texture(VkDevice device, VmaAllocator alloc,
                       const cputexture *tex, VmaPool up_pool, VmaPool tex_pool,
                       gputexture *t);
int32_t create_gputexture_cgltf(VkDevice device, VmaAllocator alloc,
                                const cgltf_texture *gltf, const uint8_t *bin,
                                VmaPool up_pool, VmaPool tex_pool,
                                gputexture *t);
void destroy_texture(VkDevice device, VmaAllocator alloc, const gputexture *t);

int32_t create_gpumaterial_cgltf(VkDevice device, VmaAllocator alloc,
                                 const cgltf_material *gltf, const uint8_t *bin,
                                 gpumaterial *m);
void destroy_material(VkDevice device, VmaAllocator alloc,
                      const gpumaterial *m);